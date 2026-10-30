#include "rt/ModelClient.hpp"
#include "platform/Process.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <thread>

#include "rt/Http.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {

namespace {

using sandbox::AgentTelemetryEmitter;

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Cabecalhos nao podem carregar CR/LF nem valores arbitrariamente longos.
std::string headerSafe(std::string value, size_t maxLen = 200) {
    std::string out;
    out.reserve(std::min(value.size(), maxLen));
    for (unsigned char c : value) {
        if (c < 0x20 || c == 0x7f) continue;
        out.push_back(static_cast<char>(c));
        if (out.size() >= maxLen) break;
    }
    return out;
}

std::string envOr(const std::string& name, const std::string& fallback) {
    // Ambiente real do processo: os Padroes Globais gravam com a API do SO, que o getenv do CRT nao enxerga.
    std::string v = platform::getEnv(name);
    return v.empty() ? fallback : v;
}

// Chamada HTTP com retentativas: 429/5xx/529 respeitam Retry-After ou backoff exponencial.
HttpResponse postWithRetry(const std::string& telemetryId, const HttpRequest& req, int maxAttempts,
                           int* retries = nullptr) {
    HttpResponse resp;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        resp = httpSend(req);
        bool retryable = resp.status == 0 || resp.status == 429 || resp.status == 500 ||
                         resp.status == 502 || resp.status == 503 || resp.status == 504 || resp.status == 529;
        if (!retryable || attempt == maxAttempts) break;
        if (retries) ++(*retries);

        int wait = std::min(60, 2 << attempt);   // 4, 8, 16, 32, 60
        auto ra = resp.headers.find("retry-after");
        if (ra != resp.headers.end()) {
            try { wait = std::max(1, std::min(120, std::stoi(ra->second))); } catch (...) {}
        }
        if (!telemetryId.empty()) {
            if (resp.status == 429) {
                AgentTelemetryEmitter::emitRateLimit(telemetryId, wait, "HTTP 429 (limite de taxa/cota). Tentativa " + std::to_string(attempt));
            } else {
                AgentTelemetryEmitter::emitEvent(telemetryId, "log",
                    Json{{"logLine", "Falha temporaria (" + (resp.status ? "HTTP " + std::to_string(resp.status) : resp.error) +
                                     "); nova tentativa em " + std::to_string(wait) + "s"}});
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(wait));
    }
    return resp;
}

std::string errorDetail(const HttpResponse& r) {
    if (r.status == 0) return r.error;
    return "HTTP " + std::to_string(r.status) + ": " + r.body.substr(0, 600);
}

bool looksLikeToolsRejected(const HttpResponse& r) {
    if (r.status != 400 && r.status != 404 && r.status != 422) return false;
    std::string b = r.body;
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return b.find("tool") != std::string::npos &&
           (b.find("not support") != std::string::npos || b.find("unsupported") != std::string::npos ||
            b.find("does not support") != std::string::npos);
}

Json openAiMessages(const ChatRequest& req, bool includeReasoning) {
    Json msgs = Json::array();
    if (!req.system.empty()) msgs.push_back({{"role", "system"}, {"content", req.system}});
    for (const auto& m : req.messages) {
        if (m.role == "assistant") {
            Json a = Json::object();
            a["role"] = "assistant";
            a["content"] = m.text;
            if (includeReasoning && !m.reasoning.empty()) a["reasoning_content"] = m.reasoning;
            if (!m.toolCalls.empty()) {
                Json calls = Json::array();
                for (const auto& c : m.toolCalls) {
                    calls.push_back({{"id", c.id}, {"type", "function"},
                                     {"function", {{"name", c.name}, {"arguments", c.args.dump()}}}});
                }
                a["tool_calls"] = calls;
            }
            msgs.push_back(a);
        } else if (m.role == "tool") {
            msgs.push_back({{"role", "tool"}, {"tool_call_id", m.toolCallId}, {"content", m.text}});
        } else {
            msgs.push_back({{"role", "user"}, {"content", m.text}});
        }
    }
    return msgs;
}

// Anthropic: resultados de ferramenta viajam numa mensagem "user" com blocos tool_result; mensagens
// consecutivas do mesmo papel sao fundidas (a API exige alternancia).
Json anthropicMessages(const ChatRequest& req) {
    Json msgs = Json::array();
    std::string lastRole;
    auto append = [&](const std::string& role, Json blocks) {
        if (role == lastRole && msgs.size() > 0) {
            Json& prev = msgs[msgs.size() - 1]["content"];
            for (auto& b : blocks) prev.push_back(b);
        } else {
            msgs.push_back({{"role", role}, {"content", std::move(blocks)}});
            lastRole = role;
        }
    };
    for (const auto& m : req.messages) {
        if (m.role == "assistant") {
            Json blocks = Json::array();
            if (!m.text.empty()) blocks.push_back({{"type", "text"}, {"text", m.text}});
            for (const auto& c : m.toolCalls) {
                blocks.push_back({{"type", "tool_use"}, {"id", c.id}, {"name", c.name}, {"input", c.args}});
            }
            if (blocks.size() == 0) blocks.push_back({{"type", "text"}, {"text", "..."}});
            append("assistant", std::move(blocks));
        } else if (m.role == "tool") {
            append("user", Json::array({Json{{"type", "tool_result"}, {"tool_use_id", m.toolCallId},
                                             {"content", m.text}, {"is_error", m.isError}}}));
        } else {
            append("user", Json::array({Json{{"type", "text"}, {"text", m.text}}}));
        }
    }
    return msgs;
}

} // namespace

