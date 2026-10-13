#include "cli/Cli.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#include "build/BuildService.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "core/Graph.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace cli {

int runCli(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Uso: arestrix run|gen <grafo.json> [--root <pasta-do-projeto>]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    std::filesystem::path graphFile = argv[2];
    std::filesystem::path root = std::filesystem::current_path();
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--root") root = argv[i + 1];
    }

    core::Graph graph;
    try {
        auto j = jsonutil::parseFile(graphFile);
        if (j.contains("schema") && j["schema"].is_object()) graph.fromJson(j["schema"]);
        else graph.fromJson(j);
    } catch (const std::exception& ex) {
        std::cerr << "Erro ao ler o grafo: " << ex.what() << "\n";
        return 2;
    }

    if (cmd == "gen") {
        std::cout << compiler::TaskflowGenerator::generateSource(graph);
        return 0;
    }
    if (cmd != "run") {
        std::cerr << "Comando desconhecido: " << cmd << "\n";
        return 2;
    }

    build::BuildEvents ev;
    ev.onPhase = [](const std::string& phase, const std::string& msg) { std::cout << "[" << phase << "] " << msg << std::endl; };
    ev.onLog = [](const std::string& line, bool) { std::cout << line << std::endl; };
    ev.onTelemetry = [](const std::string& line) { std::cout << "[telemetria] " << line << std::endl; };
    auto result = build::buildAndRun(graph, root, ev);
    if (!result.success) {
        std::cerr << "[FALHA:" << result.stage << "] " << result.error << std::endl;
        return 1;
    }
    std::cout << "[OK] Pipeline concluido em " << result.elapsedMs << " ms" << std::endl;
    return 0;
}

} // namespace cli
} // namespace taskforge
