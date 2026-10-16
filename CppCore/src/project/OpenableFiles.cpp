#include "project/OpenableFiles.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace taskforge {
namespace project {

namespace {

namespace fs = std::filesystem;

std::string lowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int64_t toEpochMs(fs::file_time_type ft) {
    using namespace std::chrono;
    auto sys = time_point_cast<system_clock::duration>(ft - fs::file_time_type::clock::now() + system_clock::now());
    return duration_cast<milliseconds>(sys.time_since_epoch()).count();
}

// Itens que o Explorer esconde ($Recycle.Bin, System Volume Information...) tambem ficam de fora.
bool isHiddenOrSystem(const fs::path& p) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
    (void)p;
    return false;
#endif
}

struct Entry {
    std::string name;
    std::string sortKey;
    fs::path path;
    const char* kind;
    int64_t modified;
    bool isDir;
};

} // namespace

fs::path pathFromUtf8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const fs::path& p) {
    auto u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

bool isProjectDirectory(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "arestrix.project.json", ec) || fs::is_regular_file(dir / "taskforge.project.json", ec);
}

Json listOpenableEntries(const fs::path& requested, std::size_t limit) {
    std::error_code ec;
    fs::path dir = requested.empty() ? fs::current_path(ec) : requested;
    dir = fs::absolute(dir, ec).lexically_normal();
    // "C:/Projetos/" vira "C:/Projetos"; a raiz da unidade ("C:/") fica como esta.
    if (!dir.has_filename() && dir != dir.root_path()) dir = dir.parent_path();

    Json out = Json::object();
    out["path"] = pathToUtf8(dir);
    out["parent"] = (dir == dir.root_path()) ? std::string() : pathToUtf8(dir.parent_path());
    out["truncated"] = false;
    bool exists = fs::is_directory(dir, ec);
    out["exists"] = exists;

    std::vector<Entry> found;
    if (exists) {
        fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
            const auto& entry = *it;
            auto u8name = entry.path().filename().u8string();
            std::string name(u8name.begin(), u8name.end());
            if (name.empty() || name[0] == '.' || isHiddenOrSystem(entry.path())) continue;

            std::error_code entryEc;
            bool isDir = entry.is_directory(entryEc);
            const char* kind = nullptr;
            if (isDir) {
                kind = isProjectDirectory(entry.path()) ? "project" : "folder";
            } else if (entry.is_regular_file(entryEc)) {
                std::string lower = lowerAscii(name);
                if (lower.ends_with(".arestrix")) kind = "package";
                else if (lower == "arestrix.project.json" || lower == "taskforge.project.json") kind = "manifest";
                else if (lower.ends_with(".json")) kind = "json";
            }
            if (!kind) continue;

            auto ft = entry.last_write_time(entryEc);
            found.push_back({name, lowerAscii(name), entry.path(), kind, entryEc ? 0 : toEpochMs(ft), isDir});
        }
    }

    std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return a.sortKey < b.sortKey;
    });
    if (found.size() > limit) {
        found.resize(limit);
        out["truncated"] = true;
    }

    Json entries = Json::array();
    for (const auto& e : found) {
        Json item = Json::object();
        item["name"] = e.name;
        item["path"] = pathToUtf8(e.path);
        item["kind"] = e.kind;
        item["modified"] = e.modified;
        entries.push_back(std::move(item));
    }
    out["entries"] = std::move(entries);
    return out;
}

} // namespace project
} // namespace taskforge
