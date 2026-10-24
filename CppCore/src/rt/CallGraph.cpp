#include "rt/CallGraph.hpp"
#include "rt/Interaction.hpp"
#include "rt/NodeInput.hpp"
#include "rt/Notification.hpp"
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
    std::map<std::string, std::function<std::string(const std::string&)>> subflows;
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

int& threadCallDepth() {
    static thread_local int depth = 0;
    return depth;
}

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
            target.notify = value.value("notify", true);
            targets.push_back(std::move(target));
        }
        parsedCallers[it.key()] = std::move(targets);
    }

    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.callables = std::move(parsedCallables);
    r.callers = std::move(parsedCallers);
    r.subflows.clear();
}

void registerSubflow(const std::string& id, std::function<std::string(const std::string&)> runner) {
    if (id.empty() || !runner) return;
    Registry& r = registry();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.subflows[id] = std::move(runner);
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
        "Chama um dos agentes, interacoes humanas ou subflows declarados no grafo. O alvo precisa estar na lista permitida. "
        "A chamada e sincrona: voce recebe a resposta do alvo antes de continuar. Voce pode pedir varias chamadas "
        "call_agent no mesmo turno; elas rodam em paralelo e os resultados voltam na ordem pedida.",
        "Calls one of the agents, human interactions or subflows declared in the graph. The target must be in the allowed list. "
        "The call is synchronous: you receive the target's answer before continuing. You may request several "
        "call_agent calls in the same turn; they run in parallel and results return in the requested order.");
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
    targetSchema["description"] = langPick(lang, "Id do alvo a chamar.", "Id of the target to call.");

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

ToolResult invokeCallImpl(const std::string& callerId, const std::string& callerRunId,
                          const std::vector<CallTarget>& targets, const taskforge::Json& args,
                          RuntimeLang lang, int startDepth) {
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
    if (target->kind != "agent" && target->kind != "interaction" && target->kind != "subflow") {
        return invokeError(langPick(lang, "Destino de chamada nao suportado: ", "Call target not supported: ") + target->kind);
    }

    int& depth = threadCallDepth();
    depth = startDepth;
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
                throw std::runtime_error(langPick(lang, "Alvo chamado nao registrado: ", "Called target not registered: ") + targetId);
            }
            callable = it->second;
        }

        DepthGuard guard(depth);
        NodeScope scope(targetId, target->label.empty() ? targetId : target->label);
        if (callable.kind == "interaction") {
            output = runInteraction(targetId, target->label.empty() ? targetId : target->label,
                                    callable.specJson, input);
        } else if (callable.kind == "subflow") {
            std::function<std::string(const std::string&)> runner;
            {
                Registry& r = registry();
                std::lock_guard<std::mutex> lock(r.mutex);
                auto it = r.subflows.find(targetId);
                if (it != r.subflows.end()) runner = it->second;
            }
            if (!runner) {
                throw std::runtime_error(langPick(lang, "Subflow chamado nao registrado: ", "Called subflow not registered: ") + targetId);
            }
            output = runner(input);
        } else {
            output = runAgent(targetId, target->label.empty() ? targetId : target->label,
                              callable.specJson, runKey, "", input);
        }
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
        if (target->notify) {
            notify(callerId, "call.failed", "error",
                   langPick(lang, "Chamada falhou", "Call failed"),
                   callerId + " -> " + (target->label.empty() ? target->id : target->label) + ": " + message,
                   Json{{"callId", callId}, {"target", targetId}, {"kind", target->kind},
                        {"depth", callDepth}, {"status", "error"}});
        }
        if (target->onError == "abort") throw;
        return ToolResult{errorResult(callId, *target, message).dump(), true};
    }

    Json result{
        {"callId", callId},
        {"target", targetId},
        {"label", target->label},
        {"kind", target->kind},
        {"status", "ok"},
        {"output", output},
        {"depth", callDepth}
    };
    if (target->kind == "interaction") {
        auto parsed = jsonutil::tryParse(output);
        if (parsed && parsed->is_object() && parsed->contains("answers")) {
            result["answers"] = (*parsed)["answers"];
        }
    }
    sandbox::AgentTelemetryEmitter::emitEvent(callerId, "call_end", Json{
        {"callId", callId},
        {"target", targetId},
        {"status", "ok"},
        {"depth", callDepth}
    });
    if (target->notify) {
        notify(callerId, "call.completed", "success",
               langPick(lang, "Chamada concluída", "Call completed"),
               callerId + " -> " + (target->label.empty() ? target->id : target->label) + " (" + target->kind + ")",
               Json{{"callId", callId}, {"target", targetId}, {"kind", target->kind},
                    {"depth", callDepth}, {"status", "ok"}});
    }
    return ToolResult{result.dump(), false};
}

ToolResult invokeCall(const std::string& callerId, const std::string& callerRunId,
                      const std::vector<CallTarget>& targets, const taskforge::Json& args,
                      RuntimeLang lang) {
    return invokeCallImpl(callerId, callerRunId, targets, args, lang, threadCallDepth());
}

ToolResult invokeCallAtDepth(const std::string& callerId, const std::string& callerRunId,
                             const std::vector<CallTarget>& targets, const taskforge::Json& args,
                             RuntimeLang lang, int parentDepth) {
    struct RestoreDepth {
        int& depth;
        int previous;
        ~RestoreDepth() { depth = previous; }
    } restore{threadCallDepth(), threadCallDepth()};
    return invokeCallImpl(callerId, callerRunId, targets, args, lang, parentDepth);
}

std::string runFlowCall(const std::string& nodeId, const std::string& label,
                        const std::string& sourceJson, const std::string& fixedInput) {
    const std::vector<CallTarget> targets = callTargetsFor(nodeId);
    if (targets.empty()) {
        throw std::runtime_error("Vertice Call '" + label + "' sem alvo chamavel declarado.");
    }
    if (targets.size() > 1) {
        throw std::runtime_error("Vertice Call '" + label + "' tem mais de um alvo; use uma unica aresta de chamada.");
    }

    std::string input = fixedInput;
    if (input.empty()) {
        auto source = jsonutil::tryParse(sourceJson);
        if (!source || !source->is_object()) source = Json{{"kind", "none"}};
        input = gatherNodeInput(*source, label);
    }

    static std::atomic<unsigned long long> flowCallSequence{0};
    const std::string runKey = nodeId + "@" + std::to_string(++flowCallSequence);
    const Json args{{"target", targets.front().id}, {"input", input}};
    const ToolResult result = invokeCall(nodeId, runKey, targets, args, RuntimeLang::Pt);
    if (result.isError) {
        throw std::runtime_error(result.content);
    }

    auto parsed = jsonutil::tryParse(result.content);
    std::string text = result.content;
    std::string kind = "agent";
    if (parsed && parsed->is_object()) {
        kind = parsed->value("kind", std::string("agent"));
        if (parsed->contains("output") && (*parsed)["output"].is_string()) {
            text = (*parsed)["output"].str();
        }
    }
    if (kind == "interaction") {
        auto inner = jsonutil::tryParse(text);
        if (inner && inner->is_object() && inner->contains("text") && (*inner)["text"].is_string()) {
            text = (*inner)["text"].str();
        }
    }
    writeNodeOutput(nodeId, text);
    return text;
}

int currentCallDepth() {
    return threadCallDepth();
}

} // namespace rt
} // namespace taskforge
