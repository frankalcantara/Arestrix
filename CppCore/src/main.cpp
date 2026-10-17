#include "webui.hpp"
#include "util/Json.hpp"
#include "core/Graph.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "sandbox/SandboxManager.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "sandbox/PipelineResumeEngine.hpp"
#include "memory/AgentBlackboard.hpp"
#include "memory/SemanticMemoryEngine.hpp"
#include "mcp/McpRegistry.hpp"
#include "project/ProjectManager.hpp"
#include "project/OpenableFiles.hpp"
#include "assistant/AssistantSession.hpp"
#include "rt/ModelClient.hpp"
#include "pi/PiProjectScanner.hpp"
#include "build/BuildService.hpp"
#include "cli/Cli.hpp"
#include "platform/Process.hpp"
#include "rt/Tools.hpp"
#include "mcp/McpClient.hpp"
#include "native/NativeTools.hpp"
#include "rt/McpTools.hpp"
#include "search/ProjectSearch.hpp"
#include "search/SynonymAgent.hpp"
#include <atomic>
#include <condition_variable>
#include <thread>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>


// ---------------------------------------------------------------------------
// Helpers de seguranca / projeto
// ---------------------------------------------------------------------------
namespace {

// Raizes onde o frontend pode ler/gravar arquivos: o projeto ativo e a raiz do Arestrix (cwd).
std::vector<std::filesystem::path> allowedRoots() {
    std::vector<std::filesystem::path> roots;
    std::error_code ec;
    std::string active = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    if (!active.empty()) roots.emplace_back(active);
    roots.push_back(std::filesystem::current_path(ec));
    return roots;
}

// Resolve `requested` (relativo a cada raiz, ou absoluto) e so aceita se ficar DENTRO de uma raiz.
std::optional<std::filesystem::path> confineToAllowedRoots(const std::string& requested) {
    if (requested.empty()) return std::nullopt;
    for (const auto& root : allowedRoots()) {
        auto p = taskforge::rt::resolveInWorkspace(root, requested);
        if (p) return p;
    }
    return std::nullopt;
}

bool isValidEnvName(const std::string& s) {
    if (s.empty() || s.size() > 128) return false;
    for (char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

// Aceita o JSON do grafo (graph.toJSON()) ou {"schema": {...}}.
bool loadGraphFromPayload(const std::string& payload, taskforge::core::Graph& graph) {
    if (payload.empty() || payload.front() != '{') return false;
    auto j = taskforge::jsonutil::parse(payload);
    if (j.contains("schema") && j["schema"].is_object()) {
        graph.fromJson(j["schema"]);
        return true;
    }
    if (j.contains("nodes")) {
        graph.fromJson(j);
        return true;
    }
    return false;
}

webui::window* gWindow = nullptr;

void pushToUi(const std::string& jsFunction, const taskforge::Json& payload) {
    if (!gWindow) return;
    gWindow->run("if (window." + jsFunction + ") { window." + jsFunction + "(" + payload.dump() + "); }");
}

} // namespace

// Instância global do nosso grafo central em C++
taskforge::core::Graph globalGraph;

// Wrapper JS -> C++ (Preview do codigo gerado). Payload: JSON do grafo (graph.toJSON()); vazio = grafo C++ interno.
void ui_get_cpp_preview(webui::window::event* e) {
    try {
        taskforge::core::Graph graph;
        if (!loadGraphFromPayload(e->get_string(), graph)) graph = globalGraph;

        taskforge::Json result;
        result["cpp"] = taskforge::compiler::TaskflowGenerator::generateSource(graph);
        result["cmake"] = taskforge::compiler::TaskflowGenerator::generateCMakeLists(graph);
        result["make"] = taskforge::compiler::TaskflowGenerator::generateMakefile(graph);
        auto msvc = taskforge::compiler::TaskflowGenerator::generateMsvcBuildScript(graph);
        result["msvc_ps1"] = msvc.ps1;
        e->return_string(result.dump());
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_get_cpp_preview: " << ex.what() << "\n";
        e->return_string("");
    }
}

// Wrapper JS -> C++ (Exportar codigo C++ para uma pasta)
// Payload: {"outputDir": "...", "schema": <graph.toJSON()>}. Sem schema usa o grafo C++ interno.
void ui_generate_cpp_code(webui::window::event* e) {
    try {
        std::string outputDirName = "Output";
        taskforge::core::Graph graph;
        bool haveGraph = false;

        std::string rawArg = e->get_string();
        if (!rawArg.empty() && rawArg.front() == '{') {
            auto j = taskforge::jsonutil::parse(rawArg);
            if (j.contains("outputDir") && j["outputDir"].is_string() && !j["outputDir"].get<std::string>().empty()) {
                outputDirName = j["outputDir"].get<std::string>();
            }
            if (j.contains("schema") && j["schema"].is_object()) {
                graph.fromJson(j["schema"]);
                haveGraph = true;
            }
        } else if (!rawArg.empty()) {
            outputDirName = rawArg;
        }
        if (!haveGraph) graph = globalGraph;

        auto target = confineToAllowedRoots(outputDirName);
        if (!target) {
            std::cerr << "[C++ Erro] Pasta de saida fora das areas permitidas: " << outputDirName << "\n";
            e->return_bool(false);
            return;
        }

        std::cout << "[C++] Exportando codigo C++ Taskflow para " << target->string() << "\n";
        std::filesystem::create_directories(*target);
        auto writeFile = [&](const char* name, const std::string& content) {
            std::ofstream f(*target / name, std::ios::binary | std::ios::trunc);
            f << content;
        };
        writeFile("main.cpp", taskforge::compiler::TaskflowGenerator::generateSource(graph));
        writeFile("CMakeLists.txt", taskforge::compiler::TaskflowGenerator::generateCMakeLists(graph));
        writeFile("Makefile", taskforge::compiler::TaskflowGenerator::generateMakefile(graph));
        auto msvcScripts = taskforge::compiler::TaskflowGenerator::generateMsvcBuildScript(graph);
        writeFile("build_msvc.ps1", msvcScripts.ps1);
        writeFile("build.bat", msvcScripts.bat);
        e->return_bool(true);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] Falha ao gerar codigo: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Abrir no VS Code)
void ui_open_in_vscode(webui::window::event* e) {
    try {
        std::string target = e->get_string();
        if (target.empty()) target = "Output";
        auto p = confineToAllowedRoots(target);
        if (!p) { e->return_bool(false); return; }
        e->return_bool(taskforge::platform::openInVsCode(*p));
    } catch (...) {
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Adicionar Nó)
void ui_add_node(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto nodeData = taskforge::jsonutil::parse(jsonStr).get<taskforge::core::NodeData>();
        bool success = globalGraph.addNode(nodeData);
        e->return_bool(success);
    } catch(const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_add_node: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Adicionar Aresta)
void ui_add_edge(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto edgeData = taskforge::jsonutil::parse(jsonStr).get<taskforge::core::EdgeData>();
        bool success = globalGraph.addEdge(edgeData);
        e->return_bool(success);
    } catch(const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_add_edge: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Remover Nó)
void ui_remove_node(webui::window::event* e) {
    std::string id = e->get_string();
    bool success = globalGraph.removeNode(id);
    e->return_bool(success);
}

// Wrapper de comunicação JS -> C++ (Remover Aresta)
void ui_remove_edge(webui::window::event* e) {
    std::string id = e->get_string();
    bool success = globalGraph.removeEdge(id);
    e->return_bool(success);
}

// Wrapper de comunicação JS -> C++ (Limpar Grafo)
void ui_clear_graph(webui::window::event* e) {
    globalGraph.clear();
    std::cout << "[Graph] Clear triggered.\n";
}

// Wrapper de comunicação JS -> C++ (Pegar estado do grafo)
void ui_get_graph_state(webui::window::event* e) {
    taskforge::Json state = globalGraph.toJson();
    e->return_string(state.dump());
}

// Wrapper JS -> C++ (Status dos drivers de sandbox e do toolchain)
void ui_get_sandbox_status(webui::window::event* e) {
    using taskforge::sandbox::IsolationType;
    auto& mgr = taskforge::sandbox::SandboxManager::instance();
    taskforge::Json status;
    status["docker"] = mgr.isAvailable(IsolationType::Container);
    status["microvm"] = mgr.isAvailable(IsolationType::MicroVM);
    status["host"] = true;

    auto tc = taskforge::build::detectToolchain();
    status["compiler"] = tc.kind != taskforge::build::Toolchain::Kind::None;
    status["compilerName"] = tc.description;
    auto rt = taskforge::build::locateRuntime();
    status["runtime"] = rt.ok;
    status["runtimeError"] = rt.error;
    e->return_string(status.dump());
}

// Wrapper de comunicação JS -> C++ (Obter Checkpoint do Resume Engine)
void ui_get_pipeline_checkpoint(webui::window::event* e) {
    auto& engine = taskforge::sandbox::PipelineResumeEngine::getInstance();
    taskforge::Json st = engine.getStateJson();
    e->return_string(st.dump());
}

// Wrapper de comunicação JS -> C++ (Resetar Checkpoint)
void ui_reset_pipeline_checkpoint(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string itemId = "";
    std::string stageId = "";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            itemId = j.value("itemId", "");
            stageId = j.value("stageId", "");
        }
    } catch (...) {}
    auto& engine = taskforge::sandbox::PipelineResumeEngine::getInstance();
    engine.resetCheckpoint(itemId, stageId);
    e->return_bool(true);
}

// Wrapper de comunicação JS -> C++ (Status da Memória Heterogênea)
void ui_get_memory_status(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
        } else if (!payload.empty()) {
            projectId = payload;
        }
    } catch (...) {}

    auto& bb = taskforge::memory::AgentBlackboard::getInstance();
    auto& sem = taskforge::memory::SemanticMemoryEngine::getInstance();

    taskforge::Json res;
    res["projectId"] = projectId;
    res["blackboard"] = bb.toJson(projectId);
    res["semantic"] = sem.toJson(projectId);
    res["isBlackboardPersistent"] = bb.isPersistent(projectId);
    res["isSemanticPersistent"] = sem.isPersistent(projectId);

    e->return_string(res.dump());
}

