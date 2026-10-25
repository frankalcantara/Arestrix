#include "MemorySanitizer.hpp"

#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace taskforge {
namespace memory {

namespace {

struct Rule {
    const std::regex* pattern = nullptr;
    const char* replacement = "[REDACTED]";
};

const std::vector<Rule>& secretRules() {
    using namespace std::regex_constants;
    static const std::regex openai(R"(sk-[A-Za-z0-9_-]{16,})", optimize);
    static const std::regex stripe(R"((?:sk|rk)_(?:live|test)_[A-Za-z0-9]{16,})", optimize);
    static const std::regex google(R"(AIza[0-9A-Za-z_-]{30,})", optimize);
    static const std::regex aws(R"(AKIA[0-9A-Z]{16})", optimize);
    static const std::regex githubFine(R"(github_pat_[A-Za-z0-9_]{20,})", optimize);
    static const std::regex githubClassic(R"(gh[pousr]_[A-Za-z0-9]{20,})", optimize);
    static const std::regex slack(R"(xox[baprs]-[A-Za-z0-9-]{10,})", optimize);
    static const std::regex bearer(R"(Bearer\s+[A-Za-z0-9._~+/=-]{8,})", icase | optimize);
    static const std::regex privateKey(
        R"(-----BEGIN [A-Z ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z ]*PRIVATE KEY-----)",
        optimize);
    static const std::regex assignment(
        R"((api[_-]?key|access[_-]?token|auth[_-]?token|token|secret|password|passwd)\s*[:=]\s*["']?[^\s"'&,;]{6,})",
        icase | optimize);
    static const std::regex queryParam(
        R"(([?&](?:key|token|access_token|sig|signature|api_key)=)[^&\s]+)",
        icase | optimize);

    static const std::vector<Rule> rules = {
        {&privateKey, "[REDACTED PRIVATE KEY]"},
        {&openai, "[REDACTED]"},
        {&stripe, "[REDACTED]"},
        {&google, "[REDACTED]"},
        {&aws, "[REDACTED]"},
        {&githubFine, "[REDACTED]"},
        {&githubClassic, "[REDACTED]"},
        {&slack, "[REDACTED]"},
        {&bearer, "Bearer [REDACTED]"},
        {&assignment, "$1=[REDACTED]"},
        {&queryParam, "$1[REDACTED]"},
    };
    return rules;
}

size_t applyRule(std::string& text, const Rule& rule) {
    if (!rule.pattern) return 0;
    size_t matches = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), *rule.pattern), end; it != end; ++it) {
        ++matches;
    }
    if (matches == 0) return 0;
    std::string replaced = std::regex_replace(text, *rule.pattern, rule.replacement);
    if (replaced == text) return 0;
    text = std::move(replaced);
    return matches;
}

std::string stripControlChars(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool lastWasSpace = false;
    for (unsigned char c : text) {
        if (c == '\n' || c == '\r' || c == '\t') {
            out.push_back(static_cast<char>(c));
            lastWasSpace = false;
            continue;
        }
        if (c < 0x20 || c == 0x7f) {
            if (!lastWasSpace) out.push_back(' ');
            lastWasSpace = true;
            continue;
        }
        out.push_back(static_cast<char>(c));
        lastWasSpace = false;
    }
    return out;
}

} // namespace

SanitizeResult sanitizeText(const std::string& input, size_t maxChars) {
    SanitizeResult result;
    result.text = input;

    for (const auto& rule : secretRules()) {
        const size_t matches = applyRule(result.text, rule);
        if (matches > 0) {
            result.redacted = true;
            result.redactions += matches;
        }
    }

    result.text = stripControlChars(result.text);

    if (maxChars > 0 && result.text.size() > maxChars) {
        result.truncated = true;
        if (maxChars > 3) {
            result.text.resize(maxChars - 3);
            result.text += "...";
        } else {
            result.text.resize(maxChars);
        }
    }

    return result;
}

size_t sanitizeJsonInPlace(taskforge::Json& value) {
    if (value.is_string()) {
        SanitizeResult clean = sanitizeText(value.str());
        if (clean.redacted) {
            value = taskforge::Json(clean.text);
        }
        return clean.redactions;
    }

    size_t redactions = 0;
    if (value.is_array() || value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            redactions += sanitizeJsonInPlace(it.value());
        }
    }
    return redactions;
}

} // namespace memory
} // namespace taskforge
