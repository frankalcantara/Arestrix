#include "libraries/LibraryCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include "platform/Process.hpp"
#include "project/RunnerSecurity.hpp"
#include "rt/RuntimeLang.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace libraries {

namespace {

namespace fs = std::filesystem;
using rt::langPick;
using rt::RuntimeLang;

constexpr int kDefaultLimit = 40;
constexpr int kMaxLimit = 100;
constexpr size_t kMaxDescription = 320;
constexpr size_t kMaxMarkdown = 6000;
constexpr size_t kMaxPrompt = 3000;

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string jsonString(const Json& j, const char* key) {
    return j.is_object() ? j.value(key, std::string()) : std::string();
}

std::string cutText(const std::string& text, size_t maxChars) {
    if (text.size() <= maxChars) return text;
    return text.substr(0, maxChars) + "\n[... truncado ...]";
}

void replaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string jsonScalar(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return "";
    return value.dump();
}

std::string applyTemplate(std::string body, const std::string& label, const Json& parameters) {
    replaceAll(body, "${label}", label);
    if (parameters.is_object()) {
        for (auto it = parameters.begin(); it != parameters.end(); ++it) {
            replaceAll(body, "${params." + it.key() + "}", jsonScalar(it.value()));
        }
    }
    return body;
}

fs::path resolveBundledCatalog() {
    std::error_code ec;
    if (const char* env = std::getenv("ARESTRIX_LIBRARY_CATALOG"); env && *env) {
        fs::path candidate(env);
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }

    static const char* relatives[] = {
        "src/core/libraries/catalog/bundledAgentVertices.json",
        "CppCore/resources/bundledAgentVertices.json",
        "CppCore/dist/resources/bundledAgentVertices.json",
        "dist/resources/bundledAgentVertices.json",
        "resources/bundledAgentVertices.json"
    };

    std::vector<fs::path> bases;
    bases.push_back(fs::current_path(ec));
    const fs::path exe = platform::executablePath();
    if (!exe.empty()) bases.push_back(exe.parent_path());

    for (const auto& base : bases) {
        if (base.empty()) continue;
        for (const char* rel : relatives) {
            fs::path candidate = base / rel;
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
        fs::path parent = base.parent_path();
        if (!parent.empty() && parent != base) {
            for (const char* rel : relatives) {
                fs::path candidate = parent / rel;
                if (fs::is_regular_file(candidate, ec)) return candidate;
            }
        }
    }
    return {};
}

const fs::path& bundledPath() {
    static const fs::path path = resolveBundledCatalog();
    return path;
}

const Json& bundledRoot() {
    static const Json root = []() {
        Json parsed;
        const fs::path& path = bundledPath();
        if (path.empty()) return parsed;
        auto candidate = jsonutil::tryParse(jsonutil::readFile(path));
        if (candidate && candidate->is_object()) parsed = *candidate;
        return parsed;
    }();
    return root;
}

fs::path resolveBundledMcpCatalog() {
    std::error_code ec;
    if (const char* env = std::getenv("ARESTRIX_MCP_CATALOG"); env && *env) {
        fs::path candidate(env);
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }

    static const char* relatives[] = {
        "src/core/libraries/catalog/bundledMcpServers.json",
        "CppCore/resources/bundledMcpServers.json",
        "CppCore/dist/resources/bundledMcpServers.json",
        "dist/resources/bundledMcpServers.json",
        "resources/bundledMcpServers.json"
    };

    std::vector<fs::path> bases;
    bases.push_back(fs::current_path(ec));
    const fs::path exe = platform::executablePath();
    if (!exe.empty()) bases.push_back(exe.parent_path());

    for (const auto& base : bases) {
        if (base.empty()) continue;
        for (const char* rel : relatives) {
            fs::path candidate = base / rel;
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
        fs::path parent = base.parent_path();
        if (!parent.empty() && parent != base) {
            for (const char* rel : relatives) {
                fs::path candidate = parent / rel;
                if (fs::is_regular_file(candidate, ec)) return candidate;
            }
        }
    }
    return {};
}

const Json& bundledMcpRoot() {
    static const Json root = []() {
        Json parsed;
        const fs::path path = resolveBundledMcpCatalog();
        if (path.empty()) return parsed;
        auto candidate = jsonutil::tryParse(jsonutil::readFile(path));
        if (candidate && candidate->is_object()) parsed = *candidate;
        return parsed;
    }();
    return root;
}

std::vector<Json> makeBundledAssets() {
    std::vector<Json> assets;
    const Json& root = bundledRoot();
    if (root.is_object() && root.contains("items") && root.at("items").is_array()) {
        const std::string generatedAt = root.value("generatedAt", std::string());
        for (const auto& entry : root.at("items").arr()) {
            if (!entry.is_object()) continue;
            const std::string id = entry.value("id", std::string());
            if (id.empty() || !entry.contains("template") || !entry.at("template").is_object()) continue;
            const Json& tpl = entry.at("template");

            const Json& origin = entry.contains("origin") && entry.at("origin").is_object()
                                     ? entry.at("origin") : Json();
            const std::string kind = tpl.value("assetKind", std::string()) == "pipeline"
                                         ? "pipeline" : "agent-template";
            std::string version = origin.is_object() ? origin.value("installVersion", std::string()) : std::string();
            if (version.empty()) version = tpl.value("version", std::string());
            if (version.empty()) version = "1.0.0";
            const std::string hash = origin.is_object() ? origin.value("installHash", std::string()) : std::string();

            Json asset = Json::object();
            asset["id"] = id;
            asset["libraryId"] = "library-arestrix";
            asset["libraryName"] = "Biblioteca Arestrix";
            asset["kind"] = kind;
            asset["name"] = entry.value("name", std::string());
            asset["description"] = entry.value("description", std::string());
            asset["category"] = entry.value("category", std::string("Agentes"));
            if (entry.contains("domain")) asset["domain"] = entry.at("domain");
            if (entry.contains("subdomain")) asset["subdomain"] = entry.at("subdomain");
            if (entry.contains("icon")) asset["icon"] = entry.at("icon");
            asset["bundledInProduct"] = true;
            asset["version"] = version;
            asset["license"] = entry.value("license", std::string());
            asset["status"] = "available";
            asset["origin"] = origin;
            asset["provenance"] = Json{{"sourceKind", "builtin"},
                                       {"sourceId", "library-arestrix"},
                                       {"importedAt", generatedAt}};
            asset["audit"] = Json{{"verdict", "APPROVE"},
                                  {"riskScore", 0},
                                  {"policyVersion", "bundled-catalog-v1"},
                                  {"hash", hash},
                                  {"findings", Json::array()},
                                  {"warnings", Json::array()}};
            asset["payload"] = Json{{"kind", kind}, {"template", tpl}};
            asset["createdAt"] = generatedAt;
            asset["updatedAt"] = generatedAt;
            assets.push_back(std::move(asset));
        }
    }

    const Json& mcpRoot = bundledMcpRoot();
    if (mcpRoot.is_object() && mcpRoot.contains("items") && mcpRoot.at("items").is_array()) {
        const std::string generatedAt = mcpRoot.value("generatedAt", std::string());
        for (const auto& entry : mcpRoot.at("items").arr()) {
            if (!entry.is_object()) continue;
            const std::string id = entry.value("id", std::string());
            if (id.empty() || !entry.contains("payload") || !entry.at("payload").is_object()) continue;

            const Json& origin = entry.contains("origin") && entry.at("origin").is_object()
                                     ? entry.at("origin") : Json();
            const Json& entryAudit = entry.contains("audit") && entry.at("audit").is_object()
                                         ? entry.at("audit") : Json();
            const std::string hash = origin.is_object() ? origin.value("installHash", std::string())
                                                        : entryAudit.value("hash", std::string());

            Json asset = Json::object();
            asset["id"] = id;
            asset["libraryId"] = "library-arestrix";
            asset["libraryName"] = "Biblioteca Arestrix";
            asset["kind"] = "mcp";
            asset["name"] = entry.value("name", std::string());
            asset["description"] = entry.value("description", std::string());
            asset["category"] = entry.value("category", std::string("MCP"));
            if (entry.contains("domain")) asset["domain"] = entry.at("domain");
            if (entry.contains("subdomain")) asset["subdomain"] = entry.at("subdomain");
            if (entry.contains("icon")) asset["icon"] = entry.at("icon");
            asset["bundledInProduct"] = true;
            asset["version"] = entry.value("version", std::string("1.0.0"));
            asset["license"] = entry.value("license", std::string("MIT"));
            asset["status"] = "available";
            asset["origin"] = origin;
            asset["provenance"] = Json{{"sourceKind", "builtin"},
                                       {"sourceId", "library-arestrix"},
                                       {"importedAt", generatedAt}};
            asset["audit"] = Json{
                {"verdict", entryAudit.is_object() ? entryAudit.value("verdict", std::string("APPROVE")) : std::string("APPROVE")},
                {"riskScore", entryAudit.is_object() ? entryAudit.value("riskScore", 0) : 0},
                {"policyVersion", entryAudit.is_object() ? entryAudit.value("policyVersion", std::string("mcp-scanner-v1")) : std::string("mcp-scanner-v1")},
                {"hash", hash},
                {"findings", entryAudit.is_object() && entryAudit.contains("findings") ? entryAudit.at("findings") : Json::array()},
                {"warnings", entryAudit.is_object() && entryAudit.contains("warnings") ? entryAudit.at("warnings") : Json::array()}
            };
            asset["payload"] = entry.at("payload");
            asset["createdAt"] = generatedAt;
            asset["updatedAt"] = generatedAt;
            assets.push_back(std::move(asset));
        }
    }
    return assets;
}

const std::vector<Json>& bundledAssets() {
    static const std::vector<Json> assets = makeBundledAssets();
    return assets;
}

bool hasRealPayload(const Json& asset) {
    if (!asset.is_object() || !asset.contains("payload") || !asset.at("payload").is_object()) return false;
    const Json& payload = asset.at("payload");
    const std::string kind = payload.value("kind", std::string());
    if (kind == "agent-template" || kind == "pipeline") {
        if (!payload.contains("template") || !payload.at("template").is_object()) return false;
        const Json& tpl = payload.at("template");
        if (tpl.contains("agentSettings") && tpl.at("agentSettings").is_object()) return true;
        if (tpl.contains("childGraph") && tpl.at("childGraph").is_object()) return true;
        if (!tpl.value("lambdaBodyTemplate", std::string()).empty()) return true;
        const Json& inputs = tpl.contains("inputs") ? tpl.at("inputs") : Json();
        const Json& outputs = tpl.contains("outputs") ? tpl.at("outputs") : Json();
        return inputs.is_array() && !inputs.empty() && outputs.is_array() && !outputs.empty();
    }
    if (kind == "skill") {
        return !payload.value("markdownContent", std::string()).empty() ||
               !payload.value("systemPrompt", std::string()).empty();
    }
    if (kind == "mcp") {
        return !payload.value("command", std::string()).empty() ||
               !payload.value("url", std::string()).empty() ||
               (payload.contains("cachedTools") && payload.at("cachedTools").is_array() &&
                !payload.at("cachedTools").empty());
    }
    return true;
}

bool matchesKind(const Json& asset, const std::string& kind) {
    return kind.empty() || asset.value("kind", std::string()) == kind;
}

bool matchesText(const Json& asset, const std::string& needle) {
    if (needle.empty()) return true;
    for (const char* field : {"name", "description", "category", "domain", "subdomain"}) {
        if (lower(asset.value(field, std::string())).find(needle) != std::string::npos) return true;
    }
    return false;
}

Json nodeLibrarySource(const Json& asset) {
    Json source = Json::object();
    source["libraryId"] = asset.value("libraryId", std::string());
    source["assetId"] = asset.value("id", std::string());
    source["version"] = asset.value("version", std::string());
    source["bundledInProduct"] = asset.value("bundledInProduct", false);
    if (asset.contains("audit") && asset.at("audit").is_object()) {
        source["hash"] = asset.at("audit").value("hash", std::string());
    }
    if (asset.contains("origin") && asset.at("origin").is_object()) {
        const Json& origin = asset.at("origin");
        source["originRepo"] = origin.value("repo", std::string());
        source["originUrl"] = origin.value("url", std::string());
        source["originLicense"] = origin.value("license", std::string());
    }
    return source;
}

std::string nowIso() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string sha256Json(const Json& value) {
    return project::RunnerSecurity::sha256(value.dump());
}

bool isEnvReference(const std::string& raw) {
    std::string value = raw;
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.empty()) return true;
    if (value.size() < 4 || value.front() != '$' || value[1] != '{' || value.back() != '}') return false;
    if (!(std::isalpha(static_cast<unsigned char>(value[2])) || value[2] == '_')) return false;
    for (size_t i = 2; i + 1 < value.size(); ++i) {
        const char c = value[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

bool safeSegment(const std::string& id) {
    if (id.empty() || id.size() > 120 || id == "." || id == "..") return false;
    for (unsigned char c : id) {
        if (c < 0x20 || std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos) return false;
    }
    return true;
}

std::string base36(uint64_t value) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (value == 0) return "0";
    std::string out;
    while (value > 0) {
        out.push_back(digits[value % 36]);
        value /= 36;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string slugifyText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool dash = false;
    for (unsigned char c : text) {
        if (c < 0x80 && std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
            dash = false;
        } else if (c >= 0x80) {
            // Remove o byte de acento; "cópia" vira "copia".
        } else if (!out.empty() && !dash) {
            out.push_back('-');
            dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 32) out.resize(32);
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

Json makeAudit(const std::string& verdict, int riskScore, const std::string& policyVersion,
               const std::string& hash, const std::vector<std::string>& findings,
               const std::vector<std::string>& warnings) {
    Json audit = Json::object();
    audit["verdict"] = verdict;
    audit["riskScore"] = std::max(0, std::min(100, riskScore));
    audit["policyVersion"] = policyVersion;
    audit["auditedAt"] = nowIso();
    audit["hash"] = hash;
    Json findingList = Json::array();
    for (const auto& item : findings) findingList.push_back(item);
    Json warningList = Json::array();
    for (const auto& item : warnings) warningList.push_back(item);
    audit["findings"] = findingList;
    audit["warnings"] = warningList;
    return audit;
}

Json auditTemplateValue(const Json& tpl) {
    const std::string hash = sha256Json(tpl);
    if (!tpl.is_object()) {
        return makeAudit("REJECT", 100, "template-schema-v1", hash, {},
                         {"Esquema de template invalido."});
    }
    const std::string id = tpl.value("id", std::string());
    const std::string name = tpl.value("name", std::string());
    const std::string taskType = tpl.value("taskType", std::string());
    if (id.empty() || name.empty() || taskType.empty() ||
        !tpl.contains("lambdaBodyTemplate") || !tpl.at("lambdaBodyTemplate").is_string() ||
        (tpl.contains("agentSettings") && !tpl.at("agentSettings").is_object()) ||
        (tpl.contains("childGraph") && !tpl.at("childGraph").is_object())) {
        return makeAudit("REJECT", 100, "template-schema-v1", hash, {},
                         {"Esquema invalido: id, name, taskType e lambdaBodyTemplate sao obrigatorios; "
                          "agentSettings e childGraph, quando presentes, devem ser objetos."});
    }

    const std::string body = tpl.value("lambdaBodyTemplate", std::string());
    if (body.size() > 200000) {
        return makeAudit("REJECT", 90, "template-schema-v1", hash, {},
                         {"Corpo C++ do template excede o limite de 200 KB."});
    }

    std::vector<std::string> findings;
    std::vector<std::string> warnings;
    int risk = 0;
    findings.push_back("Esquema basico do template valido.");
    findings.push_back("Tipo de tarefa declarado: " + taskType + ".");
    findings.push_back("Corpo C++ tratado como texto; o template nao e executado durante a auditoria.");
    if (tpl.contains("agentSettings")) {
        findings.push_back("Configuracao de agente presente; prompt, skills, memoria e ferramentas sao declarativos.");
    }
    if (tpl.contains("childGraph")) {
        findings.push_back("Pipeline composto presente; o grafo sera importado como subflow.");
    }

    static const std::regex processRe(
        R"(\b(?:system|popen|_popen|CreateProcess[AW]?|ShellExecute[AW]?|exec[lv]?p?e?)\s*\()",
        std::regex::icase);
    static const std::regex secretRe(
        R"(\b(?:password|passwd|secret|api[_-]?key|token)\s*[:=]\s*["'][^"']+["'])",
        std::regex::icase);
    if (std::regex_search(body, processRe)) {
        warnings.push_back("Template contem chamada de criacao/execucao de processo no corpo C++.");
        risk += 40;
    }
    const std::string settingsText = tpl.contains("agentSettings") ? tpl.at("agentSettings").dump() : std::string();
    if (std::regex_search(body, secretRe) || std::regex_search(settingsText, secretRe)) {
        warnings.push_back("Possivel segredo literal embutido no corpo ou na configuracao do agente.");
        risk += 30;
    }
    if (body.find("http://") != std::string::npos || body.find("https://") != std::string::npos) {
        warnings.push_back("Template referencia URL externa; revise o destino antes de compilar.");
        risk += 10;
    }

    Json hashPayload = Json::object();
    hashPayload["id"] = id;
    hashPayload["name"] = name;
    hashPayload["taskType"] = taskType;
    hashPayload["inputs"] = tpl.contains("inputs") ? tpl.at("inputs") : Json::array();
    hashPayload["outputs"] = tpl.contains("outputs") ? tpl.at("outputs") : Json::array();
    hashPayload["parameters"] = tpl.contains("parameters") ? tpl.at("parameters") : Json::object();
    hashPayload["requiredHeaders"] = tpl.contains("requiredHeaders") ? tpl.at("requiredHeaders") : Json::array();
    hashPayload["lambdaBodyTemplate"] = body;
    if (tpl.contains("agentSettings")) hashPayload["agentSettings"] = tpl.at("agentSettings");
    if (tpl.contains("childGraph")) hashPayload["childGraph"] = tpl.at("childGraph");

    const std::string verdict = warnings.empty() ? "APPROVE" : "CAUTION";
    return makeAudit(verdict, risk, "template-schema-v1", sha256Json(hashPayload), findings, warnings);
}

Json auditSkillValue(const Json& asset) {
    const Json& payload = asset.contains("payload") && asset.at("payload").is_object()
                              ? asset.at("payload") : Json();
    const std::string name = asset.value("name", std::string());
    const std::string license = asset.value("license", std::string());
    const std::string markdown = payload.value("markdownContent", std::string());
    const std::string prompt = payload.value("systemPrompt", std::string());
    const Json& tools = payload.contains("tools") && payload.at("tools").is_array()
                            ? payload.at("tools") : Json();

    std::vector<std::string> findings;
    std::vector<std::string> warnings;
    int risk = 0;

    if (name.size() >= 3) findings.push_back("Identificacao da habilidade consistente.");
    else { warnings.push_back("Nome da habilidade deve possuir pelo menos 3 caracteres."); risk += 15; }

    const std::string licenseUpper = [&]() {
        std::string out = license;
        for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    }();
    if (licenseUpper.find("MIT") != std::string::npos || licenseUpper.find("APACHE") != std::string::npos ||
        licenseUpper.find("BSD") != std::string::npos || licenseUpper.find("ISC") != std::string::npos ||
        licenseUpper.find("CC0") != std::string::npos) {
        findings.push_back("Licenca aberta declarada: " + license);
    } else {
        warnings.push_back("Habilidade sem licenca aberta explicitamente permissiva.");
        risk += 10;
    }

    const bool frontmatter = markdown.rfind("---", 0) == 0 && markdown.find("---", 3) != std::string::npos;
    if (frontmatter) findings.push_back("Frontmatter YAML canonico encontrado.");
    else { warnings.push_back("Frontmatter YAML ausente ou malformatado no SKILL.md."); risk += 10; }

    bool highRiskTool = false;
    if (tools.is_array()) {
        for (const auto& tool : tools.arr()) {
            if (!tool.is_string()) continue;
            std::string value = lower(tool.get<std::string>());
            if (value == "bash" || value == "terminal" || value == "exec" || value == "system" ||
                value == "root" || value == "shell" || value == "powershell" || value == "cmd") {
                highRiskTool = true;
            }
        }
    }
    if (highRiskTool) {
        warnings.push_back("A habilidade solicita execucao de terminal; exige sandbox isolado.");
        risk += 30;
    } else {
        findings.push_back("Ferramentas declaradas respeitam o perimetro padrao.");
    }

    const std::string content = markdown + "\n" + prompt;
    static const std::regex secretRe(
        R"(\b(?:password|passwd|secret|api[_-]?key|token)\s*[:=]\s*["'][^"']+["'])",
        std::regex::icase);
    static const std::regex destructiveRe(
        R"((rm\s+-rf|del\s+/[fsq]|format\s+[a-z]:|remove-item\s+-recurse))",
        std::regex::icase);
    static const std::regex exfilRe(
        R"(((curl|wget)[^\n|]*\|\s*(sh|bash))|invoke-webrequest|base64\s+-d)",
        std::regex::icase);
    static const std::regex injectionRe(
        R"(((ignore|disregard)\s+(all\s+)?(previous|prior)\s+instructions)|jailbreak)",
        std::regex::icase);
    if (std::regex_search(content, secretRe)) {
        warnings.push_back("Possivel segredo literal embutido na skill.");
        risk += 35;
    }
    if (std::regex_search(content, destructiveRe)) {
        warnings.push_back("Comando destrutivo de sistema detectado na skill.");
        risk += 30;
    }
    if (std::regex_search(content, exfilRe)) {
        warnings.push_back("Padrao de download/execucao ou exfiltracao detectado na skill.");
        risk += 30;
    }
    if (std::regex_search(content, injectionRe)) {
        warnings.push_back("Tentativa de injecao de instrucoes detectada na skill.");
        risk += 20;
    }
    if (content.size() > 200000) {
        warnings.push_back("Conteudo da skill excede 200 KB.");
        risk += 20;
    }

    Json hashPayload = Json::object();
    hashPayload["markdownContent"] = markdown;
    hashPayload["systemPrompt"] = prompt;
    hashPayload["tools"] = tools;
    const std::string hash = sha256Json(hashPayload);
    const std::string verdict = risk >= 70 ? "REJECT" : (risk >= 20 || !warnings.empty() ? "CAUTION" : "APPROVE");
    return makeAudit(verdict, risk, "arestrix-native-skill-v1", hash, findings, warnings);
}

Json auditMcpValue(const Json& asset) {
    const Json& payload = asset.contains("payload") && asset.at("payload").is_object()
                              ? asset.at("payload") : Json();
    const std::string command = payload.value("command", std::string());
    const std::string url = payload.value("url", std::string());
    const std::string transport = payload.value("transport", std::string("stdio"));
    std::vector<std::string> findings;
    std::vector<std::string> warnings;
    int risk = 0;

    if (command.empty() && url.empty()) {
        return makeAudit("REJECT", 100, "arestrix-native-mcp-v1", sha256Json(payload), {},
                         {"Servidor MCP sem command nem url."});
    }

    const std::string commandLower = lower(command);
    if (commandLower == "cmd" || commandLower == "cmd.exe" || commandLower == "powershell" ||
        commandLower == "powershell.exe" || commandLower == "bash" || commandLower == "sh") {
        warnings.push_back("Servidor MCP usa um shell como executavel; exige revisao de argumentos.");
        risk += 25;
    }

    if (payload.contains("args") && payload.at("args").is_array()) {
        bool checkedPackageArg = false;
        for (const auto& arg : payload.at("args").arr()) {
            if (!arg.is_string()) continue;
            const std::string value = arg.get<std::string>();
            if (value.find_first_of("&|;><`") != std::string::npos) {
                warnings.push_back("Argumento contem metacaractere de shell: " + value);
                risk += 35;
            }
            if ((commandLower == "npx" || commandLower == "uvx") && !checkedPackageArg &&
                !value.empty() && value.front() != '-') {
                checkedPackageArg = true;
                if (value.find('@') == std::string::npos) {
                    warnings.push_back("Pacote sem versao fixa em servidor MCP: " + value);
                    risk += 15;
                }
            }
        }
    }

    auto checkReferences = [&](const char* field) {
        if (!payload.contains(field) || !payload.at(field).is_object()) return;
        for (auto it = payload.at(field).begin(); it != payload.at(field).end(); ++it) {
            if (!it.value().is_string() || !isEnvReference(it.value().get<std::string>())) {
                warnings.push_back(std::string("Valor literal em ") + field + "." + it.key() +
                                   "; use referencias ${VAR}.");
                risk += 35;
            }
        }
    };
    checkReferences("env");
    checkReferences("headers");

    if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
        for (const auto& tool : payload.at("cachedTools").arr()) {
            if (!tool.is_object()) continue;
            const std::string description = tool.value("description", std::string());
            const std::string toolName = tool.value("name", std::string());
            static const std::regex injectionRe(
                R"(((ignore|disregard)\s+(all\s+)?(previous|prior)\s+instructions)|jailbreak)",
                std::regex::icase);
            if (std::regex_search(description, injectionRe)) {
                warnings.push_back("Descricao de ferramenta contem injecao de instrucoes: " + toolName);
                risk += 35;
            }
            if (toolName.rfind("library_", 0) == 0 || toolName.rfind("arestrix_", 0) == 0) {
                warnings.push_back("Ferramenta usa nome reservado do nucleo: " + toolName);
                risk += 20;
            }
        }
    }

    if (!url.empty() && url.rfind("http://", 0) == 0) {
        warnings.push_back("Servidor MCP usa HTTP sem TLS.");
        risk += 20;
    }

    findings.push_back("Configuracao do servidor validada.");
    findings.push_back("Transporte declarado: " + transport + ".");
    if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
        findings.push_back("Ferramentas declaradas: " + std::to_string(payload.at("cachedTools").size()) + ".");
    }

    const std::string verdict = risk >= 70 ? "REJECT" : (risk >= 20 || !warnings.empty() ? "CAUTION" : "APPROVE");
    return makeAudit(verdict, risk, "arestrix-native-mcp-v1", sha256Json(payload), findings, warnings);
}

Json auditAssetValue(const Json& asset) {
    const std::string kind = asset.value("kind", std::string());
    if (kind == "agent-template" || kind == "pipeline") {
        if (asset.contains("payload") && asset.at("payload").is_object() &&
            asset.at("payload").contains("template")) {
            return auditTemplateValue(asset.at("payload").at("template"));
        }
        return makeAudit("REJECT", 100, "template-schema-v1", sha256Json(asset), {},
                         {"Template ausente no payload."});
    }
    if (kind == "skill") return auditSkillValue(asset);
    if (kind == "mcp") return auditMcpValue(asset);
    return makeAudit("PENDING", 0, "arestrix-native-v1", sha256Json(asset), {},
                     {"Tipo de asset sem auditoria nativa."});
}

bool canInstallAudit(const Json& audit, bool allowCaution, std::string& error) {
    const std::string verdict = audit.is_object() ? audit.value("verdict", std::string("PENDING"))
                                                   : std::string("PENDING");
    if (verdict == "PENDING") {
        error = "Asset ainda nao auditado; audite antes de instalar.";
        return false;
    }
    if (verdict == "REJECT") {
        error = "Asset rejeitado pelo auditor de seguranca.";
        return false;
    }
    if (verdict == "CAUTION" && !allowCaution) {
        error = "Asset com alerta exige confirmacao explicita (allow_caution=true).";
        return false;
    }
    return true;
}

const Json* factoryAssetById(const std::vector<Json>& factoryAssets, const std::string& id) {
    for (const auto& asset : factoryAssets) {
        if (asset.value("id", std::string()) == id) return &asset;
    }
    return nullptr;
}

bool sameFactoryPayload(const Json& asset, const Json& factory) {
    if (!asset.is_object() || !factory.is_object()) return false;
    const Json& a = asset.contains("origin") && asset.at("origin").is_object() ? asset.at("origin") : Json();
    const Json& f = factory.contains("origin") && factory.at("origin").is_object() ? factory.at("origin") : Json();
    return asset.value("name", std::string()) == factory.value("name", std::string()) &&
           asset.value("version", std::string()) == factory.value("version", std::string()) &&
           a.value("installHash", std::string()) == f.value("installHash", std::string());
}

bool sameAuditHash(const Json& asset, const Json& factory) {
    const Json& a = asset.contains("audit") && asset.at("audit").is_object() ? asset.at("audit") : Json();
    const Json& f = factory.contains("audit") && factory.at("audit").is_object() ? factory.at("audit") : Json();
    return a.value("hash", std::string()) == f.value("hash", std::string());
}

Json factoryStub(const Json& asset) {
    Json stub = Json::object();
    for (const char* field : {"id", "libraryId", "kind", "name", "category", "version", "license",
                              "status", "bundledInProduct", "domain", "subdomain", "origin"}) {
        if (asset.contains(field)) stub[field] = asset.at(field);
    }
    stub["description"] = "";
    stub["provenance"] = Json{{"sourceKind", "builtin"},
                              {"sourceId", asset.value("libraryId", std::string("library-arestrix"))},
                              {"importedAt", asset.contains("provenance") && asset.at("provenance").is_object()
                                                 ? asset.at("provenance").value("importedAt", std::string())
                                                 : std::string()}};
    const Json& audit = asset.contains("audit") && asset.at("audit").is_object() ? asset.at("audit") : Json();
    stub["audit"] = Json{{"verdict", audit.value("verdict", std::string("APPROVE"))},
                         {"riskScore", audit.value("riskScore", 0)},
                         {"policyVersion", audit.value("policyVersion", std::string("bundled-catalog-v1"))},
                         {"hash", audit.value("hash", std::string())},
                         {"findings", Json::array()},
                         {"warnings", Json::array()}};
    const std::string kind = asset.value("kind", std::string());
    if (kind == "skill") {
        stub["payload"] = Json{{"kind", "skill"}, {"markdownContent", ""}, {"systemPrompt", ""},
                               {"tools", Json::array()}};
    } else if (kind == "mcp") {
        stub["payload"] = Json{{"kind", "mcp"}, {"transport", "stdio"}, {"command", ""},
                               {"args", Json::array()}, {"url", ""}, {"cachedTools", Json::array()}};
    } else {
        Json tpl = Json::object();
        tpl["id"] = asset.value("origin", Json::object()).is_object()
                        ? asset.at("origin").value("templateId", std::string())
                        : std::string();
        tpl["name"] = asset.value("name", std::string());
        tpl["taskType"] = "agent";
        tpl["description"] = "";
        tpl["inputs"] = Json::array();
        tpl["outputs"] = Json::array();
        tpl["lambdaBodyTemplate"] = "";
        stub["payload"] = Json{{"kind", kind}, {"template", tpl}};
    }
    stub["createdAt"] = asset.value("createdAt", std::string());
    stub["updatedAt"] = asset.value("updatedAt", std::string());
    return stub;
}

} // namespace

bool LibraryCatalog::load(const fs::path& projectRoot, LibraryCatalog& out, std::string& warning) {
    out = LibraryCatalog{};
    out.projectRoot_ = projectRoot;
    out.bundledFile_ = bundledPath();
    out.factoryAssets_ = bundledAssets();
    return out.rebuildFromWorkspace(warning);
}

bool LibraryCatalog::refresh(std::string& warning) {
    warning.clear();
    if (projectRoot_.empty()) return true;
    const fs::path file = projectRoot_ / ".workspace" / "libraries.json";
    std::error_code ec;
    const bool exists = fs::is_regular_file(file, ec);
    if (!exists) {
        if (workspaceMtimeValid_) return rebuildFromWorkspace(warning);
        return true;
    }
    const auto mtime = fs::last_write_time(file, ec);
    if (ec) return true;
    if (workspaceMtimeValid_ && file == workspaceFile_ && mtime == workspaceMtime_) return true;
    return rebuildFromWorkspace(warning);
}

bool LibraryCatalog::rebuildFromWorkspace(std::string& warning) {
    warning.clear();
    assets_ = factoryAssets_;
    libraries_.clear();
    workspaceSources_ = Json::array();

    Json builtin = Json::object();
    builtin["id"] = "library-arestrix";
    builtin["name"] = "Biblioteca Arestrix";
    builtin["description"] = "Vertices, agentes e pipelines nativos do Arestrix.";
    builtin["kind"] = "builtin";
    builtin["scope"] = "studio";
    builtin["isBuiltin"] = true;
    builtin["createdAt"] = bundledRoot().is_object()
                               ? bundledRoot().value("generatedAt", std::string())
                               : std::string();
    libraries_.push_back(std::move(builtin));

    workspaceMtimeValid_ = false;
    workspaceFile_ = projectRoot_.empty() ? fs::path() : projectRoot_ / ".workspace" / "libraries.json";

    std::error_code ec;
    if (!workspaceFile_.empty() && fs::is_regular_file(workspaceFile_, ec)) {
        workspaceMtime_ = fs::last_write_time(workspaceFile_, ec);
        workspaceMtimeValid_ = !ec;

        auto parsed = jsonutil::tryParse(jsonutil::readFile(workspaceFile_));
        if (!parsed || !parsed->is_object()) {
            warning = "libraries.json invalido; usando apenas o catalogo embutido";
        } else {
            const Json& state = *parsed;
            if (state.contains("sources") && state.at("sources").is_array()) {
                workspaceSources_ = state.at("sources");
            }

            std::unordered_map<std::string, std::string> libraryNames;
            for (const auto& library : libraries_) {
                libraryNames[library.value("id", std::string())] = library.value("name", std::string());
            }

            if (state.contains("libraries") && state.at("libraries").is_array()) {
                for (const auto& library : state.at("libraries").arr()) {
                    if (!library.is_object()) continue;
                    const std::string id = library.value("id", std::string());
                    if (id.empty()) continue;
                    auto existing = std::find_if(libraries_.begin(), libraries_.end(),
                                                 [&](const Json& item) { return item.value("id", std::string()) == id; });
                    if (existing != libraries_.end()) *existing = library;
                    else libraries_.push_back(library);
                    libraryNames[id] = library.value("name", id);
                }
            }

            std::unordered_map<std::string, size_t> index;
            for (size_t i = 0; i < assets_.size(); ++i) {
                index[assets_[i].value("id", std::string())] = i;
            }

            if (state.contains("assets") && state.at("assets").is_array()) {
                for (const auto& asset : state.at("assets").arr()) {
                    if (!asset.is_object()) continue;
                    const std::string id = asset.value("id", std::string());
                    if (id.empty()) continue;
                    auto found = index.find(id);
                    if (found != index.end()) {
                        Json& existing = assets_[found->second];
                        if (hasRealPayload(asset)) {
                            Json merged = asset;
                            if (!merged.contains("libraryName")) {
                                merged["libraryName"] = existing.value("libraryName", std::string());
                            }
                            existing = std::move(merged);
                        } else {
                            // Stub compactado: preserva o payload de fabrica e aplica o estado.
                            if (asset.contains("status")) existing["status"] = asset.at("status");
                            if (asset.contains("updatedAt")) existing["updatedAt"] = asset.at("updatedAt");
                            if (asset.contains("audit")) existing["audit"] = asset.at("audit");
                            if (asset.contains("derivedFrom")) existing["derivedFrom"] = asset.at("derivedFrom");
                        }
                    } else {
                        Json copy = asset;
                        const std::string libraryId = copy.value("libraryId", std::string());
                        auto name = libraryNames.find(libraryId);
                        copy["libraryName"] = name != libraryNames.end() ? name->second : libraryId;
                        assets_.push_back(std::move(copy));
                        index[id] = assets_.size() - 1;
                    }
                }
            }
        }
    } else if (bundledFile_.empty()) {
        warning = "catalogo embutido nao encontrado e Workspace sem libraries.json";
    }

    reindex();
    return true;
}

std::vector<std::string> splitSearchTokens(const std::string& text) {
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : lower(text)) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) tokens.push_back(current);
    return tokens;
}

void collectEnvVarRefsFromString(const std::string& text, std::set<std::string>& out) {
    static const std::regex kRefRe(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)(?::-[^}]*)?\})");
    auto begin = std::sregex_iterator(text.begin(), text.end(), kRefRe);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        if (it->size() >= 2) out.insert((*it)[1].str());
    }
}

