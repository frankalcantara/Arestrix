#pragma once

// Cliente de modelo sem estado: uma chamada com historico completo em formato neutro, convertido para o
// protocolo do provedor (OpenAI-compativel ou Anthropic Messages). Usado pelos agentes do pipeline e pelo
// Assistente do Studio. Provedor, chave e URL seguem as variaveis de ambiente do usuario.

#include <map>
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
    bool gateway = false;        // roteado pelo arestrix-gateway (chave virtual)
    std::string configError;     // gateway_required, etc.; preenchido antes de qualquer chamada
};

// Provedores conhecidos, na ordem de exibicao.
const std::vector<std::string>& knownProviders();
ProviderInfo resolveProvider(const std::string& provider);

// Normaliza a URL de um servidor local OpenAI-compativel. Aceita a raiz do servidor
// (http://localhost:11434), a base ja versionada (http://host:8000/v1) e, por conveniencia,
// a URL completa de /models ou /chat/completions. So acrescenta /v1 quando a URL nao tem caminho.
std::string normalizeLocalBaseUrl(std::string baseUrl);

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
    // Metadados de memoria enviados ao AI Gateway (somente quando info.gateway == true).
    std::string agentId;
    std::string runId;
    std::string memoryScope;
    bool memoryEnabled = false;
    long long memoryContextChars = 0;
};

struct ChatReply {
    bool ok = false;
    std::string text;
    std::string reasoning;
    std::vector<ChatToolCall> toolCalls;
    std::string stopReason;
    long long inTokens = 0;
    long long outTokens = 0;
    long long cachedTokens = 0;      // prompt cache read (desconto no custo)
    long long cacheWriteTokens = 0;  // prompt cache write, quando o provedor informa
    bool reasoningRejected = false;   // o servidor recusou reasoning_content: envie os proximos sem ele
    int retries = 0;                  // retentativas HTTP (429/5xx/timeout)
    int httpStatus = 0;               // ultimo status HTTP, quando houver
    // Falha: codigo estavel (model_not_configured, base_url_missing, api_key_missing, gateway_required,
    // tools_unsupported, budget_exceeded, rate_limited, http_error, bad_response) e detalhe tecnico.
    std::string errorCode;
    std::string error;
};

// Corpo da requisicao no protocolo do provedor (exposto para testes).
Json buildRequestBody(const ProviderInfo& info, const ChatRequest& req, bool includeReasoning = true);

// Cabecalhos X-Arestrix-* enviados ao gateway (memoria/agente/run), expostos para testes.
std::map<std::string, std::string> gatewayMetadataHeaders(const ChatRequest& req);

// Interpreta a resposta do provedor (exposto para testes). Lanca std::runtime_error se o formato for inesperado.
ChatReply parseResponseBody(const ProviderInfo& info, const std::string& body);

// Uma chamada ao modelo. Nunca lanca excecao: veja ok/errorCode/error.
ChatReply chatOnce(const ChatRequest& req);

} // namespace rt
} // namespace taskforge
