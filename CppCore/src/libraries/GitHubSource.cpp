#include "libraries/GitHubSource.hpp"

#include "rt/Http.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <regex>
#include <string>

namespace taskforge {
namespace libraries {
namespace {

using taskforge::Json;

constexpr long long kMaxManifestBytes = 8LL * 1024 * 1024;
constexpr long long kMaxSkillBytes = 256LL * 1024;
constexpr long long kMaxMcpBytes = 64LL * 1024;
constexpr size_t kMaxAssets = 1000;
constexpr auto kCacheTtl = std::chrono::minutes(5);

struct CacheEntry {
    std::chrono::steady_clock::time_point at;
    rt::HttpResponse response;
};

std::mutex gCacheMutex;
std::map<std::string, CacheEntry> gCache;

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size()
        && value.compare(0, prefix.size(), prefix) == 0;
}

bool isLoopbackUrl(const std::string& url) {
    static const std::regex pattern(
        R"(^http://(?:127\.0\.0\.1|localhost):[0-9]{1,5}(?:/|$))",
        std::regex::icase | std::regex::optimize);
    return std::regex_search(url, pattern);
}

std::string trimTrailingSlash(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

std::string testBase(const char* key) {
    const char* mode = std::getenv("ARESTRIX_TEST_MODE");
    if (!mode || std::string(mode) != "1") return {};
    const char* value = std::getenv(key);
    if (!value || !*value) return {};
    const std::string base = trimTrailingSlash(value);
    return isLoopbackUrl(base + "/") ? base : std::string{};
}

std::string apiBase() {
    const std::string test = testBase("ARESTRIX_TEST_GITHUB_API_BASE");
    return test.empty() ? "https://api.github.com" : test;
}

std::string rawBase() {
    const std::string test = testBase("ARESTRIX_TEST_GITHUB_RAW_BASE");
    return test.empty() ? "https://raw.githubusercontent.com" : test;
}

std::string githubToken() {
    if (const char* token = std::getenv("ARESTRIX_GITHUB_TOKEN"); token && *token) {
        return token;
    }
    if (const char* token = std::getenv("GITHUB_TOKEN"); token && *token) {
        return token;
    }
    return {};
}

std::string errorJson(const std::string& code, const std::string& message) {
    return Json{{"ok", false}, {"errorCode", code}, {"error", message}}.dump();
}

std::string statusErrorCode(int status, const std::string& body) {
    if (status == 0) return "github_network";
    if (status == 401) return "github_unauthorized";
    if (status == 403 && body.find("rate limit") != std::string::npos) return "github_rate_limited";
    if (status == 404) return "github_not_found";
    if (status == 413) return "github_too_large";
    if (status == 422) return "github_invalid_request";
    return "github_http_error";
}

rt::HttpResponse httpGet(
    const std::string& url,
    const std::string& accept,
    const std::string& token,
    int timeoutSeconds
) {
    rt::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.timeoutSeconds = timeoutSeconds;
    request.noRedirects = true;
    request.requirePublicPeer = !isLoopbackUrl(url);
    request.headers["Accept"] = accept;
    request.headers["User-Agent"] = "Arestrix-Studio/1.0";
    request.headers["X-GitHub-Api-Version"] = "2022-11-28";
    if (!token.empty()) {
        request.headers["Authorization"] = "Bearer " + token;
    }
    return rt::httpSend(request);
}

rt::HttpResponse getCached(
    const std::string& url,
    const std::string& accept,
    const std::string& token,
    int timeoutSeconds
) {
    const std::string key = url + (token.empty() ? "|anon" : "|auth");
    const auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        auto it = gCache.find(key);
        if (it != gCache.end() && now - it->second.at < kCacheTtl) {
            return it->second.response;
        }
        if (it != gCache.end()) {
            gCache.erase(it);
        }
    }

    rt::HttpResponse response = httpGet(url, accept, token, timeoutSeconds);
    if (response.status == 200) {
        std::lock_guard<std::mutex> lock(gCacheMutex);
        if (gCache.size() >= 256) {
            gCache.clear();
        }
        gCache[key] = CacheEntry{now, response};
    }
    return response;
}

std::string resolveCommit(const std::string& repo, const std::string& ref, const std::string& token) {
    const std::string url = apiBase() + "/repos/" + repo + "/commits/" + ref;
    const rt::HttpResponse response = getCached(url, "application/vnd.github+json", token, 20);
    if (response.status != 200) {
        return {};
    }
    try {
        const Json json = taskforge::jsonutil::parse(response.body);
        const std::string sha = json.value("sha", "");
        return isValidCommit(sha) ? sha : std::string{};
    } catch (...) {
        return {};
    }
}

} // namespace

bool isValidRepo(const std::string& repo) {
    if (repo.size() < 3 || repo.size() > 220) return false;
    if (repo.find("..") != std::string::npos) return false;
    if (repo.front() == '.' || repo.front() == '/' || repo.front() == '-') return false;
    if (repo.back() == '.' || repo.back() == '/' || repo.back() == '-') return false;
    static const std::regex pattern(R"(^[A-Za-z0-9_.-]{1,100}/[A-Za-z0-9_.-]{1,100}$)", std::regex::optimize);
    return std::regex_match(repo, pattern);
}

bool isValidRef(const std::string& ref) {
    if (ref.empty() || ref.size() > 200) return false;
    if (ref.find("..") != std::string::npos) return false;
    if (ref.find('\\') != std::string::npos) return false;
    if (ref.find("//") != std::string::npos) return false;
    if (ref.front() == '/') return false;
    static const std::regex pattern(R"(^[A-Za-z0-9_./-]+$)", std::regex::optimize);
    return std::regex_match(ref, pattern);
}

bool isValidCommit(const std::string& commit) {
    if (commit.size() < 7 || commit.size() > 40) return false;
    static const std::regex pattern(R"(^[0-9a-f]+$)", std::regex::optimize);
    return std::regex_match(commit, pattern);
}

bool isValidAssetPath(const std::string& path) {
    if (path.empty() || path.size() > 4096) return false;
    if (path.front() == '/') return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.find('\\') != std::string::npos) return false;
    if (path.find('\0') != std::string::npos) return false;
    if (path.back() == '/') return false;
    static const std::regex pattern(R"(^[A-Za-z0-9_.\-/]+$)", std::regex::optimize);
    return std::regex_match(path, pattern);
}