std::vector<std::string> extractMcpEnvVarsImpl(const Json& payload) {
    std::set<std::string> vars;
    if (!payload.is_object()) return {};
    if (payload.contains("command") && payload.at("command").is_string()) {
        collectEnvVarRefsFromString(payload.at("command").get<std::string>(), vars);
    }
    if (payload.contains("url") && payload.at("url").is_string()) {
        collectEnvVarRefsFromString(payload.at("url").get<std::string>(), vars);
    }
    if (payload.contains("args") && payload.at("args").is_array()) {
        for (const auto& arg : payload.at("args").arr()) {
            if (arg.is_string()) collectEnvVarRefsFromString(arg.get<std::string>(), vars);
        }
    }
    for (const char* mapKey : {"env", "headers"}) {
        if (!payload.contains(mapKey) || !payload.at(mapKey).is_object()) continue;
        for (auto it = payload.at(mapKey).begin(); it != payload.at(mapKey).end(); ++it) {
            if (it.value().is_string()) {
                const std::string val = it.value().get<std::string>();
                size_t before = vars.size();
                collectEnvVarRefsFromString(val, vars);
                if (vars.size() == before && std::string(mapKey) == "env" && !it.key().empty()) {
                    vars.insert(it.key());
                }
            }
        }
    }
    return std::vector<std::string>(vars.begin(), vars.end());
}

