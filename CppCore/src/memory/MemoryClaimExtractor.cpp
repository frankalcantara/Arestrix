#include "MemoryClaimExtractor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <regex>
#include <sstream>

#include "MemoryCatalog.hpp"
#include "MemoryPredicates.hpp"
#include "project/RunnerSecurity.hpp"
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

namespace {
ClaimIntelligenceFn g_intelligence;
std::mutex g_intelligenceMutex;
} // namespace

void MemoryClaimExtractor::setIntelligence(ClaimIntelligenceFn intelligence) {
    std::lock_guard<std::mutex> lock(g_intelligenceMutex);
    g_intelligence = std::move(intelligence);
}

bool MemoryClaimExtractor::hasIntelligence() {
    std::lock_guard<std::mutex> lock(g_intelligenceMutex);
    return static_cast<bool>(g_intelligence);
}

std::string ClaimExtractorConfig::fingerprint() const {
    return project::RunnerSecurity::sha256(
        "claim|" + promptVersion + "|" + schemaVersion + "|" + std::to_string(maxItems) +
        "|" + MemoryCatalog::instance().fingerprint() + "|" + catalogMode);
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
        config.maxItems = std::max(1, std::min(20, switchValue.value("maxItems", config.maxItems)));
        config.promptVersion = switchValue.value("promptVersion", config.promptVersion);
        config.schemaVersion = switchValue.value("schemaVersion", config.schemaVersion);
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
    ClaimIntelligenceFn intelligence;
    {
        std::lock_guard<std::mutex> lock(g_intelligenceMutex);
        intelligence = g_intelligence;
    }
    if (!intelligence) {
        error = "agente local do Studio indisponivel para extracao de claims";
        return claims;
    }
    taskforge::Json schema = taskforge::Json::object();
    schema["predicates"] = MemoryCatalog::instance().claimableNames();
    schema["requiredContextKeys"] = taskforge::Json::object();
    for (const auto& name : MemoryCatalog::instance().claimableNames()) {
        taskforge::Json keys = taskforge::Json::array();
        for (const auto& key : MemoryCatalog::instance().requiredContextKeys(name)) {
            keys.push_back(key);
        }
        schema["requiredContextKeys"][name] = keys;
    }
    ClaimIntelligenceRequest request;
    request.passageText = passageText;
    request.catalog = MemoryCatalog::instance().promptLines();
    request.schemaJson = schema.dump();
    request.maxItems = config.maxItems;
    ClaimIntelligenceResult result = intelligence(request);
    if (!result.ok) {
        error = result.error.empty() ? "agente local falhou na extracao" : result.error;
        return claims;
    }
    return parseResponse(result.content, passageText, config, error, diagnostics);
}

} // namespace memory
} // namespace taskforge
