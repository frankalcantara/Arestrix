#include "rt/compress/ContentRouter.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "util/Json.hpp"

namespace taskforge {
namespace rt {
namespace compress {
namespace {

bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trimLeft(std::string_view text) {
    size_t i = 0;
    while (i < text.size() && isSpace(static_cast<unsigned char>(text[i]))) ++i;
    return text.substr(i);
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool containsFold(std::string_view text, std::string_view needle) {
    if (needle.empty() || text.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        size_t j = 0;
        while (j < needle.size() &&
               std::tolower(static_cast<unsigned char>(text[i + j])) ==
                   std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == needle.size()) return true;
    }
    return false;
}

size_t countLines(std::string_view text, size_t* nonEmpty) {
    size_t lines = 0;
    *nonEmpty = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        ++lines;
        bool hasContent = false;
        for (size_t i = start; i < end; ++i) {
            if (!isSpace(static_cast<unsigned char>(text[i]))) { hasContent = true; break; }
        }
        if (hasContent) ++*nonEmpty;
        if (end == text.size()) break;
        start = end + 1;
    }
    return lines;
}

// Linha de log: timestamp no inicio ou nivel textual destacado.
bool looksLikeLogLine(std::string_view line) {
    if (line.size() < 8) return false;
    // 2026-10-29, 2026/10/29, 10/29/2026
    if (std::isdigit(static_cast<unsigned char>(line[0])) && line.size() > 10 &&
        (line[4] == '-' || line[4] == '/') && line[7] == line[4]) {
        return true;
    }
    // [12:34:56]
    if (line[0] == '[' && line.size() > 10 && std::isdigit(static_cast<unsigned char>(line[1])) &&
        line[3] == ':') {
        return true;
    }
    static const char* kLevels[] = {"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE", "CRITICAL"};
    for (const char* level : kLevels) {
        if (containsFold(line, level)) return true;
    }
    return false;
}

bool looksLikeJson(std::string_view text) {
    std::string_view t = trimLeft(text);
    if (t.empty() || (t[0] != '{' && t[0] != '[')) return false;
    auto parsed = jsonutil::tryParse(t);
    if (!parsed.has_value()) return false;
    const Json& j = *parsed;
    if (!j.is_object() && !j.is_array()) return false;
    // Estrutura minima: objeto vazio ou array vazio nao valem compressao.
    return j.size() > 0;
}

// Marcadores de codigo-fonte. Exigimos mais de um para evitar falso positivo em prosa.
int codeScore(const std::string& text, size_t nonEmptyLines) {
    int score = 0;
    if (text.find("#include") != std::string::npos) ++score;
    if (text.find("#pragma") != std::string::npos) ++score;
    if (text.find("std::") != std::string::npos) ++score;
    if (text.find("public:") != std::string::npos || text.find("private:") != std::string::npos) ++score;
    if (text.find("def ") != std::string::npos) ++score;
    if (text.find("function ") != std::string::npos) ++score;
    if (text.find("=>") != std::string::npos) ++score;
    if (text.find("</") != std::string::npos) ++score;

    // Densidade de linhas terminando em ; { } (codigo formatado).
    size_t codeLike = 0;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        size_t last = end;
        while (last > start && isSpace(static_cast<unsigned char>(text[last - 1]))) --last;
        if (last > start) {
            const char c = text[last - 1];
            if (c == ';' || c == '{' || c == '}') ++codeLike;
        }
        if (end == text.size()) break;
        start = end + 1;
    }
    if (nonEmptyLines > 0 && codeLike * 10 >= nonEmptyLines * 3) ++score;   // >= 30%
    return score;
}

} // namespace

const char* contentKindName(ContentKind kind) {
    switch (kind) {
        case ContentKind::Json: return "json";
        case ContentKind::Log: return "log";
        case ContentKind::Code: return "code";
        case ContentKind::Prose: return "prose";
        case ContentKind::Skip: return "skip";
    }
    return "skip";
}

Classification classifyContent(std::string_view text) {
    if (text.size() < 256) return {ContentKind::Skip, "small"};

    if (looksLikeJson(text)) return {ContentKind::Json, "json-object"};

    size_t nonEmpty = 0;
    const size_t lines = countLines(text, &nonEmpty);
    (void)lines;

    if (nonEmpty >= 6) {
        size_t logLines = 0;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string_view::npos) end = text.size();
            std::string_view line = text.substr(start, end - start);
            bool hasContent = false;
            for (char c : line) {
                if (!isSpace(static_cast<unsigned char>(c))) { hasContent = true; break; }
            }
            if (hasContent && looksLikeLogLine(line)) ++logLines;
            if (end == text.size()) break;
            start = end + 1;
        }
        // Metade das linhas parecendo log ja e sinal forte.
        if (logLines * 2 >= nonEmpty) return {ContentKind::Log, "log-lines"};
    }

    if (codeScore(std::string(text), nonEmpty) >= 2) return {ContentKind::Code, "code-markers"};

    return {ContentKind::Prose, "prose"};
}

} // namespace compress
} // namespace rt
} // namespace taskforge