bool matchesMcpFilters(const Json& asset, const std::string& transportFilter, bool zeroAuthOnly) {
    if (transportFilter.empty() && !zeroAuthOnly) return true;
    const std::string kind = asset.value("kind", std::string());
    if (kind != "mcp") return false;
    const Json& payload = asset.contains("payload") && asset.at("payload").is_object()
                              ? asset.at("payload") : Json();
    if (!transportFilter.empty()) {
        const std::string tr = lower(payload.value("transport", std::string("stdio")));
        if (tr != lower(transportFilter)) return false;
    }
    if (zeroAuthOnly) {
        if (!extractMcpEnvVarsImpl(payload).empty()) return false;
    }
    return true;
}

void LibraryCatalog::reindex() {
    assetIndex_.clear();
    libraryIndex_.clear();
    searchIndex_.clear();
    searchIndex_.reserve(assets_.size());
    for (size_t i = 0; i < assets_.size(); ++i) {
        const Json& asset = assets_[i];
        assetIndex_[asset.value("id", std::string())] = i;
        std::string haystack = asset.value("id", std::string());
        for (const char* field : {"name", "description", "category", "domain", "subdomain"}) {
            haystack.push_back(' ');
            haystack += asset.value(field, std::string());
        }
        if (asset.contains("origin") && asset.at("origin").is_object()) {
            const Json& origin = asset.at("origin");
            for (const char* ofield : {"repo", "canonicalKey", "templateId"}) {
                const std::string v = origin.value(ofield, std::string());
                if (!v.empty()) {
                    haystack.push_back(' ');
                    haystack += v;
                }
            }
        }
        if (asset.contains("payload") && asset.at("payload").is_object()) {
            const Json& payload = asset.at("payload");
            if (asset.value("kind", std::string()) == "mcp") {
                haystack.push_back(' ');
                haystack += payload.value("transport", std::string());
                haystack.push_back(' ');
                haystack += payload.value("command", std::string());
                if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
                    int toolCap = 0;
                    for (const auto& t : payload.at("cachedTools").arr()) {
                        if (!t.is_object() || toolCap >= 40) break;
                        haystack.push_back(' ');
                        haystack += t.value("name", std::string());
                        ++toolCap;
                    }
                }
            }
        }
        searchIndex_.push_back(lower(std::move(haystack)));
    }
    for (size_t i = 0; i < libraries_.size(); ++i) {
        libraryIndex_[libraries_[i].value("id", std::string())] = i;
    }
}