// Wrapper de comunicação JS -> C++ (Configurar Persistência de Memória)
void ui_set_memory_persistence(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    bool persistent = false;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            persistent = j.value("persistent", false);
        }
    } catch (...) {}

    auto& bb = taskforge::memory::AgentBlackboard::getInstance();
    auto& sem = taskforge::memory::SemanticMemoryEngine::getInstance();
    bb.setPersistence(projectId, persistent);
    sem.setPersistence(projectId, persistent);

    e->return_bool(true);
}

// Wrapper de comunicação JS -> C++ (Limpar Memória)
void ui_clear_memory(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    bool sessionOnly = false;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            sessionOnly = j.value("sessionOnly", false);
        } else if (!payload.empty()) {
            projectId = payload;
        }
    } catch (...) {}

    auto& bb = taskforge::memory::AgentBlackboard::getInstance();
    auto& sem = taskforge::memory::SemanticMemoryEngine::getInstance();

    if (sessionOnly) {
        bb.clearSession(projectId);
        sem.clearSession(projectId);
    } else {
        bb.clearProject(projectId);
        sem.clearLessons(projectId);
    }

    e->return_bool(true);
}

// Wrapper de comunicação JS -> C++ (Registrar Lição Semântica)
void ui_record_semantic_lesson(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string id = "";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            std::string projectId = j.value("projectId", "default");
            std::string domain = j.value("domain", "geral");
            std::string triggerError = j.value("triggerError", "");
            std::string lessonRule = j.value("lessonRule", "");
            std::string sourceItem = j.value("sourceItem", "");

            id = taskforge::memory::SemanticMemoryEngine::getInstance().recordLesson(
                projectId, domain, triggerError, lessonRule, sourceItem
            );
        }
    } catch (...) {}

    e->return_string(id);
}

// Wrapper de comunicação JS -> C++ (Listar Servidores MCP)
void ui_get_mcp_servers(webui::window::event* e) {
    auto servers = taskforge::mcp::McpRegistry::getInstance().listServers();
    taskforge::Json j = servers;
    e->return_string(j.dump());
}

// Wrapper de comunicação JS -> C++ (Salvar Servidor MCP)
void ui_save_mcp_server(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto cfg = taskforge::jsonutil::parse(jsonStr).get<taskforge::mcp::McpServerConfig>();
        bool ok = taskforge::mcp::McpRegistry::getInstance().registerServer(cfg);
        e->return_bool(ok);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_mcp_server: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Remover Servidor MCP)
void ui_remove_mcp_server(webui::window::event* e) {
    std::string id = e->get_string();
    bool ok = taskforge::mcp::McpRegistry::getInstance().removeServer(id);
    e->return_bool(ok);
}

// Wrapper de comunicação JS -> C++ (Testar Conexão / Listar Ferramentas do Servidor MCP)
void ui_test_mcp_server(webui::window::event* e) {
    std::string id = e->get_string();
    auto res = taskforge::mcp::McpRegistry::getInstance().testServer(id);
    e->return_string(res.dump());
}

// Wrapper de comunicação JS -> C++ (Executar Ferramenta MCP)
void ui_call_mcp_tool(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto j = taskforge::jsonutil::parse(jsonStr);
        std::string serverId = j.value("serverId", "");
        std::string toolName = j.value("toolName", "");
        taskforge::Json args = j.value("arguments", taskforge::Json::object());

        auto result = taskforge::mcp::McpRegistry::getInstance().callTool(serverId, toolName, args);
        taskforge::Json resJson = result;
        e->return_string(resJson.dump());
    } catch (const std::exception& ex) {
        taskforge::mcp::McpErrorInfo info = taskforge::mcp::McpErrorInfo::fromException(ex);
        taskforge::Json err = {
            {"success", false},
            {"isError", true},
            {"errorCode", info.codeName()},
            {"errorDetail", info.detail},
            {"errorMessage", info.message.empty() ? (std::string("Excecao na chamada MCP: ") + ex.what()) : info.message}
        };
        e->return_string(err.dump());
    }
}

// Wrapper de comunicação JS -> C++ (Criar Projeto)
void ui_create_project(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto manifest = taskforge::jsonutil::parse(jsonStr).get<taskforge::project::ProjectManifest>();
        bool ok = taskforge::project::ProjectManager::getInstance().createProject(manifest);
        e->return_bool(ok);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_create_project: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Carregar Projeto)
void ui_load_project(webui::window::event* e) {
    std::string pathOrDir = e->get_string();
    bool ok = taskforge::project::ProjectManager::getInstance().loadProject(pathOrDir);
    e->return_bool(ok);
}

