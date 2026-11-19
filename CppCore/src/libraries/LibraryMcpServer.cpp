#include "libraries/LibraryMcpServer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "libraries/LibraryCatalog.hpp"
#include "util/Json.hpp"

#ifndef ARESTRIX_LIBRARY_MCP_VERSION
#define ARESTRIX_LIBRARY_MCP_VERSION "1.0.0"
#endif

namespace taskforge {
namespace libraries {

namespace {

namespace fs = std::filesystem;

Json prop(const char* type, const std::string& description) {
    return Json{{"type", type}, {"description", description}};
}

Json objectSchema(Json properties, std::vector<std::string> required = {}) {
    Json schema = Json::object();
    schema["type"] = "object";
    schema["properties"] = std::move(properties);
    Json req = Json::array();
    for (const auto& key : required) req.push_back(key);
    schema["required"] = std::move(req);
    return schema;
}

Json toolDef(const char* name, const std::string& description, Json schema) {
    return Json{{"name", name}, {"description", description}, {"inputSchema", std::move(schema)}};
}

const Json& toolDefinitions() {
    static const Json tools = []() {
        Json list = Json::array();
        list.push_back(toolDef(
            "library_list_assets",
            "Lists the Arestrix Vertex and Agent Library (native + Workspace): id, kind, library, domain, "
            "subdomain, status, verdict and node type. Filter by library, kind, domain or subdomain.",
            objectSchema(Json{{"library", prop("string", "Library id (optional).")},
                              {"kind", prop("string", "Asset kind: agent-template, pipeline, skill or mcp (optional).")},
                              {"domain", prop("string", "Domain filter (optional).")},
                              {"subdomain", prop("string", "Subdomain filter (optional).")},
                              {"limit", prop("number", "Maximum items (default 40, max 100).")},
                              {"offset", prop("number", "Paging offset (optional).")}})));
        list.push_back(toolDef(
            "library_get_asset",
            "Reads a library asset by id: metadata, audit, payload, template and full agent settings. "
            "Long Markdown texts are truncated; the asset itself is not modified.",
            objectSchema(Json{{"id", prop("string", "Asset id (see library_list_assets).")}}, {"id"})));
        list.push_back(toolDef(
            "library_search_assets",
            "Searches library assets by name, description, category, domain or subdomain. "
            "Returns compact rows; use library_get_asset for the full asset.",
            objectSchema(Json{{"query", prop("string", "Search text.")},
                              {"kind", prop("string", "Asset kind (optional).")},
                              {"limit", prop("number", "Maximum items (default 40, max 100).")}},
                         {"query"})));
        list.push_back(toolDef(
            "library_install_asset",
            "Installs an approved skill or MCP asset into the physical Workspace (.workspace/skills or "
            ".workspace/mcps). REJECT/PENDING fail; CAUTION requires allow_caution=true. "
            "Agent templates and pipelines are inserted into a graph by the Studio Assistant, not installed.",
            objectSchema(Json{{"id", prop("string", "Asset id (see library_list_assets).")},
                              {"allow_caution", prop("boolean", "Explicitly accept a CAUTION verdict (default false).")}},
                         {"id"})));
        list.push_back(toolDef(
            "library_derive_asset",
            "Creates an editable derivative of an asset in \"Minha biblioteca\" (or another workspace library). "
            "The source asset stays immutable; the derivative records derivedFrom and is audited before saving.",
            objectSchema(Json{{"asset_id", prop("string", "Source asset id.")},
                              {"name", prop("string", "Derivative name (optional; default = source name + (copia)).")},
                              {"library_id", prop("string", "Target workspace library id (optional).")}},
                         {"asset_id"})));
        list.push_back(toolDef(
            "library_restore_asset",
            "Restores a native bundled asset to its factory install default (payload, audit and status).",
            objectSchema(Json{{"asset_id", prop("string", "Bundled asset id (library-arestrix).")}},
                         {"asset_id"})));
        list.push_back(toolDef(
            "library_audit_asset",
            "Re-audits an asset with the native Arestrix scanner (template schema, skill boundary and MCP "
            "supply-chain checks), updates its hash/verdict and persists the result.",
            objectSchema(Json{{"asset_id", prop("string", "Asset id (see library_list_assets).")}},
                         {"asset_id"})));
        return list;
    }();
    return tools;
}

std::string argStr(const Json& args, const char* key) {
    return args.is_object() && args.contains(key) && args.at(key).is_string()
               ? args.at(key).get<std::string>() : std::string();
}

std::optional<double> argNum(const Json& args, const char* key) {
    if (!args.is_object() || !args.contains(key) || !args.at(key).is_number()) return std::nullopt;
    return args.at(key).get<double>();
}

bool argBool(const Json& args, const char* key, bool fallback = false) {
    if (!args.is_object() || !args.contains(key) || !args.at(key).is_boolean()) return fallback;
    return args.at(key).get<bool>();
}

struct CallResult {
    Json payload;
    bool isError = false;
};

CallResult errorResult(std::string message) {
    CallResult result;
    result.isError = true;
    result.payload = Json{{"ok", false}, {"reason", std::move(message)}};
    return result;
}

class ServerState {
public:
    explicit ServerState(fs::path root) : root_(std::move(root)) {}