std::vector<std::string> LibraryCatalog::extractMcpEnvVars(const Json& payload) {
    return extractMcpEnvVarsImpl(payload);
}

const Json* LibraryCatalog::find(const std::string& assetId) const {
    auto it = assetIndex_.find(assetId);
    if (it == assetIndex_.end()) return nullptr;
    return &assets_[it->second];
}

const Json* LibraryCatalog::findLibrary(const std::string& libraryId) const {
    auto it = libraryIndex_.find(libraryId);
    if (it == libraryIndex_.end()) return nullptr;
    return &libraries_[it->second];
}

Json* LibraryCatalog::mutableAsset(const std::string& assetId) {
    auto it = assetIndex_.find(assetId);
    if (it == assetIndex_.end()) return nullptr;
    return &assets_[it->second];
}

std::string LibraryCatalog::libraryName(const std::string& libraryId) const {
    if (const Json* library = findLibrary(libraryId)) return library->value("name", libraryId);
    return libraryId;
}

Json LibraryCatalog::summary(const Json& asset) const {
    Json item = Json::object();
    for (const char* field : {"id", "libraryId", "kind", "name", "category", "domain", "subdomain",
                              "version", "license", "status", "bundledInProduct"}) {
        if (asset.contains(field)) item[field] = asset.at(field);
    }
    item["libraryName"] = asset.contains("libraryName")
                              ? asset.at("libraryName")
                              : Json(libraryName(asset.value("libraryId", std::string())));
    const std::string status = asset.value("status", std::string());
    item["installed"] = status == "installed" || status == "pinned";
    if (asset.contains("audit") && asset.at("audit").is_object()) {
        const Json& audit = asset.at("audit");
        item["verdict"] = audit.value("verdict", std::string());
        item["riskScore"] = audit.value("riskScore", 0);
    }
    if (asset.contains("payload") && asset.at("payload").is_object()) {
        const Json& payload = asset.at("payload");
        if (payload.contains("template") && payload.at("template").is_object()) {
            item["nodeType"] = payload.at("template").value("taskType", std::string());
        } else if (asset.value("kind", std::string()) == "mcp") {
            item["nodeType"] = "mcp";
            item["transport"] = payload.value("transport", std::string("stdio"));
            const auto reqVars = extractMcpEnvVarsImpl(payload);
            Json varsArr = Json::array();
            for (const auto& v : reqVars) varsArr.push_back(v);
            item["requiredEnvVars"] = varsArr;
            item["requiresEnv"] = !reqVars.empty();
            Json sampleTools = Json::array();
            int toolCount = 0;
            if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
                toolCount = static_cast<int>(payload.at("cachedTools").arr().size());
                for (const auto& t : payload.at("cachedTools").arr()) {
                    if (!t.is_object()) continue;
                    const std::string tname = t.value("name", std::string());
                    if (!tname.empty() && sampleTools.size() < 6) sampleTools.push_back(tname);
                }
            }
            item["toolCount"] = toolCount;
            if (!sampleTools.empty()) {
                item["defaultTool"] = sampleTools.arr().front();
                item["sampleTools"] = sampleTools;
            }
        }
    }
    item["description"] = cutText(asset.value("description", std::string()), kMaxDescription);
    return item;
}