// Wrapper de comunicação JS -> C++ (Salvar Projeto)
void ui_save_project(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto manifest = taskforge::jsonutil::parse(jsonStr).get<taskforge::project::ProjectManifest>();
        bool ok = taskforge::project::ProjectManager::getInstance().saveProject(manifest);
        e->return_bool(ok);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_project: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Apagar Projeto)
void ui_delete_project(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "";
    std::string rootPath = "";
    bool deleteFiles = true;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "");
            rootPath = j.value("rootPath", "");
            deleteFiles = j.value("deleteFiles", true);
        } else {
            projectId = payload;
        }
    } catch (...) {}

    auto& pm = taskforge::project::ProjectManager::getInstance();

    // O alvo precisa ser um projeto que o usuario realmente abriu nesta maquina: dentro das raizes
    // permitidas do frontend (projeto ativo ou cwd do app) OU um item da lista de recentes. Impede
    // que um frontend comprometido (ex.: XSS no webview) apague pastas arbitrarias do disco. A lista
    // de recentes e consultada ANTES de o projeto ser removido dela (logo abaixo).
    auto inside = [](const std::filesystem::path& child, const std::filesystem::path& parent) {
        auto rel = child.lexically_relative(parent);
        if (rel == ".") return true;
        if (rel.empty() || rel.is_absolute()) return false;
        return *rel.begin() != "..";
    };
    bool confined = false;
    std::error_code recEc;
    std::filesystem::path norm;
    if (deleteFiles && !rootPath.empty()) {
        std::filesystem::path p(rootPath);
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec) &&
            std::filesystem::exists(p / "arestrix.project.json", ec)) {
            norm = std::filesystem::weakly_canonical(p, ec);
            for (const auto& root : allowedRoots()) {
                std::filesystem::path r = std::filesystem::weakly_canonical(root, recEc);
                if (!recEc && inside(norm, r)) { confined = true; break; }
            }
            if (!confined) {
                for (const auto& rec : pm.listRecentProjects()) {
                    if (rec.rootPath.empty()) continue;
                    std::filesystem::path r = std::filesystem::weakly_canonical(rec.rootPath, recEc);
                    if (!recEc && r == norm) { confined = true; break; }
                }
            }
        }
    }

    if (!projectId.empty()) {
        pm.removeRecentProject(projectId);
    }
    if (!rootPath.empty()) {
        pm.removeRecentProject(rootPath);
    }

    if (deleteFiles && !rootPath.empty() && !norm.empty()) {
        std::error_code ec;
        std::filesystem::path rootP = norm.root_path();
        std::filesystem::path currentP = std::filesystem::current_path(ec);
        if (confined && norm != rootP && norm != currentP) {
            std::cout << "[ProjectManager] Apagando projeto e arquivos no disco: " << norm.string() << std::endl;
            std::filesystem::remove_all(norm, ec);
            if (ec) {
                std::cerr << "[ProjectManager Erro] Falha ao remover diretorio: " << ec.message() << std::endl;
            }
        } else if (!confined) {
            std::cerr << "[ProjectManager] Exclusao recusada: '" << norm.string()
                      << "' nao e o projeto ativo, nao esta nas raizes permitidas nem na lista de projetos recentes."
                      << std::endl;
        }
    }

    e->return_bool(true);
}

// Wrapper de comunicação JS -> C++ (Salvar Pipeline JSON)
void ui_save_pipeline(webui::window::event* e) {
    std::string payload = e->get_string();
    try {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();

        std::filesystem::path pipeDir = std::filesystem::path(root) / "pipelines";
        std::filesystem::create_directories(pipeDir);
        std::filesystem::path pipeFile = pipeDir / "main.json";

        std::ofstream f(pipeFile);
        if (f.is_open()) {
            f << payload;
            f.close();
            e->return_bool(true);
            return;
        }
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_pipeline: " << ex.what() << "\n";
    }
    e->return_bool(false);
}

