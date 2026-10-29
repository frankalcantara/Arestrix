#include "webui.hpp"
#include "util/Json.hpp"
#include "core/Graph.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "sandbox/SandboxManager.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "sandbox/PipelineResumeEngine.hpp"
#include "memory/MemoryService.hpp"
#include "mcp/McpRegistry.hpp"
#include "project/ProjectManager.hpp"
#include "project/GitSyncService.hpp"
#include "project/RunnerService.hpp"
#include "project/OpenableFiles.hpp"
#include "libraries/GitHubSource.hpp"
#include "assistant/AssistantSession.hpp"
#include "rt/ModelClient.hpp"
#include "rt/Http.hpp"
#include "rt/compress/ProseModel.hpp"
#include "rt/compress/ProseModelInstaller.hpp"
#include "rt/compress/ProseRecommendation.hpp"
#include "local/LocalRuntime.hpp"
#include "pi/PiProjectScanner.hpp"
#include "build/BuildService.hpp"
#include "cli/Cli.hpp"
#include "platform/Process.hpp"
#include "platform/StudioConsole.hpp"
#include "rt/Tools.hpp"
#include "mcp/McpClient.hpp"
#include "native/NativeTools.hpp"
#include "rt/McpTools.hpp"
#include "search/ProjectSearch.hpp"
#include "search/SynonymAgent.hpp"
#include <atomic>
#include <cstdlib>
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
#include <cctype>
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

// Configuracao do usuario (Studio). Nunca guarda segredos: chaves continuam no
// ambiente do sistema / cofre. No Windows usa %APPDATA%; no Linux usa XDG/HOME.
std::filesystem::path studioConfigPath() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return std::filesystem::path(appdata) / "Arestrix" / "studio.config.json";
    }
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        return std::filesystem::path(local) / "Arestrix" / "studio.config.json";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "arestrix" / "studio.config.json";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config" / "arestrix" / "studio.config.json";
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec) / ".arestrix" / "studio.config.json";
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

// Ponte entre o worker de build (bloqueado no runtime) e a UI. O RPC
// answer_interaction preenche a resposta e acorda a espera.
std::mutex g_interactionMutex;
std::condition_variable g_interactionCv;
std::string g_interactionRequestId;
std::string g_interactionAnswer;
bool g_interactionReady = false;
bool g_interactionCancelled = false;

void resetInteractionBridge() {
    std::lock_guard<std::mutex> lock(g_interactionMutex);
    g_interactionRequestId.clear();
    g_interactionAnswer.clear();
    g_interactionReady = false;
    g_interactionCancelled = false;
}

std::string awaitInteraction(const std::string& requestJson, const std::atomic<bool>* cancel) {
    auto request = taskforge::jsonutil::tryParse(requestJson);
    if (!request || !request->is_object()) {
        return taskforge::Json{{"error", "Pedido de interacao invalido."}}.dump();
    }
    const std::string requestId = request->value("requestId", std::string());
    if (requestId.empty()) {
        return taskforge::Json{{"error", "Pedido de interacao sem requestId."}}.dump();
    }
    int timeoutSeconds = request->value("timeoutSeconds", 300);
    if (timeoutSeconds < 1) timeoutSeconds = 300;
    if (timeoutSeconds > 86400) timeoutSeconds = 86400;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);

    {
        std::lock_guard<std::mutex> lock(g_interactionMutex);
        g_interactionRequestId = requestId;
        g_interactionAnswer.clear();
        g_interactionReady = false;
        g_interactionCancelled = false;
    }
    pushToUi("onArestrixPipelineEvent", {{"type", "interaction"}, {"request", *request}});

    std::unique_lock<std::mutex> lock(g_interactionMutex);
    while (!g_interactionReady) {
        if ((cancel && cancel->load()) || g_interactionCancelled) {
            g_interactionRequestId.clear();
            g_interactionReady = false;
            lock.unlock();
            pushToUi("onArestrixPipelineEvent", {{"type", "interaction_closed"},
                                                 {"requestId", requestId}, {"reason", "cancelled"}});
            return taskforge::Json{{"requestId", requestId}, {"cancelled", true}}.dump();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            g_interactionRequestId.clear();
            g_interactionReady = false;
            lock.unlock();
            pushToUi("onArestrixPipelineEvent", {{"type", "interaction_timeout"}, {"requestId", requestId}});
            return taskforge::Json{{"requestId", requestId}, {"timedOut", true}}.dump();
        }

        const auto wait = std::min<std::chrono::steady_clock::duration>(
            deadline - now, std::chrono::milliseconds(200));
        g_interactionCv.wait_for(lock, wait);
    }

    std::string answer = g_interactionAnswer;
    g_interactionRequestId.clear();
    g_interactionReady = false;
    lock.unlock();
    pushToUi("onArestrixPipelineEvent", {{"type", "interaction_closed"},
                                         {"requestId", requestId}, {"reason", "answered"}});
    return answer;
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
        auto subflowSources = taskforge::compiler::TaskflowGenerator::generateSubflowSources(graph);
        auto subflowIt = subflowSources.find("subflows.cpp");
        if (subflowIt != subflowSources.end()) writeFile("subflows.cpp", subflowIt->second);
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

