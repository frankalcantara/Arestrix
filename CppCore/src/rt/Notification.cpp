#include "rt/Notification.hpp"

#include "platform/Process.hpp"
#include "rt/Runtime.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

std::mutex& notificationMutex() {
    static std::mutex mutex;
    return mutex;
}

bool notificationsDisabled() {
    const std::string value = platform::getEnv("ARESTRIX_NOTIFICATIONS");
    if (value.empty()) return false;
    std::string normalized;
    for (char c : value) normalized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return normalized == "0" || normalized == "false" || normalized == "off" || normalized == "no";
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

std::string truncate(std::string value, size_t maxBytes) {
    if (value.size() <= maxBytes) return value;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(value[cut]) & 0xC0) == 0x80) --cut;
    value.resize(cut);
    return value;
}

} // namespace

void notify(const std::string& nodeId, const std::string& kind, const std::string& severity,
            const std::string& title, const std::string& message,
            const taskforge::Json& metadata) {
    if (notificationsDisabled()) return;

    const std::string runId = safeSegment(platform::getEnv("ARESTRIX_RUN_ID"), "manual");
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    const std::string safeSeverity =
        (severity == "success" || severity == "warning" || severity == "error") ? severity : "info";
    taskforge::Json payload = taskforge::Json::object();
    payload["runId"] = runId;
    payload["nodeId"] = nodeId.empty() ? std::string("runtime") : nodeId;
    payload["kind"] = truncate(kind, 120);
    payload["severity"] = safeSeverity;
    payload["title"] = truncate(title, 160);
    payload["message"] = truncate(message, 1000);
    payload["timestamp"] = now;
    payload["metadata"] = metadata.is_object() ? metadata : taskforge::Json::object();

    try {
        std::lock_guard<std::mutex> lock(notificationMutex());
        fs::path dir = workspaceRoot() / ".arestrix" / "runs" / runId;
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::ofstream out(dir / "notifications.jsonl", std::ios::binary | std::ios::app);
        if (out) out << payload.dump() << "\n";
    } catch (...) {
        // Notificacao nunca derruba o pipeline.
    }

    try {
        sandbox::AgentTelemetryEmitter::emitEvent(payload["nodeId"].str(), "notification", payload);
    } catch (...) {
        // Telemetria e best effort.
    }
}

} // namespace rt
} // namespace taskforge
