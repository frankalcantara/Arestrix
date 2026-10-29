#include "cli/Cli.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "build/BuildService.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "core/Graph.hpp"
#include "mcp/McpClient.hpp"
#include "mcp/McpErrors.hpp"
#include "memory/MemoryService.hpp"
#include "native/NativeTools.hpp"
#include "platform/Process.hpp"
#include "rt/McpTools.hpp"
#include "rt/compress/CodeCompressor.hpp"
#include "rt/compress/ContentRouter.hpp"
#include "rt/compress/LogCompressor.hpp"
#include "rt/compress/ProseCompressor.hpp"
#include "rt/compress/ProseModel.hpp"
#include "rt/compress/ProseRecommendation.hpp"
#include "rt/compress/ProseTokenizer.hpp"
#include "rt/compress/SmartCrusher.hpp"
#include "rt/compress/Tokens.hpp"
#include "util/Json.hpp"

#include <sqlite3.h>

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
    bool ci = false;
    bool help = false;
    int timeoutSeconds = 0;
    fs::path root;
    fs::path project;
    fs::path graph;
    fs::path report;
    fs::path batchFile;
    fs::path stateFile;
    fs::path answersFile;
    bool resume = false;
    std::vector<std::string> requireEnv;
};

struct ProjectPaths {
    bool ok = false;
    std::string error;
    fs::path root;
    fs::path graph;
    std::string projectId;
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
        "  arestrix-cli compress <arquivo> [--prose] [--model-dir <pasta>] [--json]\n"
        "  arestrix-cli check [--project <pasta> | <grafo.json>] [opcoes]\n"
        "  arestrix-cli env set <NOME> [--stdin | --value <valor>] [--json]\n"
        "  arestrix-cli env reset <NOME> [--json]\n"
        "  arestrix-cli env list [--json]\n"
        "  arestrix-cli env check <NOME>... [--json]\n"
        "  arestrix-cli doctor [--project <pasta>] [--json]\n"
        "  arestrix-cli memory report|prune|purge|proposals|embeddings|reindex|consolidate|workspace|promote [--project <pasta>] [opcoes]\n"
        "  arestrix-cli mcp list|test|tools [id] [--project <pasta>] [--json]\n"
        "  arestrix-cli project info|validate|graphs [--project <pasta>] [--json]\n"
        "  arestrix-cli completion powershell|bash|zsh\n"
        "  arestrix-cli version [--json]\n\n"
        "  --ci                    Equivale a --json --quiet --no-color e timeout padrao de 900s\n\n"
        "Opcoes globais:\n"
        "  --no-user-env           Nao carrega credenciais persistidas do usuario (CI/testes)\n"
        "                          Equivale a ARESTRIX_DISABLE_USER_ENV=1\n\n"
        "Opcoes de run:\n"
        "  --root <pasta>          Raiz do projeto (padrao: diretorio atual)\n"
        "  --project <pasta>       Le arestrix.project.json e usa o pipeline primario\n"
        "  --json                  Emite eventos JSONL (phase/log/telemetry/result)\n"
        "  --quiet                 Suprime fases, logs e telemetria\n"
        "  --no-color              Sem cores (aceito para compatibilidade)\n"
        "  --timeout <segundos>    Cancela o pipeline ao estourar (0 = sem limite)\n"
        "  --report <arquivo>      Grava um resumo JSON mesmo em falha\n"
        "  --answers <arquivo>     Respostas JSON para vertices de interacao (CI/testes)\n"
        "  --batch <arquivo.json>  Executa itens sequenciais de um manifesto de lote\n"
        "  --state <arquivo>       Checkpoint do lote (padrao .arestrix/batch-state.json)\n"
        "  --resume                Pula itens ja concluidos no checkpoint\n\n"
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
    out.projectId = j.value("projectId", std::string("default"));
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
        else if (a == "--ci") opt.ci = true;
        else if (a == "--help" || a == "-h") opt.help = true;
        else if (a == "--root") { std::string v; if (!next("--root", v)) return false; opt.root = v; }
        else if (a == "--project") { std::string v; if (!next("--project", v)) return false; opt.project = v; }
        else if (a == "--report") { std::string v; if (!next("--report", v)) return false; opt.report = v; }
        else if (a == "--batch") { std::string v; if (!next("--batch", v)) return false; opt.batchFile = v; }
        else if (a == "--answers") { std::string v; if (!next("--answers", v)) return false; opt.answersFile = v; }
        else if (a == "--state") { std::string v; if (!next("--state", v)) return false; opt.stateFile = v; }
        else if (a == "--resume") opt.resume = true;
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

int runDoctor(const std::vector<std::string>& args) {
    Options opt;
    std::string parseError;
    if (!parseOptions(args, 1, opt, parseError)) {
        std::cerr << "arestrix-cli doctor: " << parseError << "\n";
        return kExitUsage;
    }
    if (opt.help) {
        std::cout << "arestrix-cli doctor [--project <pasta>] [--root <pasta>] [--json] [--ci]\n";
        return 0;
    }
    if (opt.ci) opt.json = true;

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    if (!opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            if (opt.json) std::cout << json{{"ok", false}, {"error", project.error}}.dump() << "\n";
            else std::cerr << "arestrix-cli doctor: " << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
    }

    json checks = json::array();
    auto add = [&](const std::string& name, bool ok, bool critical, const std::string& detail) {
        checks.push_back(json{{"name", name}, {"ok", ok}, {"critical", critical}, {"detail", detail}});
    };

    std::error_code ec;
    add("root", fs::is_directory(root, ec), true, root.string());
    const auto runtime = build::locateRuntime();
    add("runtime", runtime.ok, true, runtime.ok ? runtime.home.string() : runtime.error);
    const auto toolchain = build::detectToolchain();
    add("toolchain", toolchain.kind != build::Toolchain::Kind::None, true, toolchain.description);

    const std::vector<std::string> providerKeys = {
        "DEEPSEEK_API_KEY", "OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
        "GROQ_API_KEY", "MISTRAL_API_KEY", "JEV_API_KEY", "LOCAL_LLM_API_KEY"
    };
    bool anyProvider = false;
    for (const auto& key : providerKeys) {
        const bool present = !persistedOrProcessEnv(key).empty();
        anyProvider = anyProvider || present;
        add("env:" + key, present, false, present ? "presente" : "ausente");
    }
    add("provider-key", anyProvider, false, anyProvider ? "ao menos uma chave de provedor encontrada" : "nenhuma chave de provedor encontrada");
    for (const char* key : {"KEENABLE_API", "ARESTRIX_AI_GATEWAY_URL", "ARESTRIX_AI_GATEWAY_KEY"}) {
        const bool present = !persistedOrProcessEnv(key).empty();
        add(std::string("env:") + key, present, false, present ? "presente" : "ausente");
    }

    bool ok = true;
    for (const auto& check : checks) {
        if (check.value("critical", false) && !check.value("ok", false)) ok = false;
    }
    if (opt.json) {
        std::cout << json{{"ok", ok}, {"root", root.string()}, {"checks", checks}}.dump() << "\n";
    } else {
        for (const auto& check : checks) {
            const bool passed = check.value("ok", false);
            const bool critical = check.value("critical", false);
            std::cout << (passed ? "OK    " : (critical ? "FALHA " : "AVISO "))
                      << check.value("name", std::string()) << "  "
                      << check.value("detail", std::string()) << "\n";
        }
        std::cout << (ok ? "doctor: OK\n" : "doctor: FALHOU\n");
    }
    return ok ? 0 : kExitPreflight;
}

std::string sqliteText(sqlite3_stmt* stmt, int index) {
    const unsigned char* p = sqlite3_column_text(stmt, index);
    return p ? reinterpret_cast<const char*>(p) : std::string();
}

int runMemory(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        std::cout << "arestrix-cli memory report [--project <pasta>] [--root <pasta>] [--db <arquivo>] [--json]\n"
                     "arestrix-cli memory prune  --project <pasta> [--days N] [--db <arquivo>] [--json]\n"
                     "arestrix-cli memory purge  --project <pasta> [--keep-audit] [--db <arquivo>] [--json]\n"
                     "arestrix-cli memory proposals list|accept|reject [id] --project <pasta> [--status pending|all] [--reason texto] [--json]\n"
                     "arestrix-cli memory embeddings status|test --project <pasta> [--model M] [--base-url URL] [--enable] [--sample texto] [--json]\n"
                     "arestrix-cli memory reindex --project <pasta> [--model M] [--base-url URL] [--force] [--json]\n"
                     "arestrix-cli memory consolidate --project <pasta> [--dormant-days N] [--no-decay] [--no-dedup] [--no-generalize] [--json]\n"
                     "arestrix-cli memory workspace init|status --project <pasta> [--workspace-id ID] [--json]\n"
                     "arestrix-cli memory promote --project <pasta> --kind lesson|fact|playbook --id ID --evidence texto [--workspace-id ID] [--json]\n";
        return args.size() < 2 ? kExitUsage : 0;
    }
    const std::string command = args[1];
    if (command != "report" && command != "prune" && command != "purge" && command != "proposals" &&
        command != "embeddings" && command != "reindex" && command != "consolidate" &&
        command != "workspace" && command != "promote") {
        std::cerr << "arestrix-cli memory: comando desconhecido: " << command << "\n";
        return kExitUsage;
    }