// Wrapper JS -> C++ (Abrir pasta de saidas/artefatos no Windows Explorer)
void ui_open_output_folder(webui::window::event* e) {
    try {
        std::string req = e->get_string();
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        std::filesystem::path dir = req.empty() ? (std::filesystem::path(root) / "exports") : std::filesystem::path(root) / req;
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) {
            // Se exports nao existe, tenta abrir o proprio root do projeto
            if (!std::filesystem::create_directories(dir, ec)) {
                dir = std::filesystem::path(root);
            }
        }
        e->return_bool(taskforge::platform::openFolderInExplorer(dir));
    } catch (...) {
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Listar artefatos produzidos no projeto ativo)
void ui_list_artifacts(webui::window::event* e) {
    try {
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        std::filesystem::path prjRoot(root);

        taskforge::Json list = taskforge::Json::array();
        std::error_code ec;

        // Escaneia exports/
        std::filesystem::path expDir = prjRoot / "exports";
        if (std::filesystem::is_directory(expDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(expDir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                auto filename = entry.path().filename().string();
                auto ext = entry.path().extension().string();
                std::string type = "auto";
                if (ext == ".svg") type = "svg";
                else if (ext == ".csv" || ext == ".tsv" || ext == ".xlsx") type = "sheet";
                else if (ext == ".pdf") type = "pdf";
                else if (ext == ".md") type = "markdown";
                else if (ext == ".json") type = "json";
                else if (ext == ".txt" || ext == ".log") type = "text";
                else if (ext == ".png" || ext == ".jpg" || ext == ".webp") type = "image";

                list.push_back(taskforge::Json{
                    {"id", "exp_" + filename},
                    {"name", filename},
                    {"path", (expDir / filename).string()},
                    {"relativePath", "exports/" + filename},
                    {"type", type},
                    {"sizeBytes", (size_t)entry.file_size(ec)}
                });
            }
        }

        // Escaneia workspace/ para SVGs e relatórios conhecidos
        std::filesystem::path wsDir = prjRoot / "workspace";
        if (std::filesystem::is_directory(wsDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(wsDir, ec)) {
                if (!entry.is_regular_file(ec)) continue;
                auto filename = entry.path().filename().string();
                auto ext = entry.path().extension().string();
                if (ext != ".svg" && ext != ".csv" && ext != ".tsv" && ext != ".pdf" && ext != ".md" && ext != ".json" && ext != ".txt") continue;

                std::string type = "auto";
                if (ext == ".svg") type = "svg";
                else if (ext == ".csv" || ext == ".tsv") type = "sheet";
                else if (ext == ".pdf") type = "pdf";
                else if (ext == ".md") type = "markdown";
                else if (ext == ".json") type = "json";
                else if (ext == ".txt") type = "text";

                // Se já estiver em exports, ignora duplicata de nome idêntico
                bool dup = false;
                for (const auto& existing : list) {
                    if (existing.value("name", "") == filename) { dup = true; break; }
                }
                if (!dup) {
                    list.push_back(taskforge::Json{
                        {"id", "ws_" + filename},
                        {"name", filename},
                        {"path", (wsDir / filename).string()},
                        {"relativePath", "workspace/" + filename},
                        {"type", type},
                        {"sizeBytes", (size_t)entry.file_size(ec)}
                    });
                }
            }
        }

        taskforge::Json res;
        res["ok"] = true;
        res["artifacts"] = list;
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json err;
        err["ok"] = false;
        err["error"] = ex.what();
        e->return_string(err.dump());
    }
}

// Wrapper JS -> C++ (Obter conteudo do artefato)
void ui_get_artifact(webui::window::event* e) {
    try {
        std::string req = e->get_string();
        auto p = confineToAllowedRoots(req);
        if (!p || !std::filesystem::is_regular_file(*p)) {
            taskforge::Json err;
            err["ok"] = false;
            err["error"] = "Arquivo de artefato nao encontrado";
            e->return_string(err.dump());
            return;
        }

        std::error_code ec;
        size_t size = std::filesystem::file_size(*p, ec);
        std::string ext = p->extension().string();
        bool isText = (ext == ".svg" || ext == ".txt" || ext == ".md" || ext == ".json" ||
                       ext == ".csv" || ext == ".tsv" || ext == ".xml" || ext == ".html");

        taskforge::Json res;
        res["ok"] = true;
        res["path"] = p->string();
        res["sizeBytes"] = size;
        res["isText"] = isText;
        if (isText && size < 2 * 1024 * 1024) {
            res["content"] = taskforge::jsonutil::readFile(*p);
        }
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json err;
        err["ok"] = false;
        err["error"] = ex.what();
        e->return_string(err.dump());
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

// Wrapper de comunicacao JS -> C++ (ultimo trace.json do projeto ativo).
void ui_get_last_trace(webui::window::event* e) {
    try {
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        std::filesystem::path runs = std::filesystem::path(root) / ".arestrix" / "runs";
        std::filesystem::path latest;
        std::filesystem::file_time_type latestTime{};
        std::error_code ec;
        if (std::filesystem::is_directory(runs, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(runs, ec)) {
                if (!entry.is_directory()) continue;
                std::filesystem::path candidate = entry.path() / "trace.json";
                if (!std::filesystem::is_regular_file(candidate, ec)) continue;
                auto time = std::filesystem::last_write_time(candidate, ec);
                if (latest.empty() || time > latestTime) {
                    latest = candidate;
                    latestTime = time;
                }
            }
        }
        if (latest.empty()) {
            e->return_string("");
            return;
        }
        std::ifstream in(latest, std::ios::binary);
        std::ostringstream content;
        content << in.rdbuf();
        e->return_string(content.str());
    } catch (...) {
        e->return_string("");
    }
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
namespace {
std::string activeMemoryRoot() {
    std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    if (root.empty()) root = std::filesystem::current_path().string();
    return root;
}

std::string activeMemoryDb(const std::string& projectId) {
    (void)projectId;
    return (std::filesystem::path(activeMemoryRoot()) / ".arestrix" / "memory" / "memory.db").string();
}

std::string memorySafeName(const std::string& projectId) {
    std::string out = projectId.empty() ? "default" : projectId;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return out;
}

bool configureProjectMemory(const std::string& projectId, bool persistent) {
    const std::string root = activeMemoryRoot();
    const std::string safe = memorySafeName(projectId);
    return taskforge::memory::MemoryService::getInstance().configure(
        projectId, persistent, activeMemoryDb(projectId),
        (std::filesystem::path(root) / ("arestrix-blackboard-" + safe + ".json")).string(),
        (std::filesystem::path(root) / ("arestrix-memory-" + safe + ".json")).string());
}

std::string workspaceIdForRoot() {
    if (const char* env = std::getenv("ARESTRIX_WORKSPACE_ID"); env && *env) return env;
    std::string id = std::filesystem::path(activeMemoryRoot()).filename().string();
    return id.empty() ? "default" : id;
}

std::filesystem::path workspaceDbForRoot() {
    return std::filesystem::path(activeMemoryRoot()) / ".workspace" / "memory" / "memory.db";
}

void configureEmbeddingsFromPayload(const std::string& projectId, const taskforge::Json& payload) {
    if (payload.is_object() && payload.contains("embedding")) {
        taskforge::memory::MemoryService::getInstance().configureEmbeddings(
            projectId, taskforge::memory::embeddingConfigFromJson(payload["embedding"]));
    }
}
} // namespace

void ui_get_memory_status(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            configureProjectMemory(projectId, true);
            configureEmbeddingsFromPayload(projectId, j);
        } else if (!payload.empty()) {
            projectId = payload;
        }
    } catch (...) {}

    configureProjectMemory(projectId, true);
    auto data = taskforge::memory::MemoryService::getInstance().toJson(projectId);

    taskforge::Json res = data;
    res["projectId"] = projectId;
    // Compatibilidade com o modal atual: Blackboard e Lições.
    res["blackboard"] = data.contains("working") ? data["working"] : taskforge::Json::object();
    res["semantic"] = taskforge::Json::object();
    res["semantic"]["lessons"] = data.contains("lessons") ? data["lessons"] : taskforge::Json::array();
    res["isBlackboardPersistent"] = true;
    res["isSemanticPersistent"] = true;
    res["proposals"] = taskforge::memory::MemoryService::getInstance().listProposals(projectId, "pending", 100);
    res["embedding"] = taskforge::memory::MemoryService::getInstance().embeddingStatus(projectId);
    res["consolidation"] = taskforge::memory::MemoryService::getInstance().consolidationStatus(projectId);
    res["playbooks"] = taskforge::memory::MemoryService::getInstance().listPlaybooks(projectId, "", "", 100);
    res["workspace"] = taskforge::memory::MemoryService::getInstance().workspaceStatus(projectId);

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

    e->return_bool(configureProjectMemory(projectId, persistent));
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

    configureProjectMemory(projectId, true);
    auto& memory = taskforge::memory::MemoryService::getInstance();
    if (sessionOnly) {
        memory.clearSession(projectId);
    } else {
        memory.clearProject(projectId);
    }
    e->return_bool(true);
}

// Wrapper de comunicação JS -> C++ (Aplicar Retenção de Memória)
void ui_prune_memory(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    int days = 180;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            days = j.value("days", 180);
        }
    } catch (...) {}
    if (days < 0) days = 0;
    if (days > 3650) days = 3650;

    configureProjectMemory(projectId, true);
    auto result = taskforge::memory::MemoryService::getInstance().prune(projectId, days);
    taskforge::Json out = taskforge::Json::object();
    out["episodes"] = result.episodes;
    out["hits"] = result.hits;
    out["usage"] = result.usage;
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Aceitar Proposta L2)
void ui_accept_memory_proposal(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    std::string proposalId;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            proposalId = j.value("proposalId", "");
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    std::string error;
    std::string promotedId;
    const bool ok = taskforge::memory::MemoryService::getInstance().promoteProposal(
        projectId, proposalId, "studio", error, &promotedId);
    taskforge::Json out = taskforge::Json::object();
    out["ok"] = ok;
    out["error"] = error;
    out["promotedId"] = promotedId;
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Rejeitar Proposta L2)
void ui_reject_memory_proposal(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string projectId = "default";
    std::string proposalId;
    std::string reason = "rejeitada no Studio";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            proposalId = j.value("proposalId", "");
            reason = j.value("reason", reason);
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    std::string error;
    const bool ok = taskforge::memory::MemoryService::getInstance().rejectProposal(
        projectId, proposalId, "studio", reason, error);
    taskforge::Json out = taskforge::Json::object();
    out["ok"] = ok;
    out["error"] = error;
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Testar Embeddings L2)
void ui_test_memory_embeddings(webui::window::event* e) {
    const std::string payload = e->get_string();
    std::string projectId = "default";
    taskforge::Json request = taskforge::Json::object();
    try {
        if (!payload.empty() && payload[0] == '{') {
            request = taskforge::jsonutil::parse(payload);
            projectId = request.value("projectId", "default");
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    configureEmbeddingsFromPayload(projectId, request);
    const auto out = taskforge::memory::MemoryService::getInstance().testEmbeddings(projectId);
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Reindexar Embeddings L2)
void ui_reindex_memory_embeddings(webui::window::event* e) {
    const std::string payload = e->get_string();
    std::string projectId = "default";
    bool force = true;
    taskforge::Json request = taskforge::Json::object();
    try {
        if (!payload.empty() && payload[0] == '{') {
            request = taskforge::jsonutil::parse(payload);
            projectId = request.value("projectId", "default");
            force = request.value("force", true);
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    configureEmbeddingsFromPayload(projectId, request);
    std::string error;
    auto out = taskforge::memory::MemoryService::getInstance().reindexEmbeddings(projectId, force, error);
    out["projectId"] = projectId;
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Inicializar Workspace de Memória)
void ui_init_workspace(webui::window::event* e) {
    const std::string payload = e->get_string();
    std::string projectId = "default";
    std::string workspaceId;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            workspaceId = j.value("workspaceId", std::string());
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    if (workspaceId.empty()) workspaceId = workspaceIdForRoot();
    const auto db = workspaceDbForRoot();
    const bool ok = taskforge::memory::MemoryService::getInstance().configureProjectWorkspace(
        projectId, workspaceId, db.string());
    auto out = taskforge::memory::MemoryService::getInstance().workspaceStatus(projectId);
    out["ok"] = ok;
    out["database"] = db.string();
    e->return_string(out.dump());
}

// Wrapper de comunicação JS -> C++ (Promover Memória ao Workspace)
void ui_promote_to_workspace(webui::window::event* e) {
    const std::string payload = e->get_string();
    std::string projectId = "default";
    std::string kind;
    std::string memoryId;
    std::string note;
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            projectId = j.value("projectId", "default");
            kind = j.value("kind", std::string());
            memoryId = j.value("id", std::string());
            note = j.value("evidence", std::string());
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    const auto db = workspaceDbForRoot();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(db, ec)) {
        e->return_string(taskforge::Json{{"ok", false},
                                         {"error", "workspace nao configurado"}}.dump());
        return;
    }
    taskforge::memory::MemoryService::getInstance().configureProjectWorkspace(
        projectId, workspaceIdForRoot(), db.string());
    taskforge::Json evidence = taskforge::Json::array(
        {taskforge::Json{{"kind", "manual"},
                         {"note", note.empty() ? "promocao manual no Studio" : note},
                         {"actor", "studio"}}});
    std::string error;
    const bool ok = taskforge::memory::MemoryService::getInstance().promoteToWorkspace(
        projectId, kind, memoryId, evidence, "studio", error);
    e->return_string(taskforge::Json{{"ok", ok}, {"error", error}}.dump());
}

// Wrapper de comunicação JS -> C++ (Consolidar Memória L2/M3)
void ui_consolidate_memory(webui::window::event* e) {
    const std::string payload = e->get_string();
    std::string projectId = "default";
    taskforge::Json request = taskforge::Json::object();
    try {
        if (!payload.empty() && payload[0] == '{') {
            request = taskforge::jsonutil::parse(payload);
            projectId = request.value("projectId", "default");
        }
    } catch (...) {}
    configureProjectMemory(projectId, true);
    taskforge::Json optionsJson = request.contains("consolidation")
        ? request["consolidation"] : taskforge::Json::object();
    const auto options = taskforge::memory::consolidationOptionsFromJson(optionsJson);
    std::string error;
    auto out = taskforge::memory::MemoryService::getInstance().consolidate(
        projectId, options, error);
    out["projectId"] = projectId;
    if (!error.empty()) out["error"] = error;
    e->return_string(out.dump());
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

            configureProjectMemory(projectId, true);
            id = taskforge::memory::MemoryService::getInstance().recordLesson(
                projectId, domain, triggerError, lessonRule, sourceItem);
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

        // O id vira nome de arquivo em .workspace/mcps: mesma regra do McpToolbox.
        const bool validId = !cfg.id.empty() && cfg.id.size() <= 64 &&
            std::all_of(cfg.id.begin(), cfg.id.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '_' || c == '-' || c == '.';
            });
        if (!validId) {
            e->return_bool(false);
            return;
        }

        // Segredos nunca sao gravados no projeto: headers sensiveis exigem ${VARIAVEL}.
        for (const auto& [name, value] : cfg.headers) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            const bool sensitive = lower.find("key") != std::string::npos ||
                                   lower.find("token") != std::string::npos ||
                                   lower.find("secret") != std::string::npos ||
                                   lower.find("auth") != std::string::npos ||
                                   lower.find("credential") != std::string::npos;
            if (sensitive && !value.empty() && value.find("${") == std::string::npos) {
                e->return_bool(false);
                return;
            }
        }

        bool ok = taskforge::mcp::McpRegistry::getInstance().registerServer(cfg);
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (ok && !root.empty()) {
            std::error_code ec;
            auto dir = std::filesystem::path(root) / ".workspace" / "mcps";
            std::filesystem::create_directories(dir, ec);
            std::ofstream(dir / (cfg.id + ".json"), std::ios::binary | std::ios::trunc)
                << taskforge::Json(cfg).dump(2) << "\n";
        }
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
    std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    if (!root.empty()) {
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(root) / ".workspace" / "mcps" / (id + ".json"), ec);
    }
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

// JS -> C++: descobre os modelos de um servidor local OpenAI-compativel (Ollama, llama.cpp, vLLM, LM Studio...).
// Payload opcional: {"baseUrl": "http://localhost:11434"}; sem ele usa LOCAL_LLM_BASE_URL. A chamada sai pelo
// cliente nativo, entao nao depende de CORS e funciona tambem no aplicativo empacotado. O Arestrix NAO inicia
// nem instala o servidor: ele apenas consulta um endpoint que ja esteja rodando.
void ui_list_local_models(webui::window::event* e) {
    taskforge::Json out;
    out["ok"] = false;
    out["models"] = taskforge::Json::array();
    try {
        std::string base = taskforge::platform::getEnv("LOCAL_LLM_BASE_URL");
        std::string apiKey = taskforge::platform::getEnv("LOCAL_LLM_API_KEY");
        std::string payload = e->get_string();
        if (!payload.empty()) {
            auto req = taskforge::jsonutil::parse(payload);
            std::string provided = req.value("baseUrl", std::string());
            if (!provided.empty()) base = provided;
            std::string providedKey = req.value("apiKey", std::string());
            if (!providedKey.empty()) apiKey = providedKey;
        }
        if (base.empty()) base = "http://localhost:11434/v1";
        base = taskforge::rt::normalizeLocalBaseUrl(base);
        out["baseUrl"] = base;
        if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) {
            out["errorCode"] = "invalid_url";
            out["error"] = "A URL deve comecar com http:// ou https://.";
            e->return_string(out.dump());
            return;
        }

        auto get = [&](const std::string& url) {
            taskforge::rt::HttpRequest http;
            http.method = "GET";
            http.url = url;
            http.timeoutSeconds = 6;
            if (!apiKey.empty()) http.headers["Authorization"] = "Bearer " + apiKey;
            return taskforge::rt::httpSend(http);
        };
        auto append = [&](const std::string& id) {
            if (id.empty()) return;
            for (size_t i = 0; i < out["models"].size(); ++i)
                if (out["models"][i].is_string() && out["models"][i].str() == id) return;
            out["models"].push_back(id);
        };
        auto collect = [&](const std::string& body, const char* arrayKey,
                           std::initializer_list<const char*> idKeys) {
            auto parsed = taskforge::jsonutil::tryParse(body);
            if (!parsed) return;
            const taskforge::Json& list = (*parsed)[arrayKey];
            if (!list.is_array()) return;
            for (const auto& m : list) {
                if (m.is_string()) { append(m.str()); continue; }
                if (!m.is_object()) continue;
                for (const char* key : idKeys) {
                    std::string id = m.value(key, std::string());
                    if (!id.empty()) { append(id); break; }
                }
            }
        };

        taskforge::rt::HttpResponse resp = get(base + "/models");
        if (resp.status == 200) collect(resp.body, "data", {"id", "name", "model"});
        if (out["models"].size() == 0) {
            // Fallback para a API nativa do Ollama, que lista os modelos carregados no disco.
            std::string root = base;
            if (root.size() > 3 && root.compare(root.size() - 3, 3, "/v1") == 0) root.erase(root.size() - 3);
            taskforge::rt::HttpResponse tags = get(root + "/api/tags");
            if (tags.status == 200) {
                collect(tags.body, "models", {"name", "model", "id"});
                if (out["models"].size() > 0) resp = tags;
            }
        }

        if (out["models"].size() > 0) {
            out["ok"] = true;
        } else if (resp.status == 200) {
            out["ok"] = true;   // servidor respondeu, mas nao listou modelos
        } else if (resp.status == 0) {
            out["errorCode"] = "unreachable";
            out["error"] = resp.error;
        } else {
            out["errorCode"] = "http_error";
            out["error"] = "HTTP " + std::to_string(resp.status);
        }
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["errorCode"] = "bad_request";
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// ---------------------------------------------------------------------------
// Modelos locais: catalogo, download, verificacao e remocao (Fase 1)
// ---------------------------------------------------------------------------

void ui_local_models_state(webui::window::event* e) {
    try {
        e->return_string(taskforge::local::LocalRuntime::instance().state().dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"error", std::string(ex.what())}}.dump());
    }
}

