#include "rt/CreditAuditExecutor.hpp"

#include "memory/MemoryService.hpp"
#include "sandbox/SandboxManager.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <vector>

namespace taskforge {
namespace rt {

namespace {

struct Arm {
    std::string id;
    std::string kind;
    std::vector<std::string> items;
};

Json parseArmOutput(const sandbox::ExecutionResult& result) {
    if (auto parsed = taskforge::jsonutil::tryParse(result.stdoutOutput);
        parsed && parsed->is_object()) {
        return *parsed;
    }
    std::string text = result.stdoutOutput;
    while (!text.empty()) {
        const size_t pos = text.find_last_of('\n');
        const std::string line = pos == std::string::npos
            ? text : text.substr(pos + 1);
        if (auto parsed = taskforge::jsonutil::tryParse(line); parsed && parsed->is_object()) {
            return *parsed;
        }
        if (pos == std::string::npos) break;
        text.erase(pos);
    }
    return Json::object();
}

std::string itemId(const std::string& prefix, const std::string& value) {
    std::string out = prefix;
    for (unsigned char c : value) {
        out.push_back(std::isalnum(c) || c == '-' || c == '_' || c == ':'
            ? static_cast<char>(c) : '_');
    }
    return out;
}

} // namespace

memory::CreditAuditExecutionResult runCreditAuditInContainers(
    const memory::CreditAuditExecutionRequest& request,
    const CreditAuditContainerExecutorOptions& options,
    CreditAuditSandboxRunFn run) {
    memory::CreditAuditExecutionResult out;
    std::string command = options.command.empty() ? request.containerCommand : options.command;
    if (command.empty()) {
        const char* env = std::getenv("ARESTRIX_AUDIT_ARM_COMMAND");
        if (env) command = env;
    }
    if (command.empty()) {
        out.error = "no_arm_command";
        return out;
    }
    if (request.projectId.empty() || request.traceId.empty()) {
        out.error = "invalid_request";
        return out;
    }

    std::vector<Arm> arms;
    arms.push_back({"baseline", "baseline", {}});
    arms.push_back({"no_memory", "no_memory", {}});
    std::set<std::string> delivered(request.delivered.begin(), request.delivered.end());
    for (const auto& item : delivered) {
        arms.push_back({itemId("ablate:", item), "ablate", {item}});
    }
    if (arms.size() <= 2) {
        out.error = "no_items";
        return out;
    }

    auto& memory = memory::MemoryService::getInstance();
    Json armIds = Json::array();
    for (const auto& arm : arms) armIds.push_back(arm.id);
    const Json prepared = memory.prepareCreditAuditWorkspaces(
        request.projectId, request.traceId, armIds);
    if (!prepared.value("ok", false)) {
        out.error = prepared.value("reason", std::string("workspace_error"));
        return out;
    }
    std::map<std::string, std::string> workspaces;
    for (const auto& entry : prepared["arms"]) {
        workspaces[entry.value("id", std::string())] =
            entry.value("workspace", std::string());
    }

    if (!run) {
        run = [](const sandbox::ExecutionParams& params) {
            return sandbox::SandboxManager::instance().execute(params);
        };
    }
    const int repetitions = std::max(1, request.repetitions);
    Json resultArms = Json::array();
    long long totalTokens = 0;
    for (const auto& arm : arms) {
        const auto ws = workspaces.find(arm.id);
        if (ws == workspaces.end()) {
            out.error = "workspace_missing";
            memory.cleanupCreditAuditWorkspaces(request.projectId, request.traceId);
            return out;
        }
        for (int repetition = 1; repetition <= repetitions; ++repetition) {
            Json items = Json::array();
            for (const auto& item : arm.items) items.push_back(item);
            Json payload{
                {"traceId", request.traceId},
                {"taskType", request.taskType},
                {"nodeId", request.nodeId},
                {"validator", request.validator},
                {"kind", arm.kind},
                {"items", items},
                {"delivered", request.delivered},
                {"cited", request.cited},
                {"inputHash", request.inputHash},
                {"repetition", repetition},
                {"containerPlan", request.containerPlan},
                {"snapshot", request.snapshot}};

            sandbox::ExecutionParams params;
            params.agentId = "memory-audit";
            params.command = command;
            params.useShell = true;
            params.workingDir = ws->second;
            params.policy.isolation = sandbox::IsolationType::Container;
            params.policy.network = sandbox::NetworkAccess::EgressLlmOnly;
            params.policy.allowFallbackToHost = false;
            params.policy.maxMemoryMB = request.containerPlan.value("memoryMB", 512);
            params.policy.timeoutSeconds = request.containerPlan.value("timeoutSeconds", 300);
            params.envVars["ARESTRIX_AUDIT_ARM_JSON"] = payload.dump();
            params.envVars["ARESTRIX_AUDIT_TRACE_ID"] = request.traceId;

            const auto result = run(params);
            const Json parsed = parseArmOutput(result);
            const bool infra = parsed.value("infra", false) || result.timedOut ||
                !result.errorMessage.empty();
            const bool ok = parsed.value("ok", result.exitCode == 0);
            const long long tokens = parsed.value("tokens", 0LL);
            totalTokens += tokens;
            resultArms.push_back(Json{
                {"kind", arm.kind},
                {"items", items},
                {"ok", ok},
                {"infra", infra},
                {"tokens", tokens},
                {"exitCode", result.exitCode},
                {"repetition", repetition}});
            if (infra) {
                // Uma falha de infraestrutura torna a auditoria inconclusiva.
                break;
            }
        }
    }
    memory.cleanupCreditAuditWorkspaces(request.projectId, request.traceId);
    out.ok = true;
    out.arms = resultArms;
    out.tokens = totalTokens;
    return out;
}

} // namespace rt
} // namespace taskforge
