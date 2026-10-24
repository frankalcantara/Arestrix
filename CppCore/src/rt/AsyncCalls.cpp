#include "rt/AsyncCalls.hpp"

#include "platform/Process.hpp"
#include "rt/Runtime.hpp"
#include "util/Json.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace taskforge {
namespace rt {

namespace {

struct AsyncEntry {
    std::string asyncId;
    std::string callerId;
    std::string callerRunId;
    std::string targetId;
    std::string label;
    std::string kind;

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    ToolResult result;
    std::future<ToolResult> future;
};

std::mutex& asyncMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<AsyncEntry>>& asyncEntries() {
    static std::map<std::string, std::shared_ptr<AsyncEntry>> entries;
    return entries;
}

bool disabled(const std::string& value) {
    return value == "0" || value == "false" || value == "off" || value == "no";
}

ToolResult errorResult(const std::string& message) {
    return ToolResult{message, true};
}

} // namespace

ToolResult invokeCallAsync(const std::string& callerId, const std::string& callerRunId,
                           const std::vector<CallTarget>& targets, const taskforge::Json& args,
                           RuntimeLang lang) {
    if (disabled(platform::getEnv("ARESTRIX_ASYNC_CALLS"))) {
        return errorResult(langPick(lang, "Chamadas assincronas estao desligadas.",
                                    "Asynchronous calls are disabled."));
    }
    if (!args.is_object()) return errorResult(langPick(lang, "Argumentos invalidos.", "Invalid arguments."));

    const std::string targetId = args.value("target", std::string());
    const CallTarget* target = nullptr;
    for (const auto& candidate : targets) {
        if (candidate.id == targetId) {
            target = &candidate;
            break;
        }
    }
    if (!target) {
        return errorResult(langPick(lang, "Alvo nao declarado no grafo: ", "Target not declared in the graph: ") + targetId);
    }
    if (!target->allowAsync) {
        return errorResult(langPick(lang, "A aresta nao permite chamada assincrona: ",
                                    "The call edge does not allow asynchronous calls: ") + targetId);
    }
    if (target->kind != "agent" && target->kind != "interaction" && target->kind != "subflow") {
        return errorResult(langPick(lang, "Destino de chamada nao suportado: ", "Call target not supported: ") + target->kind);
    }

    auto entry = std::make_shared<AsyncEntry>();
    entry->callerId = callerId;
    entry->callerRunId = callerRunId;
    entry->targetId = target->id;
    entry->label = target->label;
    entry->kind = target->kind;
    static std::atomic<unsigned long long> sequence{0};
    entry->asyncId = callerRunId + "/async#" + std::to_string(++sequence);

    const int parentDepth = currentCallDepth();
    try {
        entry->future = std::async(std::launch::async, [entry, callerId, callerRunId, targets, args, lang, parentDepth]() {
            ToolResult result;
            try {
                result = invokeCallAtDepth(callerId, callerRunId, targets, args, lang, parentDepth);
            } catch (const std::exception& ex) {
                result = ToolResult{taskforge::Json{
                    {"callId", entry->asyncId},
                    {"asyncId", entry->asyncId},
                    {"target", entry->targetId},
                    {"status", "error"},
                    {"error", ex.what()}
                }.dump(), true};
            } catch (...) {
                result = ToolResult{taskforge::Json{
                    {"callId", entry->asyncId},
                    {"asyncId", entry->asyncId},
                    {"target", entry->targetId},
                    {"status", "error"},
                    {"error", "unknown async call error"}
                }.dump(), true};
            }
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                entry->result = result;
                entry->done = true;
            }
            entry->cv.notify_all();
            return result;
        });
    } catch (const std::exception& ex) {
        return errorResult(langPick(lang, "Nao foi possivel iniciar a chamada assincrona: ",
                                    "Could not start asynchronous call: ") + ex.what());
    }

    {
        std::lock_guard<std::mutex> lock(asyncMutex());
        asyncEntries()[entry->asyncId] = entry;
    }

    taskforge::Json pending{
        {"callId", entry->asyncId},
        {"asyncId", entry->asyncId},
        {"target", entry->targetId},
        {"label", entry->label},
        {"kind", entry->kind},
        {"status", "pending"},
        {"async", true},
        {"depth", parentDepth + 1}
    };
    return ToolResult{pending.dump(), false};
}

ToolResult fetchAsyncCallResult(const std::string& callerId, const std::string& callerRunId,
                                const taskforge::Json& args, RuntimeLang lang) {
    if (!args.is_object()) return errorResult(langPick(lang, "Argumentos invalidos.", "Invalid arguments."));
    const std::string callId = args.value("callId", std::string());
    if (callId.empty()) {
        return errorResult(langPick(lang, "Faltou o callId da chamada assincrona.",
                                    "Missing the asynchronous callId."));
    }

    std::shared_ptr<AsyncEntry> entry;
    {
        std::lock_guard<std::mutex> lock(asyncMutex());
        auto it = asyncEntries().find(callId);
        if (it != asyncEntries().end()) entry = it->second;
    }
    if (!entry || entry->callerId != callerId || entry->callerRunId != callerRunId) {
        return errorResult(langPick(lang, "Chamada assincrona nao encontrada para este chamador: ",
                                    "Asynchronous call not found for this caller: ") + callId);
    }

    std::lock_guard<std::mutex> lock(entry->mutex);
    if (!entry->done) {
        taskforge::Json pending{
            {"callId", entry->asyncId},
            {"asyncId", entry->asyncId},
            {"target", entry->targetId},
            {"label", entry->label},
            {"kind", entry->kind},
            {"status", "pending"},
            {"async", true}
        };
        return ToolResult{pending.dump(), false};
    }

    ToolResult result = entry->result;
    auto parsed = jsonutil::tryParse(result.content);
    if (parsed && parsed->is_object()) {
        (*parsed)["asyncId"] = entry->asyncId;
        result.content = parsed->dump();
    }
    return result;
}

std::vector<ToolDef> asyncCallToolDefinitions(const std::vector<CallTarget>& targets, RuntimeLang lang) {
    bool hasAsync = false;
    for (const auto& target : targets) {
        if (target.allowAsync) {
            hasAsync = true;
            break;
        }
    }
    if (!hasAsync) return {};

    taskforge::Json callId = taskforge::Json::object();
    callId["type"] = "string";
    callId["description"] = langPick(lang, "callId devolvido por call_agent com async=true.",
                                     "callId returned by call_agent with async=true.");
    taskforge::Json properties = taskforge::Json::object();
    properties["callId"] = callId;
    taskforge::Json required = taskforge::Json::array();
    required.push_back("callId");
    taskforge::Json schema = taskforge::Json::object();
    schema["type"] = "object";
    schema["properties"] = properties;
    schema["required"] = required;

    return {ToolDef{
        "call_result",
        langPick(lang,
                 "Consulta o resultado de uma chamada assincrona. Enquanto estiver pendente, devolve status pending; "
                 "quando concluir, devolve o mesmo resultado JSON de call_agent.",
                 "Checks the result of an asynchronous call. While pending, returns status pending; when complete, "
                 "returns the same JSON result as call_agent."),
        schema
    }};
}

void drainAsyncCalls() {
    std::vector<std::shared_ptr<AsyncEntry>> entries;
    {
        std::lock_guard<std::mutex> lock(asyncMutex());
        for (const auto& pair : asyncEntries()) entries.push_back(pair.second);
    }
    for (const auto& entry : entries) {
        std::unique_lock<std::mutex> lock(entry->mutex);
        entry->cv.wait(lock, [&entry]() { return entry->done; });
    }
}

} // namespace rt
} // namespace taskforge