Json LibraryCatalog::list(const std::string& libraryId, const std::string& kind,
                          const std::string& domain, const std::string& subdomain,
                          int limit, int offset,
                          const std::string& transport,
                          bool zeroAuthOnly) const {
    Json out = Json::object();
    Json libraries = Json::array();
    for (const auto& library : libraries_) libraries.push_back(library);
    out["libraries"] = libraries;

    const int effectiveLimit = limit <= 0 ? kDefaultLimit : std::min(limit, kMaxLimit);
    const int effectiveOffset = std::max(offset, 0);
    const std::string domainNeedle = lower(domain);
    const std::string subdomainNeedle = lower(subdomain);

    Json items = Json::array();
    int matched = 0;
    int skipped = 0;
    for (const auto& asset : assets_) {
        if (!libraryId.empty() && asset.value("libraryId", std::string()) != libraryId) continue;
        if (!matchesKind(asset, kind)) continue;
        if (!domainNeedle.empty() &&
            lower(asset.value("domain", std::string())).find(domainNeedle) == std::string::npos) continue;
        if (!subdomainNeedle.empty() &&
            lower(asset.value("subdomain", std::string())).find(subdomainNeedle) == std::string::npos) continue;
        if (!matchesMcpFilters(asset, transport, zeroAuthOnly)) continue;
        ++matched;
        if (skipped < effectiveOffset) {
            ++skipped;
            continue;
        }
        if (static_cast<int>(items.size()) >= effectiveLimit) continue;
        items.push_back(summary(asset));
    }

    out["assets"] = items;
    out["total"] = matched;
    out["returned"] = static_cast<int>(items.size());
    out["offset"] = effectiveOffset;
    out["truncated"] = matched > effectiveOffset + static_cast<int>(items.size());
    return out;
}

