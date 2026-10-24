#include "rt/Trace.hpp"

#include "platform/Process.hpp"
#include "rt/Runtime.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

struct TraceState {
    std::mutex mutex;
    bool started = false;
    bool finished = false;
    std::string runId;
    long long startedAt = 0;
    long long updatedAt = 0;
    long long endedAt = 0;
    std::string status = "running";
    taskforge::Json tasks = taskforge::Json::array();
    taskforge::Json calls = taskforge::Json::array();
    taskforge::Json interactions = taskforge::Json::array();
    unsigned long long sequence = 0;
};

TraceState& state() {
    static TraceState instance;
    return instance;
}

thread_local std::vector<std::string> g_callStack;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string safeSegment(const std::string& value, const std::string& fallback) {
    std::string out;
    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') out += c;
        else out += '_';
        if (out.size() >= 96) break;
    }
    return out.empty() ? fallback : out;
}

void ensureStartedLocked(TraceState& s) {
    if (s.started) return;
    s.started = true;
    s.runId = safeSegment(platform::getEnv("ARESTRIX_RUN_ID"), "manual");
    s.startedAt = nowMs();
    s.updatedAt = s.startedAt;
}

void writeLocked(TraceState& s) {
    try {
        fs::path dir = workspaceRoot() / ".arestrix" / "runs" / s.runId;
        std::error_code ec;
        fs::create_directories(dir, ec);
        taskforge::Json root = taskforge::Json::object();
        root["version"] = 1;
        root["runId"] = s.runId;
        root["status"] = s.status;
        root["startedAt"] = s.startedAt;
        root["updatedAt"] = s.updatedAt;
        if (s.endedAt > 0) {
            root["endedAt"] = s.endedAt;
            root["durationMs"] = s.endedAt - s.startedAt;
        }
        root["tasks"] = s.tasks;
        root["calls"] = s.calls;
        root["interactions"] = s.interactions;
        std::ofstream out(dir / "trace.json", std::ios::binary | std::ios::trunc);
        if (out) out << root.dump();
    } catch (...) {
        // Trace e observabilidade best effort.
    }
}

std::string currentParentCallId() {
    return g_callStack.empty() ? std::string() : g_callStack.back();
}

} // namespace

void traceTaskStart(const std::string& nodeId, const std::string& label) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    const long long at = nowMs();
    s.tasks.push_back(taskforge::Json{
        {"id", ++s.sequence},
        {"nodeId", nodeId},
        {"label", label.empty() ? nodeId : label},
        {"status", "running"},
        {"startedAt", at},
        {"parentCallId", currentParentCallId()}
    });
    s.updatedAt = at;
    writeLocked(s);
}

void traceTaskEnd(const std::string& nodeId, const std::string& status) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    const long long at = nowMs();
    for (size_t i = s.tasks.size(); i > 0; --i) {
        auto& task = s.tasks[i - 1];
        if (task.value("nodeId", std::string()) != nodeId) continue;
        if (task.value("status", std::string()) != "running") continue;
        task["status"] = status;
        task["endedAt"] = at;
        const long long start = task.value("startedAt", at);
        task["durationMs"] = at - start;
        break;
    }
    s.updatedAt = at;
    writeLocked(s);
}

void traceCallStart(const std::string& callId, const std::string& callerId,
                    const std::string& targetId, const std::string& label,
                    const std::string& kind, int depth) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    const long long at = nowMs();
    const std::string parent = currentParentCallId();
    s.calls.push_back(taskforge::Json{
        {"id", ++s.sequence},
        {"callId", callId},
        {"callerId", callerId},
        {"targetId", targetId},
        {"label", label.empty() ? targetId : label},
        {"kind", kind},
        {"depth", depth},
        {"status", "running"},
        {"startedAt", at},
        {"parentCallId", parent}
    });
    g_callStack.push_back(callId);
    s.updatedAt = at;
    writeLocked(s);
}

void traceCallEnd(const std::string& callId, const std::string& status) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    const long long at = nowMs();
    for (size_t i = s.calls.size(); i > 0; --i) {
        auto& call = s.calls[i - 1];
        if (call.value("callId", std::string()) != callId) continue;
        if (call.value("status", std::string()) != "running") continue;
        call["status"] = status;
        call["endedAt"] = at;
        const long long start = call.value("startedAt", at);
        call["durationMs"] = at - start;
        break;
    }
    if (!g_callStack.empty() && g_callStack.back() == callId) g_callStack.pop_back();
    s.updatedAt = at;
    writeLocked(s);
}

void traceInteraction(const std::string& requestId, const std::string& nodeId,
                      const std::string& status) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    const long long at = nowMs();
    s.interactions.push_back(taskforge::Json{
        {"id", ++s.sequence},
        {"requestId", requestId},
        {"nodeId", nodeId},
        {"status", status},
        {"timestamp", at},
        {"parentCallId", currentParentCallId()}
    });
    s.updatedAt = at;
    writeLocked(s);
}

void finishTrace(bool success) {
    TraceState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    ensureStartedLocked(s);
    if (s.finished) return;
    s.finished = true;
    const long long at = nowMs();
    s.status = success ? "success" : "failed";
    s.endedAt = at;
    s.updatedAt = at;
    for (auto& task : s.tasks) {
        if (task.value("status", std::string()) == "running") {
            task["status"] = "aborted";
            task["endedAt"] = at;
            const long long start = task.value("startedAt", at);
            task["durationMs"] = at - start;
        }
    }
    for (auto& call : s.calls) {
        if (call.value("status", std::string()) == "running") {
            call["status"] = "aborted";
            call["endedAt"] = at;
            const long long start = call.value("startedAt", at);
            call["durationMs"] = at - start;
        }
    }
    writeLocked(s);
}

} // namespace rt
} // namespace taskforge