// Wrapper de comunicação JS -> C++ (Carregar Pipeline JSON)
void ui_load_pipeline(webui::window::event* e) {
    try {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();

        std::filesystem::path pipeFile = std::filesystem::path(root) / "pipelines" / "main.json";
        if (std::filesystem::exists(pipeFile)) {
            std::ifstream f(pipeFile);
            if (f.is_open()) {
                std::stringstream buffer;
                buffer << f.rdbuf();
                e->return_string(buffer.str());
                return;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_load_pipeline: " << ex.what() << "\n";
    }
    e->return_string("");
}

// Wrapper de comunicação JS -> C++ (Obter Projeto Ativo)
// ---------------------------------------------------------------------------------------------
// Busca no projeto aberto (todos os pipelines, subfluxos e skills do workspace)
// ---------------------------------------------------------------------------------------------
taskforge::search::ProjectSearchIndex& projectSearchIndex() {
    static taskforge::search::ProjectSearchIndex idx;
    return idx;
}

taskforge::Json searchStatsJson(const taskforge::search::IndexStats& st) {
    taskforge::Json j;
    j["rebuilt"] = st.rebuilt;
    j["filesTotal"] = static_cast<int64_t>(st.filesTotal);
    j["filesReindexed"] = static_cast<int64_t>(st.filesReindexed);
    j["documents"] = static_cast<int64_t>(st.documents);
    j["projectHash"] = st.projectHash;
    return j;
}

// Reindexa o que mudou (hash por arquivo e do projeto). `force` ignora o intervalo minimo.
taskforge::search::IndexStats refreshProjectSearch(bool force) {
    using Clock = std::chrono::steady_clock;
    static std::mutex mtx;
    static Clock::time_point last{};
    static taskforge::search::IndexStats lastStats;
    static std::string lastRoot;

    auto& pm = taskforge::project::ProjectManager::getInstance();
    std::string root = pm.getActiveProjectRoot();
    if (root.empty()) root = std::filesystem::current_path().string();

    std::lock_guard<std::mutex> lock(mtx);
    auto now = Clock::now();
    if (!force && root == lastRoot && now - last < std::chrono::seconds(2)) return lastStats;

    std::vector<taskforge::search::PipelineSource> sources;
    for (const auto& p : pm.getCurrentProject().pipelines) sources.push_back({p.id, p.name, p.file});
    lastStats = projectSearchIndex().refresh(root, sources);
    lastRoot = root;
    last = now;
    return lastStats;
}

// JS -> C++: {"q": "...", "limit": 20}  =>  {"results": [...], "stats": {...}}
void ui_search_project(webui::window::event* e) {
    taskforge::Json out;
    out["results"] = taskforge::Json::array();
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        std::string q = req.value("q", std::string());
        size_t limit = static_cast<size_t>(std::clamp(req.value("limit", 20), 1, 100));
        auto st = refreshProjectSearch(req.value("force", false));
        out["stats"] = searchStatsJson(st);
        for (const auto& h : projectSearchIndex().query(q, limit)) {
            taskforge::Json r;
            r["kind"] = h.kind;
            r["title"] = h.title;
            r["subtitle"] = h.subtitle;
            r["snippet"] = h.snippet;
            r["file"] = h.file;
            r["pipelineId"] = h.pipelineId;
            r["nodeId"] = h.nodeId;
            r["nodeType"] = h.nodeType;
            taskforge::Json path = taskforge::Json::array();
            for (const auto& id : h.nodePath) path.push_back(id);
            r["nodePath"] = path;
            r["score"] = h.score;
            out["results"].push_back(r);
        }
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_search_project: " << ex.what() << "\n";
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// JS -> C++: servidores MCP configurados no projeto aberto (.workspace/mcps), so metadados.
void ui_list_project_mcp_servers(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        out["servers"] = taskforge::rt::McpToolbox::listProjectServers(root);
    } catch (const std::exception& ex) {
        taskforge::mcp::McpErrorInfo info = taskforge::mcp::McpErrorInfo::fromException(ex);
        out["servers"] = taskforge::Json::array();
        out["errorCode"] = info.codeName();
        if (!info.detail.empty()) out["errorDetail"] = info.detail;
        out["error"] = info.message.empty() ? std::string(ex.what()) : info.message;
    }
    e->return_string(out.dump());
}

// JS -> C++: {"server": "id"} => ferramentas que o servidor oferece (abre a sessao MCP persistente do servidor).
// Em falha, devolve "errorCode" (estavel, para o i18n do frontend), "errorDetail" (parte dinamica) e
// "error" (fallback humano em portugues).
void ui_list_project_mcp_tools(webui::window::event* e) {
    taskforge::Json out;
    out["tools"] = taskforge::Json::array();
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        const std::string serverId = req.value("server", std::string());
        if (taskforge::native::isNativeServer(serverId)) {
            const auto* ns = taskforge::native::findServer(serverId);
            if (!ns) throw taskforge::mcp::McpError(taskforge::mcp::McpErrorCode::ServerNotFound, serverId);
            for (const auto& t : ns->tools) {
                taskforge::Json item;
                item["name"] = t.name;
                item["description"] = t.description;
                item["inputSchema"] = t.inputSchema;
                item["risk"] = t.risk;
                out["tools"].push_back(item);
            }
            e->return_string(out.dump());
            return;
        }
        auto cfg = taskforge::rt::McpToolbox::loadServerConfig(root, serverId);
        taskforge::mcp::McpClient client;
        taskforge::mcp::McpErrorInfo info;
        auto tools = client.listTools(cfg, 20000, &info);
        if (tools.empty()) {
            if (info.empty()) {
                info.set(taskforge::mcp::McpErrorCode::NoTools);
            }
            out["errorCode"] = info.codeName();
            if (!info.detail.empty()) out["errorDetail"] = info.detail;
            out["error"] = info.message;
        }
        for (const auto& t : tools) {
            taskforge::Json item;
            item["name"] = t.name;
            item["description"] = t.description;
            item["inputSchema"] = t.inputSchema;
            out["tools"].push_back(item);
        }
    } catch (const std::exception& ex) {
        taskforge::mcp::McpErrorInfo info = taskforge::mcp::McpErrorInfo::fromException(ex);
        out["errorCode"] = info.codeName();
        if (!info.detail.empty()) out["errorDetail"] = info.detail;
        out["error"] = info.message.empty() ? std::string(ex.what()) : info.message;
    }
    e->return_string(out.dump());
}

// JS -> C++: hosts que as ferramentas nativas de HTTP podem acessar neste projeto (.workspace/native/http.json).
void ui_get_http_policy(webui::window::event* e) {
    taskforge::Json out;
    out["hosts"] = taskforge::Json::array();
    try {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        for (const auto& h : taskforge::native::http::readPolicy(root)) out["hosts"].push_back(h);
    } catch (const std::exception& ex) {
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// JS -> C++: {"hosts": ["api.exemplo.com", "*.exemplo.org"]}. Valida e grava; so a tela do Studio chama isto.
void ui_set_http_policy(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        std::vector<std::string> hosts;
        const auto& list = req["hosts"];
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].is_string()) hosts.push_back(list[i].str());
        auto& pm = taskforge::project::ProjectManager::getInstance();
        std::string root = pm.getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        std::string error;
        out["ok"] = taskforge::native::http::writePolicy(root, hosts, error);
        if (!error.empty()) out["error"] = error;
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// JS -> C++: {"locale": "es-ES", "force": false}. Gera o dicionario de sinonimos do idioma com o modelo
// padrao do Studio (variaveis de ambiente do usuario). Pode demorar: a UI deve chamar sem aguardar.
void ui_search_ensure_synonyms(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        auto r = taskforge::search::ensureSynonymsFor(req.value("locale", std::string()), req.value("force", false));
        out["status"] = r.status;
        out["locale"] = r.locale;
        out["model"] = r.model;
        out["groups"] = static_cast<int64_t>(r.groups);
        out["error"] = r.error;
    } catch (const std::exception& ex) {
        out["status"] = "failed";
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// JS -> C++: chamado depois de salvar ou carregar um projeto; devolve as estatisticas da indexacao.
void ui_search_reindex(webui::window::event* e) {
    taskforge::Json out;
    try {
        out["stats"] = searchStatsJson(refreshProjectSearch(true));
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_search_reindex: " << ex.what() << "\n";
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

void ui_get_current_project(webui::window::event* e) {
    auto proj = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    taskforge::Json j = proj;
    e->return_string(j.dump());
}

// Wrapper de comunicação JS -> C++ (Listar Projetos Recentes)
void ui_list_recent_projects(webui::window::event* e) {
    auto list = taskforge::project::ProjectManager::getInstance().listRecentProjects();
    taskforge::Json j = list;
    e->return_string(j.dump());
}

// Wrapper JS -> C++ (Escanear Projeto Pi)
void ui_scan_pi_project(webui::window::event* e) {
    try {
        std::string pathStr = e->get_string();
        std::cout << "[C++] Varredura de projeto Pi requisitada: " << pathStr << std::endl;
        auto result = taskforge::pi::PiProjectScanner::scan(pathStr);
        std::string dump = taskforge::pi::PiProjectScanner::toJson(result).dump();
        e->return_string(dump);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_scan_pi_project: " << ex.what() << std::endl;
        e->return_string("{}");
    }
}

// Wrapper JS -> C++ (Obter Projetos Pi Conhecidos)
void ui_get_known_pi_projects(webui::window::event* e) {
    try {
        std::cout << "[C++] Obtendo lista de projetos Pi conhecidos no disco..." << std::endl;
        auto list = taskforge::pi::PiProjectScanner::getKnownPiProjects();
        taskforge::Json j = list;
        std::string dump = j.dump();
        e->return_string(dump);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_get_known_pi_projects: " << ex.what() << std::endl;
        e->return_string("[]");
    }
}

// Wrapper JS -> C++ (Obter variavel de ambiente: somente nomes no formato NOME_EM_MAIUSCULAS)
void ui_get_env_var(webui::window::event* e) {
    std::string key = e->get_string();
    if (!isValidEnvName(key)) { e->return_string(""); return; }
    e->return_string(taskforge::platform::getEnv(key));
}

// Wrapper JS -> C++ (Gravar variavel de ambiente do usuario). Sem shell: Registro (Windows) / ~/.config (Linux).
void ui_set_env_var(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string key = j.value("key", "");
        std::string val = j.value("value", "");
        if (!isValidEnvName(key)) { e->return_bool(false); return; }
        e->return_bool(taskforge::platform::setUserEnv(key, val));
    } catch (...) {
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Retorna o diretório de execução atual onde o Arestrix foi aberto)
void ui_get_current_working_dir(webui::window::event* e) {
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    e->return_string(cwd.string());
}

// Wrapper JS -> C++ (Selecionar pasta com o dialogo nativo)
void ui_pick_folder(webui::window::event* e) {
    e->return_string(taskforge::platform::pickFolderDialog("Selecione a Pasta do Projeto"));
}

// Wrapper JS -> C++ (Selecionar arquivo com o dialogo nativo).
// Payload: {"title", "filters": [{"name", "pattern": "*.a;*.b"}], "initialDir"}; textos ja traduzidos pela interface.
void ui_pick_file(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::vector<taskforge::platform::FileFilter> filters;
        if (j.contains("filters") && j["filters"].is_array()) {
            for (const auto& f : j["filters"]) {
                filters.push_back({f.value("name", std::string()), f.value("pattern", std::string())});
            }
        }
        e->return_string(taskforge::platform::pickFileDialog(j.value("title", std::string()), filters,
                                                             j.value("initialDir", std::string())));
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_pick_file: " << ex.what() << "\n";
        e->return_string("");
    }
}

// Wrapper JS -> C++ (Tela Abrir: subpastas, projetos, pacotes .arestrix e grafos .json de uma pasta).
// Payload: caminho em UTF-8 (vazio = diretorio atual).
void ui_list_openable_entries(webui::window::event* e) {
    try {
        e->return_string(taskforge::project::listOpenableEntries(taskforge::project::pathFromUtf8(e->get_string())).dump());
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_list_openable_entries: " << ex.what() << "\n";
        e->return_string("{\"path\":\"\",\"parent\":\"\",\"exists\":false,\"truncated\":false,\"entries\":[]}");
    }
}

// Helper Base64 para C++
namespace {
static const std::string B64_CHARS = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static std::vector<unsigned char> base64_decode_bytes(const std::string& in) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[B64_CHARS[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string base64_encode_bytes(const std::vector<unsigned char>& data) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(B64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(B64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}
} // namespace

// Wrapper JS -> C++ (Listar unidades / raizes do sistema)
void ui_list_drives(webui::window::event* e) {
    taskforge::Json drives = taskforge::platform::listRoots();
    e->return_string(drives.dump());
}

// Wrapper JS -> C++ (Listar Subdiretórios para Navegação Interativa)
void ui_list_subdirectories(webui::window::event* e) {
    try {
        std::string dirPath = e->get_string();
        if (dirPath.empty()) dirPath = "C:/Projetos";
        std::filesystem::path p(dirPath);
        taskforge::Json j = taskforge::Json::array();
        if (std::filesystem::exists(p) && std::filesystem::is_directory(p)) {
            std::vector<std::pair<std::string, std::string>> dirs;
            for (const auto& entry : std::filesystem::directory_iterator(p, std::filesystem::directory_options::skip_permission_denied)) {
                if (entry.is_directory()) {
                    dirs.push_back({entry.path().filename().string(), entry.path().string()});
                }
            }
            std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
                return a.first < b.first;
            });
            for (const auto& d : dirs) {
                j.push_back({
                    {"name", d.first},
                    {"path", d.second}
                });
            }
        }
        e->return_string(j.dump());
    } catch (...) {
        e->return_string("[]");
    }
}

// Wrapper JS -> C++ (Salvar pacote .arestrix em Base64 ou texto). Somente extensoes .arestrix / .json.
void ui_save_arestrix_file(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string targetPath = j.value("targetPath", "");
        std::string content = j.value("content", "");
        bool isBase64 = j.value("isBase64", false);

        std::filesystem::path p(targetPath);
        std::string ext = p.extension().string();
        if (targetPath.empty() || (ext != ".arestrix" && ext != ".json")) {
            std::cerr << "[C++ Erro] save_arestrix_file: extensao nao permitida: " << targetPath << "\n";
            e->return_bool(false);
            return;
        }
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) { e->return_bool(false); return; }
        if (isBase64) {
            auto bytes = base64_decode_bytes(content);
            out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else {
            out.write(content.data(), content.size());
        }
        out.close();
        std::cout << "[C++] Pacote Arestrix gravado: " << targetPath << "\n";
        e->return_bool(true);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_arestrix_file: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Carregar pacote .arestrix). Somente extensoes .arestrix / .json.
void ui_load_arestrix_file(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        std::string filePath = payload;
        if (!payload.empty() && payload[0] == '{') {
            filePath = taskforge::jsonutil::parse(payload).value("filePath", "");
        }

        std::filesystem::path p = taskforge::project::pathFromUtf8(filePath);
        std::string ext = p.extension().string();
        if (ext != ".arestrix" && ext != ".json") {
            e->return_string("{\"success\":false,\"error\":\"Extensao nao permitida\"}");
            return;
        }
        if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p)) {
            e->return_string("{\"success\":false,\"error\":\"Arquivo nao encontrado\"}");
            return;
        }
        std::ifstream in(p, std::ios::binary);
        if (!in.is_open()) {
            e->return_string("{\"success\":false,\"error\":\"Falha ao abrir arquivo\"}");
            return;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        taskforge::Json resp;
        resp["success"] = true;
        resp["base64"] = base64_encode_bytes(bytes);
        resp["isBase64"] = true;
        resp["size"] = bytes.size();
        resp["filePath"] = taskforge::project::pathToUtf8(p);
        e->return_string(resp.dump());
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_load_arestrix_file: " << ex.what() << "\n";
        e->return_string("{\"success\":false,\"error\":\"Excecao interna\"}");
    }
}

// Wrapper JS -> C++ (Garantir Pastas do Workspace na Maquina do Usuario)
void ui_ensure_workspace_directories(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        std::string root = ".workspace";
        std::string skills = ".workspace/skills";
        std::string mcps = ".workspace/mcps";
        std::string agents = ".workspace/agents";

        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            root = j.value("root", root);
            skills = j.value("skills", skills);
            mcps = j.value("mcps", mcps);
            agents = j.value("agents", agents);
        }

        std::filesystem::create_directories(root);
        std::filesystem::create_directories(skills);
        std::filesystem::create_directories(mcps);
        std::filesystem::create_directories(agents);

        std::cout << "[C++] Pastas de governanca do Workspace garantidas: " << root << "\n";
        e->return_bool(true);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_ensure_workspace_directories: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Gravar asset do workspace). Confinado ao projeto ativo / raiz do Arestrix.
void ui_write_workspace_asset(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        if (payload.empty()) { e->return_bool(false); return; }
        auto j = taskforge::jsonutil::parse(payload);
        std::string filePath = j.value("filePath", "");
        std::string content = j.value("content", "");

        auto p = confineToAllowedRoots(filePath);
        if (!p) {
            std::cerr << "[C++ Erro] write_workspace_asset: caminho fora das areas permitidas: " << filePath << "\n";
            e->return_bool(false);
            return;
        }
        std::filesystem::create_directories(p->parent_path());
        std::ofstream out(*p, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) { e->return_bool(false); return; }
        out << content;
        e->return_bool(true);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_write_workspace_asset: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Gravar SKILL.md). Confinado ao projeto ativo; somente .md; escrita atomica.
void ui_write_skill_file(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        if (payload.empty()) { e->return_string("{\"ok\":false,\"error\":\"Payload vazio\"}"); return; }
        auto j = taskforge::jsonutil::parse(payload);
        std::string filePath = j.value("path", j.value("filePath", ""));
        std::string content = j.value("content", "");

        auto p = confineToAllowedRoots(filePath);
        if (!p || p->extension() != ".md") {
            e->return_string("{\"ok\":false,\"error\":\"Caminho invalido: a skill deve ser um .md dentro do projeto\"}");
            return;
        }
        std::filesystem::create_directories(p->parent_path());
        std::filesystem::path tmp = *p;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) { e->return_string("{\"ok\":false,\"error\":\"Falha ao abrir arquivo para escrita\"}"); return; }
            out << content;
        }
        std::filesystem::rename(tmp, *p);
        e->return_string("{\"ok\":true}");
    } catch (const std::exception& ex) {
        taskforge::Json err;
        err["ok"] = false;
        err["error"] = ex.what();
        e->return_string(err.dump());
    }
}