std::string classifyAssetKind(const std::string& path) {
    if (!isValidAssetPath(path)) return {};
    if (endsWith(path, "SKILL.md")) return "skill";
    if (endsWith(path, ".json")
        && (path.find("/mcps/") != std::string::npos
            || startsWith(path, "mcps/")
            || endsWith(path, "mcp.json"))) {
        return "mcp";
    }
    return {};
}

std::string deriveAssetName(const std::string& path, const std::string& repo) {
    const size_t slash = path.find_last_of('/');
    const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);

    if (endsWith(filename, "SKILL.md")) {
        const std::string parent = slash == std::string::npos ? std::string{} : path.substr(0, slash);
        const size_t parentSlash = parent.find_last_of('/');
        const std::string folder = parentSlash == std::string::npos
            ? parent
            : parent.substr(parentSlash + 1);
        if (!folder.empty() && folder != "skills") return folder;
    }

    if (endsWith(filename, ".json")) {
        return filename.substr(0, filename.size() - 5);
    }

    return repo;
}

std::string fetchLibrarySourceJson(const std::string& requestJson) {
    try {
        if (requestJson.empty()) {
            return errorJson("github_invalid_request", "Payload vazio.");
        }

        const Json request = taskforge::jsonutil::parse(requestJson);
        const std::string mode = request.value("mode", "manifest");
        const std::string repo = request.value("repo", "");
        const std::string ref = request.value("ref", "main");
        const std::string token = githubToken();

        if (!isValidRepo(repo)) {
            return errorJson("github_invalid_request", "Repositório inválido. Use o formato organizacao/projeto.");
        }
        if (!isValidRef(ref)) {
            return errorJson("github_invalid_request", "Referência (branch/tag) inválida.");
        }
        if (mode != "manifest" && mode != "asset") {
            return errorJson("github_invalid_request", "Modo inválido. Use manifest ou asset.");
        }

        if (mode == "asset") {
            const std::string path = request.value("path", "");
            std::string commit = request.value("commit", "");
            if (!isValidAssetPath(path)) {
                return errorJson("github_invalid_request", "Caminho de asset inválido.");
            }
            const std::string kind = classifyAssetKind(path);
            if (kind.empty()) {
                return errorJson("github_invalid_request", "Somente SKILL.md e configurações de MCP são aceitos.");
            }
            if (commit.empty()) {
                commit = resolveCommit(repo, ref, token);
            }
            if (!isValidCommit(commit)) {
                return errorJson("github_not_found", "Não foi possível resolver o commit da referência.");
            }

            const long long limit = kind == "skill" ? kMaxSkillBytes : kMaxMcpBytes;
            const std::string url = rawBase() + "/" + repo + "/" + commit + "/" + path;
            const rt::HttpResponse response = getCached(url, "text/plain", token, 20);
            if (response.status != 200) {
                return errorJson(
                    statusErrorCode(response.status, response.body),
                    response.error.empty() ? "Falha ao baixar o asset no GitHub." : "Falha de rede ao baixar o asset."
                );
            }
            if (static_cast<long long>(response.body.size()) > limit) {
                return errorJson("github_too_large", "O asset excede o limite permitido.");
            }
            if (response.body.empty()) {
                return errorJson("github_invalid_request", "O asset está vazio.");
            }

            Json out;
            out["ok"] = true;
            out["repo"] = repo;
            out["ref"] = ref;
            out["commit"] = commit;
            out["path"] = path;
            out["kind"] = kind;
            out["content"] = response.body;
            out["size"] = static_cast<long long>(response.body.size());
            return out.dump();
        }

        const std::string repoUrl = apiBase() + "/repos/" + repo;
        const rt::HttpResponse repoResponse = getCached(repoUrl, "application/vnd.github+json", token, 20);
        if (repoResponse.status != 200) {
            return errorJson(
                statusErrorCode(repoResponse.status, repoResponse.body),
                repoResponse.error.empty() ? "Falha ao consultar o repositório no GitHub." : "Falha de rede ao consultar o GitHub."
            );
        }
        if (static_cast<long long>(repoResponse.body.size()) > kMaxManifestBytes) {
            return errorJson("github_too_large", "Resposta do repositório acima do limite.");
        }

        const Json repoJson = taskforge::jsonutil::parse(repoResponse.body);
        const std::string defaultBranch = repoJson.value("default_branch", "main");
        const bool isPrivate = repoJson.value("private", false);
        const bool archived = repoJson.value("archived", false);
        std::string license = "NOASSERTION";
        if (repoJson.contains("license") && repoJson["license"].contains("spdx_id")) {
            license = repoJson["license"].value("spdx_id", "NOASSERTION");
        }

        const std::string commit = resolveCommit(repo, ref, token);
        if (!isValidCommit(commit)) {
            return errorJson("github_not_found", "Não foi possível resolver o commit da referência.");
        }

        const std::string treeUrl = apiBase() + "/repos/" + repo + "/git/trees/" + commit + "?recursive=1";
        const rt::HttpResponse treeResponse = getCached(treeUrl, "application/vnd.github+json", token, 30);
        if (treeResponse.status != 200) {
            return errorJson(
                statusErrorCode(treeResponse.status, treeResponse.body),
                "Falha ao ler a árvore do repositório."
            );
        }
        if (static_cast<long long>(treeResponse.body.size()) > kMaxManifestBytes) {
            return errorJson("github_too_large", "Árvore do repositório acima do limite.");
        }

        const Json treeJson = taskforge::jsonutil::parse(treeResponse.body);
        const bool truncated = treeJson.value("truncated", false);
        Json assets = Json::array();
        if (treeJson.contains("tree")) {
            for (const auto& item : treeJson["tree"]) {
                const std::string path = item.value("path", "");
                const std::string kind = classifyAssetKind(path);
                if (kind.empty()) continue;
                if (item.value("type", "") != "blob") continue;

                const long long size = item.value("size", 0);
                const long long limit = kind == "skill" ? kMaxSkillBytes : kMaxMcpBytes;
                if (size <= 0 || size > limit) continue;
                if (assets.size() >= kMaxAssets) break;

                Json asset;
                asset["path"] = path;
                asset["sha"] = item.value("sha", "");
                asset["size"] = size;
                asset["kind"] = kind;
                asset["name"] = deriveAssetName(path, repo);
                assets.push_back(asset);
            }
        }

        Json out;
        out["ok"] = true;
        out["repo"] = repo;
        out["ref"] = ref;
        out["commit"] = commit;
        out["license"] = license;
        out["defaultBranch"] = defaultBranch;
        out["private"] = isPrivate;
        out["archived"] = archived;
        out["truncated"] = truncated;
        out["assets"] = assets;
        return out.dump();
    } catch (const std::exception&) {
        return errorJson("github_internal", "Falha interna ao consultar o GitHub.");
    } catch (...) {
        return errorJson("github_internal", "Falha interna ao consultar o GitHub.");
    }
}

} // namespace libraries
} // namespace taskforge
