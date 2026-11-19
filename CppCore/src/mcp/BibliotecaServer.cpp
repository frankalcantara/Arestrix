// Executavel do servidor MCP da Biblioteca de Vertices e Agentes.
//
// Uso:
//   arestrix-mcp-biblioteca --root <pasta do projeto>
//   arestrix-mcp-biblioteca --scope <pasta do projeto>
//
// Sem argumento, usa ARESTRIX_PROJECT_ROOT ou o diretorio atual. O protocolo
// roda em stdin/stdout; logs e erros fatais vao para stderr.

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "libraries/LibraryMcpServer.hpp"

namespace fs = std::filesystem;

namespace {

fs::path projectRootFromArgs(int argc, char** argv) {
    fs::path root;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--root" || arg == "--scope") && i + 1 < argc) {
            root = argv[++i];
        } else if (arg.rfind("--root=", 0) == 0) {
            root = arg.substr(7);
        } else if (arg.rfind("--scope=", 0) == 0) {
            root = arg.substr(8);
        }
    }
    if (root.empty()) {
        if (const char* env = std::getenv("ARESTRIX_PROJECT_ROOT"); env && *env) root = env;
    }
    if (root.empty()) {
        std::error_code ec;
        root = fs::current_path(ec);
    }
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) root = root.parent_path();
    if (root.filename() == ".workspace") root = root.parent_path();
    return root;
}

} // namespace

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    return taskforge::libraries::runLibraryMcpServer(std::cin, std::cout, projectRootFromArgs(argc, argv));
}