// Wrapper JS -> C++ (Ler SKILL.md). Confinado ao projeto ativo; somente .md.
void ui_read_skill_file(webui::window::event* e) {
    try {
        std::string filePath = e->get_string();
        auto p = confineToAllowedRoots(filePath);
        if (!p || p->extension() != ".md") {
            e->return_string("{\"ok\":false,\"error\":\"Caminho invalido: a skill deve ser um .md dentro do projeto\"}");
            return;
        }
        if (!std::filesystem::is_regular_file(*p)) {
            e->return_string("{\"ok\":false,\"error\":\"Arquivo nao encontrado\"}");
            return;
        }
        taskforge::Json resp;
        resp["ok"] = true;
        resp["content"] = taskforge::jsonutil::readFile(*p);
        e->return_string(resp.dump());
    } catch (const std::exception& ex) {
        taskforge::Json err;
        err["ok"] = false;
        err["error"] = ex.what();
        e->return_string(err.dump());
    }
}


// Wrapper JS -> C++ (Compilar e executar o grafo). Payload: graph.toJSON().
// O progresso chega assincronamente em window.onArestrixPipelineEvent / window.onArestrixTelemetry.
// Compila e executa o grafo (JSON do grafo raiz) no projeto ativo; progresso vai para o console do Studio.
// Devolve vazio se a execucao comecou, ou o motivo da recusa. Usado pela interface e pelo Assistente.
std::string startPipelineRun(const std::string& payload) {
    taskforge::core::Graph graph;
    if (!loadGraphFromPayload(payload, graph)) return "Grafo invalido ou vazio";
    std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    if (root.empty()) root = std::filesystem::current_path().string();

    taskforge::build::BuildEvents ev;
    ev.onPhase = [](const std::string& phase, const std::string& message) {
        pushToUi("onArestrixPipelineEvent", {{"type", "phase"}, {"phase", phase}, {"message", message}});
    };
    ev.onLog = [](const std::string& line, bool isError) {
        pushToUi("onArestrixPipelineEvent", {{"type", "log"}, {"line", line}, {"isError", isError}});
    };
    ev.onTelemetry = [](const std::string& jsonLine) {
        auto parsed = taskforge::jsonutil::tryParse(jsonLine);
        if (parsed) pushToUi("onArestrixTelemetry", *parsed);
    };
    ev.onFinished = [](bool success, int exitCode, long long elapsedMs, const std::string& error) {
        pushToUi("onArestrixPipelineEvent", {{"type", "finished"}, {"success", success}, {"exitCode", exitCode},
                                              {"elapsedMs", elapsedMs}, {"error", error}});
    };
    if (!taskforge::build::BuildService::instance().start(graph, root, ev)) return "Ja existe uma execucao em andamento";
    return "";
}