Json LibraryCatalog::search(const std::string& query, const std::string& kind, int limit,
                            const std::string& domain, const std::string& subdomain,
                            const std::string& transport, bool zeroAuthOnly,
                            int offset) const {
    Json out = Json::object();
    const auto tokens = splitSearchTokens(query);
    const std::string domainNeedle = lower(domain);
    const std::string subdomainNeedle = lower(subdomain);
    const int effectiveLimit = limit <= 0 ? kDefaultLimit : std::min(limit, kMaxLimit);
    const int effectiveOffset = std::max(offset, 0);
    Json items = Json::array();
    int matched = 0;
    int skipped = 0;
    for (size_t i = 0; i < assets_.size(); ++i) {
        const Json& asset = assets_[i];
        if (!matchesKind(asset, kind)) continue;
        if (!domainNeedle.empty() &&
            lower(asset.value("domain", std::string())).find(domainNeedle) == std::string::npos) continue;
        if (!subdomainNeedle.empty() &&
            lower(asset.value("subdomain", std::string())).find(subdomainNeedle) == std::string::npos) continue;
        if (!matchesMcpFilters(asset, transport, zeroAuthOnly)) continue;
        if (!tokens.empty()) {
            const std::string& haystack = i < searchIndex_.size() ? searchIndex_[i] : std::string();
            bool allMatch = true;
            for (const auto& tok : tokens) {
                if (haystack.find(tok) == std::string::npos) {
                    allMatch = false;
                    break;
                }
            }
            if (!allMatch) continue;
        }
        ++matched;
        if (skipped < effectiveOffset) {
            ++skipped;
            continue;
        }
        if (static_cast<int>(items.size()) >= effectiveLimit) continue;
        items.push_back(summary(asset));
    }
    out["query"] = query;
    out["assets"] = items;
    out["total"] = matched;
    out["returned"] = static_cast<int>(items.size());
    out["offset"] = effectiveOffset;
    out["truncated"] = matched > effectiveOffset + static_cast<int>(items.size());
    return out;
}

Json LibraryCatalog::taxonomy(const std::string& kind) const {
    std::map<std::string, std::map<std::string, int>> counts;
    std::map<std::string, int> byKind;
    int total = 0;
    for (const auto& asset : assets_) {
        if (!matchesKind(asset, kind)) continue;
        const std::string dom = asset.value("domain", std::string("Geral"));
        const std::string sub = asset.value("subdomain", asset.value("category", std::string("Outros")));
        counts[dom.empty() ? "Geral" : dom][sub.empty() ? "Outros" : sub] += 1;
        byKind[asset.value("kind", std::string("unknown"))] += 1;
        ++total;
    }

    Json domains = Json::object();
    for (const auto& [dom, subs] : counts) {
        Json subObj = Json::object();
        int domTotal = 0;
        for (const auto& [sub, cnt] : subs) {
            subObj[sub] = cnt;
            domTotal += cnt;
        }
        domains[dom] = Json{{"total", domTotal}, {"subdomains", subObj}};
    }
    Json kindCounts = Json::object();
    for (const auto& [k, cnt] : byKind) kindCounts[k] = cnt;
    return Json{
        {"kind", kind.empty() ? "all" : kind},
        {"total", total},
        {"byKind", kindCounts},
        {"domains", domains}
    };
}

std::optional<Json> LibraryCatalog::detail(const std::string& assetId) const {
    const Json* asset = find(assetId);
    if (!asset) return std::nullopt;
    Json out = *asset;
    if (out.contains("payload") && out.at("payload").is_object()) {
        Json& payload = out["payload"];
        if (out.value("kind", std::string()) == "mcp") {
            const auto reqVars = extractMcpEnvVarsImpl(payload);
            Json varsArr = Json::array();
            Json missingArr = Json::array();
            Json envStatus = Json::object();
            for (const auto& v : reqVars) {
                varsArr.push_back(v);
                const bool configured = !platform::getEnv(v).empty();
                envStatus[v] = configured;
                if (!configured) missingArr.push_back(v);
            }
            out["requiredEnvVars"] = varsArr;
            out["missingEnvVars"] = missingArr;
            out["envStatus"] = envStatus;
            if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
                const auto& rawTools = payload.at("cachedTools").arr();
                out["totalTools"] = static_cast<int>(rawTools.size());
                Json compactTools = Json::array();
                constexpr size_t kMaxDetailTools = 30;
                for (size_t i = 0; i < rawTools.size() && i < kMaxDetailTools; ++i) {
                    const auto& t = rawTools[i];
                    if (!t.is_object()) continue;
                    compactTools.push_back(Json{
                        {"name", t.value("name", std::string())},
                        {"description", cutText(t.value("description", std::string()), 160)}
                    });
                }
                payload["cachedTools"] = compactTools;
                out["toolsTruncated"] = rawTools.size() > kMaxDetailTools;
            }
        }
        if (payload.contains("markdownContent")) {
            payload["markdownContent"] = cutText(payload.value("markdownContent", std::string()), kMaxMarkdown);
        }
        if (payload.contains("systemPrompt")) {
            payload["systemPrompt"] = cutText(payload.value("systemPrompt", std::string()), kMaxPrompt);
        }
        if (payload.contains("template") && payload.at("template").is_object()) {
            Json& tpl = payload["template"];
            if (tpl.contains("lambdaBodyTemplate")) {
                tpl["lambdaBodyTemplate"] = cutText(tpl.value("lambdaBodyTemplate", std::string()), kMaxMarkdown);
            }
            if (tpl.contains("agentSettings") && tpl.at("agentSettings").is_object()) {
                Json& settings = tpl["agentSettings"];
                if (settings.contains("skills") && settings.at("skills").is_array()) {
                    for (auto& skill : settings["skills"]) {
                        if (!skill.is_object()) continue;
                        if (skill.contains("markdownContent")) {
                            skill["markdownContent"] = cutText(skill.value("markdownContent", std::string()), kMaxMarkdown);
                        }
                        if (skill.contains("systemPrompt")) {
                            skill["systemPrompt"] = cutText(skill.value("systemPrompt", std::string()), kMaxPrompt);
                        }
                    }
                }
            }
        }
    }
    return out;
}

