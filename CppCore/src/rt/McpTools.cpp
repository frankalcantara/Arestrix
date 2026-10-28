#include "rt/McpTools.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include "mcp/McpClient.hpp"
#include "mcp/McpErrors.hpp"
#include "search/ProjectSearch.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

constexpr size_t kMaxResultBytes = 64 * 1024;
constexpr int kListTimeoutMs = 20000;
constexpr int kCallTimeoutMs = 60000;

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

mcp::McpServerConfig expandedCopy(mcp::McpServerConfig cfg) {
    cfg.command = McpToolbox::expandEnv(cfg.command);
    cfg.url = McpToolbox::expandEnv(cfg.url);
    for (auto& a : cfg.args) a = McpToolbox::expandEnv(a);
    for (auto& [k, v] : cfg.env) v = McpToolbox::expandEnv(v);
    for (auto& [k, v] : cfg.headers) v = McpToolbox::expandEnv(v);
    return cfg;
}

std::string truncated(const std::string& s) {
    if (s.size() <= kMaxResultBytes) return s;
    size_t cut = kMaxResultBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\n[... resultado truncado em 64 KB ...]";
}

} // namespace

std::string McpToolbox::expandEnv(const std::string& text) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            size_t end = text.find('}', i + 2);
            if (end != std::string::npos) {
                std::string inner = text.substr(i + 2, end - i - 2);
                // ${NOME} ou ${NOME:-padrao}
                std::string name = inner, fallback;
                bool hasFallback = false;
                size_t sep = inner.find(":-");
                if (sep != std::string::npos) {
                    name = inner.substr(0, sep);
                    fallback = inner.substr(sep + 2);
                    hasFallback = true;
                }
                bool ok = !name.empty() && (std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_');
                for (char c : name)
                    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) ok = false;
                if (ok) {
                    const char* v = std::getenv(name.c_str());
                    if (v && *v) out += v;
                    else if (hasFallback) out += fallback;
                    else throw mcp::McpError(mcp::McpErrorCode::EnvVarUnset, name);
                    i = end + 1;
                    continue;
                }
            }
        }
        out += text[i++];
    }
    return out;
}

std::string McpToolbox::agentToolName(const std::string& server, const std::string& tool) {
    auto clean = [](std::string s) {
        for (char& c : s)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
        return s;
    };
    std::string name = "mcp__" + clean(server) + "__" + clean(tool);
    if (name.size() > 64) name = name.substr(0, 55) + "_" + search::hashHex(name).substr(0, 8);
    return name;
}

mcp::McpServerConfig McpToolbox::loadServerConfig(const fs::path& root, const std::string& id) {
    if (!validId(id)) throw mcp::McpError(mcp::McpErrorCode::InvalidServerId, id);
    if (native::isNativeServer(id))
        throw mcp::McpError(mcp::McpErrorCode::NativeServerReserved, id);
    fs::path dir = root / ".workspace" / "mcps";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        throw mcp::McpError(mcp::McpErrorCode::ServerNotFound, id);
    }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());

    for (const auto& f : files) {
        auto parsed = jsonutil::tryParse(jsonutil::readFile(f));
        if (!parsed) continue;
        std::vector<Json> candidates;
        if (parsed->is_array()) {
            for (size_t i = 0; i < parsed->size(); ++i) candidates.push_back((*parsed)[i]);
        } else {
            candidates.push_back(*parsed);
        }
        for (const auto& c : candidates) {
            if (!c.is_object() || c.value("id", std::string()) != id) continue;
            auto cfg = c.get<mcp::McpServerConfig>();
            if (!cfg.isEnabled) throw mcp::McpError(mcp::McpErrorCode::ServerDisabled, id);
            try {
                return expandedCopy(cfg);
            } catch (const mcp::McpError& ex) {
                // Mantem o codigo estavel e acrescenta o id do servidor ao detalhe dinamico.
                throw mcp::McpError(ex.code(), "servidor '" + id + "' (" + ex.detail() + ")");
            } catch (const std::exception& ex) {
                throw mcp::McpError(mcp::McpErrorCode::Unknown, ex.what());
            }
        }
    }
    throw mcp::McpError(mcp::McpErrorCode::ServerNotFound, id);
}

taskforge::Json McpToolbox::listProjectServers(const fs::path& root) {
    Json out = Json::array();
    // servidores nativos (em processo): sempre disponiveis, nao exigem configuracao nem instalacao
    for (const auto& ns : native::servers()) {
        Json item;
        item["id"] = ns.id;
        item["name"] = ns.name;
        item["transport"] = "native";
        item["isEnabled"] = true;
        item["isNative"] = true;
        item["description"] = ns.description;
        item["category"] = ns.category;
        out.push_back(item);
    }
    fs::path dir = root / ".workspace" / "mcps";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".json") files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto parsed = jsonutil::tryParse(jsonutil::readFile(f));
        if (!parsed) continue;
        std::vector<Json> candidates;
        if (parsed->is_array()) {
            for (size_t i = 0; i < parsed->size(); ++i) candidates.push_back((*parsed)[i]);
        } else {
            candidates.push_back(*parsed);
        }
        for (const auto& c : candidates) {
            std::string id = c.is_object() ? c.value("id", std::string()) : std::string();
            if (!validId(id)) continue;
            Json item;
            item["id"] = id;
            item["name"] = c.value("name", id);
            const bool hasUrl = !c.value("url", std::string()).empty();
            item["transport"] = c.value("transport", hasUrl ? std::string("http") : std::string("stdio"));
            item["isEnabled"] = c.value("isEnabled", true);
            out.push_back(item);
        }
    }
    return out;
}