// Payload: {"modelId": "qwen2.5-7b-instruct-q4_k_m"}. Devolve {ok, jobId, error}.
void ui_local_model_download(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        const std::string modelId = req.value("modelId", std::string());
        std::string error;
        const std::string jobId = taskforge::local::LocalRuntime::instance().startModelDownload(modelId, error);
        out["ok"] = !jobId.empty();
        out["jobId"] = jobId;
        if (!error.empty()) out["error"] = error;
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"jobId": "dl-..."} ou vazio para listar todos.
void ui_local_model_download_status(webui::window::event* e) {
    taskforge::Json out;
    try {
        const std::string payload = e->get_string();
        if (payload.empty()) {
            out["ok"] = true;
            out["downloads"] = taskforge::local::LocalRuntime::instance().downloads();
        } else {
            auto req = taskforge::jsonutil::parse(payload);
            out = taskforge::local::LocalRuntime::instance().downloadStatus(req.value("jobId", std::string()));
        }
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"jobId": "dl-..."}.
void ui_local_model_download_cancel(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        out["ok"] = taskforge::local::LocalRuntime::instance().cancelDownload(req.value("jobId", std::string()));
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"modelId": "..."}.
void ui_local_model_delete(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        out = taskforge::local::LocalRuntime::instance().deleteModel(req.value("modelId", std::string()));
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"modelId": "..."}.
void ui_local_model_verify(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        out = taskforge::local::LocalRuntime::instance().verifyModel(req.value("modelId", std::string()));
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

void ui_local_runtime_status(webui::window::event* e) {
    try {
        e->return_string(taskforge::local::LocalRuntime::instance().runtimeStatus().dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"error", std::string(ex.what())}}.dump());
    }
}

