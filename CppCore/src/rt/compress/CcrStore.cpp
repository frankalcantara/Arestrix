#include "rt/compress/CcrStore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

constexpr size_t kTokenLength = 16;

std::string safeSegment(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        const unsigned char u = static_cast<unsigned char>(c);
        out += (std::isalnum(u) || c == '-' || c == '_') ? c : '_';
    }
    if (out.empty()) return "manual";
    return out;
}

bool isHexToken(const std::string& token) {
    if (token.size() != kTokenLength) return false;
    for (char c : token) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

std::string randomToken() {
    static const char* hex = "0123456789abcdef";
    std::random_device device;
    std::mt19937_64 engine(((static_cast<uint64_t>(device()) << 32) ^ device()) ^
                           static_cast<uint64_t>(
                               std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> digit(0, 15);
    std::string token;
    token.reserve(kTokenLength);
    for (size_t i = 0; i < kTokenLength; ++i) token += hex[digit(engine)];
    return token;
}

struct CacheFile {
    std::filesystem::path path;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type written;
};

} // namespace

std::filesystem::path defaultCcrRoot() {
    if (const char* override = std::getenv("ARESTRIX_CCR_DIR")) {
        if (*override) return std::filesystem::path(override);
    }
#ifdef _WIN32
    for (const char* env : {"LOCALAPPDATA", "APPDATA"}) {
        const char* value = std::getenv(env);
        if (value && *value) return std::filesystem::path(value) / "Arestrix" / "ccr";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        if (*home) return std::filesystem::path(home) / "Library" / "Application Support" / "Arestrix" / "ccr";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg) return std::filesystem::path(xdg) / "Arestrix" / "ccr";
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) return std::filesystem::path(home) / ".local" / "share" / "Arestrix" / "ccr";
    }
#endif
    return std::filesystem::temp_directory_path() / "Arestrix" / "ccr";
}

CcrStore::CcrStore(std::filesystem::path baseRoot, std::string projectId, std::string runId,
                   CcrLimits limits)
    : dir_(std::move(baseRoot) / safeSegment(projectId) / safeSegment(runId)),
      projectDir_(dir_.parent_path()),
      limits_(limits) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
}

std::string CcrStore::store(std::string_view original) {
    std::error_code ec;
    if (!std::filesystem::exists(dir_, ec)) {
        std::filesystem::create_directories(dir_, ec);
        if (ec) return {};
    }

    std::string token = randomToken();
    for (int attempt = 0; attempt < 4 && std::filesystem::exists(dir_ / (token + ".txt"), ec); ++attempt) {
        token = randomToken();
    }

    const std::filesystem::path file = dir_ / (token + ".txt");
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return {};
    out.write(original.data(), static_cast<std::streamsize>(original.size()));
    if (!out) return {};

    prune();
    return token;
}

std::optional<std::string> CcrStore::retrieve(const std::string& token) const {
    if (!isHexToken(token)) return std::nullopt;
    const std::filesystem::path file = dir_ / (token + ".txt");
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return std::nullopt;

    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return content;
}

void CcrStore::prune() const {
    std::error_code ec;
    if (!std::filesystem::is_directory(projectDir_, ec)) return;

    // A poda e do projeto, nao da execucao: runs antigas tambem sao limpas.
    std::vector<CacheFile> files;
    for (std::filesystem::recursive_directory_iterator it(projectDir_, ec), end; it != end; it.increment(ec)) {
        if (ec) return;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".txt") continue;
        CacheFile entry;
        entry.path = it->path();
        entry.size = it->file_size(ec);
        if (ec) continue;
        entry.written = it->last_write_time(ec);
        if (ec) continue;
        files.push_back(std::move(entry));
    }
    if (files.empty()) return;

    const auto now = std::filesystem::file_time_type::clock::now();
    std::uintmax_t total = 0;
    for (const auto& file : files) {
        if (now - file.written > limits_.maxAge) {
            std::filesystem::remove(file.path, ec);
            continue;
        }
        total += file.size;
    }

    if (total > limits_.maxBytes) {
        std::sort(files.begin(), files.end(), [](const CacheFile& a, const CacheFile& b) {
            return a.written < b.written;
        });
        for (const auto& file : files) {
            if (total <= limits_.maxBytes) break;
            if (!std::filesystem::exists(file.path, ec)) continue;   // removido pela idade
            std::filesystem::remove(file.path, ec);
            total -= std::min(total, file.size);
        }
    }

    // Remove diretorios de execucao vazios, mantendo a raiz do projeto.
    for (std::filesystem::directory_iterator it(projectDir_, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        if (std::filesystem::is_empty(it->path(), ec)) std::filesystem::remove(it->path(), ec);
    }
}

} // namespace compress
} // namespace rt
} // namespace taskforge