void ui_run_pipeline(webui::window::event* e) {
    try {
        std::string error = startPipelineRun(e->get_string());
        taskforge::Json out = error.empty() ? taskforge::Json{{"ok", true}} : taskforge::Json{{"ok", false}, {"error", error}};
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        taskforge::Json err = {{"ok", false}, {"error", ex.what()}};
        e->return_string(err.dump());
    }
}

// Wrapper JS -> C++ (Interromper a execucao em andamento)
void ui_stop_pipeline(webui::window::event* e) {
    taskforge::build::BuildService::instance().stop();
    e->return_bool(true);
}

void exit_app(webui::window::event* e) {
    std::cout << "[C++] Comando de encerramento recebido." << std::endl;
    webui::exit();
}

namespace {

// Determina o diretório base onde residem a pasta dist/ e os recursos do projeto
std::filesystem::path findProjectRoot() {
    std::vector<std::filesystem::path> candidates;

    // 1. Diretório de trabalho atual e seu diretório pai
    std::error_code ec;
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!cwd.empty()) {
        candidates.push_back(cwd);
        candidates.push_back(cwd.parent_path());
    }

    // 2. Diretório onde o executável reside e seu diretório pai
    std::filesystem::path exePath = taskforge::platform::executablePath();
    if (!exePath.empty()) {
        std::filesystem::path exeDir = exePath.parent_path();
        candidates.push_back(exeDir);
        candidates.push_back(exeDir.parent_path());
    }

    // Se qualquer candidato for a subpasta 'CppCore', prioriza seu diretório pai
    for (auto& candidate : candidates) {
        if (!candidate.empty() && candidate.filename() == "CppCore") {
            candidate = candidate.parent_path();
        }
    }