const std::vector<std::string>& knownProviders() {
    static const std::vector<std::string> list = {"deepseek", "anthropic", "openai", "google", "groq", "mistral", "jev", "local"};
    return list;
}

std::string normalizeLocalBaseUrl(std::string baseUrl) {
    auto trim = [](std::string& s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) { s.clear(); return; }
        size_t e = s.find_last_not_of(" \t\r\n");
        s = s.substr(b, e - b + 1);
    };
    trim(baseUrl);
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    if (baseUrl.empty()) return baseUrl;

    // O usuario pode colar a URL de um endpoint visto na documentacao do servidor.
    for (const char* suffix : {"/chat/completions", "/models"}) {
        size_t n = std::strlen(suffix);
        if (baseUrl.size() > n && baseUrl.compare(baseUrl.size() - n, n, suffix) == 0) {
            baseUrl.erase(baseUrl.size() - n);
            while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
            break;
        }
    }
    if (baseUrl.empty()) return baseUrl;

    // Ollama, llama.cpp, vLLM e LM Studio expoem a API OpenAI em /v1. Se o usuario informou so
    // host:porta (sem caminho), completamos; caminhos customizados sao preservados.
    size_t scheme = baseUrl.find("://");
    size_t pathStart = scheme == std::string::npos ? std::string::npos : baseUrl.find('/', scheme + 3);
    if (pathStart == std::string::npos) baseUrl += "/v1";
    return baseUrl;
}

