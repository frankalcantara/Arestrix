#include "rt/Runtime.hpp"
#include "platform/Process.hpp"

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
#include "rt/McpTools.hpp"
#include "rt/ModelClient.hpp"
#include "rt/RuntimeLang.hpp"
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
    std::vector<std::string> mcpServers;   // ids em .workspace/mcps
    std::vector<std::string> mcpAllowed;   // nomes de ferramentas MCP permitidas (vazio = todas do servidor)
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
    if (j.contains("mcp") && j["mcp"].is_object()) {
        for (const char* key : {"servers", "allowedTools"}) {
            if (!j["mcp"].contains(key) || !j["mcp"][key].is_array()) continue;
            auto& dst = std::string(key) == "servers" ? s.mcpServers : s.mcpAllowed;
            for (const auto& v : j["mcp"][key]) if (v.is_string()) dst.push_back(v.get<std::string>());
        }
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

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string envOr(const std::string& name, const std::string& fallback) {
    std::string v = platform::getEnv(name);
    return v.empty() ? fallback : v;
}

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

    // Idioma do agente: definido pela propria constituicao dele (skill + prompt). O scaffolding do
    // runtime que o agente enxerga (contexto de sistema, prompt padrao, descricoes de ferramentas e
    // erros devolvidos a ele) segue esse idioma.
    RuntimeLang lang = detectRuntimeLang(std::vector<std::string>{skillText, spec.promptTemplate});

    std::ostringstream sys;
    if (!skillText.empty()) sys << skillText << "\n\n";
    sys << langPick(lang,
                    "---\nContexto de execucao (Arestrix): voce e o no \"",
                    "---\nExecution context (Arestrix): you are the node \"")
        << label
        << langPick(lang,
                    "\" de um pipeline de agentes. O diretorio de trabalho e a raiz do workspace; use SEMPRE caminhos "
                    "relativos. Voce so pode agir pelas ferramentas fornecidas. Quando terminar, responda com um resumo "
                    "curto do que foi feito, sem chamar mais ferramentas.\n",
                    "\" of an agent pipeline. The working directory is the workspace root; ALWAYS use relative paths. "
                    "You can only act through the tools provided. When you finish, reply with a short summary of what "
                    "was done, without calling more tools.\n");

    std::string userPrompt = spec.promptTemplate;
    if (userPrompt.empty()) {
        userPrompt = langPick(lang,
            "Execute a tarefa descrita nas suas instrucoes (skill). Esta e a execucao numero {{iteration}} deste no. "
            "Se houver relatorios, diagnosticos ou decisoes anteriores no workspace (por exemplo arquivos de auditoria), "
            "leia-os com a ferramenta read e corrija o que foi apontado antes de concluir.",
            "Carry out the task described in your instructions (skill). This is execution number {{iteration}} of this node. "
            "If there are earlier reports, diagnostics or decisions in the workspace (for example audit files), read them "
            "with the read tool and fix what was pointed out before concluding.");
    }
    userPrompt = replaceAll(userPrompt, "{{iteration}}", std::to_string(iteration));
    userPrompt = replaceAll(userPrompt, "{{nodeId}}", nodeId);
    userPrompt = replaceAll(userPrompt, "{{label}}", label);

    std::vector<ToolDef> tools = toolDefinitions(spec.tools, lang);
    // Ferramentas MCP (servidores de .workspace/mcps): so as dos servidores que o agente declarou.
    std::unique_ptr<McpToolbox> mcpTools;
    if (!spec.mcpServers.empty()) {
        try {
            mcpTools = std::make_unique<McpToolbox>(root, spec.mcpServers, spec.mcpAllowed, lang);
        } catch (const std::exception& ex) {
            throw std::runtime_error("Agente '" + label + "': " + ex.what());
        }
        for (const auto& d : mcpTools->definitions()) tools.push_back(d);
    }

    ChatRequest chat;
    chat.provider = spec.provider;
    chat.model = spec.model;
    chat.system = sys.str();
    chat.tools = tools;
    chat.maxTokens = spec.maxTokens;
    chat.hasTemperature = spec.hasTemperature;
    chat.temperature = spec.temperature;
    chat.httpAttempts = spec.httpAttempts;
    chat.telemetryId = nodeId;
    {
        ChatMessage first;
        first.role = "user";
        first.text = userPrompt;
        chat.messages.push_back(std::move(first));
    }

    ToolContext tctx;
    tctx.workspaceRoot = root;
    tctx.sandbox = spec.sandbox;
    tctx.agentId = nodeId;
    tctx.allowed = spec.tools;
    tctx.lang = lang;

    std::cout << "[Agent " << label << "] iniciando (execucao " << iteration << ", " << spec.provider << "/" << spec.model << ")\n";

    std::string finalText;
    bool finished = false;
    long long inTokens = 0, outTokens = 0;
    for (int turn = 1; turn <= spec.maxTurns; ++turn) {
        AgentTelemetryEmitter::emitStatus(nodeId, "thinking", "Turno " + std::to_string(turn));
        ChatReply t = chatOnce(chat);
        if (!t.ok) throw std::runtime_error(t.error);
        if (t.reasoningRejected) chat.includeReasoning = false;
        inTokens += t.inTokens;
        outTokens += t.outTokens;

        if (!t.reasoning.empty()) {
            AgentTelemetryEmitter::emitThinking(nodeId, t.reasoning.size(), t.reasoning.substr(0, 200));
        }
        if (!t.text.empty()) {
            finalText = t.text;
            AgentTelemetryEmitter::emitEvent(nodeId, "log", json{{"logLine", t.text.substr(0, 400)}});
        }
        ChatMessage reply;
        reply.role = "assistant";
        reply.text = t.text;
        reply.reasoning = t.reasoning;
        reply.toolCalls = t.toolCalls;
        chat.messages.push_back(std::move(reply));
        if (t.toolCalls.empty()) { finished = true; break; }

        for (auto& call : t.toolCalls) {
            AgentTelemetryEmitter::emitToolStart(nodeId, call.name, summarizeArgs(call.args));
            ToolResult res = (mcpTools && mcpTools->owns(call.name)) ? mcpTools->call(call.name, call.args)
                                                                       : executeTool(tctx, call.name, call.args);
            AgentTelemetryEmitter::emitToolEnd(nodeId, call.name, !res.isError);
            if (res.isError) {
                AgentTelemetryEmitter::emitEvent(nodeId, "log", json{{"logLine", "[" + call.name + "] " + res.content.substr(0, 300)}});
            }
            ChatMessage result;
            result.role = "tool";
            result.toolCallId = call.id;
            result.text = res.content;
            result.isError = res.isError;
            chat.messages.push_back(std::move(result));
        }
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

    std::cout << "[Agent " << label << "] concluido (tokens: entrada=" << inTokens
              << ", saida=" << outTokens << ")\n";
}

// ---------------------------------------------------------------------------
// Chamada unica ao modelo (sem ferramentas, sem sandbox): usada pelo Studio para tarefas curtas,
// como gerar o dicionario de sinonimos de um idioma.
// ---------------------------------------------------------------------------
CompletionResult completeText(const CompletionRequest& req) {
    CompletionResult out;
    ChatRequest chat;
    chat.provider = req.provider;
    chat.model = req.model;
    chat.system = req.system;
    chat.maxTokens = req.maxTokens;
    chat.httpAttempts = std::max(1, req.maxAttempts);
    chat.hasTemperature = req.hasTemperature;
    chat.temperature = req.temperature;
    chat.telemetryId = "studio:complete";
    ChatMessage user;
    user.role = "user";
    user.text = req.user;
    chat.messages.push_back(std::move(user));

    ChatReply t = chatOnce(chat);
    if (!t.ok) {
        out.error = t.error;
        return out;
    }
    out.text = t.text;
    out.ok = !out.text.empty();
    if (!out.ok) out.error = "resposta vazia do modelo";
    return out;
}

} // namespace rt
} // namespace taskforge