    // Prioridade 1: pasta que contenha package.json e dist/index.html (raiz canônica do Arestrix)
    for (const auto& candidate : candidates) {
        if (!candidate.empty()) {
            if (std::filesystem::exists(candidate / "package.json", ec) &&
                std::filesystem::exists(candidate / "dist" / "index.html", ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
    }

    // Prioridade 2: pasta que contenha package.json ou arestrix.project.json
    for (const auto& candidate : candidates) {
        if (!candidate.empty()) {
            if (std::filesystem::exists(candidate / "package.json", ec) ||
                std::filesystem::exists(candidate / "arestrix.project.json", ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
    }

    // Prioridade 3: pasta que contenha dist/index.html
    for (const auto& candidate : candidates) {
        if (!candidate.empty()) {
            if (std::filesystem::exists(candidate / "dist" / "index.html", ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
    }

    // Prioridade 4: fallback para ui/index.html ou CppCore/ui/index.html
    for (const auto& candidate : candidates) {
        if (!candidate.empty()) {
            if (std::filesystem::exists(candidate / "ui" / "index.html", ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
            if (std::filesystem::exists(candidate / "CppCore" / "ui" / "index.html", ec)) {
                return std::filesystem::weakly_canonical(candidate, ec);
            }
        }
    }

    return cwd;
}

// ---------------------------------------------------------------------------------------------
// Assistente do Studio: o laco, as ferramentas e as sessoes rodam aqui; a interface so desenha.
// ---------------------------------------------------------------------------------------------
namespace fs = std::filesystem;
using taskforge::assistant::AssistantHost;
using taskforge::assistant::GraphDocument;
using taskforge::assistant::SessionStore;

taskforge::Json readMainGraph(const std::string& root, const taskforge::project::ProjectManifest& manifest) {
    std::error_code ec;
    fs::path file = fs::path(root) / "pipelines" / "main.json";
    if (fs::is_regular_file(file, ec)) {
        auto parsed = taskforge::jsonutil::tryParse(taskforge::jsonutil::readFile(file));
        if (parsed && parsed->is_object()) return *parsed;
    }
    return GraphDocument::emptySchema(manifest.name, manifest.projectId);
}

class StudioAssistantHost : public AssistantHost {
public:
    taskforge::Json currentProject() override {
        return taskforge::project::ProjectManager::getInstance().getCurrentProject();
    }

    taskforge::Json recentProjects() override {
        taskforge::Json out = taskforge::Json::array();
        for (const auto& r : taskforge::project::ProjectManager::getInstance().listRecentProjects()) {
            out.push_back(taskforge::Json{{"name", r.name}, {"rootPath", r.rootPath}, {"lastOpened", r.lastOpened}});
        }
        return out;
    }

    std::optional<taskforge::Json> createProject(const std::string& name, const std::string& directory,
                                                 const std::string& description, std::string& error) override {
        std::string clean = name;
        while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
        if (clean.empty() || clean.find_first_of("<>:\"/\\|?*") != std::string::npos) {
            error = "nome de projeto invalido";
            return std::nullopt;
        }
        fs::path root = directory.empty() ? fs::current_path() / "projects" / clean : fs::path(directory);
        std::error_code ec;
        if (fs::exists(root / "arestrix.project.json", ec)) {
            error = "ja existe um projeto em " + root.string() + " (use open_project)";
            return std::nullopt;
        }
        taskforge::project::ProjectManifest manifest;
        manifest.name = clean;
        manifest.description = description;
        manifest.rootPath = root.string();
        auto& pm = taskforge::project::ProjectManager::getInstance();
        if (!pm.createProject(manifest)) {
            error = "nao foi possivel criar o projeto em " + root.string();
            return std::nullopt;
        }
        auto current = pm.getCurrentProject();
        return readMainGraph(current.rootPath, current);
    }

    std::optional<taskforge::Json> openProject(const std::string& path, std::string& error) override {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        if (!pm.loadProject(path)) {
            error = "nenhum projeto do Arestrix em " + path;
            return std::nullopt;
        }
        auto current = pm.getCurrentProject();
        return readMainGraph(current.rootPath, current);
    }

    bool savePipeline(const taskforge::Json& schema, std::string& error) override {
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (root.empty()) root = fs::current_path().string();
        std::error_code ec;
        fs::create_directories(fs::path(root) / "pipelines", ec);
        std::ofstream f(fs::path(root) / "pipelines" / "main.json", std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            error = "nao foi possivel gravar pipelines/main.json";
            return false;
        }
        f << schema.dump();
        return true;
    }

    bool runPipeline(const taskforge::Json& schema, std::string& error) override {
        error = startPipelineRun(schema.dump());
        return error.empty();
    }

    fs::path docsDirectory(taskforge::rt::RuntimeLang lang) override {
        return findProjectRoot() / "docs" / (lang == taskforge::rt::RuntimeLang::En ? "en" : "pt-BR");
    }
};

// Uma conversa por vez. A confirmacao bloqueia a thread do laco ate a interface responder.
struct AssistantRuntime {
    std::mutex mu;
    std::condition_variable cv;
    bool busy = false;
    bool confirmPending = false;
    std::optional<bool> confirmAnswer;
    std::atomic<bool> cancel{false};
};

AssistantRuntime& assistantRuntime() {
    static AssistantRuntime rt;
    return rt;
}

fs::path assistantProjectRoot() {
    std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    return root.empty() ? fs::current_path() : fs::path(root);
}

void ui_assistant_status(webui::window::event* e) {
    e->return_string(taskforge::assistant::assistantStatus().dump());
}

void ui_assistant_sessions(webui::window::event* e) {
    e->return_string(SessionStore(assistantProjectRoot()).list().dump());
}

void ui_assistant_session_load(webui::window::event* e) {
    auto session = SessionStore(assistantProjectRoot()).load(e->get_string());
    if (!session) { e->return_string("{\"ok\":false}"); return; }
    taskforge::Json out = {{"ok", true}, {"id", session->id}, {"title", session->title}, {"transcript", session->transcript}};
    e->return_string(out.dump());
}

void ui_assistant_session_delete(webui::window::event* e) {
    e->return_bool(SessionStore(assistantProjectRoot()).remove(e->get_string()));
}

// Payload: {"sessionId" (vazio = nova), "text", "locale", "graph" (grafo raiz do canvas)}.
// Responde na hora com {ok, sessionId}; o restante chega por window.onArestrixAssistant.
void ui_assistant_send(webui::window::event* e) {
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        std::string text = req.value("text", std::string());
        if (text.empty()) { e->return_string("{\"ok\":false,\"error\":\"empty\"}"); return; }

        auto& rt = assistantRuntime();
        {
            std::lock_guard<std::mutex> lock(rt.mu);
            if (rt.busy) { e->return_string("{\"ok\":false,\"error\":\"busy\"}"); return; }
            rt.busy = true;
            rt.confirmPending = false;
            rt.confirmAnswer.reset();
            rt.cancel = false;
        }

        fs::path root = assistantProjectRoot();
        SessionStore store(root);
        std::string sessionId = req.value("sessionId", std::string());
        auto loaded = sessionId.empty() ? std::nullopt : store.load(sessionId);
        taskforge::assistant::Session session = loaded ? *loaded : SessionStore::create();
        taskforge::Json graph = req.contains("graph") ? req.at("graph") : taskforge::Json();
        std::string locale = req.value("locale", std::string("pt-BR"));

        std::thread([session, root, text, graph, locale]() mutable {
            auto& rt = assistantRuntime();
            StudioAssistantHost host;
            GraphDocument doc(graph);
            taskforge::assistant::TurnIO io;
            io.emit = [](const taskforge::Json& ev) { pushToUi("onArestrixAssistant", ev); };
            io.cancelled = [&rt]() { return rt.cancel.load(); };
            io.confirm = [&rt](const taskforge::Json& request) {
                std::unique_lock<std::mutex> lock(rt.mu);
                rt.confirmPending = true;
                rt.confirmAnswer.reset();
                taskforge::Json ev = request;
                ev["type"] = "confirm";
                lock.unlock();
                pushToUi("onArestrixAssistant", ev);
                lock.lock();
                rt.cv.wait(lock, [&rt]() { return rt.confirmAnswer.has_value() || rt.cancel.load(); });
                rt.confirmPending = false;
                return rt.confirmAnswer.value_or(false);
            };
            taskforge::assistant::TurnOptions opts;
            opts.locale = locale;
            try {
                taskforge::assistant::runTurn(session, SessionStore(root), text, doc, host,
                                              taskforge::assistant::defaultModelChoice(),
                                              [](const taskforge::rt::ChatRequest& r) { return taskforge::rt::chatOnce(r); },
                                              io, opts);
            } catch (const std::exception& ex) {
                pushToUi("onArestrixAssistant", {{"type", "error"}, {"sessionId", session.id}, {"code", "internal"}, {"detail", ex.what()}});
                pushToUi("onArestrixAssistant", {{"type", "done"}, {"sessionId", session.id}, {"ok", false}});
            }
            std::lock_guard<std::mutex> lock(rt.mu);
            rt.busy = false;
        }).detach();

        taskforge::Json out = {{"ok", true}, {"sessionId", session.id}};
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        {
            std::lock_guard<std::mutex> lock(assistantRuntime().mu);
            assistantRuntime().busy = false;
        }
        taskforge::Json err = {{"ok", false}, {"error", ex.what()}};
        e->return_string(err.dump());
    }
}

// Payload: true/false (o usuario aceitou ou recusou a acao pedida).
void ui_assistant_confirm(webui::window::event* e) {
    auto& rt = assistantRuntime();
    {
        std::lock_guard<std::mutex> lock(rt.mu);
        if (!rt.confirmPending) { e->return_bool(false); return; }
        rt.confirmAnswer = e->get_string() == "true";
    }
    rt.cv.notify_all();
    e->return_bool(true);
}

// Interrompe no proximo passo (uma chamada ao modelo em andamento termina antes) e recusa confirmacoes pendentes.
void ui_assistant_cancel(webui::window::event* e) {
    auto& rt = assistantRuntime();
    rt.cancel = true;
    rt.cv.notify_all();
    e->return_bool(true);
}

} // namespace


int main(int argc, char** argv) {
    if (argc >= 2 && (std::string(argv[1]) == "run" || std::string(argv[1]) == "gen")) {
        return taskforge::cli::runCli(argc, argv);
    }
    std::cout << "[C++] Inicializando Arestrix Engine (C++)..." << std::endl;

    std::filesystem::path projectRoot = findProjectRoot();
    std::cout << "[C++] Raiz do projeto identificada: " << projectRoot.string() << std::endl;

    // Alinha o diretório de trabalho com a raiz do projeto para consistência de arquivos e diretórios
    std::error_code ec;
    std::filesystem::current_path(projectRoot, ec);
    if (ec) {
        std::cerr << "[C++ Aviso] Nao foi possivel definir current_path: " << ec.message() << std::endl;
    }

    webui::window win;
    gWindow = &win;

    // Registra as funções que o TypeScript poderá chamar
    win.bind("add_node", ui_add_node);
    win.bind("add_edge", ui_add_edge);
    win.bind("remove_node", ui_remove_node);
    win.bind("remove_edge", ui_remove_edge);
    win.bind("clear_graph", ui_clear_graph);
    win.bind("generate_cpp_code", ui_generate_cpp_code);
    win.bind("get_cpp_preview", ui_get_cpp_preview);
    win.bind("get_graph_state", ui_get_graph_state);
    win.bind("get_sandbox_status", ui_get_sandbox_status);
    win.bind("get_pipeline_checkpoint", ui_get_pipeline_checkpoint);
    win.bind("reset_pipeline_checkpoint", ui_reset_pipeline_checkpoint);
    win.bind("get_memory_status", ui_get_memory_status);
    win.bind("set_memory_persistence", ui_set_memory_persistence);
    win.bind("clear_memory", ui_clear_memory);
    win.bind("record_semantic_lesson", ui_record_semantic_lesson);
    win.bind("get_mcp_servers", ui_get_mcp_servers);
    win.bind("save_mcp_server", ui_save_mcp_server);
    win.bind("remove_mcp_server", ui_remove_mcp_server);
    win.bind("test_mcp_server", ui_test_mcp_server);
    win.bind("call_mcp_tool", ui_call_mcp_tool);
    win.bind("create_project", ui_create_project);
    win.bind("load_project", ui_load_project);
    win.bind("save_project", ui_save_project);
    win.bind("delete_project", ui_delete_project);
    win.bind("save_pipeline", ui_save_pipeline);
    win.bind("load_pipeline", ui_load_pipeline);
    win.bind("get_current_project", ui_get_current_project);
    win.bind("search_project", ui_search_project);
    win.bind("search_reindex", ui_search_reindex);
    win.bind("list_project_mcp_servers", ui_list_project_mcp_servers);
    win.bind("list_project_mcp_tools", ui_list_project_mcp_tools);
    win.bind("get_http_policy", ui_get_http_policy);
    win.bind("set_http_policy", ui_set_http_policy);
    win.bind("search_ensure_synonyms", ui_search_ensure_synonyms);
    win.bind("list_recent_projects", ui_list_recent_projects);
    win.bind("scan_pi_project", ui_scan_pi_project);
    win.bind("get_known_pi_projects", ui_get_known_pi_projects);
    win.bind("get_env_var", ui_get_env_var);
    win.bind("set_env_var", ui_set_env_var);
    win.bind("pick_folder", ui_pick_folder);
    win.bind("pick_file", ui_pick_file);
    win.bind("list_openable_entries", ui_list_openable_entries);
    win.bind("get_current_working_dir", ui_get_current_working_dir);
    win.bind("list_drives", ui_list_drives);
    win.bind("list_subdirectories", ui_list_subdirectories);
    win.bind("save_arestrix_file", ui_save_arestrix_file);
    win.bind("load_arestrix_file", ui_load_arestrix_file);
    win.bind("ensure_workspace_directories", ui_ensure_workspace_directories);
    win.bind("write_workspace_asset", ui_write_workspace_asset);
    win.bind("write_skill_file", ui_write_skill_file);
    win.bind("read_skill_file", ui_read_skill_file);
    win.bind("open_in_vscode", ui_open_in_vscode);
    win.bind("run_pipeline", ui_run_pipeline);
    win.bind("stop_pipeline", ui_stop_pipeline);
    win.bind("assistant_status", ui_assistant_status);
    win.bind("assistant_sessions", ui_assistant_sessions);
    win.bind("assistant_session_load", ui_assistant_session_load);
    win.bind("assistant_session_delete", ui_assistant_session_delete);
    win.bind("assistant_send", ui_assistant_send);
    win.bind("assistant_confirm", ui_assistant_confirm);
    win.bind("assistant_cancel", ui_assistant_cancel);
    win.bind("exit_app", exit_app);

    // Registra o Emitter de Telemetria de Agentes em Tempo Real (Fase 10)
    taskforge::sandbox::AgentTelemetryEmitter::setCallback([&win](const std::string& jsonMsg) {
        // jsonMsg nasce do dump() do emissor, mas chega por um callback cru: reparseia e re-serializa para
        // garantir que e JSON valido (e portanto seguro de embutir) antes de virar codigo JavaScript no webview.
        auto parsed = taskforge::jsonutil::tryParse(jsonMsg);
        std::string safe = parsed ? parsed->dump() : "null";
        std::string jsCode = "if (window.onArestrixTelemetry) { window.onArestrixTelemetry(" + safe + "); } else if (window.onTaskForgeTelemetry) { window.onTaskForgeTelemetry(" + safe + "); }";
        win.run(jsCode);
    });

    std::filesystem::path distDir = projectRoot / "dist";
    std::filesystem::path distIndex = distDir / "index.html";
    std::filesystem::path uiIndex = projectRoot / "ui" / "index.html";
    if (!std::filesystem::exists(uiIndex)) {
        uiIndex = projectRoot / "CppCore" / "ui" / "index.html";
    }

    if (std::filesystem::exists(distIndex)) {
        std::cout << "[C++] Servindo a UI oficial: " << distIndex.string() << std::endl;
        bool rootOk = win.set_root_folder(distDir.string());
        if (rootOk) {
            win.show("index.html");
        } else {
            win.show("dist/index.html");
        }
    } else if (std::filesystem::exists(uiIndex)) {
        std::cout << "[C++] Servindo UI de desenvolvimento: " << uiIndex.string() << std::endl;
        win.set_root_folder(uiIndex.parent_path().string());
        win.show("index.html");
    } else {
        std::cerr << "[C++ Erro] index.html nao foi encontrado em dist/ ou ui/." << std::endl;
        win.show("<html><head><title>Arestrix</title><style>body{font-family:Segoe UI,sans-serif;padding:2rem;background:#0f172a;color:#f8fafc;text-align:center;}</style></head><body><h1>Arestrix Studio</h1><p>Frontend nao encontrado em dist/index.html.</p><p>Execute <code>npm run build</code> na pasta raiz do projeto.</p></body></html>");
    }

    webui::wait();
    taskforge::mcp::McpClient::shutdownSessions();   // encerra os servidores MCP que ficaram abertos
    return 0;
}