ProviderInfo resolveProvider(const std::string& provider) {
    ProviderInfo p;
    if (provider == "anthropic") {
        p = {"anthropic", "https://api.anthropic.com", "/v1/messages", "ANTHROPIC_API_KEY", true, true};
    } else if (provider == "openai") {
        p = {"openai", "https://api.openai.com/v1", "/chat/completions", "OPENAI_API_KEY", true, true};
    } else if (provider == "google") {
        p = {"openai", "https://generativelanguage.googleapis.com/v1beta/openai", "/chat/completions", "GEMINI_API_KEY", true, true};
    } else if (provider == "groq") {
        p = {"openai", "https://api.groq.com/openai/v1", "/chat/completions", "GROQ_API_KEY", true, true};
    } else if (provider == "mistral") {
        p = {"openai", "https://api.mistral.ai/v1", "/chat/completions", "MISTRAL_API_KEY", true, true};
    } else if (provider == "jev") {
        // Modelos System One do Jev devolvem julgamentos tipados, sem chamada de ferramentas.
        p = {"openai", "", "/chat/completions", "JEV_API_KEY", false, false};
    } else if (provider == "local") {
        // Servidor local (Ollama, llama.cpp, vLLM, LM Studio...). A chamada de ferramentas depende do
        // modelo carregado; quando o servidor recusa tools, o erro tools_unsupported e devolvido.
        p = {"openai", "http://localhost:11434/v1", "/chat/completions", "LOCAL_LLM_API_KEY", false, true};
    } else {   // deepseek e qualquer outro compativel com OpenAI
        p = {"openai", "https://api.deepseek.com", "/chat/completions", "DEEPSEEK_API_KEY", true, true};
    }
    // Sobrescrita por ambiente: DEEPSEEK_BASE_URL, JEV_BASE_URL, LOCAL_LLM_BASE_URL...
    std::string baseVar = provider == "local" ? "LOCAL_LLM_BASE_URL" : upper(provider) + "_BASE_URL";
    p.baseUrl = envOr(baseVar, p.baseUrl);
    if (provider == "local") {
        p.baseUrl = normalizeLocalBaseUrl(p.baseUrl);
    } else {
        while (!p.baseUrl.empty() && p.baseUrl.back() == '/') p.baseUrl.pop_back();
    }

    // Provedores hospedados podem ser obrigados a passar pelo AI Gateway. Modelos locais
    // (local e jev) nunca passam pelo gateway.
    const bool localLike = provider == "local" || provider == "jev";
    std::string gatewayUrl = envOr("ARESTRIX_AI_GATEWAY_URL", "");
    while (!gatewayUrl.empty() && gatewayUrl.back() == '/') gatewayUrl.pop_back();
    const std::string requireValue = lower(envOr("ARESTRIX_REQUIRE_AI_GATEWAY", ""));
    const std::string policyValue = lower(envOr("ARESTRIX_AI_GATEWAY_POLICY", ""));
    const bool policyLocked = policyValue == "locked" || policyValue == "enforced";
    const bool requireGateway = policyLocked || requireValue == "1" ||
                                requireValue == "true" || requireValue == "yes";
    if (!localLike && !gatewayUrl.empty()) {
        p.baseUrl = gatewayUrl + "/" + provider;
        p.keyEnv = "ARESTRIX_AI_GATEWAY_KEY";
        p.gateway = true;
    } else if (!localLike && requireGateway) {
        p.baseUrl.clear();
        p.configError = "gateway_required";
    }
    return p;
}

Json ChatMessage::toJson() const {
    Json j = Json::object();
    j["role"] = role;
    j["text"] = text;
    if (!reasoning.empty()) j["reasoning"] = reasoning;
    if (!toolCalls.empty()) {
        Json calls = Json::array();
        for (const auto& c : toolCalls) calls.push_back({{"id", c.id}, {"name", c.name}, {"args", c.args}});
        j["toolCalls"] = calls;
    }
    if (!toolCallId.empty()) j["toolCallId"] = toolCallId;
    if (isError) j["isError"] = true;
    return j;
}

ChatMessage ChatMessage::fromJson(const Json& j) {
    ChatMessage m;
    m.role = j.value("role", std::string("user"));
    m.text = j.value("text", std::string());
    m.reasoning = j.value("reasoning", std::string());
    m.toolCallId = j.value("toolCallId", std::string());
    m.isError = j.value("isError", false);
    if (j.contains("toolCalls") && j.at("toolCalls").is_array()) {
        for (const auto& c : j.at("toolCalls")) {
            ChatToolCall tc;
            tc.id = c.value("id", std::string());
            tc.name = c.value("name", std::string());
            tc.args = c.contains("args") ? c.at("args") : Json::object();
            m.toolCalls.push_back(std::move(tc));
        }
    }
    return m;
}