    std::string proposalAction;
    std::string proposalId;
    std::string embeddingAction;
    size_t optionStart = 2;
    if (command == "proposals") {
        if (args.size() < 3) {
            std::cerr << "arestrix-cli memory proposals: informe list|accept|reject\n";
            return kExitUsage;
        }
        proposalAction = args[2];
        optionStart = 3;
        if (proposalAction == "accept" || proposalAction == "reject") {
            if (args.size() < 4) {
                std::cerr << "arestrix-cli memory proposals " << proposalAction << ": informe o id\n";
                return kExitUsage;
            }
            proposalId = args[3];
            optionStart = 4;
        } else if (proposalAction != "list") {
            std::cerr << "arestrix-cli memory proposals: acao desconhecida: " << proposalAction << "\n";
            return kExitUsage;
        }
    } else if (command == "embeddings") {
        if (args.size() < 3) {
            std::cerr << "arestrix-cli memory embeddings: informe status|test\n";
            return kExitUsage;
        }
        embeddingAction = args[2];
        if (embeddingAction != "status" && embeddingAction != "test") {
            std::cerr << "arestrix-cli memory embeddings: acao desconhecida: " << embeddingAction << "\n";
            return kExitUsage;
        }
        optionStart = 3;
    } else if (command == "workspace") {
        if (args.size() < 3) {
            std::cerr << "arestrix-cli memory workspace: informe init|status\n";
            return kExitUsage;
        }
        embeddingAction = args[2];
        if (embeddingAction != "init" && embeddingAction != "status") {
            std::cerr << "arestrix-cli memory workspace: acao desconhecida: " << embeddingAction << "\n";
            return kExitUsage;
        }
        optionStart = 3;
    }