taskforge::Json McpToolbox::listServerTools(const fs::path& root, const std::string& id) {
    Json out = Json::array();
    if (native::isNativeServer(id)) {
        const native::Server* ns = native::findServer(id);
        if (!ns) throw mcp::McpError(mcp::McpErrorCode::ServerNotFound, id);
        for (const auto& t : ns->tools) {
            out.push_back(Json{{"name", t.name},
                               {"description", t.description},
                               {"inputSchema", t.inputSchema},
                               {"risk", t.risk}});
        }
        return out;
    }
    mcp::McpServerConfig cfg = loadServerConfig(root, id);
    mcp::McpClient client;
    mcp::McpErrorInfo info;
    auto tools = client.listTools(cfg, kListTimeoutMs, &info);
    if (tools.empty() && !info.empty()) throw mcp::McpError(info.code, info.detail);
    for (const auto& t : tools) {
        out.push_back(Json{{"name", t.name},
                           {"description", t.description},
                           {"inputSchema", t.inputSchema}});
    }
    return out;
}

bool McpToolbox::isToolAllowed(const std::vector<std::string>& allowed, const std::string& serverId, const std::string& toolName) {
    if (allowed.empty()) return true;   // lista vazia = todas as ferramentas dos servidores declarados
    for (const auto& entry : allowed) {
        if (entry == toolName || entry == serverId + "/" + toolName || entry == serverId + "/*") return true;
    }
    return false;
}

McpToolbox::McpToolbox(const fs::path& root, const std::vector<std::string>& serverIds,
                       const std::vector<std::string>& allowedTools, RuntimeLang lang)
    : root_(root), lang_(lang) {
    mcp::McpClient client;
    for (const auto& id : serverIds) {
        if (native::isNativeServer(id)) {
            // Catalogo no idioma do agente: descricoes e schemas ja vêm localizados.
            const native::Server* ns = nullptr;
            for (const auto& s : native::serversForLang(lang)) {
                if (s.id == id) { ns = &s; break; }
            }
            if (!ns) throw std::runtime_error("servidor nativo desconhecido: " + id);
            for (const auto& t : ns->tools) {
                if (!isToolAllowed(allowedTools, id, t.name)) continue;
                std::string name = agentToolName(id, t.name);
                if (bindings_.count(name)) continue;
                ToolDef def;
                def.name = name;
                def.description = "[Arestrix " + ns->name + "] " + t.description;
                def.parameters = t.inputSchema;
                defs_.push_back(std::move(def));
                Binding b;
                b.tool = t.name;
                b.nativeServer = ns;
                bindings_[name] = std::move(b);
            }
            continue;
        }
        mcp::McpServerConfig cfg = loadServerConfig(root, id);
        std::vector<mcp::McpTool> tools = client.listTools(cfg, kListTimeoutMs);
        if (tools.empty()) {
            throw mcp::McpError(mcp::McpErrorCode::NoTools, id);
        }
        for (const auto& t : tools) {
            if (!isToolAllowed(allowedTools, id, t.name)) continue;
            std::string name = agentToolName(id, t.name);
            if (bindings_.count(name)) continue;
            ToolDef def;
            def.name = name;
            def.description = "[MCP " + id + "] " + t.description;
            def.parameters = t.inputSchema.is_object() && t.inputSchema.contains("type")
                                 ? t.inputSchema
                                 : Json{{"type", "object"}, {"properties", Json::object()}};
            defs_.push_back(std::move(def));
            bindings_[name] = Binding{cfg, t.name};
        }
    }
}

ToolResult McpToolbox::call(const std::string& toolName, const Json& args) const {
    auto it = bindings_.find(toolName);
    if (it == bindings_.end())
        return {langPick(lang_, "ferramenta MCP desconhecida: ", "unknown MCP tool: ") + toolName, true};
    if (it->second.nativeServer)
        return native::call(it->second.nativeServer->id, it->second.tool, args, native::Context{root_, lang_});
    mcp::McpClient client;
    mcp::McpCallResult r = client.callTool(it->second.config, it->second.tool, args.is_object() ? args : Json::object(),
                                           kCallTimeoutMs);
    if (r.isError || !r.success) {
        std::string msg = r.errorMessage.empty() ? r.content : r.errorMessage;
        // Para agentes em ingles, o fallback padrao (pt) dos codigos estaveis ganha a versao em ingles.
        if (lang_ == RuntimeLang::En && !r.errorCode.empty()) {
            mcp::McpErrorCode code = mcp::mcpErrorCodeFromName(r.errorCode);
            if (code != mcp::McpErrorCode::Unknown) {
                msg = mcp::mcpErrorDefaultMessageEn(code, r.errorDetail);
            }
        }
        std::string prefix = langPick(lang_, "falha ao executar a ferramenta MCP", "failed to execute the MCP tool");
        return {truncated(msg.empty() ? prefix : (prefix + ": " + msg)), true};
    }
    return {truncated(r.content), false};
}

} // namespace rt
} // namespace taskforge
