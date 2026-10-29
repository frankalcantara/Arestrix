#include "rt/compress/LogCompressor.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

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

bool isCritical(std::string_view line) {
    return containsFold(line, "FATAL") || containsFold(line, "CRITICAL") || containsFold(line, "panic") ||
           containsFold(line, "Traceback");
}

bool isError(std::string_view line) {
    return containsFold(line, "ERROR") || containsFold(line, "ERRO") || containsFold(line, "Exception");
}

// Normaliza a linha para agrupar repeticoes: digitos, hex e UUIDs viram marcadores.
// O texto original nunca e alterado; isto serve so como chave de comparacao.
std::string normalizeKey(std::string_view line) {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (std::isdigit(c)) {
            while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) ++i;
            out += "0";
            --i;
            continue;
        }
        if (std::isxdigit(c) && c >= 'a' && c <= 'f') {
            // Sequencias hexadecimais longas (ids, enderecos)
            size_t j = i;
            while (j < line.size() && std::isxdigit(static_cast<unsigned char>(line[j]))) ++j;
            if (j - i >= 8) {
                out += "x";
                i = j - 1;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

} // namespace

CompressResult compressLog(std::string_view input, const LogOptions& options) {
    CompressResult result;
    result.bytesBefore = input.size();
    result.bytesAfter = input.size();
    result.text.assign(input);

    const std::vector<std::string_view> lines = splitLines(input);
    if (lines.size() < 8) return result;

    std::map<std::string, size_t> occurrences;
    for (const auto& line : lines) ++occurrences[normalizeKey(line)];

    std::vector<std::string> kept;
    kept.reserve(lines.size());
    size_t errorKept = 0;
    std::map<std::string, size_t> seen;

    for (const auto& line : lines) {
        const std::string key = normalizeKey(line);
        const size_t total = occurrences[key];
        const size_t index = ++seen[key];

        const bool critical = isCritical(line);
        const bool error = !critical && isError(line);

        if (critical) {
            kept.push_back(std::string(line));
            continue;
        }
        if (error && errorKept < options.maxErrorLines) {
            ++errorKept;
            kept.push_back(std::string(line));
            continue;
        }
        if (total > options.keepRepeats) {
            if (index <= options.keepRepeats) {
                kept.push_back(std::string(line));
            } else if (index == options.keepRepeats + 1) {
                kept.push_back("[... " + std::to_string(total - options.keepRepeats) +
                               " linha(s) repetida(s) omitida(s) ...]");
            }
            continue;
        }
        kept.push_back(std::string(line));
    }

    // Teto de linhas: preserva inicio e fim, com marcador no meio.
    if (kept.size() > options.maxLines) {
        const size_t head = options.maxLines * 3 / 4;
        const size_t tail = options.maxLines / 4;
        std::vector<std::string> capped;
        capped.reserve(options.maxLines + 1);
        for (size_t i = 0; i < head; ++i) capped.push_back(kept[i]);
        capped.push_back("[... " + std::to_string(kept.size() - head - tail) + " linha(s) omitida(s) ...]");
        for (size_t i = kept.size() - tail; i < kept.size(); ++i) capped.push_back(kept[i]);
        kept.swap(capped);
    }

    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out += '\n';
        out += kept[i];
    }

    const double before = static_cast<double>(input.size());
    const double after = static_cast<double>(out.size());
    if (after >= before * (1.0 - options.minGain)) return result;

    result.changed = true;
    result.text = std::move(out);
    result.bytesAfter = result.text.size();
    return result;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