    Options opt;
    fs::path dbPath;
    std::string parseError;
    int days = -1;
    bool keepAudit = false;
    std::string status = "pending";
    std::string reason;
    std::string embeddingModel;
    std::string embeddingBaseUrl;
    std::string embeddingSample;
    bool forceEmbeddings = false;
    bool embeddingEnabled = false;
    int dormantDays = -1;
    bool noDecay = false;
    bool noDedup = false;
    bool noGeneralize = false;
    bool noConflicts = false;
    bool noExpire = false;
    std::string workspaceId;
    std::string promoteKind;
    std::string promoteId;
    std::string promoteEvidence;
    for (size_t i = optionStart; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") opt.json = true;
        else if (a == "--keep-audit") keepAudit = true;
        else if (a == "--days") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --days\n"; return kExitUsage; }
            try { days = std::stoi(args[++i]); } catch (...) { std::cerr << "--days invalido\n"; return kExitUsage; }
        }
        else if (a == "--dormant-days") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --dormant-days\n"; return kExitUsage; }
            try { dormantDays = std::stoi(args[++i]); } catch (...) { std::cerr << "--dormant-days invalido\n"; return kExitUsage; }
        }
        else if (a == "--no-decay") {
            noDecay = true;
        }
        else if (a == "--no-dedup") {
            noDedup = true;
        }
        else if (a == "--no-generalize") {
            noGeneralize = true;
        }
        else if (a == "--no-conflicts") {
            noConflicts = true;
        }
        else if (a == "--no-expire") {
            noExpire = true;
        }
        else if (a == "--workspace-id") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --workspace-id\n"; return kExitUsage; }
            workspaceId = args[++i];
        }
        else if (a == "--kind") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --kind\n"; return kExitUsage; }
            promoteKind = args[++i];
        }
        else if (a == "--id") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --id\n"; return kExitUsage; }
            promoteId = args[++i];
        }
        else if (a == "--evidence") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --evidence\n"; return kExitUsage; }
            promoteEvidence = args[++i];
        }
        else if (a == "--status") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --status\n"; return kExitUsage; }
            status = args[++i];
            if (status == "all") status.clear();
        } else if (a == "--reason") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --reason\n"; return kExitUsage; }
            reason = args[++i];
        } else if (a == "--model") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --model\n"; return kExitUsage; }
            embeddingModel = args[++i];
        } else if (a == "--base-url") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --base-url\n"; return kExitUsage; }
            embeddingBaseUrl = args[++i];
        } else if (a == "--sample") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --sample\n"; return kExitUsage; }
            embeddingSample = args[++i];
        } else if (a == "--force") {
            forceEmbeddings = true;
        } else if (a == "--enable") {
            embeddingEnabled = true;
        }
        else if (a == "--project") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --project\n"; return kExitUsage; }
            opt.project = args[++i];
        } else if (a == "--root") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --root\n"; return kExitUsage; }
            opt.root = args[++i];
        } else if (a == "--db") {
            if (i + 1 >= args.size()) { std::cerr << "faltou --db\n"; return kExitUsage; }
            dbPath = args[++i];
        } else {
            parseError = "opcao desconhecida: " + a;
        }
    }
    if (!parseError.empty()) {
        std::cerr << "arestrix-cli memory: " << parseError << "\n";
        return kExitUsage;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    std::string projectId = "default";
    if (!opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            std::cerr << "arestrix-cli memory: " << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
        projectId = project.projectId;
    } else {
        const fs::path manifest = root / "arestrix.project.json";
        std::error_code manifestEc;
        if (fs::is_regular_file(manifest, manifestEc)) {
            json manifestJson;
            std::string manifestError;
            if (readJsonFile(manifest, manifestJson, manifestError)) {
                projectId = manifestJson.value("projectId", std::string("default"));
                if (days < 0 && manifestJson.contains("globalDefaults") &&
                    manifestJson["globalDefaults"].is_object() &&
                    manifestJson["globalDefaults"].contains("memory") &&
                    manifestJson["globalDefaults"]["memory"].is_object()) {
                    days = manifestJson["globalDefaults"]["memory"].value("retentionDays", 180);
                }
            }
        }
    }
    if (command != "report" && opt.project.empty() && projectId == "default") {
        std::cerr << "arestrix-cli memory " << command
                  << ": informe --project ou --root com arestrix.project.json\n";
        return kExitConfig;
    }
    if (days < 0) {
        const fs::path manifest = root / "arestrix.project.json";
        std::error_code manifestEc;
        if (fs::is_regular_file(manifest, manifestEc)) {
            json manifestJson;
            std::string manifestError;
            if (readJsonFile(manifest, manifestJson, manifestError) &&
                manifestJson.contains("globalDefaults") && manifestJson["globalDefaults"].is_object() &&
                manifestJson["globalDefaults"].contains("memory") &&
                manifestJson["globalDefaults"]["memory"].is_object()) {
                days = manifestJson["globalDefaults"]["memory"].value("retentionDays", 180);
            }
        }
    }
    json memorySettings = json::object();
    {
        const fs::path manifest = root / "arestrix.project.json";
        std::error_code manifestEc;
        if (fs::is_regular_file(manifest, manifestEc)) {
            json manifestJson;
            std::string manifestError;
            if (readJsonFile(manifest, manifestJson, manifestError) &&
                manifestJson.contains("globalDefaults") && manifestJson["globalDefaults"].is_object() &&
                manifestJson["globalDefaults"].contains("memory") &&
                manifestJson["globalDefaults"]["memory"].is_object()) {
                memorySettings = manifestJson["globalDefaults"]["memory"];
            }
        }
    }
    if (dbPath.empty()) dbPath = root / ".arestrix" / "memory" / "memory.db";

    if (command != "report") {
        std::error_code ec;
        if (!fs::is_regular_file(dbPath, ec)) {
            std::cerr << "Banco de memoria nao encontrado: " << dbPath.string() << "\n";
            return kExitConfig;
        }
        auto& memory = taskforge::memory::MemoryService::getInstance();
        memory.closeProject(projectId);
        if (!memory.configure(projectId, true, dbPath.string())) {
            std::cerr << "Falha ao abrir o banco de memoria: " << dbPath.string() << "\n";
            return kExitConfig;
        }

        if (command == "consolidate") {
            taskforge::memory::ConsolidationOptions consolidation;
            if (dormantDays >= 0) consolidation.dormantAfterDays = dormantDays;
            consolidation.applyDecay = !noDecay;
            consolidation.deduplicate = !noDedup;
            consolidation.generalize = !noGeneralize;
            consolidation.detectContradictions = !noConflicts;
            consolidation.expireFacts = !noExpire;
            std::string consolidateError;
            auto report = memory.consolidate(projectId, consolidation, consolidateError);
            if (opt.json) {
                std::cout << report.dump(2) << "\n";
            } else {
                std::cout << "Consolidacao: duplicatas=" << report.value("duplicates", 0LL)
                          << " conflitos=" << report.value("contradictions", 0LL)
                          << " expirados=" << report.value("expired", 0LL)
                          << " dormant=" << report.value("dormant", 0LL)
                          << " generalizacoes=" << report.value("generalizations", 0LL)
                          << " erros=" << report.value("errors", 0LL) << "\n";
                if (!consolidateError.empty()) std::cerr << "Erro: " << consolidateError << "\n";
            }
            return report.value("ok", false) ? 0 : kExitConfig;
        }

        if (command == "workspace" || command == "promote") {
            std::string resolvedWorkspace = workspaceId;
            if (resolvedWorkspace.empty()) {
                if (const char* env = std::getenv("ARESTRIX_WORKSPACE_ID"); env && *env) {
                    resolvedWorkspace = env;
                } else {
                    resolvedWorkspace = root.filename().string();
                    if (resolvedWorkspace.empty()) resolvedWorkspace = "default";
                }
            }
            const fs::path workspaceDb = root / ".workspace" / "memory" / "memory.db";

            if (command == "workspace" && embeddingAction == "status") {
                std::error_code ec;
                if (!fs::is_regular_file(workspaceDb, ec)) {
                    json out{{"configured", false}, {"projectId", projectId},
                             {"workspaceId", resolvedWorkspace},
                             {"database", workspaceDb.string()}};
                    std::cout << (opt.json ? out.dump(2) : "Workspace nao configurado: " + workspaceDb.string())
                              << "\n";
                    return 0;
                }
                memory.configureProjectWorkspace(projectId, resolvedWorkspace, workspaceDb.string());
                auto out = memory.workspaceStatus(projectId);
                if (opt.json) {
                    std::cout << out.dump(2) << "\n";
                } else {
                    std::cout << "Workspace: " << out.value("workspaceId", std::string())
                              << " | licoes=" << out.value("lessons", 0LL)
                              << " | playbooks=" << out.value("playbooks", 0LL)
                              << " | arestas=" << out.value("activeEdges", 0LL) << "\n";
                }
                return 0;
            }

            if (command == "workspace") {
                if (!memory.configureProjectWorkspace(projectId, resolvedWorkspace, workspaceDb.string())) {
                    std::cerr << "Falha ao criar workspace: " << workspaceDb.string() << "\n";
                    return kExitConfig;
                }
                auto out = memory.workspaceStatus(projectId);
                if (opt.json) {
                    std::cout << out.dump(2) << "\n";
                } else {
                    std::cout << "Workspace criado: " << workspaceDb.string() << "\n";
                }
                return 0;
            }

            if (promoteKind.empty() || promoteId.empty() || promoteEvidence.empty()) {
                std::cerr << "arestrix-cli memory promote: informe --kind, --id e --evidence\n";
                return kExitUsage;
            }
            std::error_code ec;
            if (!fs::is_regular_file(workspaceDb, ec)) {
                std::cerr << "Workspace nao configurado: " << workspaceDb.string()
                          << " (use memory workspace init)\n";
                return kExitConfig;
            }
            memory.configureProjectWorkspace(projectId, resolvedWorkspace, workspaceDb.string());
            taskforge::Json evidence = taskforge::Json::array(
                {taskforge::Json{{"kind", "manual"}, {"note", promoteEvidence}, {"actor", "cli"}}});
            std::string promoteError;
            const bool ok = memory.promoteToWorkspace(projectId, promoteKind, promoteId,
                                                      evidence, "cli", promoteError);
            if (opt.json) {
                std::cout << json{{"command", "promote-workspace"}, {"projectId", projectId},
                                  {"workspaceId", resolvedWorkspace}, {"kind", promoteKind},
                                  {"id", promoteId}, {"ok", ok}, {"error", promoteError}}.dump(2)
                          << "\n";
            } else if (ok) {
                std::cout << "Memoria " << promoteId << " promovida ao workspace "
                          << resolvedWorkspace << ".\n";
            } else {
                std::cerr << "Falha na promocao: " << promoteError << "\n";
            }
            return ok ? 0 : kExitConfig;
        }

        if (command == "embeddings" || command == "reindex") {
            auto config = memorySettings.is_object()
                ? taskforge::memory::embeddingConfigFromJson(memorySettings)
                : taskforge::memory::defaultEmbeddingConfig();
            config.enabled = config.enabled || embeddingEnabled;
            if (!embeddingModel.empty()) config.model = embeddingModel;
            if (!embeddingBaseUrl.empty()) config.baseUrl = embeddingBaseUrl;
            memory.configureEmbeddings(projectId, config);

            taskforge::Json out = taskforge::Json::object();
            if (command == "embeddings" && embeddingAction == "status") {
                out = memory.embeddingStatus(projectId);
            } else if (command == "embeddings" && embeddingAction == "test") {
                out = memory.testEmbeddings(projectId, embeddingSample);
            } else {
                std::string indexError;
                out = memory.reindexEmbeddings(projectId, forceEmbeddings, indexError);
            }
            const bool ok = out.value("ok", true);
            if (opt.json) {
                std::cout << out.dump(2) << "\n";
            } else if (command == "embeddings" && embeddingAction == "status") {
                std::cout << "Embeddings: " << (out.value("active", false) ? "ativo" : "inativo")
                          << " | modelo " << out.value("model", std::string())
                          << " | endpoint " << out.value("localBaseUrl", std::string()) << "\n"
                          << "Indexados: " << out.value("indexed", 0LL) << " / "
                          << out.value("candidates", 0LL) << " candidatos\n";
            } else if (command == "embeddings") {
                if (ok) {
                    std::cout << "Embeddings OK: " << out.value("model", std::string())
                              << " (" << out.value("dim", 0) << "d) em "
                              << out.value("latencyMs", 0LL) << " ms.\n";
                } else {
                    std::cerr << "Falha nos embeddings: " << out.value("error", std::string()) << "\n";
                }
            } else if (ok) {
                std::cout << "Reindexacao concluida: " << out.value("indexed", 0LL)
                          << " indexados, " << out.value("skipped", 0LL)
                          << " inalterados, " << out.value("failed", 0LL) << " falhas.\n";
            } else {
                std::cerr << "Falha na reindexacao: " << out.value("error", std::string()) << "\n";
            }
            return ok ? 0 : kExitConfig;
        }

        if (command == "proposals") {
            std::string proposalError;
            if (proposalAction == "list") {
                auto list = memory.listProposals(projectId, status, 100);
                if (opt.json) {
                    std::cout << json{{"command", "proposals-list"}, {"projectId", projectId}, {"proposals", list}}.dump(2) << "\n";
                } else if (list.empty()) {
                    std::cout << "Nenhuma proposta " << (status.empty() ? "registrada" : status) << ".\n";
                } else {
                    for (const auto& proposal : list) {
                        std::cout << proposal.value("id", std::string()) << " | "
                                  << proposal.value("kind", std::string()) << " | "
                                  << proposal.value("status", std::string()) << " | confianca "
                                  << proposal.value("confidence", 0.0) << " | "
                                  << proposal.value("payloadJson", std::string()) << "\n";
                    }
                }
                return 0;
            }
            std::string promotedId;
            bool ok = proposalAction == "accept"
                ? memory.promoteProposal(projectId, proposalId, "cli", proposalError, &promotedId)
                : memory.rejectProposal(projectId, proposalId, "cli", reason, proposalError);
            if (opt.json) {
                std::cout << json{{"command", "proposals-" + proposalAction}, {"projectId", projectId},
                                  {"id", proposalId}, {"ok", ok}, {"promotedId", promotedId},
                                  {"error", proposalError}}.dump(2) << "\n";
            } else if (ok) {
                std::cout << "Proposta " << proposalId << " " << (proposalAction == "accept" ? "aceita" : "rejeitada") << ".\n";
                if (!promotedId.empty()) std::cout << "Destino: " << promotedId << "\n";
            } else {
                std::cerr << "Falha ao processar proposta: " << proposalError << "\n";
            }
            return ok ? 0 : kExitConfig;
        }

        if (command == "prune") {
            if (days < 0) days = 180;
            if (days > 3650) days = 3650;
            auto pruned = memory.prune(projectId, days);
            if (opt.json) {
                std::cout << json{{"command", "prune"}, {"database", dbPath.string()}, {"projectId", projectId},
                                  {"days", days}, {"episodes", pruned.episodes}, {"hits", pruned.hits},
                                  {"usage", pruned.usage}}.dump(2) << "\n";
            } else {
                std::cout << "Retencao aplicada (" << days << " dias): "
                          << pruned.episodes << " episodio(s), " << pruned.hits << " hit(s), "
                          << pruned.usage << " usage removidos.\n";
            }
        } else {
            const bool ok = memory.purgeProject(projectId, keepAudit, true);
            if (opt.json) {
                std::cout << json{{"command", "purge"}, {"database", dbPath.string()}, {"projectId", projectId},
                                  {"ok", ok}, {"keepAudit", keepAudit}}.dump(2) << "\n";
            } else {
                std::cout << (ok ? "Expurgo concluido" : "Expurgo incompleto")
                          << " | auditoria: " << (keepAudit ? "preservada" : "removida")
                          << "\n";
            }
            return ok ? 0 : kExitConfig;
        }
        return 0;
    }

    std::error_code ec;
    if (!fs::is_regular_file(dbPath, ec)) {
        std::cerr << "Banco de memoria nao encontrado: " << dbPath.string() << "\n";
        return kExitConfig;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::cerr << "Falha ao abrir o banco: " << (db ? sqlite3_errmsg(db) : "sqlite") << "\n";
        if (db) sqlite3_close(db);
        return kExitConfig;
    }

    auto prepare = [&](const char* sql, sqlite3_stmt** stmt) {
        return sqlite3_prepare_v2(db, sql, -1, stmt, nullptr) == SQLITE_OK;
    };

    json out{{"database", dbPath.string()}, {"runs", 0}, {"successRuns", 0}, {"failureRuns", 0},
             {"retries", 0}, {"promptTokens", 0}, {"completionTokens", 0}, {"totalTokens", 0},
             {"calls", 0}, {"avgLatencyMs", 0.0}, {"memoryContextChars", 0}, {"memoryEnabledRuns", 0},
             {"hits", json{{"total", 0}, {"injected", 0}, {"used", 0}, {"consulted", 0},
                           {"usedInjected", 0}, {"success", 0}, {"failure", 0}}},
             {"modes", json::array()}, {"nodes", json::array()}};

    sqlite3_stmt* st = nullptr;
    if (prepare("SELECT COUNT(*), COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0), COALESCE(SUM(retries),0), "
                "COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), "
                "COALESCE(SUM(calls),0), COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0), "
                "COALESCE(SUM(memory_enabled),0) FROM memory_usage", &st)) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            out["runs"] = sqlite3_column_int64(st, 0);
            out["successRuns"] = sqlite3_column_int64(st, 1);
            out["failureRuns"] = sqlite3_column_int64(st, 2);
            out["retries"] = sqlite3_column_int64(st, 3);
            out["promptTokens"] = sqlite3_column_int64(st, 4);
            out["completionTokens"] = sqlite3_column_int64(st, 5);
            out["totalTokens"] = sqlite3_column_int64(st, 6);
            out["calls"] = sqlite3_column_int64(st, 7);
            out["avgLatencyMs"] = sqlite3_column_double(st, 8);
            out["memoryContextChars"] = sqlite3_column_int64(st, 9);
            out["memoryEnabledRuns"] = sqlite3_column_int64(st, 10);
        }
        sqlite3_finalize(st);
        st = nullptr;
    }

    if (prepare("SELECT memory_enabled, COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                "COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), COALESCE(AVG(latency_ms),0), "
                "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(retries),0), "
                "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
                "FROM memory_usage GROUP BY memory_enabled ORDER BY memory_enabled DESC", &st)) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            out["modes"].push_back(json{{"memoryEnabled", sqlite3_column_int64(st, 0) != 0},
                                        {"runs", sqlite3_column_int64(st, 1)},
                                        {"promptTokens", sqlite3_column_int64(st, 2)},
                                        {"completionTokens", sqlite3_column_int64(st, 3)},
                                        {"totalTokens", sqlite3_column_int64(st, 4)},
                                        {"calls", sqlite3_column_int64(st, 5)},
                                        {"avgLatencyMs", sqlite3_column_double(st, 6)},
                                        {"memoryContextChars", sqlite3_column_int64(st, 7)},
                                        {"retries", sqlite3_column_int64(st, 8)},
                                        {"successRuns", sqlite3_column_int64(st, 9)},
                                        {"failureRuns", sqlite3_column_int64(st, 10)}});
        }
        sqlite3_finalize(st);
        st = nullptr;
    }

    if (prepare("SELECT COALESCE(node_id,'(sem no)'), COUNT(*), COALESCE(SUM(prompt_tokens),0), "
                "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), "
                "COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(retries),0), "
                "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
                "FROM memory_usage GROUP BY node_id ORDER BY SUM(total_tokens) DESC", &st)) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            out["nodes"].push_back(json{{"nodeId", sqliteText(st, 0)},
                                        {"runs", sqlite3_column_int64(st, 1)},
                                        {"promptTokens", sqlite3_column_int64(st, 2)},
                                        {"completionTokens", sqlite3_column_int64(st, 3)},
                                        {"totalTokens", sqlite3_column_int64(st, 4)},
                                        {"calls", sqlite3_column_int64(st, 5)},
                                        {"avgLatencyMs", sqlite3_column_double(st, 6)},
                                        {"memoryContextChars", sqlite3_column_int64(st, 7)},
                                        {"retries", sqlite3_column_int64(st, 8)},
                                        {"successRuns", sqlite3_column_int64(st, 9)},
                                        {"failureRuns", sqlite3_column_int64(st, 10)}});
        }
        sqlite3_finalize(st);
        st = nullptr;
    }

    if (prepare("SELECT COUNT(*), COALESCE(SUM(injected),0), COALESCE(SUM(used),0), "
                "COALESCE(SUM(CASE WHEN used=1 AND injected=0 THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN used=1 AND injected=1 THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) FROM memory_hit", &st)) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            out["hits"] = json{{"total", sqlite3_column_int64(st, 0)},
                               {"injected", sqlite3_column_int64(st, 1)},
                               {"used", sqlite3_column_int64(st, 2)},
                               {"consulted", sqlite3_column_int64(st, 3)},
                               {"usedInjected", sqlite3_column_int64(st, 4)},
                               {"success", sqlite3_column_int64(st, 5)},
                               {"failure", sqlite3_column_int64(st, 6)}};
        }
        sqlite3_finalize(st);
        st = nullptr;
    }

    if (prepare("SELECT COUNT(*), COUNT(DISTINCT model), COALESCE(SUM(dim),0) FROM memory_embedding", &st)) {
        if (sqlite3_step(st) == SQLITE_ROW) {
            out["embeddings"] = json{{"vectors", sqlite3_column_int64(st, 0)},
                                     {"models", sqlite3_column_int64(st, 1)},
                                     {"dimensions", sqlite3_column_int64(st, 2)}};
        }
        sqlite3_finalize(st);
        st = nullptr;
    }

    sqlite3_close(db);

    if (opt.json) {
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << "Banco: " << out["database"].str() << "\n";
        std::cout << "Execucoes: " << out["runs"].get<long long>()
                  << " | sucesso: " << out["successRuns"].get<long long>()
                  << " | falha: " << out["failureRuns"].get<long long>()
                  << " | chamadas: " << out["calls"].get<long long>()
                  << " | retries: " << out["retries"].get<long long>() << "\n";
        std::cout << "Tokens: " << out["totalTokens"].get<long long>()
                  << " (prompt " << out["promptTokens"].get<long long>()
                  << " / saida " << out["completionTokens"].get<long long>() << ")\n";
        std::cout << "Contexto: " << out["memoryContextChars"].get<long long>()
                  << " caracteres | runs com memoria: " << out["memoryEnabledRuns"].get<long long>() << "\n";
        std::cout << "Memorias injetadas: " << out["hits"]["injected"].get<long long>()
                  << " | usadas do brief: " << out["hits"]["usedInjected"].get<long long>()
                  << " | consultadas via busca: " << out["hits"]["consulted"].get<long long>() << "\n";
        if (out.contains("embeddings")) {
            std::cout << "Embeddings: " << out["embeddings"]["vectors"].get<long long>()
                      << " vetores | " << out["embeddings"]["models"].get<long long>() << " modelo(s)\n";
        }
        for (const auto& mode : out["modes"]) {
            std::cout << "  " << (mode.value("memoryEnabled", false) ? "memoria ON " : "memoria OFF")
                      << ": runs=" << mode["runs"].get<long long>()
                      << " tokens=" << mode["totalTokens"].get<long long>() << "\n";
        }
    }
    return 0;
}

