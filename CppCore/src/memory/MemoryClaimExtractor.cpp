#include "MemoryClaimExtractor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "MemoryPredicates.hpp"
#include "project/RunnerSecurity.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string stringField(const taskforge::Json& object, const char* key,
                        const std::string& fallback = "") {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const auto& value = object[key];
    if (value.is_string()) return value.str();
    if (value.is_number() || value.is_boolean()) return value.dump();
    return fallback;
}

std::string objectText(const taskforge::Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) return "";
    const auto& value = object[key];
    if (value.is_null()) return "";
    if (value.is_string()) return value.str();
    return value.dump();
}

bool dateParsable(const std::string& value) {
    if (value.empty()) return true;
    static const std::regex date(
        R"(^\d{4}-\d{2}-\d{2}(?:[T ]\d{2}:\d{2}(?::\d{2}(?:\.\d+)?)?(?:Z|[+-]\d{2}:?\d{2})?)?$)");
    return std::regex_match(value, date);
}

bool unitParsable(const std::string& value) {
    if (value.empty()) return true;
    if (value.size() > 40) return false;
    for (unsigned char c : value) {
        if (c == '\n' || c == '\r' || c == '\0') return false;
    }
    return true;
}

std::string stripFence(std::string value) {
    value = trim(std::move(value));
    if (value.rfind("```", 0) == 0) {
        const size_t firstLine = value.find('\n');
        if (firstLine != std::string::npos) value.erase(0, firstLine + 1);
        const size_t lastFence = value.rfind("```");
        if (lastFence != std::string::npos) value.erase(lastFence);
    }
    return trim(std::move(value));
}

} // namespace

std::string ClaimExtractorConfig::fingerprint() const {
    return project::RunnerSecurity::sha256(
        "claim|" + model + "|" + revision + "|" + promptVersion + "|" + schemaVersion +
        "|" + std::to_string(maxItems));
}

ClaimExtractorConfig MemoryClaimExtractor::configFromJson(const taskforge::Json& memory) {
    ClaimExtractorConfig config;
    if (!memory.is_object()) return config;
    const auto& switchValue = memory.contains("claimExtraction")
        ? memory["claimExtraction"] : taskforge::Json();
    if (switchValue.is_boolean()) {
        config.enabled = switchValue.get<bool>();
    } else if (switchValue.is_object()) {
        config.enabled = switchValue.value("enabled", false);
    }
    const taskforge::Json extractor = memory.contains("extractor") && memory["extractor"].is_object()
        ? memory["extractor"] : taskforge::Json::object();
    if (extractor.is_object()) {
        config.baseUrl = extractor.value("baseUrl", config.baseUrl);
        config.model = extractor.value("model", config.model);
        config.revision = extractor.value("revision", config.revision);
        config.promptVersion = extractor.value("promptVersion", config.promptVersion);
        config.schemaVersion = extractor.value("schemaVersion", config.schemaVersion);
        config.apiKeyEnv = extractor.value("apiKeyEnv", config.apiKeyEnv);
        config.maxItems = std::max(1, std::min(20, extractor.value("maxItems", config.maxItems)));
        config.timeoutSeconds = std::max(1, std::min(600,
            extractor.value("timeoutSeconds", config.timeoutSeconds)));
    }
    return config;
}