bool LibraryCatalog::buildNodeFromAsset(const Json& asset, const std::string& label, const Json& parameterOverrides,
                                        RuntimeLang lang, Json& node, std::string& error) const {
    node = Json::object();
    error.clear();
    if (!asset.is_object()) {
        error = langPick(lang, "Ativo invalido.", "Invalid asset.");
        return false;
    }

    const std::string assetId = asset.value("id", std::string());
    const std::string kind = asset.value("kind", std::string());
    const std::string name = asset.value("name", assetId);
    const std::string finalLabel = label.empty() ? name : label;
    const Json& payload = asset.contains("payload") && asset.at("payload").is_object()
                              ? asset.at("payload") : Json();

    node["label"] = finalLabel;
    node["librarySource"] = nodeLibrarySource(asset);

    if (kind == "agent-template" || kind == "pipeline") {
        if (!payload.is_object() || !payload.contains("template") || !payload.at("template").is_object()) {
            error = langPick(lang, "O ativo nao contem um template de vertice.",
                             "The asset does not contain a vertex template.");
            return false;
        }
        const Json& tpl = payload.at("template");
        std::string type = tpl.value("taskType", std::string());
        if (type.empty()) type = "agent";
        node["type"] = type;

        Json parameters = Json::object();
        if (tpl.contains("parameters") && tpl.at("parameters").is_object()) {
            for (auto it = tpl.at("parameters").begin(); it != tpl.at("parameters").end(); ++it) {
                const Json& def = it.value();
                parameters[it.key()] = def.is_object() && def.contains("defaultValue")
                                           ? def.at("defaultValue") : Json();
            }
        }
        if (parameterOverrides.is_object()) {
            for (auto it = parameterOverrides.begin(); it != parameterOverrides.end(); ++it) {
                parameters[it.key()] = it.value();
            }
        }
        node["parameters"] = parameters;

        if (tpl.contains("inputs")) node["inputs"] = tpl.at("inputs");
        if (tpl.contains("outputs")) node["outputs"] = tpl.at("outputs");
        if (tpl.contains("requiredHeaders")) node["requiredHeaders"] = tpl.at("requiredHeaders");
        if (tpl.contains("agentSettings")) node["agentSettings"] = tpl.at("agentSettings");
        if (tpl.contains("childGraph")) node["childGraph"] = tpl.at("childGraph");
        if (tpl.contains("pipelineInterface")) node["pipelineInterface"] = tpl.at("pipelineInterface");
        if (tpl.contains("presentation")) node["presentation"] = tpl.at("presentation");
        node["templateId"] = tpl.value("id", assetId);
        node["presetId"] = tpl.value("id", assetId);
        node["presetLocked"] = tpl.value("presetLocked", true);

        const std::string body = applyTemplate(tpl.value("lambdaBodyTemplate", std::string()), finalLabel, parameters);
        if (!body.empty()) {
            node["customLambdaBody"] = body;
        } else if (type == "agent" || kind == "pipeline" ||
                   tpl.value("assetKind", std::string()) == "pipeline") {
            // Agentes e pipelines usam o executor do runtime, nao um corpo C++.
            node["customLambdaBody"] = "";
        }

        if (asset.contains("origin") && asset.at("origin").is_object()) {
            node["origin"] = asset.at("origin");
        } else if (tpl.contains("origin") && tpl.at("origin").is_object()) {
            node["origin"] = tpl.at("origin");
        }
        return true;
    }

    if (kind == "skill") {
        node["type"] = "agent";
        node["parameters"] = Json::object();
        node["customLambdaBody"] = "";

        Json skill = Json::object();
        skill["id"] = assetId;
        skill["name"] = name;
        skill["description"] = asset.value("description", std::string());
        skill["markdownContent"] = payload.value("markdownContent", std::string());
        skill["systemPrompt"] = payload.value("systemPrompt", std::string());
        skill["tools"] = payload.contains("tools") && payload.at("tools").is_array()
                             ? payload.at("tools") : Json::array();
        skill["license"] = asset.value("license", std::string());
        skill["updatedAt"] = asset.value("updatedAt", std::string());
        if (asset.contains("provenance") && asset.at("provenance").is_object()) {
            const Json& provenance = asset.at("provenance");
            if (!provenance.value("url", std::string()).empty()) skill["sourceUrl"] = provenance.at("url");
        }
        if (asset.contains("audit") && asset.at("audit").is_object()) skill["audit"] = asset.at("audit");

        Json settings = Json::object();
        settings["inheritSandbox"] = true;
        settings["inheritModel"] = true;
        settings["inheritCodeMode"] = true;
        settings["inheritMemory"] = true;
        settings["skills"] = Json::array({skill});
        settings["activeSkillId"] = assetId;
        node["agentSettings"] = settings;
        return true;
    }

    if (kind == "mcp") {
        std::string selectedTool;
        if (parameterOverrides.is_object() && parameterOverrides.contains("tool") &&
            parameterOverrides.at("tool").is_string()) {
            selectedTool = parameterOverrides.at("tool").get<std::string>();
        }
        if (selectedTool.empty() && payload.contains("cachedTools") && payload.at("cachedTools").is_array() &&
            !payload.at("cachedTools").arr().empty() && payload.at("cachedTools").arr().front().is_object()) {
            selectedTool = payload.at("cachedTools").arr().front().value("name", std::string());
        }
        Json toolArgs = Json::object();
        if (parameterOverrides.is_object() && parameterOverrides.contains("arguments") &&
            parameterOverrides.at("arguments").is_object()) {
            toolArgs = parameterOverrides.at("arguments");
        }
        Json sourceSpec = Json{{"kind", "previous"}};
        if (parameterOverrides.is_object() && parameterOverrides.contains("source") &&
            parameterOverrides.at("source").is_object()) {
            sourceSpec = parameterOverrides.at("source");
        }
        Json outputSpec = Json::object();
        if (parameterOverrides.is_object() && parameterOverrides.contains("output") &&
            parameterOverrides.at("output").is_object()) {
            outputSpec = parameterOverrides.at("output");
        }
        node["type"] = "mcp";
        node["parameters"] = Json::object();
        node["customLambdaBody"] = "";
        node["mcpSettings"] = Json{
            {"server", assetId},
            {"tool", selectedTool},
            {"arguments", toolArgs},
            {"source", sourceSpec},
            {"output", outputSpec}
        };
        if (asset.contains("origin") && asset.at("origin").is_object()) {
            node["origin"] = asset.at("origin");
        }
        return true;
    }

    error = langPick(lang, "Tipo de ativo sem suporte para criacao de vertice: '",
                     "Unsupported asset kind for vertex creation: '") + kind + "'.";
    return false;
}

bool LibraryCatalog::writeFileAtomic(const fs::path& path, const std::string& content, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Falha ao criar diretorio: " + path.parent_path().string();
        return false;
    }
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "Falha ao abrir arquivo para escrita: " + tmp.string();
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out.good()) {
            error = "Falha ao gravar arquivo: " + tmp.string();
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    if (ec) {
        error = "Falha ao publicar arquivo: " + path.string();
        return false;
    }
    return true;
}

bool LibraryCatalog::saveState(std::string& error) {
    error.clear();
    if (projectRoot_.empty()) {
        error = "Projeto sem rootPath; a biblioteca nao pode ser gravada.";
        return false;
    }
    Json state = Json::object();
    state["version"] = 2;

    Json libs = Json::array();
    for (const auto& library : libraries_) libs.push_back(library);
    state["libraries"] = libs;

    Json assets = Json::array();
    for (const auto& asset : assets_) {
        const bool factory = asset.value("bundledInProduct", false) &&
                             asset.value("libraryId", std::string()) == "library-arestrix" &&
                             !asset.contains("derivedFrom");
        if (factory) {
            const Json* original = factoryAssetById(factoryAssets_, asset.value("id", std::string()));
            const bool payload = original && sameFactoryPayload(asset, *original);
            const bool audit = original && sameAuditHash(asset, *original);
            if (payload && audit && asset.value("status", std::string("available")) == "available") {
                continue;   // fabrica intacta: nao grava os 6 MB
            }
            // Status alterado cabe em um stub; payload ou auditoria alterados
            // precisam do asset completo para sobreviver ao proximo load.
            assets.push_back(original && payload && audit ? factoryStub(asset) : asset);
        } else {
            assets.push_back(asset);
        }
    }
    state["assets"] = assets;
    if (workspaceSources_.is_array()) state["sources"] = workspaceSources_;
    state["updatedAt"] = nowIso();

    const fs::path file = projectRoot_ / ".workspace" / "libraries.json";
    if (!writeFileAtomic(file, state.dump(2), error)) return false;
    workspaceFile_ = file;
    std::error_code ec;
    workspaceMtime_ = fs::last_write_time(file, ec);
    workspaceMtimeValid_ = !ec;
    return true;
}

std::string LibraryCatalog::uniqueId(const std::string& prefix, const std::vector<Json>& entries) const {
    auto exists = [&](const std::string& id) {
        for (const auto& entry : entries) {
            if (entry.value("id", std::string()) == id) return true;
        }
        return false;
    };
    if (!exists(prefix)) return prefix;
    for (int i = 2; i < 10000; ++i) {
        const std::string candidate = prefix + "-" + std::to_string(i);
        if (!exists(candidate)) return candidate;
    }
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return prefix + "-" + std::to_string(now);
}

std::string LibraryCatalog::slugify(const std::string& text) const {
    std::string slug = slugifyText(text);
    return slug.empty() ? "asset" : slug;
}