    Json call(const std::string& name, const Json& args) {
        if (!loaded_) {
            std::string warning;
            LibraryCatalog::load(root_, catalog_, warning);
            loaded_ = true;
            warning_ = std::move(warning);
        } else {
            std::string warning;
            catalog_.refresh(warning);
            if (!warning.empty()) warning_ = std::move(warning);
        }

        if (name == "library_list_assets") {
            int limit = 0;
            int offset = 0;
            if (auto n = argNum(args, "limit")) limit = static_cast<int>(*n);
            if (auto n = argNum(args, "offset")) offset = static_cast<int>(*n);
            Json payload = catalog_.list(argStr(args, "library"), argStr(args, "kind"),
                                         argStr(args, "domain"), argStr(args, "subdomain"),
                                         limit, offset);
            return withWarning(payload);
        }
        if (name == "library_search_assets") {
            const std::string query = argStr(args, "query");
            if (query.empty()) return errorResult("A busca exige o parametro 'query'.").payload;
            int limit = 0;
            if (auto n = argNum(args, "limit")) limit = static_cast<int>(*n);
            return withWarning(catalog_.search(query, argStr(args, "kind"), limit));
        }
        if (name == "library_get_asset") {
            const std::string id = argStr(args, "id");
            auto asset = catalog_.detail(id);
            if (!asset) {
                return Json{{"ok", false}, {"reason", "Ativo nao encontrado: " + id}};
            }
            return withWarning(*asset);
        }
        if (name == "library_install_asset") {
            std::string error;
            Json result = catalog_.installAsset(argStr(args, "id"), argBool(args, "allow_caution"), error);
            return result;
        }
        if (name == "library_derive_asset") {
            std::string error;
            return catalog_.deriveAsset(argStr(args, "asset_id"), argStr(args, "name"),
                                        argStr(args, "library_id"), error);
        }
        if (name == "library_restore_asset") {
            std::string error;
            return catalog_.restoreAsset(argStr(args, "asset_id"), error);
        }
        if (name == "library_audit_asset") {
            std::string error;
            return catalog_.auditAsset(argStr(args, "asset_id"), error);
        }
        return Json{{"ok", false}, {"reason", "Ferramenta desconhecida: " + name}};
    }

private:
    Json withWarning(Json payload) {
        if (!warning_.empty()) payload["warning"] = warning_;
        return payload;
    }

    fs::path root_;
    LibraryCatalog catalog_;
    bool loaded_ = false;
    std::string warning_;
};

bool resultIsError(const Json& payload) {
    return payload.is_object() && payload.contains("ok") && payload.at("ok").is_boolean() &&
           !payload.at("ok").get<bool>();
}

Json toolCallResult(const std::string& name, const Json& args, ServerState& state) {
    Json payload = state.call(name, args);
    const bool isError = resultIsError(payload);
    Json content = Json::array();
    content.push_back(Json{{"type", "text"}, {"text", payload.dump()}});
    Json result = Json::object();
    result["content"] = std::move(content);
    if (isError) result["isError"] = true;
    return result;
}

Json responseFor(const Json& id, Json result) {
    Json response = Json::object();
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = std::move(result);
    return response;
}

Json errorResponse(const Json& id, int code, const std::string& message) {
    Json response = Json::object();
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["error"] = Json{{"code", code}, {"message", message}};
    return response;
}

void writeMessage(std::ostream& out, const Json& message) {
    out << message.dump() << '\n';
    out.flush();
}

void handleRequest(const Json& request, std::ostream& out, ServerState& state, bool& stop) {
    if (!request.is_object()) {
        writeMessage(out, errorResponse(Json(), -32600, "Invalid Request"));
        return;
    }
    const bool notification = !request.contains("id");
    const Json id = notification ? Json() : request.at("id");
    const std::string method = request.value("method", std::string());

    auto reply = [&](Json result) {
        if (!notification) writeMessage(out, responseFor(id, std::move(result)));
    };

    if (method == "initialize") {
        const Json& params = request.contains("params") ? request.at("params") : Json();
        std::string protocol = params.is_object() ? params.value("protocolVersion", std::string()) : std::string();
        if (protocol.empty()) protocol = "2024-11-05";
        Json result = Json::object();
        result["protocolVersion"] = protocol;
        result["capabilities"] = Json{{"tools", Json{{"listChanged", false}}}};
        result["serverInfo"] = Json{{"name", "arestrix-mcp-biblioteca"},
                                    {"version", ARESTRIX_LIBRARY_MCP_VERSION}};
        reply(std::move(result));
        return;
    }
    if (method == "notifications/initialized" || method == "notifications/cancelled" ||
        method == "notifications/roots/list_changed") {
        return;
    }
    if (method == "ping") {
        reply(Json::object());
        return;
    }
    if (method == "tools/list") {
        reply(Json{{"tools", toolDefinitions()}});
        return;
    }
    if (method == "tools/call") {
        const Json& params = request.contains("params") ? request.at("params") : Json();
        const std::string name = params.is_object() ? params.value("name", std::string()) : std::string();
        const Json args = params.is_object() && params.contains("arguments") && params.at("arguments").is_object()
                              ? params.at("arguments") : Json::object();
        if (name.empty()) {
            reply(errorResponse(id, -32602, "Missing tool name"));
            return;
        }
        reply(toolCallResult(name, args, state));
        return;
    }
    if (method == "shutdown") {
        reply(Json::object());
        return;
    }
    if (method == "exit") {
        stop = true;
        return;
    }
    if (notification) return;
    writeMessage(out, errorResponse(id, -32601, "Method not found: " + method));
}

} // namespace

int runLibraryMcpServer(std::istream& in, std::ostream& out, const fs::path& projectRoot) {
    ServerState state(projectRoot);
    bool stop = false;
    std::string line;
    line.reserve(1 << 16);
    while (!stop && std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        Json request;
        try {
            request = Json::parse(line);
        } catch (const std::exception&) {
            writeMessage(out, errorResponse(Json(), -32700, "Parse error"));
            continue;
        }

        if (request.is_array()) {
            for (const auto& item : request.arr()) handleRequest(item, out, state, stop);
        } else {
            handleRequest(request, out, state, stop);
        }
    }
    return 0;
}

} // namespace libraries
} // namespace taskforge
