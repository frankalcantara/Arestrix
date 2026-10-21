#pragma once

// Tipos compartilhados do arestrix-gateway: configuracao, chaves virtuais, budgets e uso.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace gateway {

struct Price {
    double input = 0.0;       // USD por 1M tokens de entrada
    double output = 0.0;      // USD por 1M tokens de saida
    double cachedInput = 0.0; // USD por 1M tokens de entrada em cache
};

struct ProviderRoute {
    std::string id;
    std::string protocol = "openai"; // openai | anthropic
    std::string baseUrl;
    std::string keyEnv;
    bool enabled = true;
};

struct GatewayConfig {
    std::string host = "127.0.0.1";
    int port = 8787;
    std::string database = "arestrix-gateway.db";
    int defaultMaxTokens = 8192;
    int requestTimeoutSeconds = 600;
    long long maxBodyBytes = 32LL * 1024 * 1024;
    std::map<std::string, ProviderRoute> routes;
    std::map<std::string, std::map<std::string, Price>> pricing;

    static bool load(const std::filesystem::path& path, GatewayConfig& config, std::string& error);
};

struct VirtualKey {
    std::string id;
    std::string name;
    std::string project;
    std::string user;
    bool enabled = false;
    int rpmLimit = 0;
    int tpmLimit = 0;
    std::string createdAt;
    std::string lastUsedAt;
};

struct RequestContext {
    std::string requestId;
    std::string keyId;
    std::string project;
    std::string user;
    std::string provider;
    std::string model;
    std::string runner;
};

struct TokenUsage {
    long long input = 0;
    long long output = 0;
    long long cached = 0;

    long long total() const { return input + output; }
};

struct BudgetDecision {
    bool allowed = true;
    bool warning = false;
    std::string reason;
    double estimatedUsd = 0.0;
    long long estimatedTokens = 0;
    Json budgets = Json::array();
};

struct UsageEvent {
    RequestContext context;
    TokenUsage usage;
    double costUsd = 0.0;
    long long latencyMs = 0;
    std::string status;
    std::string errorCode;
    // Metadados de memoria enviados pelo runtime (X-Arestrix-Memory-*).
    std::string agent;
    std::string runId;
    std::string memoryScope;
    bool memoryEnabled = false;
    long long memoryContextChars = 0;
    long long memoryTokens = 0;   // estimativa: caracteres / 4
};

std::string nowIso();
std::string periodWindow(const std::string& period);
bool modelPrice(const GatewayConfig& config, const std::string& provider,
                const std::string& model, Price& price);

} // namespace gateway
} // namespace taskforge