Json buildRequestBody(const ProviderInfo& info, const ChatRequest& req, bool includeReasoning) {
    Json body = Json::object();
    body["model"] = req.model;
    if (info.protocol == "anthropic") {
        body["max_tokens"] = req.maxTokens;
        if (!req.system.empty()) body["system"] = req.system;
        body["messages"] = anthropicMessages(req);
        if (!req.tools.empty()) {
            Json tools = Json::array();
            for (const auto& t : req.tools) {
                tools.push_back({{"name", t.name}, {"description", t.description}, {"input_schema", t.parameters}});
            }
            body["tools"] = tools;
        }
    } else {
        body["messages"] = openAiMessages(req, includeReasoning);
        if (!req.tools.empty()) {
            Json tools = Json::array();
            for (const auto& t : req.tools) {
                tools.push_back({{"type", "function"},
                                 {"function", {{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
            }
            body["tools"] = tools;
        }
    }
    if (req.hasTemperature) body["temperature"] = req.temperature;
    return body;
}

ChatReply parseResponseBody(const ProviderInfo& info, const std::string& body) {
    Json j = jsonutil::parse(body);
    ChatReply r;
    if (info.protocol == "anthropic") {
        if (j.contains("usage") && j["usage"].is_object()) {
            r.inTokens = j["usage"].value("input_tokens", 0LL);
            r.outTokens = j["usage"].value("output_tokens", 0LL);
        }
        r.stopReason = j.value("stop_reason", std::string());
        if (!j.contains("content") || !j["content"].is_array()) {
            throw std::runtime_error("Resposta inesperada da API (sem 'content'): " + body.substr(0, 400));
        }
        for (const auto& block : j["content"]) {
            std::string type = block.value("type", std::string());
            if (type == "text") r.text += block.value("text", std::string());
            else if (type == "thinking") r.reasoning += block.value("thinking", std::string());
            else if (type == "tool_use") {
                ChatToolCall tc;
                tc.id = block.value("id", std::string());
                tc.name = block.value("name", std::string());
                tc.args = block.contains("input") ? block.at("input") : Json::object();
                r.toolCalls.push_back(std::move(tc));
            }
        }
    } else {
        if (j.contains("usage") && j["usage"].is_object()) {
            r.inTokens = j["usage"].value("prompt_tokens", 0LL);
            r.outTokens = j["usage"].value("completion_tokens", 0LL);
        }
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
            throw std::runtime_error("Resposta inesperada da API (sem 'choices'): " + body.substr(0, 400));
        }
        const Json& choice = j["choices"][0];
        const Json& msg = choice.at("message");
        if (msg.contains("content") && msg.at("content").is_string()) r.text = msg.at("content").get<std::string>();
        if (msg.contains("reasoning_content") && msg.at("reasoning_content").is_string()) {
            r.reasoning = msg.at("reasoning_content").get<std::string>();
        }
        r.stopReason = choice.value("finish_reason", std::string());
        if (msg.contains("tool_calls") && msg.at("tool_calls").is_array()) {
            for (const auto& c : msg.at("tool_calls")) {
                ChatToolCall tc;
                tc.id = c.value("id", std::string());
                const Json& fn = c.at("function");
                tc.name = fn.value("name", std::string());
                std::string argsText = fn.contains("arguments") && fn.at("arguments").is_string()
                                           ? fn.at("arguments").get<std::string>() : "{}";
                auto parsed = argsText.empty() ? std::optional<Json>(Json::object()) : jsonutil::tryParse(argsText);
                tc.args = parsed ? *parsed : Json{{"__invalid_arguments", argsText}};
                r.toolCalls.push_back(std::move(tc));
            }
        }
    }
    r.ok = true;
    return r;
}

std::map<std::string, std::string> gatewayMetadataHeaders(const ChatRequest& req) {
    std::map<std::string, std::string> headers;
    if (!req.agentId.empty()) headers["X-Arestrix-Agent"] = headerSafe(req.agentId);
    if (!req.runId.empty()) headers["X-Arestrix-Run-Id"] = headerSafe(req.runId);
    if (!req.memoryScope.empty()) headers["X-Arestrix-Memory-Scope"] = headerSafe(req.memoryScope);
    headers["X-Arestrix-Memory-Enabled"] = req.memoryEnabled ? "1" : "0";
    headers["X-Arestrix-Memory-Context-Chars"] = std::to_string(req.memoryContextChars);
    return headers;
}

ChatReply chatOnce(const ChatRequest& req) {
    ChatReply out;
    if (req.model.empty()) {
        out.errorCode = "model_not_configured";
        out.error = "modelo nao configurado";
        return out;
    }
    ProviderInfo info = resolveProvider(req.provider);
    if (!info.configError.empty()) {
        out.errorCode = info.configError;
        out.error = "AI Gateway obrigatorio: defina ARESTRIX_AI_GATEWAY_URL e "
                    "ARESTRIX_AI_GATEWAY_KEY (ou desative ARESTRIX_REQUIRE_AI_GATEWAY).";
        return out;
    }
    if (info.baseUrl.empty()) {
        out.errorCode = "base_url_missing";
        out.error = "defina a variavel " + upper(req.provider) + "_BASE_URL ou ARESTRIX_AI_GATEWAY_URL";
        return out;
    }
    std::string apiKey = envOr(info.keyEnv, "");
    if (apiKey.empty() && info.keyRequired) {
        out.errorCode = "api_key_missing";
        out.error = "defina a variavel de ambiente " + info.keyEnv;
        return out;
    }

    try {
        HttpRequest http;
        http.url = info.baseUrl + info.path;
        http.headers["Content-Type"] = "application/json";
        if (info.gateway) {
            // O gateway aceita a chave virtual via Bearer, inclusive na rota Anthropic.
            http.headers["Authorization"] = "Bearer " + apiKey;
            if (info.protocol == "anthropic") {
                http.headers["anthropic-version"] = "2023-06-01";
            }
            // Metadados de memoria: o gateway agrega o impacto sem precisar interpretar o prompt.
            for (const auto& [name, value] : gatewayMetadataHeaders(req)) http.headers[name] = value;
        } else if (info.protocol == "anthropic") {
            http.headers["x-api-key"] = apiKey;
            http.headers["anthropic-version"] = "2023-06-01";
        } else if (!apiKey.empty()) {
            http.headers["Authorization"] = "Bearer " + apiKey;
        }
        http.body = buildRequestBody(info, req, req.includeReasoning).dump();
        int attempts = std::max(1, req.httpAttempts);
        // Servidor local desligado/inalcancavel: falhar rapido em vez de esperar o backoff das APIs remotas.
        if (req.provider == "local") attempts = std::min(attempts, 2);
        int retries = 0;
        HttpResponse resp = postWithRetry(req.telemetryId, http, attempts, &retries);
        bool reasoningRejected = false;
        if (info.protocol == "openai" && req.includeReasoning && resp.status == 400 &&
            resp.body.find("reasoning_content") != std::string::npos) {
            // Alguns servidores recusam reasoning_content no historico: reenvia sem ele.
            reasoningRejected = true;
            http.body = buildRequestBody(info, req, false).dump();
            resp = postWithRetry(req.telemetryId, http, attempts, &retries);
        }
        out.retries = retries;
        out.httpStatus = resp.status;
        if (resp.status != 200) {
            out.errorCode = looksLikeToolsRejected(resp) ? "tools_unsupported"
                          : resp.status == 402 ? "budget_exceeded"
                          : resp.status == 429 ? "rate_limited" : "http_error";
            out.error = "Falha na API (" + req.provider + "): " + errorDetail(resp);
            return out;
        }
        ChatReply reply = parseResponseBody(info, resp.body);
        reply.reasoningRejected = reasoningRejected;
        reply.retries = retries;
        return reply;
    } catch (const std::exception& ex) {
        out.ok = false;
        out.errorCode = "bad_response";
        out.error = ex.what();
    }
    return out;
}

} // namespace rt
} // namespace taskforge
