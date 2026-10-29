#include "rt/compress/CodeCompressor.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

struct Frame {
    bool keep = false;       // o bloco correspondente sobrevive na visao comprimida
    bool typeBlock = false;  // class/struct/namespace/interface/enum...
};

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

std::string_view trim(std::string_view line) {
    size_t first = 0;
    while (first < line.size() && std::isspace(static_cast<unsigned char>(line[first]))) ++first;
    size_t last = line.size();
    while (last > first && std::isspace(static_cast<unsigned char>(line[last - 1]))) --last;
    return line.substr(first, last - first);
}

size_t indentOf(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    return i;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool isImportLike(std::string_view t) {
    return startsWith(t, "#include") || startsWith(t, "#pragma") || startsWith(t, "import ") ||
           startsWith(t, "from ") || startsWith(t, "using ") || startsWith(t, "package ") ||
           startsWith(t, "require(") || startsWith(t, "export ") || startsWith(t, "use ");
}

bool hasTodo(std::string_view line) {
    return line.find("TODO") != std::string_view::npos || line.find("FIXME") != std::string_view::npos;
}

bool isTypeKeyword(std::string_view t) {
    static const char* kKeywords[] = {"class ", "struct ", "namespace ", "interface ", "enum ",
                                      "trait ", "union ", "impl ", "record ", "module "};
    for (const char* keyword : kKeywords) {
        if (t.find(keyword) != std::string_view::npos) return true;
    }
    return false;
}

bool isComment(std::string_view t) {
    return startsWith(t, "//") || startsWith(t, "/*") || startsWith(t, "*") || startsWith(t, "#") ||
           startsWith(t, "--");
}

bool isPythonLike(const std::vector<std::string_view>& lines) {
    size_t defs = 0;
    for (const auto& line : lines) {
        const std::string_view t = trim(line);
        if ((startsWith(t, "def ") || startsWith(t, "class ") || startsWith(t, "async def ")) &&
            !t.empty() && t.back() == ':') {
            ++defs;
        }
    }
    return defs >= 2;
}

std::string markerFor(bool python, size_t omitted) {
    return python ? "# ... " + std::to_string(omitted) + " linha(s) omitida(s) ..."
                  : "// ... " + std::to_string(omitted) + " linha(s) omitida(s) ...";
}

std::string compressIndented(const std::vector<std::string_view>& lines, const CodeOptions& options) {
    std::vector<std::string> kept;
    kept.reserve(lines.size());
    size_t omittedRun = 0;

    auto flush = [&]() {
        if (omittedRun > 0) {
            kept.push_back(markerFor(true, omittedRun));
            omittedRun = 0;
        }
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        const std::string_view t = trim(line);

        if (i < options.keepHeaderLines) {
            const bool headerLine = t.empty() || isComment(t) || isImportLike(t);
            if (headerLine) {
                flush();
                kept.push_back(std::string(line));
                continue;
            }
        }

        const bool keep = t.empty() || indentOf(line) == 0 || startsWith(t, "def ") ||
                          startsWith(t, "class ") || startsWith(t, "async def ") || startsWith(t, "@") ||
                          isImportLike(t) || hasTodo(line);
        if (keep) {
            flush();
            kept.push_back(std::string(line));
        } else {
            ++omittedRun;
        }
    }
    flush();

    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out += '\n';
        out += kept[i];
    }
    return out;
}

std::string compressBraced(const std::vector<std::string_view>& lines, const CodeOptions& options) {
    std::vector<std::string> kept;
    kept.reserve(lines.size());
    std::vector<Frame> stack;
    size_t omittedRun = 0;
    int parenDepth = 0;

    auto flush = [&]() {
        if (omittedRun > 0) {
            kept.push_back(markerFor(false, omittedRun));
            omittedRun = 0;
        }
    };

    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = lines[i];
        const std::string_view t = trim(line);

        size_t opens = 0;
        size_t closes = 0;
        for (char c : line) {
            if (c == '{') ++opens;
            else if (c == '}') ++closes;
        }

        // Blocos fechados nesta linha: mantem o fechamento apenas se o bloco sobreviveu.
        bool closeKept = false;
        for (size_t n = 0; n < closes && !stack.empty(); ++n) {
            closeKept = stack.back().keep || closeKept;
            stack.pop_back();
        }

        const bool typeOpener = opens > 0 && isTypeKeyword(t);
        const bool topTypeBlock = stack.empty() || stack.back().typeBlock;
        const bool header = (i < options.keepHeaderLines &&
                             (t.empty() || isComment(t) || isImportLike(t))) ||
                            isImportLike(t) || hasTodo(line);
        const bool typeLevel = topTypeBlock && (opens > 0 || (!t.empty() && t.back() == ';') || isComment(t));
        // Assinatura quebrada em varias linhas: mantem enquanto estiver dentro dos parenteses.
        const bool inSignature = topTypeBlock && parenDepth > 0;

        const bool keep = header || stack.empty() || typeLevel || inSignature || closeKept;

        if (keep) {
            flush();
            kept.push_back(std::string(line));
        } else {
            ++omittedRun;
        }

        for (char c : line) {
            if (c == '(') ++parenDepth;
            else if (c == ')') parenDepth = std::max(0, parenDepth - 1);
        }

        for (size_t n = 0; n < opens; ++n) {
            stack.push_back(Frame{keep, typeOpener});
        }
    }
    flush();

    std::string out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out += '\n';
        out += kept[i];
    }
    return out;
}

} // namespace

CompressResult compressCode(std::string_view input, const CodeOptions& options) {
    CompressResult result;
    result.bytesBefore = input.size();
    result.bytesAfter = input.size();
    result.text.assign(input);

    const std::vector<std::string_view> lines = splitLines(input);
    if (lines.size() < 12) return result;

    const bool python = isPythonLike(lines);
    std::string out = python ? compressIndented(lines, options) : compressBraced(lines, options);

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
