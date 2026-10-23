#include "rt/CallGraph.hpp"
#include "rt/Runtime.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "util/Json.hpp"

#include <atomic>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace taskforge {
namespace rt {

namespace {

struct Callable {
    std::string kind = "agent";
    std::string label;
    std::string specJson = "{}";
};

struct Registry {
    std::mutex mutex;
    std::map<std::string, Callable> callables;
    std::map<std::string, std::vector<CallTarget>> callers;
};

Registry& registry() {
    static Registry instance;
    return instance;
}

int envLimit(const char* name, int fallback) {
    const char* value = std::getenv(name);
    if (!value || !*value) return fallback;
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

Json errorResult(const std::string& callId, const CallTarget& target, const std::string& message) {
    return Json{
        {"callId", callId},
        {"target", target.id},
        {"label", target.label},
        {"kind", target.kind},
        {"status", "error"},
        {"error", message}
    };
}

ToolResult invokeError(const std::string& message) {
    return ToolResult{message, true};
}

// Garante que a profundidade volte mesmo em excecao.
struct DepthGuard {
    int& depth;
    explicit DepthGuard(int& d) : depth(d) { ++depth; }
    ~DepthGuard() { --depth; }
};

} // namespace

void setCallRegistry(const std::string& callablesJson, const std::string& callersJson) {
    Json callables = callablesJson.empty() ? Json::object() : jsonutil::parse(callablesJson);
    Json callers = callersJson.empty() ? Json::object() : jsonutil::parse(callersJson);
    if (!callables.is_object() || !callers.is_object()) {
        throw std::runtime_error("Registro de chamadas invalido: esperados dois objetos JSON.");
    }

    std::map<std::string, Callable> parsedCallables;
    for (auto it = callables.begin(); it != callables.end(); ++it) {
        const Json& value = it.value();
        if (!value.is_object()) continue;
        Callable callable;
        callable.kind = value.value("kind", std::string("agent"));
        callable.label = value.value("label", it.key());
        if (value.contains("spec")) callable.specJson = value.at("spec").dump();
        parsedCallables[it.key()] = std::move(callable);
    }

    std::map<std::string, std::vector<CallTarget>> parsedCallers;
    for (auto it = callers.begin(); it != callers.end(); ++it) {
        if (!it.value().is_array()) continue;
        std::vector<CallTarget> targets;
        for (const auto& value : it.value()) {
            if (!value.is_object()) continue;
            CallTarget target;
            target.id = value.value("target", std::string());
            if (target.id.empty()) continue;
            target.label = value.value("label", target.id);
            target.kind = value.value("kind", std::string("agent"));
            target.maxCalls = value.value("maxCalls", 1);
            target.timeoutSeconds = value.value("timeoutSeconds", 0);
            target.onError = value.value("onError", std::string("fail"));
            target.description = value.value("description", std::string());
            targets.push_back(std::move(target));
        }
        parsedCallers[it.key()] = std::move(targets);
    }

    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.callables = std::move(parsedCallables);
    r.callers = std::move(parsedCallers);
}

std::vector<CallTarget> callTargetsFor(const std::string& callerId) {
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.callers.find(callerId);
    return it == r.callers.end() ? std::vector<CallTarget>() : it->second;
}

std::vector<ToolDef> callToolDefinitions(const std::vector<CallTarget>& targets, RuntimeLang lang) {
    if (targets.empty()) return {};

    Json allowed = Json::array();
    std::ostringstream description;
    description << langPick(lang,
        "Chama um dos agentes declarados no grafo. O alvo precisa estar na lista permitida. "
        "A chamada e sincrona: voce recebe a resposta do agente antes de continuar.",
        "Calls one of the agents declared in the graph. The target must be in the allowed list. "
        "The call is synchronous: you receive the agent's answer before continuing.");
    for (const auto& target : targets) {
        allowed.push_back(target.id);
        description << "\n- " << target.id;
        if (!target.label.empty()) description << " (" << target.label << ")";
        if (!target.description.empty()) description << ": " << target.description;
        description << " [max " << target.maxCalls << "]";
    }

    Json targetSchema = Json::object();
    targetSchema["type"] = "string";
    targetSchema["enum"] = allowed;
    targetSchema["description"] = langPick(lang, "Id do agente a chamar.", "Id of the agent to call.");

    Json inputSchema = Json::object();
    inputSchema["type"] = "string";
    inputSchema["description"] = langPick(lang,
        "Contexto/payload enviado ao agente chamado.",
        "Context/payload sent to the called agent.");

    Json properties = Json::object();
    properties["target"] = targetSchema;
    properties["input"] = inputSchema;

    Json required = Json::array();
    required.push_back("target");
    required.push_back("input");

    Json schema = Json::object();
    schema["type"] = "object";
    schema["properties"] = properties;
    schema["required"] = required;

    return {ToolDef{"call_agent", description.str(), schema}};
}

ToolResult invokeCall(const std::string& callerId, const std::string& callerRunId,
                      const std::vector<CallTarget>& targets, const taskforge::Json& args,
                      RuntimeLang lang) {
    if (!args.is_object()) return invokeError(langPick(lang, "Argumentos invalidos.", "Invalid arguments."));

    const std::string targetId = args.value("target", std::string());
    const std::string input = args.value("input", std::string());
    if (targetId.empty()) return invokeError(langPick(lang, "Faltou o alvo da chamada.", "Missing call target."));

    const CallTarget* target = nullptr;
    for (const auto& candidate : targets) {
        if (candidate.id == targetId) {
            target = &candidate;
            break;
        }
    }
    if (!target) {
        return invokeError(langPick(lang, "Alvo nao declarado no grafo: ", "Target not declared in the graph: ") + targetId);
    }
    if (target->kind != "agent") {
        return invokeError(langPick(lang, "Destino de chamada nao suportado no v1: ", "Call target not supported in v1: ") + target->kind);
    }

    static thread_local int depth = 0;
    const int maxDepth = envLimit("ARESTRIX_MAX_CALL_DEPTH", 3);
    if (maxDepth > 0 && depth + 1 > maxDepth) {
        return invokeError(langPick(lang, "Limite de profundidade de chamadas atingido.", "Call depth limit reached."));
    }

    static std::atomic<int> totalCalls{0};
    const int maxTotalCalls = envLimit("ARESTRIX_MAX_TOTAL_CALLS", 50);
    const int total = ++totalCalls;
    if (maxTotalCalls > 0 && total > maxTotalCalls) {
        return invokeError(langPick(lang, "Limite total de chamadas atingido.", "Total call limit reached."));
    }

    // Orcamento por aresta/execucao do chamador.
    static std::mutex countsMutex;
    static std::map<std::string, int> counts;
    const std::string countKey = callerRunId + "|" + targetId;
    int callNumber = 0;
    {
        std::lock_guard<std::mutex> lock(countsMutex);
        int& used = counts[countKey];
        if (target->maxCalls > 0 && used >= target->maxCalls) {
            return invokeError(langPick(lang, "Limite de chamadas atingido para ", "Call limit reached for ") +
                               targetId + " (" + std::to_string(target->maxCalls) + ").");
        }
        callNumber = ++used;
    }

    const std::string callId = callerRunId + "/" + targetId + "#" + std::to_string(callNumber);
    const std::string runKey = callerRunId + "/" + targetId;
    const int callDepth = depth + 1;

    Json start{
        {"callId", callId},
        {"target", targetId},
        {"label", target->label},
        {"depth", callDepth}
    };
    sandbox::AgentTelemetryEmitter::emitEvent(callerId, "call_start", start);

    std::string output;
    try {
        Callable callable;
        {
            Registry& r = registry();
            std::lock_guard<std::mutex> lock(r.mutex);
            auto it = r.callables.find(targetId);
            if (it == r.callables.end()) {
                throw std::runtime_error(langPick(lang, "Agente chamado nao registrado: ", "Called agent not registered: ") + targetId);
            }
            callable = it->second;
        }

        DepthGuard guard(depth);
        NodeScope scope(targetId, target->label.empty() ? targetId : target->label);
        output = runAgent(targetId, target->label.empty() ? targetId : target->label,
                          callable.specJson, runKey, "", input);
    } catch (const std::exception& ex) {
        const std::string message = ex.what();
        Json end{
            {"callId", callId},
            {"target", targetId},
            {"status", "error"},
            {"error", message},
            {"depth", callDepth}
        };
        sandbox::AgentTelemetryEmitter::emitEvent(callerId, "call_end", end);
        if (target->onError == "abort") throw;
        return ToolResult{errorResult(callId, *target, message).dump(), true};
    }

    Json result{
        {"callId", callId},
        {"target", targetId},
        {"label", target->label},
        {"kind", "agent"},
        {"status", "ok"},
        {"output", output},
        {"depth", callDepth}
    };
    sandbox::AgentTelemetryEmitter::emitEvent(callerId, "call_end", Json{
        {"callId", callId},
        {"target", targetId},
        {"status", "ok"},
        {"depth", callDepth}
    });
    return ToolResult{result.dump(), false};
}

} // namespace rt
} // namespace taskforge
