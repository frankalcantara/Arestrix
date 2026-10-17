#pragma once

// Cliente de modelo sem estado: uma chamada com historico completo em formato neutro, convertido para o
// protocolo do provedor (OpenAI-compativel ou Anthropic Messages). Usado pelos agentes do pipeline e pelo
// Assistente do Studio. Provedor, chave e URL seguem as variaveis de ambiente do usuario.

#include <string>
#include <vector>

#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

struct ProviderInfo {
    std::string protocol;   // "openai" | "anthropic"
    std::string baseUrl;
    std::string path;
    std::string keyEnv;
    bool keyRequired = true;
    bool supportsTools = true;   // false: o provedor nao serve ao Assistente (sem chamada de ferramentas)
};

// Provedores conhecidos, na ordem de exibicao.
const std::vector<std::string>& knownProviders();
ProviderInfo resolveProvider(const std::string& provider);

struct ChatToolCall {
    std::string id;
    std::string name;
    Json args = Json::object();
};

// role: "user" | "assistant" | "tool". Mensagens "tool" consecutivas respondem as chamadas do assistente anterior.
struct ChatMessage {
    std::string role;
    std::string text;
    std::string reasoning;               // so assistant (DeepSeek reasoning_content)
    std::vector<ChatToolCall> toolCalls; // so assistant
    std::string toolCallId;              // so tool
    bool isError = false;                // so tool

    Json toJson() const;
    static ChatMessage fromJson(const Json& j);
};

struct ChatRequest {
    std::string provider;
    std::string model;
    std::string system;
    std::vector<ChatMessage> messages;
    std::vector<ToolDef> tools;
    int maxTokens = 8192;
    bool hasTemperature = false;
    double temperature = 0.0;
    int httpAttempts = 6;
    bool includeReasoning = true;   // false depois que o servidor recusou reasoning_content no historico
    std::string telemetryId;   // vazio = sem telemetria de retentativas
};

struct ChatReply {
    bool ok = false;
    std::string text;
    std::string reasoning;
    std::vector<ChatToolCall> toolCalls;
    std::string stopReason;
    long long inTokens = 0;
    long long outTokens = 0;
    bool reasoningRejected = false;   // o servidor recusou reasoning_content: envie os proximos sem ele
    // Falha: codigo estavel (model_not_configured, base_url_missing, api_key_missing, tools_unsupported,
    // rate_limited, http_error, bad_response) e detalhe tecnico.
    std::string errorCode;
    std::string error;
};

// Corpo da requisicao no protocolo do provedor (exposto para testes).
Json buildRequestBody(const ProviderInfo& info, const ChatRequest& req, bool includeReasoning = true);

// Interpreta a resposta do provedor (exposto para testes). Lanca std::runtime_error se o formato for inesperado.
ChatReply parseResponseBody(const ProviderInfo& info, const std::string& body);

// Uma chamada ao modelo. Nunca lanca excecao: veja ok/errorCode/error.
ChatReply chatOnce(const ChatRequest& req);

} // namespace rt
} // namespace taskforge
