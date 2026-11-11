#include "rt/SystemOne.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "rt/Http.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {

namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string systemOneUrl(const std::string& base) {
    const std::string clean = trimTrailingSlash(base);
    if (clean.size() >= 10 &&
        clean.compare(clean.size() - 10, 10, "/systemone") == 0) {
        return clean;
    }
    if (clean.size() >= 3 && clean.compare(clean.size() - 3, 3, "/v1") == 0) {
        return clean + "/systemone";
    }
    if (clean.size() >= 4 && clean.compare(clean.size() - 4, 4, "/api") == 0) {
        return clean + "/v1/systemone";
    }
    return clean + "/v1/systemone";
}

}  // namespace

SystemOneResult systemOneMemoryDecision(const std::string& model,
                                        const std::string& state,
                                        int maxSearches) {
    SystemOneResult out;
    const std::string key = envOr("JEV_API_KEY", "");
    if (key.empty()) {
        out.error = "jev_api_key_missing";
        return out;
    }
    const std::string base = envOr("JEV_BASE_URL", "https://api.typesafe.ai");
    const std::string url = systemOneUrl(base);
    const std::string chosenModel = model.empty() ? "jev-latest" : model;

    taskforge::Json body = taskforge::Json::object();
    body["model"] = chosenModel;
    body["state"] = state;
    body["questions"] = taskforge::Json{
        {"use_memory", taskforge::Json{
            {"type", "choice"},
            {"instructions", "How should the agent use project memory for this task?"},
            {"criteria", taskforge::Json{
                {"none", "Memory cannot help; the task is unrelated to project knowledge."},
                {"brief", "The existing brief is enough; the agent does not need memory tools."},
                {"search", "The brief is insufficient; the agent should query project memory."},
                {"learn", "The task explicitly asks to record a lesson and memory writing is enabled."}
            }}
        }}
    };

    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.timeoutSeconds = 60;
    request.headers["Authorization"] = "Bearer " + key;
    request.headers["Content-Type"] = "application/json";
    request.body = body.dump();

    const HttpResponse response = httpSend(request);
    if (response.status != 200) {
        std::string detail = response.body.substr(0, 280);
        out.error = "sysone_http_" + std::to_string(response.status) +
                    (detail.empty() ? "" : (": " + detail));
        return out;
    }
    const auto parsed = jsonutil::tryParse(response.body);
    if (!parsed || !parsed->is_object()) {
        out.error = "sysone_response_not_json";
        return out;
    }
    if (!parsed->contains("answers") || !(*parsed)["answers"].is_object() ||
        !(*parsed)["answers"].contains("use_memory") ||
        !(*parsed)["answers"]["use_memory"].is_object()) {
        out.error = "sysone_response_missing_use_memory";
        return out;
    }
    const taskforge::Json& answer = (*parsed)["answers"]["use_memory"];
    const std::string choice = answer.value("choice", std::string());
    if (choice != "none" && choice != "brief" && choice != "search" &&
        choice != "learn") {
        out.error = "sysone_response_invalid_choice";
        return out;
    }
    if (parsed->contains("usage") && (*parsed)["usage"].is_object()) {
        const taskforge::Json& usage = (*parsed)["usage"];
        out.inTokens = usage.value("input_tokens", 0LL);
        out.outTokens = usage.value("output_tokens", 0LL);
    }

    taskforge::Json decision = taskforge::Json::object();
    decision["useMemory"] = choice;
    decision["maxSearches"] = choice == "search" ? std::max(1, maxSearches) : 0;
    decision["query"] = "";
    decision["reason"] = "jev_choice";
    decision["confidence"] = answer.value("confidence", 0.0);
    out.text = decision.dump();
    out.ok = true;
    return out;
}

}  // namespace rt
}  // namespace taskforge
