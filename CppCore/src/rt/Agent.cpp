#include "rt/Runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "rt/Http.hpp"
#include "rt/Tools.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {

namespace fs = std::filesystem;
using json = taskforge::Json;
using sandbox::AgentTelemetryEmitter;

// ---------------------------------------------------------------------------
// Utilitarios do runtime
// ---------------------------------------------------------------------------
void init() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
}

fs::path workspaceRoot() {
    std::error_code ec;
    return fs::current_path(ec);
}

void nodeEvent(const std::string& nodeId, const std::string& status, const std::string& detail) {
    AgentTelemetryEmitter::emitStatus(nodeId, status, detail);
}

NodeScope::NodeScope(std::string nodeId, std::string label)
    : nodeId_(std::move(nodeId)), label_(std::move(label)), uncaughtAtEntry_(std::uncaught_exceptions()) {
    nodeEvent(nodeId_, "running", label_);
}

NodeScope::~NodeScope() {
    bool failed = std::uncaught_exceptions() > uncaughtAtEntry_;
    nodeEvent(nodeId_, failed ? "error" : "settled", failed ? "Falhou: " + label_ : label_);
}

namespace {

// ---------------------------------------------------------------------------
// Especificacao do agente (vem do grafo, ja com heranca global resolvida)
// ---------------------------------------------------------------------------
struct AgentSpec {
    std::string nodeId;
    std::string label;
    std::string provider = "deepseek";
    std::string model;
    bool hasTemperature = false;
    double temperature = 0.0;
    int maxTokens = 8192;
    int maxTurns = 30;
    int maxRuns = 10;
    int httpAttempts = 6;   // tentativas por chamada HTTP (chamadas unicas usam menos)
    std::string skillPath;
    std::string promptTemplate;
    std::vector<std::string> tools;
    sandbox::SandboxPolicy sandbox;
};

sandbox::IsolationType parseIsolation(const std::string& s) {
    if (s == "container") return sandbox::IsolationType::Container;
    if (s == "microvm") return sandbox::IsolationType::MicroVM;
    return sandbox::IsolationType::HostDirect;
}

sandbox::NetworkAccess parseNetwork(const std::string& s) {
    if (s == "offline") return sandbox::NetworkAccess::Offline;
    if (s == "full") return sandbox::NetworkAccess::Full;
    return sandbox::NetworkAccess::EgressLlmOnly;
}

AgentSpec parseSpec(const json& j, const std::string& nodeId, const std::string& label) {
    AgentSpec s;
    s.nodeId = nodeId;
    s.label = label;
    s.provider = j.value("provider", "deepseek");
    s.model = j.value("model", "");
    if (j.contains("temperature") && j["temperature"].is_number()) {
        s.hasTemperature = true;
        s.temperature = j["temperature"].get<double>();
    }
    s.maxTokens = j.value("maxTokens", 8192);
    s.maxTurns = j.value("maxTurns", 30);
    s.maxRuns = j.value("maxRuns", 10);
    s.skillPath = j.value("skillPath", "");
    s.promptTemplate = j.value("promptTemplate", "");
    if (j.contains("tools") && j["tools"].is_array()) {
        for (const auto& t : j["tools"]) if (t.is_string()) s.tools.push_back(t.get<std::string>());
    }
    if (j.contains("sandbox") && j["sandbox"].is_object()) {
        const auto& sb = j["sandbox"];
        s.sandbox.isolation = parseIsolation(sb.value("isolationLevel", "host_direct"));
        s.sandbox.network = parseNetwork(sb.value("network", "egress_llm_only"));
        s.sandbox.maxMemoryMB = sb.value("maxMemoryMB", 512);
        s.sandbox.timeoutSeconds = sb.value("timeoutSeconds", 120);
        s.sandbox.allowFallbackToHost = sb.value("allowFallbackToHost", false);
        for (const char* key : {"readOnlyMounts", "readWriteMounts"}) {
            if (sb.contains(key) && sb[key].is_array()) {
                auto& dst = std::string(key) == "readOnlyMounts" ? s.sandbox.readOnlyMounts : s.sandbox.readWriteMounts;
                for (const auto& m : sb[key]) if (m.is_string()) dst.push_back(m.get<std::string>());
            }
        }
    }
    return s;
}

// Orcamento: evita lacos de feedback infinitos (cada execucao custa dinheiro).
int registerRun(const std::string& nodeId, int maxRuns) {
    static std::mutex mtx;
    static std::map<std::string, int> runs;
    std::lock_guard<std::mutex> lock(mtx);
    int n = ++runs[nodeId];
    const std::string env = std::getenv("ARESTRIX_MAX_AGENT_RUNS") ? std::getenv("ARESTRIX_MAX_AGENT_RUNS") : "";
    int limit = maxRuns;
    if (!env.empty()) { try { limit = std::stoi(env); } catch (...) {} }
    if (limit > 0 && n > limit) {
        throw std::runtime_error("Limite de execucoes do agente '" + nodeId + "' atingido (" + std::to_string(limit) +
                                 "). Possivel laco de feedback sem convergencia.");
    }
    return n;
}

std::string stripFrontmatter(const std::string& text) {
    if (text.rfind("---", 0) != 0) return text;
    size_t eol = text.find('\n');
    if (eol == std::string::npos) return text;
    size_t close = text.find("\n---", eol);
    if (close == std::string::npos) return text;
    size_t after = text.find('\n', close + 1);
    return after == std::string::npos ? std::string() : text.substr(after + 1);
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ---------------------------------------------------------------------------
// Provedores
// ---------------------------------------------------------------------------
struct ProviderInfo {
    std::string protocol;      // "openai" | "anthropic"
    std::string baseUrl;
    std::string path;
    std::string keyEnv;
    bool keyRequired = true;
};

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string envOr(const std::string& name, const std::string& fallback) {
    const char* v = std::getenv(name.c_str());
    return (v && *v) ? std::string(v) : fallback;
}

ProviderInfo resolveProvider(const std::string& provider) {
    ProviderInfo p;
    if (provider == "anthropic") {
        p = {"anthropic", "https://api.anthropic.com", "/v1/messages", "ANTHROPIC_API_KEY", true};
    } else if (provider == "openai") {
        p = {"openai", "https://api.openai.com/v1", "/chat/completions", "OPENAI_API_KEY", true};
    } else if (provider == "google") {
        p = {"openai", "https://generativelanguage.googleapis.com/v1beta/openai", "/chat/completions", "GEMINI_API_KEY", true};
    } else if (provider == "groq") {
        p = {"openai", "https://api.groq.com/openai/v1", "/chat/completions", "GROQ_API_KEY", true};
    } else if (provider == "jev") {
        p = {"openai", "", "/chat/completions", "JEV_API_KEY", false};
    } else if (provider == "local") {
        p = {"openai", "http://localhost:11434/v1", "/chat/completions", "LOCAL_LLM_API_KEY", false};
    } else {   // deepseek e qualquer outro compativel com OpenAI
        p = {"openai", "https://api.deepseek.com", "/chat/completions", "DEEPSEEK_API_KEY", true};
    }
    // Sobrescrita por ambiente: DEEPSEEK_BASE_URL, JEV_BASE_URL, LOCAL_LLM_BASE_URL...
    std::string baseVar = provider == "local" ? "LOCAL_LLM_BASE_URL" : upper(provider) + "_BASE_URL";
    p.baseUrl = envOr(baseVar, p.baseUrl);
    while (!p.baseUrl.empty() && p.baseUrl.back() == '/') p.baseUrl.pop_back();
    return p;
}

struct ToolCall {
    std::string id;
    std::string name;
    json args = json::object();
};

struct Turn {
    std::string text;
    std::string reasoning;
    std::vector<ToolCall> calls;
    std::string stopReason;
};

struct Usage {
    long long inTokens = 0;
    long long outTokens = 0;
};

// Chamada HTTP com retentativas: 429/5xx/529 respeitam Retry-After ou backoff exponencial.
HttpResponse postWithRetry(const std::string& nodeId, const HttpRequest& req, int maxAttempts = 6) {
    HttpResponse resp;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        resp = httpSend(req);
        bool retryable = resp.status == 0 || resp.status == 429 || resp.status == 500 ||
                         resp.status == 502 || resp.status == 503 || resp.status == 504 || resp.status == 529;
        if (!retryable || attempt == maxAttempts) break;

        int wait = std::min(60, 2 << attempt);   // 4, 8, 16, 32, 60
        auto ra = resp.headers.find("retry-after");
        if (ra != resp.headers.end()) {
            try { wait = std::max(1, std::min(120, std::stoi(ra->second))); } catch (...) {}
        }
        if (resp.status == 429) {
            AgentTelemetryEmitter::emitRateLimit(nodeId, wait, "HTTP 429 (limite de taxa/cota). Tentativa " + std::to_string(attempt));
        } else {
            AgentTelemetryEmitter::emitEvent(nodeId, "log",
                json{{"logLine", "Falha temporaria (" + (resp.status ? "HTTP " + std::to_string(resp.status) : resp.error) +
                                  "); nova tentativa em " + std::to_string(wait) + "s"}});
        }
        std::this_thread::sleep_for(std::chrono::seconds(wait));
    }
    return resp;
}

std::string errorDetail(const HttpResponse& r) {
    if (r.status == 0) return r.error;
    std::string body = r.body.substr(0, 600);
    return "HTTP " + std::to_string(r.status) + ": " + body;
}

class ChatBackend {
public:
    virtual ~ChatBackend() = default;
    virtual Turn complete() = 0;
    virtual void addToolResults(const std::vector<std::pair<ToolCall, ToolResult>>& results) = 0;
    Usage usage;
};

// --- Protocolo OpenAI (DeepSeek, OpenAI, Gemini, Groq, Jev, Ollama/llama.cpp...) -------------
class OpenAiBackend : public ChatBackend {
public:
    OpenAiBackend(const AgentSpec& spec, const ProviderInfo& info, std::string apiKey, const std::string& system,
                  const std::string& user, const std::vector<ToolDef>& tools)
        : spec_(spec), url_(info.baseUrl + info.path), apiKey_(std::move(apiKey)) {
        messages_ = json::array();
        messages_.push_back({{"role", "system"}, {"content", system}});
        messages_.push_back({{"role", "user"}, {"content", user}});
        tools_ = json::array();
        for (const auto& t : tools) {
            tools_.push_back({{"type", "function"},
                              {"function", {{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
        }
    }

    Turn complete() override {
        json body = {{"model", spec_.model}, {"messages", messages_}};
        if (!tools_.empty()) body["tools"] = tools_;
        if (spec_.hasTemperature) body["temperature"] = spec_.temperature;

        HttpRequest req;
        req.url = url_;
        req.headers["Content-Type"] = "application/json";
        if (!apiKey_.empty()) req.headers["Authorization"] = "Bearer " + apiKey_;
        req.body = body.dump();

        HttpResponse resp = postWithRetry(spec_.nodeId, req, spec_.httpAttempts);
        if (resp.status == 400 && resp.body.find("reasoning_content") != std::string::npos && !strippedReasoning_) {
            // Alguns servidores recusam reasoning_content no historico: reenvia sem ele.
            strippedReasoning_ = true;
            for (auto& m : messages_) if (m.is_object()) m.erase("reasoning_content");
            body["messages"] = messages_;
            req.body = body.dump();
            resp = postWithRetry(spec_.nodeId, req, spec_.httpAttempts);
        }
        if (resp.status != 200) throw std::runtime_error("Falha na API (" + spec_.provider + "): " + errorDetail(resp));

        json j = jsonutil::parse(resp.body);
        if (j.contains("usage") && j["usage"].is_object()) {
            usage.inTokens += j["usage"].value("prompt_tokens", 0);
            usage.outTokens += j["usage"].value("completion_tokens", 0);
        }
        if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) {
            throw std::runtime_error("Resposta inesperada da API (sem 'choices'): " + resp.body.substr(0, 400));
        }
        const json& choice = j["choices"][0];
        const json& msg = choice.at("message");

        Turn t;
        if (msg.contains("content") && msg["content"].is_string()) t.text = msg["content"].get<std::string>();
        if (msg.contains("reasoning_content") && msg["reasoning_content"].is_string()) t.reasoning = msg["reasoning_content"].get<std::string>();
        t.stopReason = choice.value("finish_reason", "");
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& c : msg["tool_calls"]) {
                ToolCall tc;
                tc.id = c.value("id", "");
                const json& fn = c.at("function");
                tc.name = fn.value("name", "");
                std::string argsText = fn.contains("arguments") && fn["arguments"].is_string() ? fn["arguments"].get<std::string>() : "{}";
                auto parsed = argsText.empty() ? std::optional<json>(json::object()) : jsonutil::tryParse(argsText);
                tc.args = parsed ? *parsed : json::object();
                if (!parsed) tc.args = json{{"__invalid_arguments", argsText}};
                t.calls.push_back(std::move(tc));
            }
        }
        messages_.push_back(msg);   // historico fiel (inclui tool_calls e reasoning_content)
        return t;
    }

    void addToolResults(const std::vector<std::pair<ToolCall, ToolResult>>& results) override {
        for (const auto& [call, res] : results) {
            messages_.push_back({{"role", "tool"}, {"tool_call_id", call.id}, {"content", res.content}});
        }
    }

private:
    AgentSpec spec_;
    std::string url_;
    std::string apiKey_;
    json messages_;
    json tools_;
    bool strippedReasoning_ = false;
};

// --- Protocolo Anthropic Messages --------------------------------------------------------------
class AnthropicBackend : public ChatBackend {
public:
    AnthropicBackend(const AgentSpec& spec, const ProviderInfo& info, std::string apiKey, const std::string& system,
                     const std::string& user, const std::vector<ToolDef>& tools)
        : spec_(spec), url_(info.baseUrl + info.path), apiKey_(std::move(apiKey)), system_(system) {
        messages_ = json::array();
        messages_.push_back({{"role", "user"}, {"content", user}});
        tools_ = json::array();
        for (const auto& t : tools) {
            tools_.push_back({{"name", t.name}, {"description", t.description}, {"input_schema", t.parameters}});
        }
    }

    Turn complete() override {
        json body = {{"model", spec_.model}, {"max_tokens", spec_.maxTokens}, {"system", system_}, {"messages", messages_}};
        if (!tools_.empty()) body["tools"] = tools_;
        if (spec_.hasTemperature) body["temperature"] = spec_.temperature;

        HttpRequest req;
        req.url = url_;
        req.headers["Content-Type"] = "application/json";
        req.headers["x-api-key"] = apiKey_;
        req.headers["anthropic-version"] = "2023-06-01";
        req.body = body.dump();

        HttpResponse resp = postWithRetry(spec_.nodeId, req, spec_.httpAttempts);
        if (resp.status != 200) throw std::runtime_error("Falha na API (anthropic): " + errorDetail(resp));

        json j = jsonutil::parse(resp.body);
        if (j.contains("usage") && j["usage"].is_object()) {
            usage.inTokens += j["usage"].value("input_tokens", 0);
            usage.outTokens += j["usage"].value("output_tokens", 0);
        }
        Turn t;
        t.stopReason = j.value("stop_reason", "");
        if (j.contains("content") && j["content"].is_array()) {
            for (const auto& block : j["content"]) {
                std::string type = block.value("type", "");
                if (type == "text") t.text += block.value("text", "");
                else if (type == "thinking") t.reasoning += block.value("thinking", "");
                else if (type == "tool_use") {
                    ToolCall tc;
                    tc.id = block.value("id", "");
                    tc.name = block.value("name", "");
                    tc.args = block.contains("input") ? block["input"] : json::object();
                    t.calls.push_back(std::move(tc));
                }
            }
        }
        messages_.push_back({{"role", "assistant"}, {"content", j.value("content", json::array())}});
        return t;
    }

    void addToolResults(const std::vector<std::pair<ToolCall, ToolResult>>& results) override {
        json blocks = json::array();
        for (const auto& [call, res] : results) {
            blocks.push_back({{"type", "tool_result"}, {"tool_use_id", call.id}, {"content", res.content}, {"is_error", res.isError}});
        }
        messages_.push_back({{"role", "user"}, {"content", blocks}});
    }

private:
    AgentSpec spec_;
    std::string url_;
    std::string apiKey_;
    std::string system_;
    json messages_;
    json tools_;
};

std::string summarizeArgs(const json& args) {
    std::string s = args.dump();
    if (s.size() > 160) s = s.substr(0, 157) + "...";
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// Execucao do no de agente
// ---------------------------------------------------------------------------
void runAgent(const std::string& nodeId, const std::string& label, const std::string& specJson,
              const std::string& runKey) {
    AgentSpec spec = parseSpec(jsonutil::parse(specJson), nodeId, label);
    int iteration = registerRun(runKey.empty() ? nodeId : runKey, spec.maxRuns);

    if (spec.model.empty()) throw std::runtime_error("Agente '" + label + "': modelo nao configurado.");

    ProviderInfo info = resolveProvider(spec.provider);
    if (info.baseUrl.empty()) {
        throw std::runtime_error("Provedor '" + spec.provider + "': defina a variavel " + upper(spec.provider) + "_BASE_URL.");
    }
    std::string apiKey = envOr(info.keyEnv, "");
    if (apiKey.empty() && info.keyRequired) {
        throw std::runtime_error("Chave de API ausente: defina a variavel de ambiente " + info.keyEnv +
                                 " (Padroes Globais > Chaves de API).");
    }

    fs::path root = workspaceRoot();

    // Skill = instrucoes de sistema do agente.
    std::string skillText;
    if (!spec.skillPath.empty()) {
        auto p = resolveInWorkspace(root, spec.skillPath);
        std::error_code ec;
        if (!p || !fs::is_regular_file(*p, ec)) {
            throw std::runtime_error("Skill nao encontrada no workspace: " + spec.skillPath);
        }
        skillText = stripFrontmatter(jsonutil::readFile(*p));
    }

    std::ostringstream sys;
    if (!skillText.empty()) sys << skillText << "\n\n";
    sys << "---\n"
        << "Contexto de execucao (Arestrix): voce e o no \"" << label << "\" de um pipeline de agentes. "
        << "O diretorio de trabalho e a raiz do workspace; use SEMPRE caminhos relativos. "
        << "Voce so pode agir pelas ferramentas fornecidas. Quando terminar, responda com um resumo curto "
        << "do que foi feito, sem chamar mais ferramentas.\n";

    std::string userPrompt = spec.promptTemplate;
    if (userPrompt.empty()) {
        userPrompt = "Execute a tarefa descrita nas suas instrucoes (skill). Esta e a execucao numero {{iteration}} deste no. "
                     "Se houver relatorios, diagnosticos ou decisoes anteriores no workspace (por exemplo arquivos de auditoria), "
                     "leia-os com a ferramenta read e corrija o que foi apontado antes de concluir.";
    }
    userPrompt = replaceAll(userPrompt, "{{iteration}}", std::to_string(iteration));
    userPrompt = replaceAll(userPrompt, "{{nodeId}}", nodeId);
    userPrompt = replaceAll(userPrompt, "{{label}}", label);

    std::vector<ToolDef> tools = toolDefinitions(spec.tools);

    std::unique_ptr<ChatBackend> backend;
    if (info.protocol == "anthropic") {
        backend = std::make_unique<AnthropicBackend>(spec, info, apiKey, sys.str(), userPrompt, tools);
    } else {
        backend = std::make_unique<OpenAiBackend>(spec, info, apiKey, sys.str(), userPrompt, tools);
    }

    ToolContext tctx;
    tctx.workspaceRoot = root;
    tctx.sandbox = spec.sandbox;
    tctx.agentId = nodeId;
    tctx.allowed = spec.tools;

    std::cout << "[Agent " << label << "] iniciando (execucao " << iteration << ", " << spec.provider << "/" << spec.model << ")\n";

    std::string finalText;
    bool finished = false;
    for (int turn = 1; turn <= spec.maxTurns; ++turn) {
        AgentTelemetryEmitter::emitStatus(nodeId, "thinking", "Turno " + std::to_string(turn));
        Turn t = backend->complete();

        if (!t.reasoning.empty()) {
            AgentTelemetryEmitter::emitThinking(nodeId, t.reasoning.size(), t.reasoning.substr(0, 200));
        }
        if (!t.text.empty()) {
            finalText = t.text;
            AgentTelemetryEmitter::emitEvent(nodeId, "log", json{{"logLine", t.text.substr(0, 400)}});
        }
        if (t.calls.empty()) { finished = true; break; }

        std::vector<std::pair<ToolCall, ToolResult>> results;
        for (auto& call : t.calls) {
            AgentTelemetryEmitter::emitToolStart(nodeId, call.name, summarizeArgs(call.args));
            ToolResult res = executeTool(tctx, call.name, call.args);
            AgentTelemetryEmitter::emitToolEnd(nodeId, call.name, !res.isError);
            if (res.isError) {
                AgentTelemetryEmitter::emitEvent(nodeId, "log", json{{"logLine", "[" + call.name + "] " + res.content.substr(0, 300)}});
            }
            results.emplace_back(call, std::move(res));
        }
        backend->addToolResults(results);
    }

    if (!finished) {
        throw std::runtime_error("Agente '" + label + "' excedeu o limite de " + std::to_string(spec.maxTurns) +
                                 " turnos sem concluir.");
    }

    // Registra a resposta final do agente para consulta/depuracao.
    std::error_code ec;
    fs::path outDir = root / ".arestrix" / "out";
    fs::create_directories(outDir, ec);
    std::ofstream(outDir / (nodeId + ".txt"), std::ios::binary | std::ios::trunc) << finalText;

    std::cout << "[Agent " << label << "] concluido (tokens: entrada=" << backend->usage.inTokens
              << ", saida=" << backend->usage.outTokens << ")\n";
}

// ---------------------------------------------------------------------------
// Chamada unica ao modelo (sem ferramentas, sem sandbox): usada pelo Studio para tarefas curtas,
// como gerar o dicionario de sinonimos de um idioma.
// ---------------------------------------------------------------------------
CompletionResult completeText(const CompletionRequest& req) {
    CompletionResult out;
    try {
        if (req.model.empty()) { out.error = "modelo nao configurado"; return out; }
        ProviderInfo info = resolveProvider(req.provider);
        if (info.baseUrl.empty()) {
            out.error = "defina a variavel " + upper(req.provider) + "_BASE_URL";
            return out;
        }
        std::string apiKey = envOr(info.keyEnv, "");
        if (apiKey.empty() && info.keyRequired) {
            out.error = "chave de API ausente: defina " + info.keyEnv;
            return out;
        }

        AgentSpec spec;
        spec.nodeId = "studio:complete";
        spec.label = "studio";
        spec.provider = req.provider;
        spec.model = req.model;
        spec.maxTokens = req.maxTokens;
        spec.httpAttempts = std::max(1, req.maxAttempts);
        if (req.hasTemperature) { spec.hasTemperature = true; spec.temperature = req.temperature; }

        std::unique_ptr<ChatBackend> backend;
        if (info.protocol == "anthropic") {
            backend = std::make_unique<AnthropicBackend>(spec, info, apiKey, req.system, req.user, std::vector<ToolDef>{});
        } else {
            backend = std::make_unique<OpenAiBackend>(spec, info, apiKey, req.system, req.user, std::vector<ToolDef>{});
        }
        Turn t = backend->complete();
        out.text = t.text;
        out.ok = !out.text.empty();
        if (!out.ok) out.error = "resposta vazia do modelo";
    } catch (const std::exception& ex) {
        out.ok = false;
        out.error = ex.what();
    }
    return out;
}

} // namespace rt
} // namespace taskforge
