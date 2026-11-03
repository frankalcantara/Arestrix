#include "MemoryClaimExtractor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "MemoryCatalog.hpp"
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
        "|" + std::to_string(maxItems) + "|" + MemoryCatalog::instance().fingerprint() +
        "|" + catalogMode + "|" + structuredOutput + "|" + protocol);
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
        config.protocol = extractor.value("protocol", config.protocol);
        config.structuredOutput = extractor.value("structuredOutput", config.structuredOutput);
    }
    if (switchValue.is_object()) {
        config.structuredOutput = switchValue.value("structuredOutput", config.structuredOutput);
        config.protocol = switchValue.value("protocol", config.protocol);
        config.maxCatalogPromptTokens = std::max(100, std::min(20000,
            switchValue.value("maxCatalogPromptTokens", config.maxCatalogPromptTokens)));
    }
    if (memory.contains("catalog") && memory["catalog"].is_object()) {
        config.catalogMode = memory["catalog"].value("mode", config.catalogMode);
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
        if (isStructuralPredicate(claim.predicate)) {
            reject("structural_predicate");
            continue;
        }
        if (!claim.knownPredicate && config.catalogMode == "strict") {
            reject("unknown_predicate");
            continue;
        }
        if (claim.knownPredicate) {
            taskforge::Json qualifiers = taskforge::Json::object();
            auto parsedQualifiers = taskforge::jsonutil::tryParse(claim.qualifiersJson);
            if (parsedQualifiers && parsedQualifiers->is_object()) qualifiers = *parsedQualifiers;
            std::string contextKey;
            std::string contextError;
            const bool contextOk = MemoryCatalog::instance().canonicalContext(
                claim.predicate, qualifiers, contextKey, contextError);
            if (!contextOk && contextError == "missing_context" &&
                config.catalogMode == "strict") {
                reject("missing_context");
                continue;
            }
        }
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
    const bool anthropic = config.protocol == "anthropic" ||
        (config.protocol == "auto" && url.find("anthropic") != std::string::npos);
    if (anthropic) {
        if (url.size() >= 3 && url.substr(url.size() - 3) == "/v1") {
            url += "/messages";
        } else if (url.size() < 12 || url.substr(url.size() - 12) != "/v1/messages") {
            url += "/v1/messages";
        }
    } else if (url.size() < 18 || url.substr(url.size() - 17) != "/chat/completions") {
        url += "/chat/completions";
    }
    const std::string catalogLines = MemoryCatalog::instance().promptLines();
    const std::string system =
        "Extract factual claims from the passage. Return ONLY JSON: "
        "{\"claims\":[{\"subject\":\"...\",\"subject_type\":\"entity\",\"predicate\":\"USES\","
        "\"object\":\"...\",\"value\":\"\",\"unit\":\"\",\"polarity\":\"positive\","
        "\"qualifiers\":{},\"valid_from\":\"\",\"valid_to\":\"\",\"lens\":\"\","
        "\"quote\":\"exact substring\"}]}. "
        "An empty claims list is valid. The quote MUST be a literal substring of the passage. "
        "Use only a claimable predicate from this catalog; never use a structural predicate:\n" +
        catalogLines;
    taskforge::Json requestBody{
        {"model", config.model},
        {"temperature", 0.0},
        {"max_tokens", 1200},
        {"messages", taskforge::Json::array({
            taskforge::Json{{"role", "system"}, {"content", system}},
            taskforge::Json{{"role", "user"}, {"content", passageText}}})}};
    if (config.structuredOutput == "json_schema") {
        taskforge::Json predicateEnum = taskforge::Json::array();
        for (const auto& name : MemoryCatalog::instance().claimableNames()) {
            predicateEnum.push_back(name);
        }
        taskforge::Json schema{
            {"type", "object"},
            {"additionalProperties", false},
            {"properties", taskforge::Json{
                {"claims", taskforge::Json{
                    {"type", "array"},
                    {"items", taskforge::Json{
                        {"type", "object"},
                        {"additionalProperties", false},
                        {"properties", taskforge::Json{
                            {"subject", taskforge::Json{{"type", "string"}}},
                            {"predicate", taskforge::Json{{"type", "string"},
                                                          {"enum", predicateEnum}}},
                            {"object", taskforge::Json{{"type", "string"}}},
                            {"value", taskforge::Json{{"type", "string"}}},
                            {"unit", taskforge::Json{{"type", "string"}}},
                            {"polarity", taskforge::Json{{"type", "string"},
                                                         {"enum", taskforge::Json::array({"positive", "negative"})}}},
                            {"qualifiers", taskforge::Json{{"type", "object"}}},
                            {"valid_from", taskforge::Json{{"type", "string"}}},
                            {"valid_to", taskforge::Json{{"type", "string"}}},
                            {"lens", taskforge::Json{{"type", "string"}}},
                            {"quote", taskforge::Json{{"type", "string"}}}}},
                        {"required", taskforge::Json::array({"subject", "predicate", "object", "quote"})}}}}}}},
            {"required", taskforge::Json::array({"claims"})}};
        requestBody["response_format"] = taskforge::Json{
            {"type", "json_schema"},
            {"json_schema", taskforge::Json{{"name", "claims"}, {"strict", true},
                                            {"schema", schema}}}};
    } else if (config.structuredOutput == "json_object") {
        requestBody["response_format"] = taskforge::Json{{"type", "json_object"}};
    }
    if (anthropic) {
        taskforge::Json predicateEnum = taskforge::Json::array();
        for (const auto& name : MemoryCatalog::instance().claimableNames()) {
            predicateEnum.push_back(name);
        }
        taskforge::Json schema{
            {"type", "object"},
            {"properties", taskforge::Json{
                {"claims", taskforge::Json{
                    {"type", "array"},
                    {"items", taskforge::Json{
                        {"type", "object"},
                        {"properties", taskforge::Json{
                            {"subject", taskforge::Json{{"type", "string"}}},
                            {"predicate", taskforge::Json{{"type", "string"},
                                                          {"enum", predicateEnum}}},
                            {"object", taskforge::Json{{"type", "string"}}},
                            {"value", taskforge::Json{{"type", "string"}}},
                            {"unit", taskforge::Json{{"type", "string"}}},
                            {"polarity", taskforge::Json{{"type", "string"},
                                                         {"enum", taskforge::Json::array({"positive", "negative"})}}},
                            {"qualifiers", taskforge::Json{{"type", "object"}}},
                            {"valid_from", taskforge::Json{{"type", "string"}}},
                            {"valid_to", taskforge::Json{{"type", "string"}}},
                            {"lens", taskforge::Json{{"type", "string"}}},
                            {"quote", taskforge::Json{{"type", "string"}}}}},
                        {"required", taskforge::Json::array({"subject", "predicate", "object", "quote"})}}}}}}},
            {"required", taskforge::Json::array({"claims"})}};
        requestBody = taskforge::Json{
            {"model", config.model},
            {"max_tokens", 1200},
            {"temperature", 0.0},
            {"system", system},
            {"messages", taskforge::Json::array({
                taskforge::Json{{"role", "user"}, {"content", passageText}}})},
            {"tools", taskforge::Json::array({
                taskforge::Json{{"name", "emit_claims"},
                                {"description", "Emite as afirmacoes extraidas."},
                                {"input_schema", schema}}})},
            {"tool_choice", taskforge::Json{{"type", "tool"}, {"name", "emit_claims"}}}};
    }

    rt::HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.timeoutSeconds = config.timeoutSeconds;
    request.headers["Content-Type"] = "application/json";
    if (anthropic) {
        request.headers["anthropic-version"] = "2023-06-01";
    }
    if (!config.apiKeyEnv.empty()) {
        const char* key = std::getenv(config.apiKeyEnv.c_str());
        if (key && *key) {
            if (anthropic) request.headers["x-api-key"] = key;
            else request.headers["Authorization"] = std::string("Bearer ") + key;
        }
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
    if (content.empty() && anthropic && parsed->contains("content") &&
        (*parsed)["content"].is_array()) {
        for (const auto& block : (*parsed)["content"]) {
            if (block.is_object() && block.value("type", std::string()) == "tool_use" &&
                block.contains("input")) {
                content = block["input"].dump();
                break;
            }
        }
    }
    if (content.empty()) {
        error = "extrator sem content";
        return claims;
    }
    return parseResponse(content, passageText, config, error, diagnostics);
}

} // namespace memory
} // namespace taskforge
