#include "webui.hpp"
#include "core/Graph.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "sandbox/SandboxManager.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "sandbox/PipelineResumeEngine.hpp"
#include "memory/AgentBlackboard.hpp"
#include "memory/SemanticMemoryEngine.hpp"
#include "mcp/McpRegistry.hpp"
#include "project/ProjectManager.hpp"
#include "pi/PiProjectScanner.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// Instância global do nosso grafo central em C++
taskforge::core::Graph globalGraph;

// Wrapper JS -> C++ (Preview Code)
void ui_get_cpp_preview(webui::window::event* e) {
    nlohmann::json result;
    result["cpp"] = taskforge::compiler::TaskflowGenerator::generateSource(globalGraph);
    result["cmake"] = taskforge::compiler::TaskflowGenerator::generateCMakeLists(globalGraph);
    result["make"] = taskforge::compiler::TaskflowGenerator::generateMakefile(globalGraph);
    auto msvc = taskforge::compiler::TaskflowGenerator::generateMsvcBuildScript(globalGraph);
    result["msvc_ps1"] = msvc.ps1;
    
    e->return_string(result.dump());
}

// Wrapper de comunicação JS -> C++ (Gerar Código C++)
void ui_generate_cpp_code(webui::window::event* e) {
    try {
        std::cout << "[C++] Gerando codigo C++ Taskflow a partir do grafo atual...\n";
        std::string cppCode = taskforge::compiler::TaskflowGenerator::generateSource(globalGraph);
        std::string cmakeCode = taskforge::compiler::TaskflowGenerator::generateCMakeLists(globalGraph);
        auto msvcScripts = taskforge::compiler::TaskflowGenerator::generateMsvcBuildScript(globalGraph);
        
        std::filesystem::path rootPath = std::filesystem::current_path() / "Output";
        std::filesystem::create_directories(rootPath);
        
        std::ofstream fileCpp(rootPath / "main.cpp");
        fileCpp << cppCode;
        fileCpp.close();

        std::ofstream fileCmake(rootPath / "CMakeLists.txt");
        fileCmake << cmakeCode;
        fileCmake.close();

        std::ofstream fileBat(rootPath / "build.bat");
        fileBat << msvcScripts.bat;
        fileBat.close();
        
        std::cout << "[C++] Arquivos gerados com sucesso na pasta: " << rootPath.string() << "\n";
        e->return_bool(true);
    } catch(const std::exception& ex) {
        std::cerr << "[C++ Erro] Falha ao gerar codigo: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Adicionar Nó)
void ui_add_node(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto nodeData = nlohmann::json::parse(jsonStr).get<taskforge::core::NodeData>();
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
        auto edgeData = nlohmann::json::parse(jsonStr).get<taskforge::core::EdgeData>();
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
    nlohmann::json state = globalGraph.toJson();
    e->return_string(state.dump());
}

// Wrapper de comunicação JS -> C++ (Status dos Drivers de Sandbox)
void ui_get_sandbox_status(webui::window::event* e) {
    nlohmann::json status;
    auto& mgr = taskforge::sandbox::SandboxManager::instance();
    auto docker = mgr.getDriver(taskforge::sandbox::IsolationType::Container);
    auto microvm = mgr.getDriver(taskforge::sandbox::IsolationType::MicroVM);
    auto host = mgr.getDriver(taskforge::sandbox::IsolationType::HostDirect);

    status["docker"] = docker->isAvailable();
    status["microvm"] = microvm->isAvailable();
    status["host"] = host->isAvailable();

    e->return_string(status.dump());
}

// Wrapper de comunicação JS -> C++ (Obter Checkpoint do Resume Engine)
void ui_get_pipeline_checkpoint(webui::window::event* e) {
    auto& engine = taskforge::sandbox::PipelineResumeEngine::getInstance();
    nlohmann::json st = engine.getStateJson();
    e->return_string(st.dump());
}

// Wrapper de comunicação JS -> C++ (Resetar Checkpoint)
void ui_reset_pipeline_checkpoint(webui::window::event* e) {
    std::string payload = e->get_string();
    std::string itemId = "";
    std::string stageId = "";
    try {
        if (!payload.empty() && payload[0] == '{') {
            auto j = nlohmann::json::parse(payload);
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
            auto j = nlohmann::json::parse(payload);
            projectId = j.value("projectId", "default");
        } else if (!payload.empty()) {
            projectId = payload;
        }
    } catch (...) {}

    auto& bb = taskforge::memory::AgentBlackboard::getInstance();
    auto& sem = taskforge::memory::SemanticMemoryEngine::getInstance();

    nlohmann::json res;
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
            auto j = nlohmann::json::parse(payload);
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
            auto j = nlohmann::json::parse(payload);
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
            auto j = nlohmann::json::parse(payload);
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
    nlohmann::json j = servers;
    e->return_string(j.dump());
}

// Wrapper de comunicação JS -> C++ (Salvar Servidor MCP)
void ui_save_mcp_server(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto cfg = nlohmann::json::parse(jsonStr).get<taskforge::mcp::McpServerConfig>();
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
        auto j = nlohmann::json::parse(jsonStr);
        std::string serverId = j.value("serverId", "");
        std::string toolName = j.value("toolName", "");
        nlohmann::json args = j.value("arguments", nlohmann::json::object());

        auto result = taskforge::mcp::McpRegistry::getInstance().callTool(serverId, toolName, args);
        nlohmann::json resJson = result;
        e->return_string(resJson.dump());
    } catch (const std::exception& ex) {
        nlohmann::json err = {
            {"success", false},
            {"isError", true},
            {"errorMessage", std::string("Excecao na chamada MCP: ") + ex.what()}
        };
        e->return_string(err.dump());
    }
}

// Wrapper de comunicação JS -> C++ (Criar Projeto)
void ui_create_project(webui::window::event* e) {
    std::string jsonStr = e->get_string();
    try {
        auto manifest = nlohmann::json::parse(jsonStr).get<taskforge::project::ProjectManifest>();
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
        auto manifest = nlohmann::json::parse(jsonStr).get<taskforge::project::ProjectManifest>();
        bool ok = taskforge::project::ProjectManager::getInstance().saveProject(manifest);
        e->return_bool(ok);
    } catch (const std::exception& ex) {
        std::cerr << "[C++ Erro] ui_save_project: " << ex.what() << "\n";
        e->return_bool(false);
    }
}

// Wrapper de comunicação JS -> C++ (Obter Projeto Ativo)
void ui_get_current_project(webui::window::event* e) {
    auto proj = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    nlohmann::json j = proj;
    e->return_string(j.dump());
}

// Wrapper de comunicação JS -> C++ (Listar Projetos Recentes)
void ui_list_recent_projects(webui::window::event* e) {
    auto list = taskforge::project::ProjectManager::getInstance().listRecentProjects();
    nlohmann::json j = list;
    e->return_string(j.dump());
}

// Wrapper JS -> C++ (Escanear Projeto Pi)
void ui_scan_pi_project(webui::window::event* e) {
    std::string pathStr = e->get_string();
    auto result = taskforge::pi::PiProjectScanner::scan(pathStr);
    e->return_string(taskforge::pi::PiProjectScanner::toJson(result).dump());
}

// Wrapper JS -> C++ (Obter Projetos Pi Conhecidos)
void ui_get_known_pi_projects(webui::window::event* e) {
    auto list = taskforge::pi::PiProjectScanner::getKnownPiProjects();
    nlohmann::json j = list;
    e->return_string(j.dump());
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
#ifdef _WIN32
    wchar_t exeBuffer[MAX_PATH];
    if (GetModuleFileNameW(NULL, exeBuffer, MAX_PATH) > 0) {
        std::filesystem::path exeDir = std::filesystem::path(exeBuffer).parent_path();
        candidates.push_back(exeDir);
        candidates.push_back(exeDir.parent_path());
    }
#endif

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

} // namespace

int main() {
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
    win.bind("get_current_project", ui_get_current_project);
    win.bind("list_recent_projects", ui_list_recent_projects);
    win.bind("scan_pi_project", ui_scan_pi_project);
    win.bind("get_known_pi_projects", ui_get_known_pi_projects);
    win.bind("exit_app", exit_app);

    // Registra o Emitter de Telemetria de Agentes em Tempo Real (Fase 10)
    taskforge::sandbox::AgentTelemetryEmitter::setCallback([&win](const std::string& jsonMsg) {
        std::string jsCode = "if (window.onArestrixTelemetry) { window.onArestrixTelemetry(" + jsonMsg + "); } else if (window.onTaskForgeTelemetry) { window.onTaskForgeTelemetry(" + jsonMsg + "); }";
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
    return 0;
}
