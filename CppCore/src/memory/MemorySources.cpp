#include "MemorySources.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>

#include "project/RunnerSecurity.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool isCodeExtension(const std::string& ext) {
    static const std::set<std::string> extensions = {
        ".c", ".h", ".hpp", ".hh", ".hxx", ".cpp", ".cc", ".cxx", ".py", ".js", ".mjs",
        ".cjs", ".ts", ".tsx", ".jsx", ".java", ".go", ".rs", ".rb", ".php", ".cs",
        ".swift", ".kt", ".kts", ".sh", ".bash", ".zsh", ".ps1", ".sql", ".lua", ".r",
        ".jl", ".m", ".mm", ".gradle", ".cmake", ".vue", ".svelte"
    };
    return extensions.count(ext) != 0;
}

bool isTextExtension(const std::string& ext) {
    static const std::set<std::string> extensions = {
        ".txt", ".rst", ".json", ".jsonl", ".yaml", ".yml", ".toml", ".ini", ".cfg",
        ".conf", ".html", ".htm", ".css", ".scss", ".less", ".xml", ".csv", ".tsv",
        ".tex", ".org", ".adoc"
    };
    return extensions.count(ext) != 0;
}

std::string trimCopy(const std::string& value, size_t begin, size_t end) {
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

bool isTopLevelDeclaration(const std::string& line) {
    if (line.empty() || std::isspace(static_cast<unsigned char>(line[0]))) return false;
    static const char* prefixes[] = {
        "class ", "struct ", "enum ", "namespace ", "template", "using ", "typedef ",
        "function ", "def ", "fn ", "impl ", "interface ", "export ", "async ",
        "public ", "private ", "protected ", "void ", "int ", "bool ", "string ",
        "double ", "float ", "char ", "long ", "unsigned ", "const ", "static ",
        "package ", "module ", "func ", "type "
    };
    for (const char* prefix : prefixes) {
        if (line.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

void appendWindowed(std::vector<MemoryPassageDraft>& out, const std::string& text,
                    size_t begin, size_t end, long long& ordinal,
                    size_t window = 1000, size_t overlap = 200) {
    if (end <= begin) return;
    if (end - begin <= 1200) {
        std::string body = trimCopy(text, begin, end);
        if (body.empty()) return;
        out.push_back({ordinal++, static_cast<long long>(begin),
                       static_cast<long long>(end), std::move(body)});
        return;
    }
    size_t cursor = begin;
    while (cursor < end) {
        const size_t stop = std::min(end, cursor + window);
        std::string body = trimCopy(text, cursor, stop);
        if (!body.empty()) {
            out.push_back({ordinal++, static_cast<long long>(cursor),
                           static_cast<long long>(stop), std::move(body)});
        }
        if (stop >= end) break;
        cursor = stop > overlap ? stop - overlap : stop;
    }
}

} // namespace

std::string MemorySources::normalizeUri(const std::string& uri) {
    std::string out = uri;
    std::replace(out.begin(), out.end(), '\\', '/');
    while (out.rfind("./", 0) == 0) out.erase(0, 2);
    while (!out.empty() && out.front() == '/') out.erase(out.begin());
    return out;
}

std::string MemorySources::kindForUri(const std::string& uri) {
    const std::string ext = lower(std::string(std::filesystem::path(uri).extension().string()));
    if (ext == ".md" || ext == ".markdown") return "markdown";
    if (isCodeExtension(ext)) return "code";
    if (isTextExtension(ext)) return "text";
    return "";
}

bool MemorySources::isSupported(const std::string& uri, size_t size, size_t maxBytes) {
    if (uri.empty() || size == 0) return false;
    if (maxBytes > 0 && size > maxBytes) return false;
    return !kindForUri(uri).empty();
}

bool MemorySources::isExcluded(const std::string& relativeUri) {
    const std::string uri = lower(normalizeUri(relativeUri));
    const std::vector<std::string> parts = [&] {
        std::vector<std::string> values;
        std::string part;
        for (char c : uri) {
            if (c == '/') {
                if (!part.empty()) values.push_back(part);
                part.clear();
            } else {
                part.push_back(c);
            }
        }
        if (!part.empty()) values.push_back(part);
        return values;
    }();
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i] == ".git" || parts[i] == "node_modules" || parts[i] == ".arestrix") {
            return true;
        }
        if (parts[i] == ".workspace" && i + 1 < parts.size() && parts[i + 1] == "memory") {
            return true;
        }
    }
    return false;
}

std::vector<MemoryPassageDraft> MemorySources::segment(const std::string& text,
                                                       const std::string& kind) {
    std::vector<MemoryPassageDraft> out;
    long long ordinal = 0;
    if (text.empty()) return out;
    if (kind == "text") {
        appendWindowed(out, text, 0, text.size(), ordinal);
        return out;
    }

    size_t blockStart = 0;
    size_t lineStart = 0;
    bool code = kind == "code";
    bool sawCodeBlock = false;
    while (lineStart <= text.size()) {
        const size_t newline = text.find('\n', lineStart);
        const size_t lineEnd = newline == std::string::npos ? text.size() : newline;
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        bool boundary = false;
        if (kind == "markdown") {
            boundary = line.empty() || (!line.empty() && line[0] == '#');
        } else if (code) {
            boundary = isTopLevelDeclaration(line);
        }
        if (boundary) {
            if (lineStart > blockStart) {
                appendWindowed(out, text, blockStart, lineStart, ordinal);
            }
            blockStart = lineStart;
            if (code && !line.empty()) sawCodeBlock = true;
        }
        if (newline == std::string::npos) break;
        lineStart = newline + 1;
    }
    if (blockStart < text.size()) appendWindowed(out, text, blockStart, text.size(), ordinal);
    if (code && !sawCodeBlock) {
        out.clear();
        ordinal = 0;
        appendWindowed(out, text, 0, text.size(), ordinal);
    }
    if (out.empty()) appendWindowed(out, text, 0, text.size(), ordinal);
    return out;
}

std::string MemorySources::sourceId(const std::string& scopeId, const std::string& uri,
                                    const std::string& contentHash) {
    return "src_" + project::RunnerSecurity::sha256(
        scopeId + "|" + normalizeUri(uri) + "|" + contentHash).substr(0, 32);
}

std::string MemorySources::passageId(const std::string& contentHash, long long ordinal) {
    return "psg_" + project::RunnerSecurity::sha256(
        contentHash + "|" + std::to_string(ordinal)).substr(0, 32);
}

} // namespace memory
} // namespace taskforge