// Baixa e extrai o llama.cpp pinado para a plataforma atual (Vulkan com fallback CPU).
void ui_local_runtime_install(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto& runtime = taskforge::local::LocalRuntime::instance();
        std::string backend;
        const std::string payload = e->get_string();
        if (!payload.empty()) {
            auto req = taskforge::jsonutil::parse(payload);
            backend = req.value("backend", std::string());
        }
        if (runtime.runtimeInstalled(backend)) {
            out["ok"] = true;
            out["installed"] = true;
            out["jobId"] = "";
        } else {
            std::string error;
            const std::string jobId = runtime.installRuntime(error, backend);
            if (error == "already_installed") {
                out["ok"] = true;
                out["installed"] = true;
                out["jobId"] = "";
            } else {
                out["ok"] = !jobId.empty();
                out["installed"] = false;
                out["jobId"] = jobId;
                if (!error.empty()) out["error"] = error;
            }
        }
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

void ui_local_runtime_start(webui::window::event* e) {
    try {
        std::string backend;
        const std::string payload = e->get_string();
        if (!payload.empty()) {
            auto req = taskforge::jsonutil::parse(payload);
            backend = req.value("backend", std::string());
        }
        e->return_string(taskforge::local::LocalRuntime::instance().startServer(backend).dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"ok", false}, {"error", std::string(ex.what())}}.dump());
    }
}

void ui_local_runtime_stop(webui::window::event* e) {
    try {
        e->return_string(taskforge::local::LocalRuntime::instance().stopServer().dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"ok", false}, {"error", std::string(ex.what())}}.dump());
    }
}

void ui_local_runtime_logs(webui::window::event* e) {
    try {
        e->return_string(taskforge::local::LocalRuntime::instance().serverLogs().dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"error", std::string(ex.what())}}.dump());
    }
}

