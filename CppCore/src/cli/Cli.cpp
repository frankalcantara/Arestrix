#include "cli/Cli.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "build/BuildService.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "core/Graph.hpp"
#include "platform/Process.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace cli {

namespace fs = std::filesystem;
using json = taskforge::Json;

namespace {

constexpr int kExitRuntime = 1;
constexpr int kExitUsage = 2;
constexpr int kExitConfig = 3;
constexpr int kExitDependency = 4;
constexpr int kExitPreflight = 5;
constexpr int kExitTimeout = 124;

struct Options {
    bool json = false;
    bool quiet = false;
    bool noColor = false;
    bool help = false;
    int timeoutSeconds = 0;
    fs::path root;
    fs::path project;
    fs::path graph;
    fs::path report;
    std::vector<std::string> requireEnv;
};

struct ProjectPaths {
    bool ok = false;
    std::string error;
    fs::path root;
    fs::path graph;
};

std::string version() {
#ifdef ARESTRIX_CLI_VERSION
    return ARESTRIX_CLI_VERSION;
#else
    return "dev";
#endif
}

void printVersion(bool asJson) {
    if (asJson) {
        json out{{"name", "arestrix-cli"}, {"version", version()}};
        std::cout << out.dump() << "\n";
    } else {
        std::cout << "arestrix-cli " << version() << "\n";
    }
}

void printHelp() {
    std::cout <<
        "arestrix-cli " << version() << "\n\n"
        "Uso:\n"
        "  arestrix-cli run <grafo.json> [opcoes]\n"
        "  arestrix-cli run --project <pasta> [opcoes]\n"
        "  arestrix-cli gen <grafo.json>\n"
        "  arestrix-cli check [--project <pasta> | <grafo.json>] [opcoes]\n"
        "  arestrix-cli env set <NOME> [--stdin | --value <valor>] [--json]\n"
        "  arestrix-cli env reset <NOME> [--json]\n"
        "  arestrix-cli env list [--json]\n"
        "  arestrix-cli env check <NOME>... [--json]\n"
        "  arestrix-cli version [--json]\n\n"
        "Opcoes de run:\n"
        "  --root <pasta>          Raiz do projeto (padrao: diretorio atual)\n"
        "  --project <pasta>       Le arestrix.project.json e usa o pipeline primario\n"
        "  --json                  Emite eventos JSONL (phase/log/telemetry/result)\n"
        "  --quiet                 Suprime fases, logs e telemetria\n"
        "  --no-color              Sem cores (aceito para compatibilidade)\n"
        "  --timeout <segundos>    Cancela o pipeline ao estourar (0 = sem limite)\n"
        "  --report <arquivo>      Grava um resumo JSON mesmo em falha\n\n"
        "Opcoes de check:\n"
        "  --require-env <NOME>    Exige a variavel (pode repetir ou separar por virgula)\n\n"
        "Variaveis de ambiente sao gravadas no mesmo cofre do Studio:\n"
        "  Windows: HKCU\\Environment | Linux: ~/.config/arestrix/env\n"
        "Para segredos, prefira stdin:  \"valor\" | arestrix-cli env set DEEPSEEK_API_KEY\n";
}

bool parsePositiveInt(const std::string& text, int& out) {
    try {
        size_t used = 0;
        int value = std::stoi(text, &used);
        if (used != text.size() || value < 0) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool validEnvName(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_')) return false;
    return true;
}

std::string trimTrailingNewlines(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

std::string persistedOrProcessEnv(const std::string& name) {
    std::string value = platform::getEnv(name);
    if (value.empty()) value = platform::getPersistedEnv(name);
    return value;
}

bool readJsonFile(const fs::path& path, json& out, std::string& error) {
    try {
        out = jsonutil::parseFile(path);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

ProjectPaths resolveProject(const fs::path& projectInput) {
    ProjectPaths out;
    std::error_code ec;
    fs::path root = fs::absolute(projectInput, ec);
    if (root.filename() == "arestrix.project.json") root = root.parent_path();
    out.root = root;
    const fs::path manifest = root / "arestrix.project.json";
    if (!fs::is_regular_file(manifest, ec)) {
        out.error = "arestrix.project.json nao encontrado em " + root.string();
        return out;
    }
    json j;
    if (!readJsonFile(manifest, j, out.error)) {
        out.error = "manifesto invalido: " + out.error;
        return out;
    }
    fs::path pipeline;
    if (j.contains("pipelines") && j["pipelines"].is_array()) {
        for (const auto& item : j["pipelines"]) {
            if (!item.is_object()) continue;
            const std::string file = item.value("file", std::string());
            if (file.empty()) continue;
            if (pipeline.empty() || item.value("isPrimary", false)) pipeline = root / file;
            if (item.value("isPrimary", false)) break;
        }
    }
    if (pipeline.empty()) {
        out.error = "manifesto sem pipeline em pipelines[].file";
        return out;
    }
    if (!fs::is_regular_file(pipeline, ec)) {
        out.error = "pipeline nao encontrado: " + pipeline.string();
        return out;
    }
    out.graph = pipeline;
    out.ok = true;
    return out;
}

bool loadGraph(const fs::path& graphFile, core::Graph& graph, std::string& error) {
    try {
        auto j = jsonutil::parseFile(graphFile);
        if (j.contains("schema") && j["schema"].is_object()) graph.fromJson(j["schema"]);
        else graph.fromJson(j);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

void emitEvent(const json& event, bool asJson) {
    if (asJson) std::cout << event.dump() << "\n";
}

bool parseOptions(const std::vector<std::string>& args, size_t start, Options& opt, std::string& error) {
    for (size_t i = start; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto next = [&](const char* flag, std::string& target) -> bool {
            if (i + 1 >= args.size()) { error = std::string("faltou valor para ") + flag; return false; }
            target = args[++i];
            return true;
        };
        if (a == "--json") opt.json = true;
        else if (a == "--quiet") opt.quiet = true;
        else if (a == "--no-color") opt.noColor = true;
        else if (a == "--help" || a == "-h") opt.help = true;
        else if (a == "--root") { std::string v; if (!next("--root", v)) return false; opt.root = v; }
        else if (a == "--project") { std::string v; if (!next("--project", v)) return false; opt.project = v; }
        else if (a == "--report") { std::string v; if (!next("--report", v)) return false; opt.report = v; }
        else if (a == "--timeout") {
            std::string v;
            if (!next("--timeout", v)) return false;
            if (!parsePositiveInt(v, opt.timeoutSeconds)) { error = "timeout invalido: " + v; return false; }
        } else if (a == "--require-env") {
            std::string v;
            if (!next("--require-env", v)) return false;
            std::stringstream ss(v);
            std::string part;
            while (std::getline(ss, part, ',')) if (!part.empty()) opt.requireEnv.push_back(part);
        } else if (a.rfind("--", 0) == 0) {
            error = "opcao desconhecida: " + a;
            return false;
        } else if (opt.graph.empty()) {
            opt.graph = a;
        } else {
            error = "argumento extra: " + a;
            return false;
        }
    }
    return true;
}

int runEnv(const std::vector<std::string>& args) {
    bool asJson = false;
    std::vector<std::string> positional;
    bool fromStdin = false;
    std::string value;
    bool hasValue = false;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") asJson = true;
        else if (a == "--stdin") fromStdin = true;
        else if (a == "--value") {
            if (i + 1 >= args.size()) {
                std::cerr << "arestrix-cli env: faltou valor para --value\n";
                return kExitUsage;
            }
            value = args[++i];
            hasValue = true;
        } else if (a == "--help" || a == "-h") {
            std::cout << "arestrix-cli env set|reset|list|check\n"
                         "  env set NOME [--stdin | --value VALOR]\n"
                         "  env reset NOME\n"
                         "  env list\n"
                         "  env check NOME...\n";
            return 0;
        } else if (a.rfind("--", 0) == 0) {
            std::cerr << "arestrix-cli env: opcao desconhecida: " << a << "\n";
            return kExitUsage;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.empty()) {
        std::cerr << "arestrix-cli env: informe set, reset, list ou check\n";
        return kExitUsage;
    }
    const std::string cmd = positional[0];

    if (cmd == "list") {
        const auto names = platform::listUserEnvNames();
        if (asJson) {
            json out{{"ok", true}, {"keys", json::array()}};
            for (const auto& name : names) out["keys"].push_back(name);
            std::cout << out.dump() << "\n";
        } else {
            for (const auto& name : names) std::cout << name << "\n";
        }
        return 0;
    }

    if (cmd == "check") {
        if (positional.size() < 2) {
            std::cerr << "arestrix-cli env check: informe ao menos um NOME\n";
            return kExitUsage;
        }
        json out{{"ok", true}, {"present", json::array()}, {"missing", json::array()}};
        for (size_t i = 1; i < positional.size(); ++i) {
            const std::string& name = positional[i];
            if (!validEnvName(name)) {
                out["ok"] = false;
                out["missing"].push_back(name);
                continue;
            }
            if (persistedOrProcessEnv(name).empty()) {
                out["ok"] = false;
                out["missing"].push_back(name);
            } else {
                out["present"].push_back(name);
            }
        }
        if (asJson) {
            std::cout << out.dump() << "\n";
        } else {
            for (const auto& name : out["present"]) std::cout << "OK   " << name.get<std::string>() << "\n";
            for (const auto& name : out["missing"]) std::cerr << "AUSENTE " << name.get<std::string>() << "\n";
        }
        return out.value("ok", false) ? 0 : kExitPreflight;
    }

    if (cmd == "set" || cmd == "reset" || cmd == "unset") {
        if (positional.size() < 2) {
            std::cerr << "arestrix-cli env " << cmd << ": informe o NOME\n";
            return kExitUsage;
        }
        const std::string name = positional[1];
        if (!validEnvName(name)) {
            std::cerr << "arestrix-cli env: nome invalido: " << name << "\n";
            return kExitUsage;
        }
        if (cmd == "reset" || cmd == "unset") {
            const bool ok = platform::clearUserEnv(name);
            if (asJson) std::cout << json{{"ok", ok}, {"key", name}, {"reset", true}}.dump() << "\n";
            else std::cout << (ok ? "removida: " : "falha ao remover: ") << name << "\n";
            return ok ? 0 : kExitRuntime;
        }
        if (!hasValue) {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            value = trimTrailingNewlines(ss.str());
        }
        if (value.empty()) {
            std::cerr << "arestrix-cli env set: valor vazio (use --stdin ou --value)\n";
            return kExitUsage;
        }
        const bool ok = platform::setUserEnv(name, value);
        if (asJson) std::cout << json{{"ok", ok}, {"key", name}, {"stored", ok}}.dump() << "\n";
        else std::cout << (ok ? "gravada: " : "falha ao gravar: ") << name << "\n";
        return ok ? 0 : kExitRuntime;
    }

    std::cerr << "arestrix-cli env: comando desconhecido: " << cmd << "\n";
    return kExitUsage;
}

int runCheck(const std::vector<std::string>& args) {
    Options opt;
    std::string parseError;
    if (!parseOptions(args, 1, opt, parseError)) {
        std::cerr << "arestrix-cli check: " << parseError << "\n";
        return kExitUsage;
    }
    if (opt.help) {
        std::cout << "arestrix-cli check [--project <pasta> | <grafo.json>] [--require-env NOME] [--json]\n";
        return 0;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    fs::path graphFile = opt.graph;
    if (!opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            if (opt.json) std::cout << json{{"ok", false}, {"error", project.error}}.dump() << "\n";
            else std::cerr << "arestrix-cli check: " << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
        if (graphFile.empty()) graphFile = project.graph;
    }
    if (graphFile.empty()) {
        std::cerr << "arestrix-cli check: informe um grafo ou --project\n";
        return kExitUsage;
    }

    json checks = json::array();
    auto add = [&](const std::string& name, bool ok, const std::string& detail) {
        checks.push_back(json{{"name", name}, {"ok", ok}, {"detail", detail}});
    };

    core::Graph graph;
    std::string graphError;
    bool graphOk = loadGraph(graphFile, graph, graphError);
    add("graph", graphOk, graphOk ? graphFile.string() : graphError);
    if (graphOk) {
        try {
            const std::string src = compiler::TaskflowGenerator::generateSource(graph);
            add("codegen", !src.empty(), std::to_string(src.size()) + " bytes");
        } catch (const std::exception& ex) {
            add("codegen", false, ex.what());
        }
    }

    std::error_code ec;
    add("root", fs::is_directory(root, ec), root.string());

    const auto runtime = build::locateRuntime();
    add("runtime", runtime.ok, runtime.ok ? runtime.home.string() : runtime.error);

    const auto toolchain = build::detectToolchain();
    add("toolchain", toolchain.kind != build::Toolchain::Kind::None, toolchain.description);

    for (const auto& name : opt.requireEnv) {
        if (!validEnvName(name)) {
            add("env:" + name, false, "nome invalido");
            continue;
        }
        const bool present = !persistedOrProcessEnv(name).empty();
        add("env:" + name, present, present ? "presente" : "ausente");
    }

    bool ok = true;
    for (const auto& check : checks) if (!check.value("ok", false)) ok = false;
    if (opt.json) {
        json out{{"ok", ok}, {"graph", graphFile.string()}, {"root", root.string()}, {"checks", checks}};
        std::cout << out.dump() << "\n";
    } else {
        for (const auto& check : checks) {
            std::cout << (check.value("ok", false) ? "OK   " : "FALHA ") << check.value("name", std::string())
                      << "  " << check.value("detail", std::string()) << "\n";
        }
        std::cout << (ok ? "check: OK\n" : "check: FALHOU\n");
    }
    return ok ? 0 : kExitPreflight;
}

int runGraph(const std::vector<std::string>& args, bool genOnly) {
    Options opt;
    std::string parseError;
    if (!parseOptions(args, 1, opt, parseError)) {
        std::cerr << (genOnly ? "arestrix-cli gen: " : "arestrix-cli run: ") << parseError << "\n";
        return kExitUsage;
    }
    if (opt.help) {
        std::cout << (genOnly
            ? "arestrix-cli gen <grafo.json> [--root <pasta>]\n"
            : "arestrix-cli run <grafo.json>|--project <pasta> [--root <pasta>] [--json] [--quiet] [--timeout N] [--report FILE]\n");
        return 0;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    fs::path graphFile = opt.graph;
    if (!opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            if (opt.json) std::cout << json{{"event", "result"}, {"ok", false}, {"error", project.error}}.dump() << "\n";
            else std::cerr << (genOnly ? "arestrix-cli gen: " : "arestrix-cli run: ") << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
        if (graphFile.empty()) graphFile = project.graph;
    }
    if (graphFile.empty()) {
        std::cerr << (genOnly ? "arestrix-cli gen: informe um grafo\n" : "arestrix-cli run: informe um grafo ou --project\n");
        return kExitUsage;
    }

    core::Graph graph;
    std::string graphError;
    if (!loadGraph(graphFile, graph, graphError)) {
        if (opt.json) std::cout << json{{"event", "result"}, {"ok", false}, {"error", "Erro ao ler o grafo: " + graphError}}.dump() << "\n";
        else std::cerr << "Erro ao ler o grafo: " << graphError << "\n";
        return kExitUsage;
    }

    if (genOnly) {
        try {
            std::cout << compiler::TaskflowGenerator::generateSource(graph);
            return 0;
        } catch (const std::exception& ex) {
            std::cerr << "Erro ao gerar C++: " << ex.what() << "\n";
            return kExitConfig;
        }
    }

    build::BuildEvents ev;
    ev.onPhase = [&](const std::string& phase, const std::string& msg) {
        if (opt.quiet) return;
        if (opt.json) emitEvent(json{{"event", "phase"}, {"phase", phase}, {"message", msg}}, true);
        else std::cout << "[" << phase << "] " << msg << std::endl;
    };
    ev.onLog = [&](const std::string& line, bool isError) {
        if (opt.quiet) return;
        if (opt.json) emitEvent(json{{"event", "log"}, {"line", line}, {"error", isError}}, true);
        else std::cout << line << std::endl;
    };
    ev.onTelemetry = [&](const std::string& line) {
        if (opt.quiet) return;
        if (!opt.json) {
            std::cout << "[telemetria] " << line << std::endl;
            return;
        }
        auto parsed = jsonutil::tryParse(line);
        if (parsed) emitEvent(json{{"event", "telemetry"}, {"data", *parsed}}, true);
        else emitEvent(json{{"event", "telemetry"}, {"raw", line}}, true);
    };

    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    build::BuildResult result;
    std::atomic<bool> timeoutHit{false};
    std::thread worker([&]() {
        result = build::buildAndRun(graph, root, ev, &cancel);
        done = true;
    });

    if (opt.timeoutSeconds > 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(opt.timeoutSeconds);
        while (!done.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!done.load()) {
            timeoutHit = true;
            cancel = true;
        }
    }
    worker.join();

    bool success = result.success && !timeoutHit;
    std::string error = timeoutHit ? ("timeout de " + std::to_string(opt.timeoutSeconds) + "s") : result.error;
    int exitCode = success ? 0 : (timeoutHit ? kExitTimeout : (result.stage == "toolchain" ? kExitDependency : kExitRuntime));

    json report{{"schema", 1}, {"command", "run"}, {"graph", graphFile.string()}, {"root", root.string()},
                {"success", success}, {"stage", result.stage}, {"exitCode", exitCode},
                {"elapsedMs", result.elapsedMs}, {"error", error}};
    if (opt.json) {
        json out{{"event", "result"}, {"ok", success}, {"exitCode", exitCode}, {"stage", result.stage},
                 {"elapsedMs", result.elapsedMs}, {"graph", graphFile.string()}, {"root", root.string()}};
        if (!error.empty()) out["error"] = error;
        std::cout << out.dump() << "\n";
    } else if (success) {
        std::cout << "[OK] Pipeline concluido em " << result.elapsedMs << " ms" << std::endl;
    } else {
        std::cerr << "[FALHA:" << (timeoutHit ? "timeout" : result.stage) << "] " << error << std::endl;
    }

    if (!opt.report.empty()) {
        std::error_code ec;
        const fs::path reportPath = fs::absolute(opt.report, ec);
        if (!reportPath.parent_path().empty()) fs::create_directories(reportPath.parent_path(), ec);
        std::ofstream out(reportPath, std::ios::binary | std::ios::trunc);
        if (out) out << report.dump(2) << "\n";
        else std::cerr << "aviso: nao foi possivel gravar o relatorio: " << reportPath.string() << "\n";
    }
    return exitCode;
}

} // namespace

int runCli(int argc, char** argv) {
    platform::loadPersistedUserEnv();
    if (argc < 2) {
        printHelp();
        return 0;
    }
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    if (args[0] == "--version" || args[0] == "-V" || args[0] == "version") {
        bool asJson = std::find(args.begin() + 1, args.end(), "--json") != args.end();
        printVersion(asJson);
        return 0;
    }
    if (args[0] == "--help" || args[0] == "-h" || args[0] == "help") {
        printHelp();
        return 0;
    }
    if (args[0] == "env") return runEnv(args);
    if (args[0] == "check") return runCheck(args);
    if (args[0] == "run") return runGraph(args, false);
    if (args[0] == "gen") return runGraph(args, true);

    std::cerr << "arestrix-cli: comando desconhecido: " << args[0] << "\n\n";
    printHelp();
    return kExitUsage;
}

} // namespace cli
} // namespace taskforge
