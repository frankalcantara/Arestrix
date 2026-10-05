#include "webui.hpp"
#include "core/Graph.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>

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

void exit_app(webui::window::event* e) {
    std::cout << "[C++] Comando de encerramento recebido." << std::endl;
    webui::exit();
}

int main() {
    std::cout << "[C++] Inicializando TaskForge Engine (C++)..." << std::endl;

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
    win.bind("exit_app", exit_app);

    std::cout << "[C++] Servindo a UI oficial..." << std::endl;
    win.show("dist/index.html");

    webui::wait();
    return 0;
}