int runMcp(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        std::cout << "arestrix-cli mcp list|test|tools [id] [--project <pasta>] [--json]\n";
        return args.size() < 2 ? kExitUsage : 0;
    }
    const std::string cmd = args[1];
    Options opt;
    std::vector<std::string> positional;
    std::string parseError;
    for (size_t i = 2; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") opt.json = true;
        else if (a == "--project") {
            if (i + 1 >= args.size()) { parseError = "faltou --project"; break; }
            opt.project = args[++i];
        } else if (a == "--root") {
            if (i + 1 >= args.size()) { parseError = "faltou --root"; break; }
            opt.root = args[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout << "arestrix-cli mcp list|test|tools [id] [--project <pasta>] [--json]\n";
            return 0;
        } else if (a.rfind("--", 0) == 0) {
            parseError = "opcao desconhecida: " + a;
        } else {
            positional.push_back(a);
        }
    }
    if (!parseError.empty()) {
        std::cerr << "arestrix-cli mcp: " << parseError << "\n";
        return kExitUsage;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    if (!opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            if (opt.json) std::cout << json{{"ok", false}, {"error", project.error}}.dump() << "\n";
            else std::cerr << "arestrix-cli mcp: " << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
    }

    if (cmd == "list") {
        json servers = rt::McpToolbox::listProjectServers(root);
        if (opt.json) {
            std::cout << json{{"ok", true}, {"root", root.string()}, {"servers", servers}}.dump(2) << "\n";
        } else {
            for (const auto& server : servers) {
                std::cout << server.value("id", std::string()) << "  "
                          << server.value("name", std::string()) << "  "
                          << server.value("transport", std::string()) << "\n";
            }
        }
        return 0;
    }

    if (cmd != "test" && cmd != "tools") {
        std::cerr << "arestrix-cli mcp: comando desconhecido: " << cmd << "\n";
        return kExitUsage;
    }
    if (positional.empty()) {
        std::cerr << "arestrix-cli mcp " << cmd << ": informe o id do servidor\n";
        return kExitUsage;
    }
    const std::string serverId = positional[0];
    json tools = json::array();
    std::string error;
    std::string errorCode;
    std::string errorDetail;

    if (native::isNativeServer(serverId)) {
        const auto* ns = native::findServer(serverId);
        if (!ns) {
            error = "servidor nativo nao encontrado: " + serverId;
            errorCode = "server_not_found";
        } else {
            for (const auto& tool : ns->tools) {
                tools.push_back(json{{"name", tool.name}, {"description", tool.description},
                                     {"inputSchema", tool.inputSchema}, {"risk", tool.risk}});
            }
        }
    } else {
        try {
            auto cfg = rt::McpToolbox::loadServerConfig(root, serverId);
            mcp::McpClient client;
            mcp::McpErrorInfo info;
            const auto found = client.listTools(cfg, 20000, &info);
            for (const auto& tool : found) {
                tools.push_back(json{{"name", tool.name}, {"description", tool.description},
                                     {"inputSchema", tool.inputSchema}});
            }
            if (found.empty() && !info.empty()) {
                error = info.message;
                errorCode = info.codeName();
                errorDetail = info.detail;
            }
        } catch (const std::exception& ex) {
            error = ex.what();
        }
    }

    const bool ok = error.empty();
    if (opt.json) {
        json out{{"ok", ok}, {"server", serverId}, {"root", root.string()}, {"tools", tools}};
        if (!error.empty()) out["error"] = error;
        if (!errorCode.empty()) out["errorCode"] = errorCode;
        if (!errorDetail.empty()) out["errorDetail"] = errorDetail;
        std::cout << out.dump(2) << "\n";
    } else if (ok) {
        for (const auto& tool : tools) {
            std::cout << tool.value("name", std::string());
            const std::string desc = tool.value("description", std::string());
            if (!desc.empty()) std::cout << "  " << desc;
            std::cout << "\n";
        }
    } else {
        std::cerr << "mcp " << serverId << ": " << error << "\n";
    }
    return ok ? 0 : kExitRuntime;
}

int runProject(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        std::cout << "arestrix-cli project info|validate|graphs [--project <pasta>] [--root <pasta>] [--json]\n";
        return args.size() < 2 ? kExitUsage : 0;
    }
    const std::string cmd = args[1];
    Options opt;
    std::string parseError;
    for (size_t i = 2; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--json") opt.json = true;
        else if (a == "--project") {
            if (i + 1 >= args.size()) { parseError = "faltou --project"; break; }
            opt.project = args[++i];
        } else if (a == "--root") {
            if (i + 1 >= args.size()) { parseError = "faltou --root"; break; }
            opt.root = args[++i];
        } else {
            parseError = "opcao desconhecida: " + a;
        }
    }
    if (!parseError.empty()) {
        std::cerr << "arestrix-cli project: " << parseError << "\n";
        return kExitUsage;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    if (!opt.project.empty()) {
        std::error_code ec;
        root = fs::absolute(opt.project, ec);
        if (root.filename() == "arestrix.project.json") root = root.parent_path();
    }
    json manifest;
    std::string error;
    if (!readJsonFile(root / "arestrix.project.json", manifest, error)) {
        if (opt.json) std::cout << json{{"ok", false}, {"error", "manifesto: " + error}}.dump() << "\n";
        else std::cerr << "arestrix-cli project: manifesto: " << error << "\n";
        return kExitConfig;
    }

    json pipelines = json::array();
    if (manifest.contains("pipelines") && manifest["pipelines"].is_array()) pipelines = manifest["pipelines"];

    if (cmd == "info") {
        json out{{"ok", true}, {"root", root.string()},
                 {"projectId", manifest.value("projectId", std::string())},
                 {"name", manifest.value("name", std::string())},
                 {"version", manifest.value("version", std::string())},
                 {"rootPath", manifest.value("rootPath", std::string())},
                 {"pipelines", pipelines}};
        if (opt.json) std::cout << out.dump(2) << "\n";
        else {
            std::cout << "Projeto: " << out["name"].str() << " (" << out["projectId"].str() << ")\n";
            std::cout << "Versao: " << out["version"].str() << "\n";
            std::cout << "Raiz: " << root.string() << "\n";
            for (const auto& p : pipelines) {
                std::cout << "  pipeline: " << p.value("id", std::string()) << " -> "
                          << p.value("file", std::string());
                if (p.value("isPrimary", false)) std::cout << " (primario)";
                std::cout << "\n";
            }
        }
        return 0;
    }

    if (cmd == "graphs") {
        if (opt.json) std::cout << json{{"ok", true}, {"root", root.string()}, {"pipelines", pipelines}}.dump(2) << "\n";
        else for (const auto& p : pipelines) std::cout << p.value("id", std::string()) << " " << p.value("file", std::string()) << "\n";
        return 0;
    }

    if (cmd != "validate") {
        std::cerr << "arestrix-cli project: comando desconhecido: " << cmd << "\n";
        return kExitUsage;
    }

    json checks = json::array();
    bool ok = true;
    for (const auto& p : pipelines) {
        const std::string file = p.value("file", std::string());
        if (file.empty()) continue;
        const fs::path graphFile = root / file;
        core::Graph graph;
        std::string graphError;
        bool good = loadGraph(graphFile, graph, graphError);
        if (good) {
            try {
                good = !compiler::TaskflowGenerator::generateSource(graph).empty();
                if (!good) graphError = "codegen vazio";
            } catch (const std::exception& ex) {
                good = false;
                graphError = ex.what();
            }
        }
        checks.push_back(json{{"file", file}, {"ok", good}, {"error", graphError}});
        ok = ok && good;
    }
    if (opt.json) std::cout << json{{"ok", ok}, {"root", root.string()}, {"checks", checks}}.dump(2) << "\n";
    else {
        for (const auto& c : checks) {
            std::cout << (c.value("ok", false) ? "OK   " : "FALHA ") << c.value("file", std::string());
            if (!c.value("error", std::string()).empty()) std::cout << "  " << c.value("error", std::string());
            std::cout << "\n";
        }
        std::cout << (ok ? "project validate: OK\n" : "project validate: FALHOU\n");
    }
    return ok ? 0 : kExitPreflight;
}

int runCheck(const std::vector<std::string>& args) {
    Options opt;
    std::string parseError;
    if (!parseOptions(args, 1, opt, parseError)) {
        std::cerr << "arestrix-cli check: " << parseError << "\n";
        return kExitUsage;
    }
    if (opt.help) {
        std::cout << "arestrix-cli check [--project <pasta> | <grafo.json>] [--require-env NOME] [--json] [--ci]\n";
        return 0;
    }
    if (opt.ci) opt.json = true;

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

struct BuildOutcome {
    build::BuildResult result;
    bool timeoutHit = false;
    int exitCode = kExitRuntime;
    std::string error;
};

bool stdinIsTerminal() {
#ifdef _WIN32
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE) return false;
    DWORD mode = 0;
    if (GetConsoleMode(input, &mode) != 0) return true;
    // Git Bash/mintty usa um pty em pipe; o NUL (DEVNULL) e FILE_TYPE_CHAR.
    return _isatty(_fileno(stdin)) != 0 && GetFileType(input) == FILE_TYPE_PIPE;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

std::string lowerText(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string trimText(const std::string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> parts;
    std::stringstream ss(value);
    std::string part;
    while (std::getline(ss, part, ',')) {
        part = trimText(part);
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

std::string optionValue(const json& option) {
    if (option.is_object()) {
        return option.value("value", option.value("label", std::string()));
    }
    if (option.is_string()) return option.str();
    return "";
}

std::string optionLabel(const json& option) {
    if (option.is_object()) {
        return option.value("label", option.value("value", std::string()));
    }
    if (option.is_string()) return option.str();
    return optionValue(option);
}

// Host interativo do CLI: le o pedido do runtime e pergunta no terminal. Nunca
// e usado quando --answers esta definido (CI/automacao) nem quando stdin nao e TTY.
std::string cliPromptInteraction(const std::string& requestJson, const std::atomic<bool>* cancel) {
    auto request = jsonutil::tryParse(requestJson);
    if (!request || !request->is_object()) {
        return json{{"error", "Pedido de interacao invalido."}}.dump();
    }
    const std::string requestId = request->value("requestId", std::string());
    const auto cancelledResult = [&]() {
        return json{{"requestId", requestId}, {"cancelled", true}}.dump();
    };
    if (cancel && cancel->load()) return cancelledResult();

    const json form = request->contains("form") && (*request)["form"].is_object()
                          ? (*request)["form"] : json::object();
    const json fields = form.contains("fields") && form["fields"].is_array()
                            ? form["fields"] : json::array();
    const std::string title = form.value("title", request->value("label", std::string("Pergunta")));
    const std::string description = form.value("description", std::string());

    std::cout << "\n[INTERACAO] " << title << "\n";
    if (!description.empty()) std::cout << description << "\n";
    if (form.contains("context") && form["context"].is_object()) {
        const std::string context = form["context"].value("markdown", std::string());
        if (!context.empty()) std::cout << "\n" << context << "\n";
    }
    const std::string inputContext = request->value("inputContext", std::string());
    if (!inputContext.empty()) std::cout << "\nContexto do chamador:\n" << inputContext << "\n";
    if (request->value("sensitive", false)) {
        std::cout << "[aviso] resposta sensivel: o terminal ecoa o que for digitado.\n";
    }
    std::cout << "Digite :q para cancelar a interacao.\n";

    const auto readLine = [&](std::string& line) -> bool {
        if (!std::getline(std::cin, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
    };

    json answers = json::object();
    for (const auto& field : fields) {
        if (!field.is_object()) continue;
        const std::string id = field.value("id", std::string());
        if (id.empty()) continue;
        const std::string type = field.value("type", std::string("text"));
        const std::string label = field.value("label", id);
        const bool required = field.value("required", false);
        const json options = field.contains("options") && field["options"].is_array()
                                 ? field["options"] : json::array();
        const bool hasDefault = field.contains("defaultValue") && !field["defaultValue"].is_null();

        while (true) {
            if (cancel && cancel->load()) return cancelledResult();
            std::cout << "\n" << label;
            if (required) std::cout << " *";
            std::cout << " [" << type << "]\n";
            if (!options.empty()) {
                for (size_t i = 0; i < options.size(); ++i) {
                    std::cout << "  " << (i + 1) << ") " << optionLabel(options[i]) << "\n";
                }
            }
            if (hasDefault) std::cout << "  padrao: " << field["defaultValue"].dump() << "\n";
            std::cout << "> " << std::flush;

            std::string line;
            if (!readLine(line)) return cancelledResult();
            if (trimText(line) == ":q" || lowerText(trimText(line)) == "cancel" ||
                lowerText(trimText(line)) == "cancelar") {
                return cancelledResult();
            }

            if (trimText(line).empty()) {
                if (hasDefault) {
                    answers[id] = field["defaultValue"];
                    break;
                }
                if (required) {
                    std::cout << "Campo obrigatorio.\n";
                    continue;
                }
                break;
            }

            if (type == "number") {
                try {
                    size_t used = 0;
                    const double number = std::stod(line, &used);
                    if (used != line.size() || !std::isfinite(number)) throw std::invalid_argument("invalid");
                    answers[id] = number;
                    break;
                } catch (...) {
                    std::cout << "Informe um numero valido.\n";
                    continue;
                }
            }

            if (type == "boolean") {
                const std::string value = lowerText(trimText(line));
                if (value == "s" || value == "sim" || value == "y" || value == "yes" ||
                    value == "1" || value == "true") {
                    answers[id] = true;
                    break;
                }
                if (value == "n" || value == "nao" || value == "não" || value == "no" ||
                    value == "0" || value == "false") {
                    answers[id] = false;
                    break;
                }
                std::cout << "Responda s ou n.\n";
                continue;
            }

            if (type == "choice") {
                std::string selected = trimText(line);
                if (!options.empty()) {
                    bool matched = false;
                    const std::string maybeIndex = trimText(line);
                    if (!maybeIndex.empty() &&
                        std::all_of(maybeIndex.begin(), maybeIndex.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                        const int index = std::atoi(maybeIndex.c_str());
                        if (index >= 1 && static_cast<size_t>(index) <= options.size()) {
                            selected = optionValue(options[static_cast<size_t>(index - 1)]);
                            matched = true;
                        }
                    }
                    if (!matched) {
                        for (const auto& option : options) {
                            if (selected == optionValue(option) || selected == optionLabel(option)) {
                                selected = optionValue(option);
                                matched = true;
                                break;
                            }
                        }
                    }
                    if (!matched) {
                        std::cout << "Escolha uma das opcoes listadas.\n";
                        continue;
                    }
                }
                answers[id] = selected;
                break;
            }

            if (type == "multi_choice") {
                const auto parts = splitList(line);
                json selected = json::array();
                bool valid = true;
                for (const auto& part : parts) {
                    std::string value = part;
                    bool matched = false;
                    if (!options.empty()) {
                        if (std::all_of(part.begin(), part.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                            const int index = std::atoi(part.c_str());
                            if (index >= 1 && static_cast<size_t>(index) <= options.size()) {
                                value = optionValue(options[static_cast<size_t>(index - 1)]);
                                matched = true;
                            }
                        }
                        if (!matched) {
                            for (const auto& option : options) {
                                if (part == optionValue(option) || part == optionLabel(option)) {
                                    value = optionValue(option);
                                    matched = true;
                                    break;
                                }
                            }
                        }
                    } else {
                        matched = true;
                    }
                    if (!matched) {
                        valid = false;
                        break;
                    }
                    selected.push_back(value);
                }
                if (!valid) {
                    std::cout << "Use numeros ou valores separados por virgula.\n";
                    continue;
                }
                if (required && selected.empty()) {
                    std::cout << "Escolha ao menos uma opcao.\n";
                    continue;
                }
                answers[id] = selected;
                break;
            }

            // text, textarea, date e tipos desconhecidos: texto cru.
            answers[id] = line;
            break;
        }
    }

    return json{{"requestId", requestId}, {"answers", answers}}.dump();
}

BuildOutcome executeBuild(const core::Graph& graph, const fs::path& root, const Options& opt,
                          const build::BuildEvents& ev) {
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::atomic<bool> timeoutHit{false};
    build::BuildResult result;
    build::BuildEvents events = ev;
    const bool useCliPrompt = opt.answersFile.empty() && !opt.json && !opt.ci && stdinIsTerminal();
    if (useCliPrompt && !events.onInteraction) events.onInteraction = cliPromptInteraction;
    const std::string oldHostKind = platform::getEnv("ARESTRIX_INTERACTION_HOST_KIND");
    platform::setProcessEnv("ARESTRIX_INTERACTION_HOST_KIND", useCliPrompt ? "cli" : "");
    std::thread worker([&]() {
        result = build::buildAndRun(graph, root, events, &cancel);
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
    platform::setProcessEnv("ARESTRIX_INTERACTION_HOST_KIND", oldHostKind);

    BuildOutcome out;
    out.result = result;
    out.timeoutHit = timeoutHit;
    out.error = timeoutHit ? ("timeout de " + std::to_string(opt.timeoutSeconds) + "s") : result.error;
    out.exitCode = (result.success && !timeoutHit) ? 0
                 : (timeoutHit ? kExitTimeout : (result.stage == "toolchain" ? kExitDependency : kExitRuntime));
    return out;
}

int runBatch(const Options& opt) {
    json batch;
    std::string error;
    if (!readJsonFile(opt.batchFile, batch, error)) {
        std::cerr << "arestrix-cli run --batch: " << error << "\n";
        return kExitConfig;
    }
    if (!batch.is_object() || !batch.contains("items") || !batch["items"].is_array()) {
        std::cerr << "arestrix-cli run --batch: manifesto precisa de items[]\n";
        return kExitConfig;
    }

    fs::path root = opt.root.empty() ? fs::current_path() : fs::absolute(opt.root);
    if (opt.root.empty() && batch.contains("root") && batch["root"].is_string()) {
        root = fs::absolute(batch["root"].get<std::string>());
    }
    fs::path graphFile = opt.graph;
    if (graphFile.empty() && batch.contains("graph") && batch["graph"].is_string()) {
        graphFile = fs::path(batch["graph"].get<std::string>());
    }
    if (graphFile.empty() && !opt.project.empty()) {
        ProjectPaths project = resolveProject(opt.project);
        if (!project.ok) {
            std::cerr << "arestrix-cli run --batch: " << project.error << "\n";
            return kExitConfig;
        }
        root = project.root;
        graphFile = project.graph;
    }
    if (graphFile.empty()) {
        std::cerr << "arestrix-cli run --batch: informe graph no manifesto ou --project\n";
        return kExitConfig;
    }
    if (graphFile.is_relative()) graphFile = root / graphFile;

    core::Graph graph;
    if (!loadGraph(graphFile, graph, error)) {
        std::cerr << "arestrix-cli run --batch: " << error << "\n";
        return kExitConfig;
    }

    fs::path statePath = opt.stateFile.empty() ? root / ".arestrix" / "batch-state.json" : fs::absolute(opt.stateFile);
    json state = json::object();
    state["items"] = json::object();
    if (opt.resume) {
        json old;
        std::string oldError;
        if (readJsonFile(statePath, old, oldError) && old.is_object() && old.contains("items") && old["items"].is_object()) {
            state["items"] = old["items"];
        }
    }

    json results = json::array();
    bool allOk = true;
    for (size_t i = 0; i < batch["items"].size(); ++i) {
        const json& item = batch["items"][i];
        const std::string id = item.is_object() && item.contains("id") && item["id"].is_string()
            ? item["id"].get<std::string>() : std::to_string(i + 1);
        if (opt.resume && state["items"].contains(id) && state["items"][id].value("ok", false)) {
            if (opt.json) std::cout << json{{"event", "item"}, {"id", id}, {"ok", true}, {"skipped", true}}.dump() << "\n";
            results.push_back(json{{"id", id}, {"ok", true}, {"skipped", true}});
            continue;
        }

        std::map<std::string, std::string> oldEnv;
        bool envOk = true;
        if (item.is_object() && item.contains("env") && item["env"].is_object()) {
            for (auto it = item["env"].begin(); it != item["env"].end(); ++it) {
                if (!it->is_string() || !validEnvName(it.key())) { envOk = false; break; }
                oldEnv[it.key()] = platform::getEnv(it.key());
                platform::setProcessEnv(it.key(), it->get<std::string>());
            }
        }
        if (!envOk) {
            for (const auto& [key, value] : oldEnv) platform::setProcessEnv(key, value);
            allOk = false;
            results.push_back(json{{"id", id}, {"ok", false}, {"error", "env invalido"}});
            continue;
        }

        build::BuildEvents ev;
        ev.onPhase = [&](const std::string& phase, const std::string& msg) {
            if (opt.quiet) return;
            if (opt.json) std::cout << json{{"event", "item"}, {"id", id}, {"kind", "phase"}, {"phase", phase}, {"message", msg}}.dump() << "\n";
            else std::cout << "[" << id << "][" << phase << "] " << msg << std::endl;
        };
        ev.onLog = [&](const std::string& line, bool isError) {
            if (opt.quiet) return;
            if (opt.json) std::cout << json{{"event", "item"}, {"id", id}, {"kind", "log"}, {"line", line}, {"error", isError}}.dump() << "\n";
            else std::cout << "[" << id << "] " << line << std::endl;
        };
        ev.onTelemetry = [&](const std::string& line) {
            if (opt.quiet) return;
            if (opt.json) std::cout << json{{"event", "item"}, {"id", id}, {"kind", "telemetry"}, {"raw", line}}.dump() << "\n";
            else std::cout << "[" << id << "][telemetria] " << line << std::endl;
        };

        BuildOutcome outcome = executeBuild(graph, root, opt, ev);
        for (const auto& [key, value] : oldEnv) platform::setProcessEnv(key, value);
        const bool ok = outcome.exitCode == 0;
        allOk = allOk && ok;
        state["items"][id] = json{{"ok", ok}, {"exitCode", outcome.exitCode}, {"elapsedMs", outcome.result.elapsedMs}};
        results.push_back(json{{"id", id}, {"ok", ok}, {"exitCode", outcome.exitCode},
                               {"elapsedMs", outcome.result.elapsedMs}, {"error", outcome.error}});
        if (opt.json) std::cout << json{{"event", "item"}, {"id", id}, {"ok", ok},
                                        {"exitCode", outcome.exitCode}, {"elapsedMs", outcome.result.elapsedMs},
                                        {"error", outcome.error}}.dump() << "\n";
        else std::cout << "[" << id << "] " << (ok ? "OK" : "FALHA") << " (" << outcome.result.elapsedMs << " ms)\n";
    }

    std::error_code ec;
    fs::create_directories(statePath.parent_path(), ec);
    std::ofstream stateOut(statePath, std::ios::binary | std::ios::trunc);
    if (stateOut) stateOut << state.dump(2) << "\n";

    json report{{"schema", 1}, {"command", "run-batch"}, {"graph", graphFile.string()},
                {"root", root.string()}, {"success", allOk}, {"items", results}};
    if (opt.json) std::cout << json{{"event", "result"}, {"ok", allOk}, {"graph", graphFile.string()},
                                    {"root", root.string()}, {"items", results}}.dump() << "\n";
    else std::cout << "batch: " << (allOk ? "OK" : "FALHOU") << " (" << results.size() << " itens)\n";
    if (!opt.report.empty()) {
        const fs::path reportPath = fs::absolute(opt.report, ec);
        if (!reportPath.parent_path().empty()) fs::create_directories(reportPath.parent_path(), ec);
        std::ofstream out(reportPath, std::ios::binary | std::ios::trunc);
        if (out) out << report.dump(2) << "\n";
    }
    return allOk ? 0 : kExitRuntime;
}

void applyInteractionAnswers(const Options& opt) {
    if (opt.answersFile.empty()) return;
    fs::path answers = opt.answersFile;
    if (answers.is_relative()) answers = fs::absolute(answers);
    platform::setProcessEnv("ARESTRIX_INTERACTION_ANSWERS", answers.string());
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
            : "arestrix-cli run <grafo.json>|--project <pasta> [--root <pasta>] [--json] [--quiet] [--ci] [--timeout N] [--report FILE] [--answers FILE]\n");
        return 0;
    }
    if (opt.ci) {
        opt.json = true;
        opt.quiet = true;
        opt.noColor = true;
        if (opt.timeoutSeconds == 0) opt.timeoutSeconds = 900;
    }
    applyInteractionAnswers(opt);
    if (!genOnly && !opt.batchFile.empty()) return runBatch(opt);

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

    BuildOutcome outcome = executeBuild(graph, root, opt, ev);
    const bool success = outcome.exitCode == 0;
    const std::string& error = outcome.error;
    const int exitCode = outcome.exitCode;
    const auto& result = outcome.result;

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
        std::cerr << "[FALHA:" << (outcome.timeoutHit ? "timeout" : result.stage) << "] " << error << std::endl;
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

int runCompletion(const std::vector<std::string>& args) {
    if (args.size() < 2 || args[1] == "--help" || args[1] == "-h") {
        std::cout << "arestrix-cli completion powershell|bash|zsh\n";
        return args.size() < 2 ? kExitUsage : 0;
    }
    const std::string shell = args[1];
    const std::string commands = "run gen check doctor env memory mcp project version completion";
    if (shell == "powershell" || shell == "pwsh") {
        std::cout <<
            "Register-ArgumentCompleter -Native -CommandName arestrix-cli -ScriptBlock {\n"
            "    param($wordToComplete, $commandAst, $cursorPosition)\n"
            "    '" << commands << "'.Split(' ') | Where-Object { $_ -like \"$wordToComplete*\" } |\n"
            "        ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }\n"
            "}\n";
        return 0;
    }
    if (shell == "bash") {
        std::cout <<
            "_arestrix_cli() {\n"
            "  local cur=\"${COMP_WORDS[COMP_CWORD]}\"\n"
            "  COMPREPLY=( $(compgen -W \"" << commands << "\" -- \"$cur\") )\n"
            "}\n"
            "complete -F _arestrix_cli arestrix-cli\n";
        return 0;
    }
    if (shell == "zsh") {
        std::cout <<
            "#compdef arestrix-cli\n"
            "_arestrix_cli() {\n"
            "  compadd " << commands << "\n"
            "}\n"
            "compdef _arestrix_cli arestrix-cli\n";
        return 0;
    }
    std::cerr << "arestrix-cli completion: shell desconhecido: " << shell << "\n";
    return kExitUsage;
}

} // namespace

int runCompress(const std::vector<std::string>& args) {
    using namespace taskforge::rt::compress;

    // Diagnostico da maquina: qual artefato do Kompress faz sentido aqui.
    if (args.size() >= 2 && args[1] == "--status") {
        const auto profile = detectMachineProfile(defaultProseModelDir());
        const auto recommendation = recommendProseVariant(profile);
        json out = json::object();
        out["cpuThreads"] = profile.cpuThreads;
        out["ramTotalMB"] = profile.ramTotalMB;
        out["ramFreeMB"] = profile.ramFreeMB;
        out["diskFreeMB"] = profile.diskFreeMB;
        out["dmlAvailable"] = profile.dmlAvailable;
        out["dmlDevice"] = profile.dmlDevice;
#ifdef ARESTRIX_WITH_ONNX
        out["onnx"] = true;
#else
        out["onnx"] = false;
#endif
#ifdef ARESTRIX_WITH_ONNX_DML
        out["dmlBuild"] = true;
#else
        out["dmlBuild"] = false;
#endif
        json gpus = json::array();
        for (const auto& gpu : profile.gpus) {
            gpus.push_back(json{{"index", gpu.index},
                                {"name", gpu.name},
                                {"vramMB", gpu.vramMB},
                                {"vendor", gpu.vendor}});
        }
        out["gpus"] = std::move(gpus);
        out["recommended"] = recommendation.variant;
        out["reason"] = recommendation.reason;
        out["sizeMB"] = proseModelSizeMB(recommendation.variant);
        const bool asJson = std::find(args.begin(), args.end(), "--json") != args.end();
        if (asJson) {
            std::cout << out.dump(2) << "\n";
        } else {
            std::cout << "cpu: " << profile.cpuThreads << " nucleos\n";
            std::cout << "ram: " << profile.ramTotalMB << " MB total, " << profile.ramFreeMB << " MB livre\n";
            std::cout << "disco livre: " << profile.diskFreeMB << " MB\n";
            for (const auto& gpu : profile.gpus) {
                std::cout << "gpu " << gpu.index << ": " << gpu.name << " (" << gpu.vendor << ", "
                          << gpu.vramMB << " MB dedicados)"
                          << (gpu.index == profile.dmlDevice ? " [escolhida]" : "") << "\n";
            }
            if (!profile.gpus.empty()) {
                std::cout << "directml: " << (profile.dmlAvailable ? "disponivel" : "indisponivel")
                          << "\n";
            }
#ifndef ARESTRIX_WITH_ONNX
            std::cout << "prosa (onnx): desabilitada neste build\n";
#endif
            std::cout << "recomendado: " << recommendation.variant
                      << " (" << proseModelSizeMB(recommendation.variant) << " MB, motivo: "
                      << recommendation.reason << ")\n";
        }
        return 0;
    }

    if (args.size() < 2 || args[1].empty() || args[1].rfind("--", 0) == 0) {
        std::cerr << "uso: arestrix-cli compress <arquivo>|--status [--prose] [--model-dir <pasta>] [--json]\n";
        return kExitUsage;
    }

    const fs::path file = args[1];
    bool asJson = false;
    bool withProse = false;
    std::string modelDir = defaultProseModelDir();
    for (size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--json") asJson = true;
        else if (args[i] == "--prose") withProse = true;
        else if (args[i] == "--model-dir" && i + 1 < args.size()) modelDir = args[++i];
        else {
            std::cerr << "opcao desconhecida: " << args[i] << "\n";
            return kExitUsage;
        }
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::cerr << "nao foi possivel abrir: " << file.string() << "\n";
        return kExitRuntime;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    const Classification classification = classifyContent(text);
    const ContentKind kind = classification.kind;
    CompressResult result;
    result.bytesBefore = text.size();
    result.bytesAfter = text.size();
    result.text = text;
    std::string compressor = "none";
    std::string provider;

    switch (kind) {
        case ContentKind::Json:
            result = crushJson(text, JsonOptions{});
            compressor = "json";
            break;
        case ContentKind::Log:
            result = compressLog(text, LogOptions{});
            compressor = "log";
            break;
        case ContentKind::Code:
            result = compressCode(text, CodeOptions{});
            compressor = "code";
            break;
        case ContentKind::Prose:
            if (withProse) {
                auto tokenizer = ProseTokenizer::fromJsonFile((fs::path(modelDir) / "tokenizer.json").string());
                auto model = tokenizer ? loadProseModel(modelDir) : nullptr;
                if (tokenizer && model && model->available()) {
                    result = compressProse(text, *tokenizer, *model, ProseOptions{});
                    compressor = "prose";
                    provider = model->provider();
                } else {
                    std::cerr << "modelo de prosa indisponivel em " << modelDir << "\n";
                }
            }
            break;
        case ContentKind::Skip:
            break;
    }

    const size_t tokensBefore = estimateTokens(text);
    const size_t tokensAfter = estimateTokens(result.text);
    const double ratio = text.empty() ? 1.0 : static_cast<double>(result.text.size()) / static_cast<double>(text.size());

    if (asJson) {
        json out = json::object();
        out["file"] = file.string();
        out["kind"] = contentKindName(kind);
        out["compressor"] = compressor;
        out["provider"] = provider;
        out["changed"] = result.changed;
        out["bytesBefore"] = static_cast<int64_t>(text.size());
        out["bytesAfter"] = static_cast<int64_t>(result.text.size());
        out["ratio"] = ratio;
        out["tokensBefore"] = static_cast<int64_t>(tokensBefore);
        out["tokensAfter"] = static_cast<int64_t>(tokensAfter);
        out["text"] = result.text;
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << "arquivo: " << file.string() << "\n";
        std::cout << "tipo: " << contentKindName(kind) << " · compressor: " << compressor;
        if (!provider.empty()) std::cout << " · provedor: " << provider;
        std::cout << "\n";
        std::cout << "bytes: " << text.size() << " -> " << result.text.size()
                  << " (razao " << ratio << ")\n";
        std::cout << "tokens: ~" << tokensBefore << " -> ~" << tokensAfter << "\n";
        if (!result.changed) {
            std::cout << "sem ganho suficiente; texto original mantido\n";
        } else {
            std::cout << "preview:\n" << result.text.substr(0, 400) << "\n";
        }
    }
    return 0;
}

int runCli(int argc, char** argv) {
    bool noUserEnv = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-user-env") noUserEnv = true;
    }
    if (!noUserEnv) platform::loadPersistedUserEnv();
    if (argc < 2) {
        printHelp();
        return 0;
    }
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-user-env") continue;
        args.emplace_back(argv[i]);
    }
    if (args.empty()) {
        printHelp();
        return 0;
    }

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
    if (args[0] == "doctor") return runDoctor(args);
    if (args[0] == "memory") return runMemory(args);
    if (args[0] == "mcp") return runMcp(args);
    if (args[0] == "project") return runProject(args);
    if (args[0] == "completion") return runCompletion(args);
    if (args[0] == "run") return runGraph(args, false);
    if (args[0] == "gen") return runGraph(args, true);
    if (args[0] == "compress") return runCompress(args);

    std::cerr << "arestrix-cli: comando desconhecido: " << args[0] << "\n\n";
    printHelp();
    return kExitUsage;
}

} // namespace cli
} // namespace taskforge
