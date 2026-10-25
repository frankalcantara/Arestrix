#include "memory/MemoryInstitutionApi.hpp"
#include "memory/MemoryInstitutionServer.hpp"
#include "memory/MemoryInstitutionService.hpp"
#include "util/Json.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

using taskforge::Json;
using taskforge::memory::MemoryInstitutionService;

std::string argValue(const std::vector<std::string>& args, const std::string& flag,
                     const std::string& fallback = std::string()) {
    for (size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == flag) return args[i + 1];
    }
    return fallback;
}

int intValue(const std::vector<std::string>& args, const std::string& flag, int fallback) {
    const std::string value = argValue(args, flag);
    if (value.empty()) return fallback;
    try { return std::stoi(value); } catch (...) { return fallback; }
}

std::string databasePath(const std::vector<std::string>& args) {
    return argValue(args, "--db", ".arestrix/memory/institution.db");
}

std::string generateToken() {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device(),
                       device(), device(), device(), device()};
    std::mt19937_64 generator(seed);
    std::uniform_int_distribution<int> digit(0, 15);
    static const char* hex = "0123456789abcdef";
    std::string token;
    token.reserve(64);
    for (int i = 0; i < 64; ++i) token.push_back(hex[digit(generator)]);
    return token;
}

std::string institutionId(const std::vector<std::string>& args) {
    return argValue(args, "--institution", "default");
}

void printUsage() {
    std::cout <<
        "arestrix-memory\n\n"
        "Uso:\n"
        "  arestrix-memory serve [--db PATH] [--institution ID] [--host 127.0.0.1]\n"
        "      [--port 8790] [--tokens-file PATH] [--tokens-json JSON]\n"
        "  arestrix-memory mcp [--db PATH] [--institution ID] [--token TOKEN]\n"
        "      [--principal NOME] [--role viewer|editor|admin]\n"
        "  arestrix-memory init [--db PATH] [--institution ID]\n"
        "  arestrix-memory token [--principal NOME] [--role viewer|editor|admin]\n"
        "  arestrix-memory version\n\n"
        "Tokens:\n"
        "  ARESTRIX_INSTITUTION_TOKENS       JSON [{\"token\":\"...\",\"principal\":\"ana\",\"role\":\"editor\"}]\n"
        "  ARESTRIX_INSTITUTION_TOKEN        token unico para o modo MCP\n"
        "  Arquivo --tokens-file              uma linha por token: token principal role\n\n"
        "O servidor nao usa TLS nem persiste tokens. Escute em 127.0.0.1 e coloque um\n"
        "reverse proxy TLS na frente para exposicao em rede. Sem tokens, tudo responde 401.\n"
        "Prefira --tokens-file ou ARESTRIX_INSTITUTION_TOKENS; --tokens-json pode ficar no historico.\n";
}

bool configureService(MemoryInstitutionService& service,
                      const std::vector<std::string>& args, std::string& error) {
    return service.configure(institutionId(args), databasePath(args), error);
}

bool loadTokens(MemoryInstitutionService& service, const std::vector<std::string>& args,
                std::string& error) {
    std::string tokensJson = argValue(args, "--tokens-json");
    if (tokensJson.empty()) {
        const char* env = std::getenv("ARESTRIX_INSTITUTION_TOKENS");
        if (env && *env) tokensJson = env;
    }
    return taskforge::memory::loadInstitutionTokens(
        service, argValue(args, "--tokens-file"), tokensJson, error);
}

int runMcp(MemoryInstitutionService& service, const std::vector<std::string>& args,
           std::string& error) {
    std::string token = argValue(args, "--token");
    if (token.empty()) {
        const char* env = std::getenv("ARESTRIX_INSTITUTION_TOKEN");
        if (env && *env) token = env;
    }
    if (token.empty()) {
        error = "token obrigatorio para MCP (--token ou ARESTRIX_INSTITUTION_TOKEN)";
        return 1;
    }
    const std::string principal = argValue(args, "--principal", "mcp");
    const std::string role = argValue(args, "--role", "viewer");
    if (!service.addToken(token, principal, role)) {
        error = "papel invalido: use viewer, editor ou admin";
        return 1;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto parsed = taskforge::jsonutil::tryParse(line);
        if (!parsed) {
            const Json response{{"jsonrpc", "2.0"},
                                {"id", nullptr},
                                {"error", Json{{"code", -32700}, {"message", "JSON invalido"}}}};
            std::cout << response.dump() << "\n" << std::flush;
            continue;
        }
        auto result = taskforge::memory::handleInstitutionMcp(service, token, *parsed);
        if (result.hasResponse) {
            std::cout << result.response.dump() << "\n" << std::flush;
        }
    }
    return 0;
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
        std::cout << "arestrix-memory " << taskforge::memory::institutionServiceVersion() << "\n";
        return 0;
    }

    std::string error;
    MemoryInstitutionService service;

    if (command == "token") {
        const std::string role = argValue(args, "--role", "viewer");
        if (role != "viewer" && role != "editor" && role != "admin") {
            std::cerr << "erro: papel invalido: use viewer, editor ou admin\n";
            return 1;
        }
        const std::string token = generateToken();
        const std::string principal = argValue(args, "--principal", "operator");
        const Json out{{"token", token}, {"principal", principal}, {"role", role}};
        std::cout << out.dump(2) << "\n";
        std::cerr << "Guarde o token agora; ele nao sera exibido novamente.\n";
        return 0;
    }

    if (command == "init") {
        if (!configureService(service, args, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        const Json out{{"ok", true},
                       {"institutionId", institutionId(args)},
                       {"database", databasePath(args)}};
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    if (command == "serve") {
        if (!configureService(service, args, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        if (!loadTokens(service, args, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        const std::string host = argValue(args, "--host", "127.0.0.1");
        const int port = intValue(args, "--port", 8790);
        if (port < 1 || port > 65535) {
            std::cerr << "erro: porta invalida\n";
            return 1;
        }
        taskforge::memory::MemoryInstitutionServer server(service);
        if (!server.run(host, port, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        return 0;
    }

    if (command == "mcp") {
        if (!configureService(service, args, error)) {
            std::cerr << "erro: " << error << "\n";
            return 1;
        }
        const int code = runMcp(service, args, error);
        if (!error.empty()) std::cerr << "erro: " << error << "\n";
        return code;
    }

    std::cerr << "arestrix-memory: comando desconhecido: " << command << "\n";
    printUsage();
    return 1;
}
