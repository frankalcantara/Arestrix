#include "rt/compress/ProseCompressor.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

using Range = std::pair<size_t, size_t>;

std::vector<Range> protectedRanges(std::string_view text) {
    std::vector<Range> ranges;
    const std::string input(text);
    static const std::regex kUrl(R"(https?://[^\s]+)");
    static const std::regex kEmail(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    static const std::regex kPath(R"([A-Za-z0-9_.-]+(?:[/\\][A-Za-z0-9_.-]+)+)");
    static const std::regex kHash(R"(\b[0-9a-fA-F]{16,}\b)");
    static const std::regex kDate(R"(\b\d{4}-\d{2}-\d{2}\b)");

    for (const auto* pattern : {&kUrl, &kEmail, &kPath, &kHash, &kDate}) {
        for (auto it = std::sregex_iterator(input.begin(), input.end(), *pattern);
             it != std::sregex_iterator(); ++it) {
            const auto& match = *it;
            ranges.emplace_back(static_cast<size_t>(match.position()),
                                static_cast<size_t>(match.position() + match.length()));
        }
    }
    return ranges;
}

bool overlaps(const std::vector<Range>& ranges, size_t start, size_t end) {
    for (const auto& range : ranges) {
        if (start < range.second && end > range.first) return true;
    }
    return false;
}

bool allPunctuation(std::string_view text) {
    if (text.empty()) return false;
    for (char c : text) {
        if (!std::ispunct(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool hasWhitespace(std::string_view text) {
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return true;
    }
    return false;
}

// Espacos duplicados aparecem quando um token mantido ja comeca com "Ġ" e o buraco anterior
// tambem era espaco. Em prosa, colapsar e seguro e deixa o texto legivel.
std::string tidySpaces(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool previousSpace = false;
    for (char c : text) {
        if (c == ' ' || c == '\t') {
            if (previousSpace) continue;
            previousSpace = true;
            out += ' ';
        } else {
            previousSpace = false;
            out += c;
        }
    }
    size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first);
}

// Offsets de sub-palavra sao aproximados; nunca deixe um corte cair no meio de um UTF-8.
size_t snapToCharacter(std::string_view text, size_t index, bool forward) {
    if (forward) {
        while (index < text.size() && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) ++index;
    } else {
        while (index > 0 && (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80) --index;
    }
    return index;
}

} // namespace

CompressResult compressProse(std::string_view text, const ProseTokenizer& tokenizer,
                             ProseModel& model, const ProseOptions& options) {
    CompressResult result;
    result.bytesBefore = text.size();
    result.bytesAfter = text.size();
    result.text.assign(text);

    if (!model.available() || text.empty()) return result;

    const std::vector<ProseToken> tokens =
        tokenizer.tokenize(text, options.maxTokens > 0 ? options.maxTokens + 1 : 0);
    if (tokens.size() < 32) return result;
    // Acima do teto: nao comprime. Antes, o tokenizador truncava e o fim do texto sumia.
    if (options.maxTokens > 0 && tokens.size() > options.maxTokens) return result;

    const size_t requestedWindow = options.windowTokens > 0 ? options.windowTokens : tokens.size();
    const size_t window = std::max<size_t>(1, std::min(requestedWindow, tokens.size()));
    std::vector<float> probabilities;
    probabilities.reserve(tokens.size());
    std::vector<int> ids;
    ids.reserve(window);
    for (size_t start = 0; start < tokens.size(); start += window) {
        const size_t end = std::min(tokens.size(), start + window);
        ids.clear();
        for (size_t i = start; i < end; ++i) ids.push_back(tokens[i].id);
        const std::vector<float> part = model.keepProbabilities(ids);
        if (part.size() != ids.size()) return result;
        probabilities.insert(probabilities.end(), part.begin(), part.end());
    }
    if (probabilities.size() != tokens.size()) return result;

    const std::vector<Range> protectedSpans = protectedRanges(text);

    std::string out;
    out.reserve(text.size() / 2);
    size_t cursor = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const ProseToken& token = tokens[i];
        if (token.end <= token.start) continue;

        const std::string_view tokenText = text.substr(token.start, token.end - token.start);
        const bool keep = probabilities[i] >= options.threshold || overlaps(protectedSpans, token.start, token.end) ||
                          allPunctuation(tokenText);
        if (!keep) continue;

        size_t start = snapToCharacter(text, std::min(token.start, text.size()), false);
        size_t end = snapToCharacter(text, std::min(token.end, text.size()), true);
        if (start < cursor) start = cursor;
        if (end <= start) continue;

        if (!out.empty() && start > cursor) {
            const std::string_view gap = text.substr(cursor, start - cursor);
            if (hasWhitespace(gap)) {
                out += ' ';
            } else if (allPunctuation(gap)) {
                out.append(gap);
            }
        }
        out.append(text.substr(start, end - start));
        cursor = end;
    }

    out = tidySpaces(out);

    const double before = static_cast<double>(text.size());
    const double after = static_cast<double>(out.size());
    if (out.empty() || after >= before * (1.0 - options.minGain)) return result;

    result.changed = true;
    result.text = std::move(out);
    result.bytesAfter = result.text.size();
    return result;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