Json LibraryCatalog::installAsset(const std::string& assetId, bool allowCaution, std::string& error) {
    error.clear();
    std::string warning;
    refresh(warning);

    Json* asset = mutableAsset(assetId);
    if (!asset) {
        error = "Ativo nao encontrado: " + assetId;
        return Json{{"ok", false}, {"reason", error}};
    }
    const std::string kind = asset->value("kind", std::string());
    if (kind != "skill" && kind != "mcp") {
        error = "Somente skills e MCPs sao instalados no Workspace; templates devem ser inseridos no grafo.";
        return Json{{"ok", false}, {"reason", error}};
    }
    if (!safeSegment(assetId)) {
        error = "Id de ativo invalido para instalacao.";
        return Json{{"ok", false}, {"reason", error}};
    }
    const Json& audit = asset->contains("audit") && asset->at("audit").is_object()
                            ? asset->at("audit") : Json();
    if (!canInstallAudit(audit, allowCaution, error)) {
        return Json{{"ok", false}, {"reason", error}};
    }
    if (projectRoot_.empty()) {
        error = "Projeto sem rootPath; nao e possivel instalar no Workspace.";
        return Json{{"ok", false}, {"reason", error}};
    }

    const fs::path workspace = projectRoot_ / ".workspace";
    fs::path installedPath;
    if (kind == "skill") {
        const Json& payload = asset->at("payload");
        const std::string markdown = payload.value("markdownContent", std::string());
        installedPath = workspace / "skills" / assetId;
        if (!writeFileAtomic(installedPath / "SKILL.md", markdown, error)) {
            return Json{{"ok", false}, {"reason", error}};
        }
        Json meta = Json::object();
        meta["id"] = assetId;
        meta["name"] = asset->value("name", std::string());
        meta["description"] = asset->value("description", std::string());
        meta["license"] = asset->value("license", std::string());
        meta["tools"] = payload.contains("tools") ? payload.at("tools") : Json::array();
        meta["systemPrompt"] = payload.value("systemPrompt", std::string());
        if (asset->contains("audit")) meta["audit"] = asset->at("audit");
        if (asset->contains("provenance")) meta["provenance"] = asset->at("provenance");
        if (asset->contains("derivedFrom")) meta["derivedFrom"] = asset->at("derivedFrom");
        if (!writeFileAtomic(installedPath / "skill.json", meta.dump(2), error)) {
            return Json{{"ok", false}, {"reason", error}};
        }
    } else {
        const Json& payload = asset->at("payload");
        auto sanitize = [](const Json& source) {
            Json out = Json::object();
            if (!source.is_object()) return out;
            for (auto it = source.begin(); it != source.end(); ++it) {
                if (it.value().is_string() && isEnvReference(it.value().get<std::string>())) {
                    out[it.key()] = it.value();
                }
            }
            return out;
        };

        Json config = Json::object();
        config["id"] = assetId;
        config["name"] = asset->value("name", std::string());
        config["transport"] = payload.value("transport", std::string("stdio"));
        if (payload.contains("command")) config["command"] = payload.at("command");
        if (payload.contains("args")) config["args"] = payload.at("args");
        if (payload.contains("url")) config["url"] = payload.at("url");
        if (payload.contains("env")) config["env"] = sanitize(payload.at("env"));
        if (payload.contains("headers")) config["headers"] = sanitize(payload.at("headers"));
        if (payload.contains("cachedTools")) config["cachedTools"] = payload.at("cachedTools");
        config["isEnabled"] = true;
        installedPath = workspace / "mcps" / (assetId + ".json");
        if (!writeFileAtomic(installedPath, config.dump(2), error)) {
            return Json{{"ok", false}, {"reason", error}};
        }

        // Espelha a auditoria aprovada/caution em .workspace/governance/mcp-ledger.json
        // para que attach_mcp, validate_graph e run_pipeline reconhecam o servidor imediatamente.
        const fs::path ledgerPath = workspace / "governance" / "mcp-ledger.json";
        Json ledger = Json::object();
        std::error_code ledgerEc;
        if (fs::is_regular_file(ledgerPath, ledgerEc)) {
            if (auto existing = jsonutil::tryParse(jsonutil::readFile(ledgerPath)); existing && existing->is_object()) {
                ledger = *existing;
            }
        }
        if (!ledger.contains("version")) ledger["version"] = 1;
        if (!ledger.contains("servers") || !ledger.at("servers").is_object()) {
            ledger["servers"] = Json::object();
        }
        Json toolNames = Json::array();
        if (payload.contains("cachedTools") && payload.at("cachedTools").is_array()) {
            for (const auto& t : payload.at("cachedTools").arr()) {
                if (t.is_object() && !t.value("name", std::string()).empty()) {
                    toolNames.push_back(t.value("name", std::string()));
                }
            }
        }
        const std::string auditHash = audit.is_object() && !audit.value("hash", std::string()).empty()
                                          ? audit.value("hash", std::string())
                                          : sha256Json(payload);
        Json entry = Json::object();
        entry["hash"] = auditHash;
        entry["serverId"] = assetId;
        entry["name"] = asset->value("name", assetId);
        entry["transport"] = payload.value("transport", std::string("stdio"));
        entry["verdict"] = audit.is_object() ? audit.value("verdict", std::string("APPROVE")) : std::string("APPROVE");
        entry["riskScore"] = audit.is_object() ? audit.value("riskScore", 0) : 0;
        entry["auditedAt"] = nowIso();
        entry["auditedBy"] = "ArestrixLibraryCatalog";
        entry["toolCount"] = static_cast<int>(toolNames.size());
        entry["toolNames"] = toolNames;
        entry["provenance"] = asset->value("bundledInProduct", false) ? "workspace-curated" : "external-custom";
        ledger["servers"][auditHash] = entry;
        ledger["updatedAt"] = nowIso();
        std::string ledgerErr;
        writeFileAtomic(ledgerPath, ledger.dump(2), ledgerErr);
    }

    (*asset)["status"] = "installed";
    (*asset)["updatedAt"] = nowIso();
    if (!saveState(error)) return Json{{"ok", false}, {"reason", error}};
    reindex();
    Json result{{"ok", true}, {"asset_id", assetId}, {"kind", kind}, {"path", installedPath.string()}, {"status", "installed"}};
    if (kind == "mcp") {
        const Json& payload = asset->at("payload");
        const auto reqVars = extractMcpEnvVarsImpl(payload);
        Json reqArr = Json::array();
        Json missingArr = Json::array();
        for (const auto& v : reqVars) {
            reqArr.push_back(v);
            if (platform::getEnv(v).empty()) missingArr.push_back(v);
        }
        result["requiredEnvVars"] = reqArr;
        result["missingEnvVars"] = missingArr;
        if (payload.contains("cachedTools") && payload.at("cachedTools").is_array() &&
            !payload.at("cachedTools").arr().empty() && payload.at("cachedTools").arr().front().is_object()) {
            result["defaultTool"] = payload.at("cachedTools").arr().front().value("name", std::string());
            result["toolCount"] = static_cast<int>(payload.at("cachedTools").arr().size());
        }
    }
    return result;
}

Json LibraryCatalog::deriveAsset(const std::string& assetId, const std::string& name,
                                 const std::string& libraryId, std::string& error) {
    error.clear();
    std::string warning;
    refresh(warning);

    const Json* source = find(assetId);
    if (!source) {
        error = "Ativo de origem nao encontrado: " + assetId;
        return Json{{"ok", false}, {"reason", error}};
    }

    std::string targetLibraryId = libraryId;
    if (!targetLibraryId.empty()) {
        if (!findLibrary(targetLibraryId)) {
            error = "Biblioteca de destino nao encontrada: " + targetLibraryId;
            return Json{{"ok", false}, {"reason", error}};
        }
    } else {
        for (const auto& library : libraries_) {
            if (!library.value("isBuiltin", false) &&
                library.value("scope", std::string()) == "workspace" &&
                library.value("name", std::string()) == "Minha biblioteca") {
                targetLibraryId = library.value("id", std::string());
                break;
            }
        }
        if (targetLibraryId.empty()) {
            Json library = Json::object();
            library["id"] = uniqueId("lib-minha-biblioteca", libraries_);
            library["name"] = "Minha biblioteca";
            library["description"] = "Derivados e assets criados pelo usuario.";
            library["kind"] = "local";
            library["scope"] = "workspace";
            library["isBuiltin"] = false;
            library["createdAt"] = nowIso();
            targetLibraryId = library.value("id", std::string());
            libraries_.push_back(std::move(library));
        }
    }

    const std::string sourceName = source->value("name", assetId);
    const std::string derivedName = name.empty() ? sourceName + " (copia)" : name;
    const std::string kind = source->value("kind", std::string("asset"));
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string prefix = "asset-" + targetLibraryId + "-" + kind + "-" + slugify(derivedName) +
                               "-" + base36(static_cast<uint64_t>(nowMs));
    const std::string newId = uniqueId(prefix, assets_);

    Json derived = *source;
    derived["id"] = newId;
    derived["libraryId"] = targetLibraryId;
    derived["libraryName"] = libraryName(targetLibraryId);
    derived["name"] = derivedName;
    derived["status"] = "available";
    derived["createdAt"] = nowIso();
    derived["updatedAt"] = nowIso();

    Json provenance = source->contains("provenance") && source->at("provenance").is_object()
                          ? source->at("provenance") : Json::object();
    provenance["authority"] = "user";
    provenance["importedAt"] = nowIso();
    derived["provenance"] = provenance;

    const Json& sourceAudit = source->contains("audit") && source->at("audit").is_object()
                                  ? source->at("audit") : Json();
    derived["derivedFrom"] = Json{{"libraryId", source->value("libraryId", std::string())},
                                  {"assetId", assetId},
                                  {"hash", sourceAudit.value("hash", std::string())},
                                  {"policyVersion", sourceAudit.value("policyVersion", std::string())},
                                  {"originName", sourceName}};
    if (kind == "agent-template" || kind == "pipeline" || kind == "skill" || kind == "mcp") {
        derived["audit"] = auditAssetValue(derived);
    }

    assets_.push_back(std::move(derived));
    reindex();
    if (!saveState(error)) return Json{{"ok", false}, {"reason", error}};
    Json result = detail(newId).value_or(Json::object());
    return Json{{"ok", true}, {"asset_id", newId}, {"library_id", targetLibraryId}, {"asset", result}};
}

Json LibraryCatalog::restoreAsset(const std::string& assetId, std::string& error) {
    error.clear();
    std::string warning;
    refresh(warning);

    const Json* factory = factoryAssetById(factoryAssets_, assetId);
    if (!factory) {
        error = "Ativo nativo nao encontrado para restauracao: " + assetId;
        return Json{{"ok", false}, {"reason", error}};
    }

    Json restored = *factory;
    restored["status"] = "available";
    restored["updatedAt"] = nowIso();
    Json* current = mutableAsset(assetId);
    if (current) *current = std::move(restored);
    else assets_.push_back(std::move(restored));
    reindex();
    if (!saveState(error)) return Json{{"ok", false}, {"reason", error}};
    return Json{{"ok", true}, {"asset_id", assetId}, {"restored", true}};
}

Json LibraryCatalog::auditAsset(const std::string& assetId, std::string& error) {
    error.clear();
    std::string warning;
    refresh(warning);

    Json* asset = mutableAsset(assetId);
    if (!asset) {
        error = "Ativo nao encontrado: " + assetId;
        return Json{{"ok", false}, {"reason", error}};
    }
    Json audit = auditAssetValue(*asset);
    (*asset)["audit"] = audit;
    (*asset)["updatedAt"] = nowIso();
    if (!saveState(error)) return Json{{"ok", false}, {"reason", error}};
    return Json{{"ok", true}, {"asset_id", assetId}, {"name", asset->value("name", std::string())},
                {"audit", audit}};
}

} // namespace libraries
} // namespace taskforge