std::vector<ClaimDraft> MemoryClaimExtractor::parseResponse(
    const std::string& content, const std::string& passageText,
    const ClaimExtractorConfig& config, std::string& error,
    taskforge::Json* diagnostics) {
    error.clear();
    std::vector<ClaimDraft> claims;
    taskforge::Json diag{{"received", 0}, {"accepted", 0}, {"rejected", 0},
                         {"reasons", taskforge::Json::array()}};
    if (diagnostics) *diagnostics = diag;

    const std::string cleaned = stripFence(content);
    auto parsed = taskforge::jsonutil::tryParse(cleaned);
    if (!parsed) {
        error = "resposta do extrator nao e JSON";
        diag["error"] = error;
        if (diagnostics) *diagnostics = diag;
        return claims;
    }
    taskforge::Json root = *parsed;
    taskforge::Json items = taskforge::Json::array();
    if (root.is_array()) items = root;
    else if (root.is_object() && root.contains("claims") && root["claims"].is_array()) {
        items = root["claims"];
    } else {
        error = "resposta do extrator sem claims[]";
        diag["error"] = error;
        if (diagnostics) *diagnostics = diag;
        return claims;
    }
    diag["received"] = static_cast<long long>(items.size());

    auto reject = [&](const std::string& reason) {
        diag["rejected"] = diag.value("rejected", 0LL) + 1;
        if (diag["reasons"].size() < 20) diag["reasons"].push_back(reason);
    };

    for (const auto& item : items) {
        if (static_cast<int>(claims.size()) >= config.maxItems) {
            reject("maxItems excedido");
            break;
        }
        if (!item.is_object()) {
            reject("claim nao e objeto");
            continue;
        }
        ClaimDraft claim;
        claim.subject = stringField(item, "subject");
        claim.subjectType = stringField(item, "subject_type",
                                        stringField(item, "subjectType", "entity"));
        claim.predicate = stringField(item, "predicate");
        claim.object = objectText(item, "object");
        claim.value = objectText(item, "value");
        claim.unit = stringField(item, "unit");
        claim.polarity = stringField(item, "polarity", "positive");
        claim.lens = stringField(item, "lens");
        claim.quote = stringField(item, "quote");
        claim.validFrom = stringField(item, "valid_from", stringField(item, "validFrom"));
        claim.validTo = stringField(item, "valid_to", stringField(item, "validTo"));
        if (item.contains("qualifiers")) {
            claim.qualifiersJson = item["qualifiers"].is_string()
                ? taskforge::Json{{"note", item["qualifiers"].str()}}.dump()
                : item["qualifiers"].dump();
        }
        if (claim.subject.empty() || claim.predicate.empty()) {
            reject("claim sem subject ou predicate");
            continue;
        }
        if (claim.object.empty() && claim.value.empty()) {
            reject("claim sem object ou value");
            continue;
        }
        if (claim.quote.empty() || passageText.find(claim.quote) == std::string::npos) {
            reject("quote nao aparece literalmente no trecho");
            continue;
        }
        if (!dateParsable(claim.validFrom) || !dateParsable(claim.validTo)) {
            reject("data invalida");
            continue;
        }
        if (!unitParsable(claim.unit)) {
            reject("unidade invalida");
            continue;
        }
        if (claim.polarity != "positive" && claim.polarity != "negative") {
            reject("polaridade invalida");
            continue;
        }
        claim.knownPredicate = isKnownPredicate(claim.predicate);
        claims.push_back(std::move(claim));
    }
    diag["accepted"] = static_cast<long long>(claims.size());
    if (diagnostics) *diagnostics = diag;
    return claims;
}

std::vector<ClaimDraft> MemoryClaimExtractor::extract(const std::string& passageText,
                                                      const ClaimExtractorConfig& config,
                                                      std::string& error,
                                                      taskforge::Json* diagnostics) {
    error.clear();
    std::vector<ClaimDraft> claims;
    if (!config.enabled) {
        error = "extracao de afirmacoes desligada";
        return claims;
    }
    if (config.baseUrl.empty() || config.model.empty()) {
        error = "extrator sem baseUrl/model";
        return claims;
    }
    std::string url = config.baseUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() < 18 || url.substr(url.size() - 17) != "/chat/completions") {
        url += "/chat/completions";
    }
    const std::string system =
        "Extract factual claims from the passage. Return ONLY JSON: "
        "{\"claims\":[{\"subject\":\"...\",\"subject_type\":\"entity\",\"predicate\":\"USES\","
        "\"object\":\"...\",\"value\":\"\",\"unit\":\"\",\"polarity\":\"positive\","
        "\"qualifiers\":{},\"valid_from\":\"\",\"valid_to\":\"\",\"lens\":\"\","
        "\"quote\":\"exact substring\"}]}. "
        "An empty claims list is valid. The quote MUST be a literal substring of the passage. "
        "Use an uppercase predicate from the catalog when possible.";
    taskforge::Json requestBody{
        {"model", config.model},
        {"temperature", 0.0},
        {"max_tokens", 1200},
        {"messages", taskforge::Json::array({
            taskforge::Json{{"role", "system"}, {"content", system}},
            taskforge::Json{{"role", "user"}, {"content", passageText}}})}};

    rt::HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.timeoutSeconds = config.timeoutSeconds;
    request.headers["Content-Type"] = "application/json";
    if (!config.apiKeyEnv.empty()) {
        const char* key = std::getenv(config.apiKeyEnv.c_str());
        if (key && *key) request.headers["Authorization"] = std::string("Bearer ") + key;
    }
    request.body = requestBody.dump();
    rt::HttpResponse response = rt::httpSend(request);
    if (response.status < 200 || response.status >= 300) {
        error = "extrator HTTP " + std::to_string(response.status) +
                (response.error.empty() ? "" : " " + response.error);
        return claims;
    }
    auto parsed = taskforge::jsonutil::tryParse(response.body);
    if (!parsed || !parsed->is_object()) {
        error = "resposta HTTP do extrator nao e JSON";
        return claims;
    }
    std::string content;
    if (parsed->contains("choices") && (*parsed)["choices"].is_array() &&
        !(*parsed)["choices"].empty()) {
        const taskforge::Json message = (*parsed)["choices"][0]["message"];
        if (message.is_object()) content = stringField(message, "content");
    }
    if (content.empty()) {
        error = "extrator sem content";
        return claims;
    }
    return parseResponse(content, passageText, config, error, diagnostics);
}

} // namespace memory
} // namespace taskforge