void ui_local_models_open_folder(webui::window::event* e) {
    taskforge::Json out;
    try {
        const auto dir = taskforge::local::LocalRuntime::instance().modelsDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        out["ok"] = taskforge::platform::openFolderInExplorer(dir);
        out["path"] = taskforge::project::pathToUtf8(dir);
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

// Wrapper JS -> C++ (Remove uma variavel de ambiente do usuario)
void ui_clear_env_var(webui::window::event* e) {
    const std::string key = e->get_string();
    if (!isValidEnvName(key)) { e->return_bool(false); return; }
    e->return_bool(taskforge::platform::clearUserEnv(key));
}

// Wrapper JS -> C++ (Testa /health do AI Gateway)
void ui_ai_gateway_health(webui::window::event* e) {
    taskforge::Json out;
    try {
        std::string url;
        std::string key;
        const std::string payload = e->get_string();
        if (!payload.empty()) {
            auto req = taskforge::jsonutil::parse(payload);
            url = req.value("url", std::string());
            key = req.value("key", std::string());
        }
        if (url.empty()) url = taskforge::platform::getEnv("ARESTRIX_AI_GATEWAY_URL");
        if (key.empty()) key = taskforge::platform::getEnv("ARESTRIX_AI_GATEWAY_KEY");
        while (!url.empty() && url.back() == '/') url.pop_back();
        if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
            out["ok"] = false;
            out["error"] = "invalid_url";
            e->return_string(out.dump());
            return;
        }
        taskforge::rt::HttpRequest http;
        http.method = "GET";
        http.url = url + "/health";
        http.timeoutSeconds = 10;
        if (!key.empty()) http.headers["Authorization"] = "Bearer " + key;
        const auto response = taskforge::rt::httpSend(http);
        out["ok"] = response.status == 200;
        out["status"] = response.status;
        out["body"] = response.body.substr(0, 4000);
        if (response.status == 0) out["error"] = response.error;
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Wrapper JS -> C++ (Orcamento/uso da chave virtual no gateway)
void ui_ai_gateway_usage(webui::window::event* e) {
    taskforge::Json out;
    try {
        std::string url;
        std::string key;
        const std::string payload = e->get_string();
        if (!payload.empty()) {
            auto req = taskforge::jsonutil::parse(payload);
            url = req.value("url", std::string());
            key = req.value("key", std::string());
        }
        if (url.empty()) url = taskforge::platform::getEnv("ARESTRIX_AI_GATEWAY_URL");
        if (key.empty()) key = taskforge::platform::getEnv("ARESTRIX_AI_GATEWAY_KEY");
        while (!url.empty() && url.back() == '/') url.pop_back();
        taskforge::rt::HttpRequest http;
        http.method = "GET";
        http.url = url + "/v1/usage";
        http.timeoutSeconds = 10;
        if (!key.empty()) http.headers["Authorization"] = "Bearer " + key;
        const auto response = taskforge::rt::httpSend(http);
        out["ok"] = response.status == 200;
        out["status"] = response.status;
        if (response.status == 200) {
            auto parsed = taskforge::jsonutil::tryParse(response.body);
            if (parsed) out["usage"] = *parsed;
        } else {
            out["body"] = response.body.substr(0, 4000);
        }
        if (response.status == 0) out["error"] = response.error;
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
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

        std::filesystem::path p = taskforge::project::pathFromUtf8(targetPath);
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

// Wrapper JS -> C++ (Fonte GitHub de bibliotecas: manifesto ou asset sob demanda)
void ui_fetch_library_source(webui::window::event* e) {
    try {
        const std::string payload = e->get_string();
        e->return_string(taskforge::libraries::fetchLibrarySourceJson(payload));
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_fetch_library_source: " << ex.what() << "\n";
        e->return_string("{\"ok\":false,\"errorCode\":\"github_internal\",\"error\":\"Falha interna ao consultar o GitHub.\"}");
    }
}

// Wrapper JS -> C++ (Ler asset do workspace). Confinado as raizes permitidas.
void ui_read_workspace_asset(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        if (payload.empty()) { e->return_string("{\"ok\":false,\"error\":\"Payload vazio\"}"); return; }
        auto j = taskforge::jsonutil::parse(payload);
        std::string filePath = j.value("filePath", "");
        auto p = confineToAllowedRoots(filePath);
        if (!p || !std::filesystem::is_regular_file(*p)) {
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

// Wrapper JS -> C++ (Espelhar o ledger de governanca MCP no workspace). Arquivo sem segredos.
// O Assistente usa esse espelho para recusar attach_mcp/execucao de servidores nao auditados.
void ui_save_mcp_governance_ledger(webui::window::event* e) {
    try {
        const std::string payload = e->get_string();
        if (payload.empty() || payload.size() > 2 * 1024 * 1024) { e->return_bool(false); return; }
        auto parsed = taskforge::jsonutil::tryParse(payload);
        if (!parsed || !parsed->is_object() || !parsed->contains("servers") || !parsed->at("servers").is_object()) {
            e->return_bool(false);
            return;
        }
        std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
        if (root.empty()) root = std::filesystem::current_path().string();
        std::error_code ec;
        std::filesystem::path dir = std::filesystem::path(root) / ".workspace" / "governance";
        std::filesystem::create_directories(dir, ec);
        if (ec) { e->return_bool(false); return; }
        std::filesystem::path target = dir / "mcp-ledger.json";
        std::filesystem::path tmp = target;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) { e->return_bool(false); return; }
            out << parsed->dump(2);
            if (!out.good()) { e->return_bool(false); return; }
        }
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            std::filesystem::remove(target, ec);
            std::filesystem::rename(tmp, target, ec);
        }
        e->return_bool(!ec);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_mcp_governance_ledger: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper JS -> C++ (Studio settings). Arquivo de usuario, sem segredos.
void ui_get_studio_settings(webui::window::event* e) {
    try {
        const auto path = studioConfigPath();
        if (!std::filesystem::is_regular_file(path)) {
            e->return_string("");
            return;
        }
        auto settings = taskforge::jsonutil::parse(taskforge::jsonutil::readFile(path));
        taskforge::Json resp;
        resp["settings"] = settings;
        e->return_string(resp.dump());
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] get_studio_settings: " << ex.what() << "\n";
        e->return_string("");
    }
}

void ui_save_studio_settings(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        if (payload.empty()) { e->return_bool(false); return; }
        auto j = taskforge::jsonutil::parse(payload);
        taskforge::Json settings = j.contains("settings") ? j["settings"] : j;
        const std::string serialized = settings.dump(2);
        // Defesa adicional: configuracoes do Studio nao carregam segredos.
        if (serialized.find("\"apiKey\"") != std::string::npos ||
            serialized.find("\"password\"") != std::string::npos ||
            serialized.find("\"secret\"") != std::string::npos) {
            std::cerr << "[C++ Erro] save_studio_settings: tentativa de gravar segredo recusada\n";
            e->return_bool(false);
            return;
        }
        const auto path = studioConfigPath();
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) { e->return_bool(false); return; }
        out << serialized;
        e->return_bool(true);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] save_studio_settings: " << ex.what() << "\n";
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
// Devolve vazio se a execucao comecou, ou o motivo da recusa. Usado pela interface (PipelineRunner).
std::string startPipelineRun(const std::string& payload) {
    taskforge::core::Graph graph;
    if (!loadGraphFromPayload(payload, graph)) return "Grafo invalido ou vazio";
    std::string root = taskforge::project::ProjectManager::getInstance().getActiveProjectRoot();
    if (root.empty()) root = std::filesystem::current_path().string();

    if (taskforge::build::BuildService::instance().isRunning()) return "Ja existe uma execucao em andamento";
    taskforge::build::BuildEvents ev;
    resetInteractionBridge();
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
    ev.onInteraction = [](const std::string& requestJson, const std::atomic<bool>* cancel) {
        return awaitInteraction(requestJson, cancel);
    };
    ev.onFinished = [](bool success, int exitCode, long long elapsedMs, const std::string& error) {
        {
            std::lock_guard<std::mutex> lock(g_interactionMutex);
            g_interactionRequestId.clear();
            g_interactionAnswer.clear();
            g_interactionReady = false;
            g_interactionCancelled = false;
        }
        g_interactionCv.notify_all();
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
    {
        std::lock_guard<std::mutex> lock(g_interactionMutex);
        g_interactionCancelled = true;
    }
    g_interactionCv.notify_all();
    taskforge::build::BuildService::instance().stop();
    e->return_bool(true);
}

// Wrapper JS -> C++ (Responder a um pedido de interacao humana).
// Payload: {"requestId":"...","answers":{...}} ou {"requestId":"...","cancelled":true}.
void ui_answer_interaction(webui::window::event* e) {
    try {
        auto payload = taskforge::jsonutil::parse(e->get_string());
        if (!payload.is_object()) {
            e->return_string(taskforge::Json{{"ok", false}, {"error", "Payload de resposta invalido."}}.dump());
            return;
        }
        const std::string requestId = payload.value("requestId", std::string());
        taskforge::Json out;
        {
            std::lock_guard<std::mutex> lock(g_interactionMutex);
            if (requestId.empty() || requestId != g_interactionRequestId) {
                out = taskforge::Json{{"ok", false}, {"error", "Nao ha interacao pendente com este requestId."}};
            } else {
                g_interactionAnswer = payload.dump();
                g_interactionReady = true;
                out = taskforge::Json{{"ok", true}};
            }
        }
        if (out.value("ok", false)) g_interactionCv.notify_all();
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"ok", false}, {"error", ex.what()}}.dump());
    }
}

// ============================================================================
// Wrappers JS -> C++ (Git Sync & Integracao com SO)
// ============================================================================

void ui_git_get_status(webui::window::event* e) {
    try {
        std::string payload = e->get_string();
        std::string root = "";
        if (!payload.empty() && payload[0] == '{') {
            auto j = taskforge::jsonutil::parse(payload);
            root = j.value("rootPath", "");
        } else if (!payload.empty()) {
            root = payload;
        }
        if (root.empty()) {
            root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;
        }
        auto st = taskforge::project::GitSyncService::getStatus(root);
        taskforge::Json j = st;
        e->return_string(j.dump());
    } catch (const std::exception& ex) {
        taskforge::Json err = {{"error", ex.what()}};
        e->return_string(err.dump());
    }
}

void ui_git_get_system_identity(webui::window::event* e) {
    std::string name, email;
    bool ok = taskforge::project::GitSyncService::getSystemIdentity(name, email);
    taskforge::Json res = {{"configured", ok}, {"userName", name}, {"userEmail", email}};
    e->return_string(res.dump());
}

void ui_git_set_system_identity(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string name = j.value("name", "");
        std::string email = j.value("email", "");
        std::string err;
        bool ok = taskforge::project::GitSyncService::setSystemIdentity(name, email, err);
        taskforge::Json res = {{"ok", ok}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_list_accounts(webui::window::event* e) {
    std::string provider = e->get_string();
    auto accounts = taskforge::project::GitSyncService::listGcmAccounts(provider);
    taskforge::Json res = accounts;
    e->return_string(res.dump());
}

void ui_git_launch_login(webui::window::event* e) {
    std::string provider = e->get_string();
    std::string err;
    bool ok = taskforge::project::GitSyncService::launchGcmLogin(provider, err);
    taskforge::Json res = {{"ok", ok}, {"error", err}};
    e->return_string(res.dump());
}

void ui_git_init_project(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string root = j.value("rootPath", "");
        if (root.empty()) root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;
        std::string remoteUrl = j.value("remoteUrl", "");
        std::string err;
        bool ok = taskforge::project::GitSyncService::initProjectRepo(root, remoteUrl, err);
        taskforge::Json res = {{"ok", ok}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_create_remote_repo(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string provider = j.value("provider", "github");
        std::string repoName = j.value("repoName", "");
        bool isPrivate = j.value("isPrivate", true);
        std::string username = j.value("username", "");
        std::string serverUrl = j.value("serverUrl", "https://github.com");
        std::string root = j.value("rootPath", "");
        if (root.empty()) root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;

        std::string remoteUrl, err;
        bool ok = taskforge::project::GitSyncService::createRemoteRepo(provider, repoName, isPrivate, username, serverUrl, remoteUrl, err);
        if (ok && !root.empty()) {
            std::string initErr;
            taskforge::project::GitSyncService::initProjectRepo(root, remoteUrl, initErr);
        }
        taskforge::Json res = {{"ok", ok}, {"remoteUrl", remoteUrl}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_commit_local(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string root = j.value("rootPath", "");
        if (root.empty()) root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;
        std::string msg = j.value("message", "");
        std::string out, err;
        bool ok = taskforge::project::GitSyncService::commitLocal(root, msg, out, err);
        taskforge::Json res = {{"ok", ok}, {"output", out}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_push_remote(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string root = j.value("rootPath", "");
        if (root.empty()) root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;
        std::string branch = j.value("branch", "main");
        std::string out, err;
        bool ok = taskforge::project::GitSyncService::pushRemote(root, branch, out, err);
        taskforge::Json res = {{"ok", ok}, {"output", out}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_commit_push(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string root = j.value("rootPath", "");
        if (root.empty()) root = taskforge::project::ProjectManager::getInstance().getCurrentProject().rootPath;
        std::string msg = j.value("message", "");
        std::string branch = j.value("branch", "main");
        std::string out, err;
        bool ok = taskforge::project::GitSyncService::commitAndPush(root, msg, branch, out, err);
        taskforge::Json res = {{"ok", ok}, {"output", out}, {"error", err}};
        e->return_string(res.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_git_is_commercial(webui::window::event* e) {
    std::string s = e->get_string();
    bool comm = taskforge::project::GitSyncService::isCommercialService(s);
    e->return_bool(comm);
}

void ui_get_runner_config(webui::window::event* e) {
    auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    taskforge::Json res = cur.runner;
    e->return_string(res.dump());
}

void ui_set_runner_config(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        taskforge::project::ProjectRunnerConfig cfg = j.get<taskforge::project::ProjectRunnerConfig>();
        auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();
        cur.runner = cfg;
        taskforge::project::ProjectManager::getInstance().saveProject(cur);
        e->return_string("{\"ok\":true}");
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_export_standalone_runner(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        auto& pm = taskforge::project::ProjectManager::getInstance();
        auto cur = pm.getCurrentProject();
        std::string root = cur.rootPath.empty() ? std::filesystem::current_path().string() : cur.rootPath;

        if (j.contains("runner") && j["runner"].is_object()) {
            cur.runner = j["runner"].get<taskforge::project::ProjectRunnerConfig>();
            pm.saveProject(cur);
        }

        taskforge::core::Graph g;
        if (j.contains("graph") && j["graph"].is_object()) {
            loadGraphFromPayload(j["graph"].dump(), g);
        } else {
            g = globalGraph;
        }

        auto exportRes = taskforge::project::RunnerService::exportRunner(root, cur, g);
        taskforge::Json warnings = taskforge::Json::array();
        for (const auto& warning : exportRes.warnings) warnings.push_back(warning);
        taskforge::Json out = {
            {"ok", exportRes.ok},
            {"error", exportRes.error},
            {"outputDir", exportRes.outputDir},
            {"executablePath", exportRes.executablePath},
            {"manifestPath", exportRes.manifestPath},
            {"warnings", warnings}
        };
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void ui_launch_standalone_runner(webui::window::event* e) {
    try {
        auto j = taskforge::jsonutil::parse(e->get_string());
        std::string exe = j.value("executablePath", "");
        auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();
        std::string root = cur.rootPath.empty() ? std::filesystem::current_path().string() : cur.rootPath;
        bool launched = taskforge::project::RunnerService::launchRunner(root, exe);
        taskforge::Json out = {{"ok", launched}};
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        taskforge::Json res = {{"ok", false}, {"error", ex.what()}};
        e->return_string(res.dump());
    }
}

void exit_app(webui::window::event* e) {
    std::cout << "[C++] Comando de encerramento recebido." << std::endl;
    taskforge::local::LocalRuntime::instance().stopServer();
    auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    if (!cur.rootPath.empty()) {
        if (cur.git.enabled && cur.git.autoCommitOnShutdown) {
            taskforge::project::GitSyncService::onShutdown(cur.rootPath);
        }
    }
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

// Estado do modelo de compressao de prosa (Kompress) no disco. Nao carrega o modelo: apenas
// informa se os arquivos existem, para a tela de Propriedades mostrar o status sem custo.
void ui_prose_model_status(webui::window::event* e) {
    try {
        const std::string dir = taskforge::rt::compress::defaultProseModelDir();
        bool hasTokenizer = false;
        bool hasConfig = false;
        bool hasModel = false;
        taskforge::Json files = taskforge::Json::array();
        for (const char* name : {"kompress-fp32.onnx", "kompress-int8-wo.onnx", "kompress-int8.onnx",
                                 "tokenizer.json", "config.json"}) {
            std::error_code ec;
            const fs::path file = fs::path(dir) / name;
            if (!fs::is_regular_file(file, ec)) continue;
            const auto size = static_cast<long long>(fs::file_size(file, ec));
            files.push_back(taskforge::Json{{"name", name}, {"size", size}});
            const std::string base = name;
            if (base == "tokenizer.json") hasTokenizer = true;
            else if (base == "config.json") hasConfig = true;
            else hasModel = true;
        }
        taskforge::Json out = taskforge::Json::object();
        out["dir"] = dir;
        out["available"] = hasTokenizer && hasConfig && hasModel;
        out["files"] = files;
        out["installedVariants"] = taskforge::Json{
            {"fp32", taskforge::rt::compress::proseModelVariantInstalled("fp32")},
            {"int8", taskforge::rt::compress::proseModelVariantInstalled("int8")}};
#ifdef ARESTRIX_WITH_ONNX
        const bool onnxBuild = true;
#else
        const bool onnxBuild = false;
#endif
#ifdef ARESTRIX_WITH_ONNX_DML
        const bool dmlBuild = true;
#else
        const bool dmlBuild = false;
#endif
        out["build"] = taskforge::Json{{"onnx", onnxBuild}, {"dml", dmlBuild}};

        // Sugestao baseada na maquina: RAM/disco/CPU decidem entre fp32 (rapido) e int8 (compacto).
        const auto profile = taskforge::rt::compress::detectMachineProfile(fs::path(dir));
        const auto recommendation = taskforge::rt::compress::recommendProseVariant(profile);
        taskforge::Json gpus = taskforge::Json::array();
        std::string gpuName;
        for (const auto& gpu : profile.gpus) {
            gpus.push_back(taskforge::Json{{"index", gpu.index},
                                           {"name", gpu.name},
                                           {"vramMB", gpu.vramMB},
                                           {"vendor", gpu.vendor}});
            if (gpu.index == profile.dmlDevice) gpuName = gpu.name;
        }
        out["hardware"] = taskforge::Json{{"cpuThreads", profile.cpuThreads},
                                          {"ramTotalMB", profile.ramTotalMB},
                                          {"ramFreeMB", profile.ramFreeMB},
                                          {"diskFreeMB", profile.diskFreeMB},
                                          {"dmlAvailable", profile.dmlAvailable},
                                          {"dmlDevice", profile.dmlDevice},
                                          {"gpus", std::move(gpus)}};
        out["recommendation"] = taskforge::Json{
            {"variant", recommendation.variant},
            {"reason", recommendation.reason},
            {"gpuName", gpuName},
            {"sizeMB", taskforge::rt::compress::proseModelSizeMB(recommendation.variant)}};
        e->return_string(out.dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"dir", ""}, {"available", false}, {"error", ex.what()}}.dump());
    }
}

// Payload: {"variant": "fp32" | "int8" | "gpu"}. Devolve {ok, jobId, error}.
void ui_prose_model_download(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        std::string error;
        const std::string jobId = taskforge::rt::compress::startProseModelDownload(
            req.value("variant", std::string("fp32")), error);
        out["ok"] = !jobId.empty();
        out["jobId"] = jobId;
        if (!error.empty()) out["error"] = error;
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"jobId": "pd-..."} ou vazio para listar todos.
void ui_prose_model_download_status(webui::window::event* e) {
    taskforge::Json out;
    try {
        const std::string payload = e->get_string();
        if (payload.empty()) {
            out["ok"] = true;
            out["downloads"] = taskforge::rt::compress::proseModelDownloads();
        } else {
            auto req = taskforge::jsonutil::parse(payload);
            out = taskforge::rt::compress::proseModelDownloadStatus(
                req.value("jobId", std::string()));
        }
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

// Payload: {"jobId": "pd-..."}.
void ui_prose_model_download_cancel(webui::window::event* e) {
    taskforge::Json out;
    try {
        auto req = taskforge::jsonutil::parse(e->get_string());
        out["ok"] = taskforge::rt::compress::cancelProseModelDownload(
            req.value("jobId", std::string()));
    } catch (const std::exception& ex) {
        out["ok"] = false;
        out["error"] = std::string(ex.what());
    }
    e->return_string(out.dump());
}

void ui_prose_model_manifest(webui::window::event* e) {
    try {
        e->return_string(taskforge::rt::compress::proseModelManifest().dump());
    } catch (const std::exception& ex) {
        e->return_string(taskforge::Json{{"error", std::string(ex.what())}}.dump());
    }
}

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

    bool updateProject(const taskforge::Json& patch, taskforge::Json& updated, std::string& error) override {
        auto& pm = taskforge::project::ProjectManager::getInstance();
        auto manifest = pm.getCurrentProject();
        if (manifest.projectId.empty()) {
            error = "nenhum projeto ativo";
            return false;
        }
        auto trimmed = [](std::string s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
            return s;
        };
        bool changed = false;
        if (patch.contains("name")) {
            if (!patch.at("name").is_string()) { error = "o nome deve ser texto"; return false; }
            std::string name = trimmed(patch.at("name").get<std::string>());
            while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
            if (name.empty() || name.find_first_of("<>:\"/\\|?*\r\n") != std::string::npos) {
                error = "nome de projeto invalido";
                return false;
            }
            if (name != manifest.name) { manifest.name = name; changed = true; }
        }
        if (patch.contains("version")) {
            if (!patch.at("version").is_string()) { error = "a versao deve ser texto"; return false; }
            std::string version = trimmed(patch.at("version").get<std::string>());
            if (version.empty() || version.size() > 32 || version.find_first_of("\r\n") != std::string::npos) {
                error = "versao de projeto invalida";
                return false;
            }
            if (version != manifest.version) { manifest.version = version; changed = true; }
        }
        if (patch.contains("description")) {
            if (!patch.at("description").is_string()) { error = "a descricao deve ser texto"; return false; }
            std::string description = patch.at("description").get<std::string>();
            if (description.size() > 2000) { error = "descricao longa demais"; return false; }
            if (description != manifest.description) { manifest.description = description; changed = true; }
        }
        if (patch.contains("outputDir")) {
            if (!patch.at("outputDir").is_string()) { error = "a pasta de saida deve ser texto"; return false; }
            std::string outputDir = trimmed(patch.at("outputDir").get<std::string>());
            std::filesystem::path outPath(outputDir);
            bool unsafe = outputDir.empty() || outputDir.size() > 200 || outPath.is_absolute() ||
                          outPath.has_root_name() || outPath.has_root_directory();
            for (const auto& part : outPath) if (part == "..") unsafe = true;
            if (unsafe) { error = "pasta de saida invalida"; return false; }
            if (outputDir != manifest.outputDir) { manifest.outputDir = outputDir; changed = true; }
        }
        if (!changed) {
            error = "nada para alterar";
            return false;
        }
        if (!pm.saveProject(manifest)) {
            error = "nao foi possivel gravar o manifesto do projeto";
            return false;
        }
        taskforge::Json j = pm.getCurrentProject();
        updated = j;
        return true;
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
    taskforge::platform::setupStudioConsole();
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
    win.bind("get_last_trace", ui_get_last_trace);
    win.bind("prose_model_status", ui_prose_model_status);
    win.bind("prose_model_manifest", ui_prose_model_manifest);
    win.bind("prose_model_download", ui_prose_model_download);
    win.bind("prose_model_download_status", ui_prose_model_download_status);
    win.bind("prose_model_download_cancel", ui_prose_model_download_cancel);
    win.bind("reset_pipeline_checkpoint", ui_reset_pipeline_checkpoint);
    win.bind("get_memory_status", ui_get_memory_status);
    win.bind("set_memory_persistence", ui_set_memory_persistence);
    win.bind("clear_memory", ui_clear_memory);
    win.bind("prune_memory", ui_prune_memory);
    win.bind("accept_memory_proposal", ui_accept_memory_proposal);
    win.bind("reject_memory_proposal", ui_reject_memory_proposal);
    win.bind("test_memory_embeddings", ui_test_memory_embeddings);
    win.bind("reindex_memory_embeddings", ui_reindex_memory_embeddings);
    win.bind("consolidate_memory", ui_consolidate_memory);
    win.bind("init_workspace", ui_init_workspace);
    win.bind("promote_to_workspace", ui_promote_to_workspace);
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
    win.bind("list_local_models", ui_list_local_models);
    win.bind("local_models_state", ui_local_models_state);
    win.bind("local_runtime_status", ui_local_runtime_status);
    win.bind("local_runtime_install", ui_local_runtime_install);
    win.bind("local_runtime_start", ui_local_runtime_start);
    win.bind("local_runtime_stop", ui_local_runtime_stop);
    win.bind("local_runtime_logs", ui_local_runtime_logs);
    win.bind("local_model_download", ui_local_model_download);
    win.bind("local_model_download_status", ui_local_model_download_status);
    win.bind("local_model_download_cancel", ui_local_model_download_cancel);
    win.bind("local_model_delete", ui_local_model_delete);
    win.bind("local_model_verify", ui_local_model_verify);
    win.bind("local_models_open_folder", ui_local_models_open_folder);
    win.bind("set_http_policy", ui_set_http_policy);
    win.bind("search_ensure_synonyms", ui_search_ensure_synonyms);
    win.bind("list_recent_projects", ui_list_recent_projects);
    win.bind("scan_pi_project", ui_scan_pi_project);
    win.bind("get_known_pi_projects", ui_get_known_pi_projects);
    win.bind("get_env_var", ui_get_env_var);
    win.bind("clear_env_var", ui_clear_env_var);
    win.bind("ai_gateway_health", ui_ai_gateway_health);
    win.bind("ai_gateway_usage", ui_ai_gateway_usage);
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
    win.bind("read_workspace_asset", ui_read_workspace_asset);
    win.bind("save_mcp_governance_ledger", ui_save_mcp_governance_ledger);
    win.bind("fetch_library_source", ui_fetch_library_source);
    win.bind("get_studio_settings", ui_get_studio_settings);
    win.bind("save_studio_settings", ui_save_studio_settings);
    win.bind("write_skill_file", ui_write_skill_file);
    win.bind("read_skill_file", ui_read_skill_file);
    win.bind("open_in_vscode", ui_open_in_vscode);
    win.bind("open_output_folder", ui_open_output_folder);
    win.bind("list_artifacts", ui_list_artifacts);
    win.bind("get_artifact", ui_get_artifact);
    win.bind("run_pipeline", ui_run_pipeline);
    win.bind("stop_pipeline", ui_stop_pipeline);
    win.bind("answer_interaction", ui_answer_interaction);
    win.bind("assistant_status", ui_assistant_status);
    win.bind("assistant_sessions", ui_assistant_sessions);
    win.bind("assistant_session_load", ui_assistant_session_load);
    win.bind("assistant_session_delete", ui_assistant_session_delete);
    win.bind("assistant_send", ui_assistant_send);
    win.bind("assistant_confirm", ui_assistant_confirm);
    win.bind("assistant_cancel", ui_assistant_cancel);
    win.bind("exit_app", exit_app);

    // Bindings de Git Sync & SO
    win.bind("git_get_status", ui_git_get_status);
    win.bind("git_get_system_identity", ui_git_get_system_identity);
    win.bind("git_set_system_identity", ui_git_set_system_identity);
    win.bind("git_list_accounts", ui_git_list_accounts);
    win.bind("git_launch_login", ui_git_launch_login);
    win.bind("git_init_project", ui_git_init_project);
    win.bind("git_create_remote_repo", ui_git_create_remote_repo);
    win.bind("git_commit_local", ui_git_commit_local);
    win.bind("git_push_remote", ui_git_push_remote);
    win.bind("git_commit_push", ui_git_commit_push);
    win.bind("git_is_commercial", ui_git_is_commercial);

    // Bindings de Standalone Runner (Fase 1)
    win.bind("get_runner_config", ui_get_runner_config);
    win.bind("set_runner_config", ui_set_runner_config);
    win.bind("export_standalone_runner", ui_export_standalone_runner);
    win.bind("launch_standalone_runner", ui_launch_standalone_runner);

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
    taskforge::local::LocalRuntime::instance().stopServer();
    auto curProj = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    if (!curProj.rootPath.empty()) {
        if (curProj.git.enabled && curProj.git.autoCommitOnShutdown) {
            taskforge::project::GitSyncService::onShutdown(curProj.rootPath);
        }
    }
    taskforge::mcp::McpClient::shutdownSessions();   // encerra os servidores MCP que ficaram abertos
    return 0;
}
