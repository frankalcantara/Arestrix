#pragma once

#include <string>
#include "util/Json.hpp"
#include <functional>
#include <chrono>
#include <mutex>
#include <iostream>
#include <regex>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace sandbox {

class AgentTelemetryEmitter {
public:
    using TelemetryCallback = std::function<void(const std::string& jsonString)>;

private:
    static std::mutex& getMutex() {
        static std::mutex mtx;
        return mtx;
    }

    static TelemetryCallback& getCallback() {
        static TelemetryCallback cb = nullptr;
        return cb;
    }

public:
    static void setCallback(TelemetryCallback cb) {
        std::lock_guard<std::mutex> lock(getMutex());
        getCallback() = cb;
    }

    static void emitEvent(const std::string& nodeId, const std::string& eventType, const taskforge::Json& payload) {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        taskforge::Json msg;
        msg["type"] = "agent:telemetry";
        msg["event"] = {
            {"nodeId", nodeId},
            {"type", eventType},
            {"timestamp", now},
            {"payload", payload}
        };

        std::string jsonStr = msg.dump();

        // Log sincronizado no terminal e notifica a UI via callback
        std::lock_guard<std::mutex> lock(getMutex());
        std::cout << "[NF_TELEMETRY:AGENT] " << jsonStr << std::endl;
        auto& cb = getCallback();
        if (cb) {
            cb(jsonStr);
        }
    }

    static void emitThinking(const std::string& nodeId, size_t totalChars, const std::string& preview) {
        taskforge::Json payload;
        payload["thinkingChars"] = totalChars;
        payload["thinkingPreview"] = preview;
        payload["status"] = "thinking";
        emitEvent(nodeId, "thinking", payload);
    }

    static void emitToolStart(const std::string& nodeId, const std::string& toolName, const std::string& argsSummary) {
        taskforge::Json payload;
        payload["toolName"] = toolName;
        payload["toolArgs"] = argsSummary;
        payload["status"] = "tool_call";
        emitEvent(nodeId, "tool_start", payload);
    }

    static void emitToolEnd(const std::string& nodeId, const std::string& toolName, bool success) {
        taskforge::Json payload;
        payload["toolName"] = toolName;
        payload["toolSuccess"] = success;
        payload["status"] = "running";
        emitEvent(nodeId, "tool_end", payload);
    }

    static void emitRateLimit(const std::string& nodeId, int retrySeconds, const std::string& detail = "") {
        taskforge::Json payload;
        payload["retrySeconds"] = retrySeconds;
        payload["detail"] = detail.empty() ? "Limite de quota da API temporariamente atingido." : detail;
        payload["status"] = "running";
        emitEvent(nodeId, "rate_limit", payload);
    }

    static void emitStatus(const std::string& nodeId, const std::string& status, const std::string& detail = "") {
        taskforge::Json payload;
        payload["status"] = status;
        payload["detail"] = detail;
        emitEvent(nodeId, "status", payload);
    }

    // Parser incremental de linhas de saída (JSONL do Pi ou stream do Agent)
    static void parseAndEmitStreamLine(const std::string& nodeId, const std::string& rawLine) {
        if (rawLine.empty()) return;

        // 1. Tenta parse como JSON estruturado (ex: Pi / codemode stream)
        try {
            if (rawLine.front() == '{' && rawLine.back() == '}') {
                auto j = taskforge::jsonutil::parse(rawLine);
                std::string type = j.value("type", "");

                if (type == "message_update") {
                    auto e = j.value("assistantMessageEvent", taskforge::Json::object());
                    if (e.value("type", "") == "thinking_delta") {
                        std::string delta = e.value("delta", "");
                        emitThinking(nodeId, delta.length(), delta);
                        return;
                    }
                } else if (type == "tool_execution_start") {
                    std::string tool = j.value("toolName", "");
                    std::string args = j.value("args", taskforge::Json::object()).dump();
                    emitToolStart(nodeId, tool, args);
                    return;
                } else if (type == "tool_execution_end") {
                    std::string tool = j.value("toolName", "");
                    bool isError = j.value("isError", false);
                    emitToolEnd(nodeId, tool, !isError);
                    return;
                } else if (type == "agent_settled") {
                    emitStatus(nodeId, "settled", "Agente estabilizado.");
                    return;
                }
            }
        } catch (...) {}

        // 2. Detecção de Rate Limit 429 via regex
        if (rawLine.find("429") != std::string::npos ||
            rawLine.find("RESOURCE_EXHAUSTED") != std::string::npos ||
            rawLine.find("Rate limit reached") != std::string::npos) {
            int retrySec = 15;
            std::regex retryRegex(R"((?:retry in|try again in|Retry-After)\s*[:\s]?([0-9.]+))", std::regex::icase);
            std::smatch m;
            if (std::regex_search(rawLine, m, retryRegex)) {
                try { retrySec = std::stoi(m[1].str()); } catch (...) {}
            }
            emitRateLimit(nodeId, retrySec, rawLine);
            return;
        }

        // 3. Emite como log padrão
        taskforge::Json payload;
        payload["logLine"] = rawLine;
        emitEvent(nodeId, "log", payload);
    }
};

} // namespace sandbox
} // namespace taskforge
