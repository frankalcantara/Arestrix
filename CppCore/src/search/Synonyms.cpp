#include "search/Synonyms.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#include "search/ProjectSearch.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace search {

namespace {

constexpr size_t kMaxGroups = 200;
constexpr size_t kMaxGroupSize = 14;
constexpr size_t kMaxWordBytes = 40;

std::mutex g_mutex;
std::string g_testDir;
SynonymGroups g_installed;
bool g_loaded = false;

bool validLocale(const std::string& l) {
    if (l.empty() || l.size() > 16) return false;
    for (char c : l)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;
}

std::string isoNow() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

std::string synonymDir() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_testDir.empty()) return g_testDir;
    }
    fs::path base;
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    base = appdata && *appdata ? fs::path(appdata) / "Arestrix" : fs::path(".") / ".arestrix-user";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && *xdg) base = fs::path(xdg) / "arestrix";
    else if (home && *home) base = fs::path(home) / ".config" / "arestrix";
    else base = fs::path(".") / ".arestrix-user";
#endif
    return (base / "synonyms").string();
}

void setSynonymDirForTesting(const std::string& dir) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_testDir = dir;
    }
    reloadSynonyms();
}

bool hasSynonymFile(const std::string& locale) {
    if (!validLocale(locale)) return false;
    std::error_code ec;
    return fs::is_regular_file(fs::path(synonymDir()) / (locale + ".json"), ec);
}

const std::vector<std::string>& synonymSeedConcepts() {
    static const std::vector<std::string> seeds = {
        "agent", "vertex", "node", "edge", "graph", "pipeline", "subflow", "module", "task", "queue",
        "database", "memory", "tool", "skill", "prompt", "model", "validate", "error", "failure", "warning",
        "run", "execute", "build", "compile", "file", "folder", "image", "vision", "parallel", "concurrent",
        "fork", "join", "condition", "decision", "branch", "loop", "feedback", "retry", "limit", "consensus",
        "summary", "report", "review", "fix", "correct", "generate", "create", "delete", "save", "load",
        "import", "export", "search", "settings", "configuration", "sandbox", "isolation", "network",
        "security", "key", "token", "cost", "test", "log", "output", "input", "result", "data", "text",
        "document", "table", "spreadsheet", "chart", "typography", "font", "color", "layout", "animation",
        "video", "audio", "schedule", "batch", "resume", "checkpoint", "server", "client", "project",
        "workspace", "template", "library", "plugin", "install", "update", "version"};
    return seeds;
}

std::string synonymSystemPrompt() {
    return "You build search-synonym dictionaries for a software tool. You answer with a single JSON object and "
           "nothing else: no explanations, no markdown.";
}

std::string synonymUserPrompt(const std::string& locale) {
    std::ostringstream o;
    o << "Target language: the language with BCP-47 code \"" << locale << "\".\n"
      << "The tool is a visual editor of graphs of AI agents (vertices, edges, pipelines, subflows, queues, "
         "databases, memory, tools, skills, prompts, models, sandboxes, validation, execution).\n\n"
      << "For EACH reference concept below, return one group of lowercase single words that a user of the tool "
         "might type to look for it, in the target language: the common translations and the closest synonyms "
         "used in software or business contexts (at most 8 words in the target language). ALWAYS put the English "
         "reference concept itself first in the group. Words only: no spaces, no punctuation, no phrases.\n\n"
      << "Reference concepts:\n";
    const auto& seeds = synonymSeedConcepts();
    for (size_t i = 0; i < seeds.size(); ++i) o << (i ? ", " : "") << seeds[i];
    o << "\n\nReturn exactly this JSON shape:\n{\"groups\": [[\"queue\", \"...\", \"...\"], [\"database\", \"...\"]]}\n";
    return o.str();
}

SynonymGroups parseSynonymResponse(const std::string& text) {
    SynonymGroups out;
    size_t a = text.find('{');
    size_t b = text.rfind('}');
    if (a == std::string::npos || b == std::string::npos || b <= a) return out;
    auto parsed = jsonutil::tryParse(std::string_view(text).substr(a, b - a + 1));
    if (!parsed || !parsed->is_object()) return out;
    const Json& groups = (*parsed)["groups"];
    if (!groups.is_array()) return out;

    for (size_t i = 0; i < groups.size() && out.size() < kMaxGroups; ++i) {
        const Json& g = groups[i];
        if (!g.is_array()) continue;
        std::vector<std::string> words;
        std::set<std::string> seen;
        for (size_t k = 0; k < g.size() && words.size() < kMaxGroupSize; ++k) {
            if (!g[k].is_string()) continue;
            std::string w = lower(g[k].str());
            // so palavras simples: sem espaco, sem pontuacao (acentos e letras nao latinas sao aceitos)
            bool ok = !w.empty() && w.size() <= kMaxWordBytes;
            for (unsigned char c : w) {
                if (c < 0x80 && !(std::isalnum(c) || c == '-' || c == '_')) { ok = false; break; }
            }
            if (!ok) continue;
            if (tokenize(w).size() != 1) continue; // stopword ou lixo
            if (seen.insert(w).second) words.push_back(w);
        }
        if (words.size() >= 2) out.push_back(std::move(words));
    }
    return out;
}

bool writeSynonymFile(const std::string& locale, const std::string& modelName, const SynonymGroups& groups,
                      std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (!validLocale(locale)) return fail("codigo de idioma invalido");
    if (groups.empty()) return fail("nenhum grupo valido");
    try {
        Json j;
        j["version"] = 1;
        j["locale"] = locale;
        j["model"] = modelName;
        j["generatedAt"] = isoNow();
        Json gs = Json::array();
        for (const auto& g : groups) {
            Json arr = Json::array();
            for (const auto& w : g) arr.push_back(w);
            gs.push_back(arr);
        }
        j["groups"] = gs;

        fs::path dir = synonymDir();
        fs::create_directories(dir);
        fs::path tmp = dir / (locale + ".json.tmp");
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return fail("nao foi possivel gravar em " + dir.string());
            out << j.dump(2);
        }
        std::error_code ec;
        fs::path dst = dir / (locale + ".json");
        fs::remove(dst, ec);
        fs::rename(tmp, dst, ec);
        if (ec) return fail(ec.message());
    } catch (const std::exception& ex) {
        return fail(ex.what());
    }
    reloadSynonyms();
    return true;
}

void reloadSynonyms() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_loaded = false;
    g_installed.clear();
}

SynonymGroups installedSynonymGroups() {
    std::string dir = synonymDir(); // fora do lock: synonymDir() tambem o usa
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loaded) return g_installed;
    g_installed.clear();
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
            try {
                auto parsed = jsonutil::tryParse(jsonutil::readFile(it->path()));
                if (!parsed || parsed->value("version", 0) != 1) continue;
                const Json& groups = (*parsed)["groups"];
                for (size_t i = 0; i < groups.size(); ++i) {
                    std::vector<std::string> norm;
                    for (size_t k = 0; k < groups[i].size(); ++k) {
                        if (!groups[i][k].is_string()) continue;
                        auto t = tokenize(groups[i][k].str());
                        if (t.size() == 1) norm.push_back(t[0]);
                    }
                    if (norm.size() >= 2) g_installed.push_back(std::move(norm));
                }
            } catch (...) {
                // arquivo corrompido: ignora
            }
        }
    }
    g_loaded = true;
    return g_installed;
}

} // namespace search
} // namespace taskforge
