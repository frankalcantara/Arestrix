#include "gateway/GatewayServer.hpp"
#include "gateway/GatewayStore.hpp"
#include "gateway/GatewayTypes.hpp"
#include "project/OpenableFiles.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

using taskforge::Json;
namespace gw = taskforge::gateway;
using gw::GatewayStore;

void printUsage() {
    std::cout <<
        "arestrix-gateway\n\n"
        "Uso:\n"
        "  arestrix-gateway serve [--config PATH] [--db PATH]\n"
        "  arestrix-gateway init [--db PATH]\n"
        "  arestrix-gateway key create --name NOME [--project P] [--user U]\n"
        "      [--rpm N] [--tpm N] [--budget-usd N] [--period daily|monthly|total] [--db PATH]\n"
        "  arestrix-gateway key list [--db PATH]\n"
        "  arestrix-gateway key revoke KEY_ID [--db PATH]\n"
        "  arestrix-gateway budget set --scope key|project|user|provider|model --id ID\n"
        "      --period daily|monthly|total [--usd N] [--tokens N] [--soft] [--alert 0.8] [--db PATH]\n"
        "  arestrix-gateway usage report [--since ISO] [--key KEY_ID] [--provider P] [--db PATH]\n"
        "  arestrix-gateway version\n\n"
        "Variaveis de ambiente do gateway:\n"
        "  OPENAI_API_KEY, ANTHROPIC_API_KEY, GEMINI_API_KEY, DEEPSEEK_API_KEY, GROQ_API_KEY, MISTRAL_API_KEY\n\n"
        "O gateway escuta por padrao em 127.0.0.1:8787. Coloque um reverse proxy TLS na frente\n"
        "para exposicao em rede. O Studio/runner usam ARESTRIX_AI_GATEWAY_URL e ARESTRIX_AI_GATEWAY_KEY.\n";
}

std::string argValue(const std::vector<std::string>& args, const std::string& flag,
                     const std::string& fallback = std::string()) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) return args[i + 1];
    }
    return fallback;
}

bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& arg : args) if (arg == flag) return true;
    return false;
}

long long intValue(const std::vector<std::string>& args, const std::string& flag, long long fallback) {
    const std::string value = argValue(args, flag);
    if (value.empty()) return fallback;
    try { return std::stoll(value); } catch (...) { return fallback; }
}

double doubleValue(const std::vector<std::string>& args, const std::string& flag, double fallback) {
    const std::string value = argValue(args, flag);
    if (value.empty()) return fallback;
    try { return std::stod(value); } catch (...) { return fallback; }
}

std::string databasePath(const std::vector<std::string>& args, const std::string& fallback = "arestrix-gateway.db") {
    return argValue(args, "--db", fallback);
}

bool openStore(GatewayStore& store, const std::string& path, std::string& error) {
    if (!store.open(path, error)) return false;
    return store.initSchema(error);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        printUsage();
        return 0;
    }

    const std::string command = args[0];
    if (command == "version" || command == "--version") {
#ifdef ARESTRIX_GATEWAY_VERSION
        std::cout << "arestrix-gateway " << ARESTRIX_GATEWAY_VERSION << "\n";
#else
        std::cout << "arestrix-gateway dev\n";
#endif
        return 0;
    }

    std::string error;

    if (command == "serve") {
        const std::string configPath = argValue(args, "--config", "arestrix-gateway.json");
        gw::GatewayConfig config;
        if (!gw::GatewayConfig::load(taskforge::project::pathFromUtf8(configPath), config, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        const std::string dbOverride = argValue(args, "--db");
        if (!dbOverride.empty()) config.database = dbOverride;

        GatewayStore store;
        if (!openStore(store, config.database, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        gw::GatewayServer server(std::move(config), store);
        if (!server.run(error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        return 0;
    }

    if (command == "init") {
        GatewayStore store;
        if (!openStore(store, databasePath(args), error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        std::cout << "banco inicializado: " << databasePath(args) << "\n";
        return 0;
    }

    if (command == "key" && args.size() >= 2 && args[1] == "create") {
        const std::string db = databasePath(args);
        GatewayStore store;
        if (!openStore(store, db, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        std::string plaintext;
        std::string keyId;
        if (!store.createKey(argValue(args, "--name", "default"),
                             argValue(args, "--project"),
                             argValue(args, "--user"),
                             static_cast<int>(intValue(args, "--rpm", 0)),
                             static_cast<int>(intValue(args, "--tpm", 0)),
                             plaintext, keyId, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        const double budget = doubleValue(args, "--budget-usd", -1.0);
        if (budget >= 0) {
            const std::string period = argValue(args, "--period", "monthly");
            if (!store.setBudget("key", keyId, period, budget, -1, true, 0.8, error)) {
                std::cerr << "aviso: chave criada, mas o budget falhou: " << error << "\n";
            }
        }
        Json out = Json::object();
        out["keyId"] = keyId;
        out["key"] = plaintext;
        out["project"] = argValue(args, "--project");
        out["user"] = argValue(args, "--user");
        std::cout << out.dump(2) << "\n";
        std::cerr << "Guarde a chave agora; ela nao sera exibida novamente.\n";
        return 0;
    }

    if (command == "key" && args.size() >= 2 && args[1] == "list") {
        GatewayStore store;
        if (!openStore(store, databasePath(args), error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        Json out;
        if (!store.listKeys(out, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (command == "key" && args.size() >= 3 && args[1] == "revoke") {
        GatewayStore store;
        if (!openStore(store, databasePath(args), error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        if (!store.revokeKey(args[2], error)) {
            std::cerr << "erro: " << (error.empty() ? "chave nao encontrada" : error) << "\n";
            return 1;
        }
        std::cout << "chave revogada: " << args[2] << "\n";
        return 0;
    }

    if (command == "budget" && args.size() >= 2 && args[1] == "set") {
        const std::string scope = argValue(args, "--scope");
        const std::string id = argValue(args, "--id");
        const std::string period = argValue(args, "--period", "monthly");
        const double usd = doubleValue(args, "--usd", -1.0);
        const long long tokens = intValue(args, "--tokens", -1);
        const bool hardStop = !hasFlag(args, "--soft");
        const double alert = doubleValue(args, "--alert", 0.8);

        GatewayStore store;
        if (!openStore(store, databasePath(args), error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        if (!store.setBudget(scope, id, period, usd, tokens, hardStop, alert, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        std::cout << "budget atualizado: " << scope << "/" << id << " " << period << "\n";
        return 0;
    }

    if (command == "usage" && args.size() >= 2 && args[1] == "report") {
        GatewayStore store;
        if (!openStore(store, databasePath(args), error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        Json out;
        if (!store.usageReport(argValue(args, "--since"), argValue(args, "--key"),
                               argValue(args, "--provider"), out, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    std::cerr << "comando invalido\n\n";
    printUsage();
    return 1;
}
