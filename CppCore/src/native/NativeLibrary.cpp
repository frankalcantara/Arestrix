#include "native/NativeTools.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "libraries/LibraryCatalog.hpp"

namespace taskforge {
namespace native {

namespace {

using libraries::LibraryCatalog;
using rt::RuntimeLang;

constexpr int kDefaultLimit = 40;
constexpr int kMaxLimit = 100;

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

/**
 * Cache por projeto: o catalogo de 6 MB e indexado uma unica vez e reutilizado
 * por todas as chamadas nativas. O mutex por entrada serializa refreshes e
 * escritas, mantendo leituras consistentes mesmo com pipelines paralelos.
 */
struct CatalogEntry {
    std::mutex mutex;
    LibraryCatalog catalog;
    bool loaded = false;
};

CatalogEntry& entryFor(const std::filesystem::path& root) {
    static std::mutex mapMutex;
    static std::map<std::string, std::unique_ptr<CatalogEntry>> entries;
    const std::string key = root.lexically_normal().string();
    std::lock_guard<std::mutex> lock(mapMutex);
    auto& entry = entries[key];
    if (!entry) entry = std::make_unique<CatalogEntry>();
    return *entry;
}

struct LockedCatalog {
    explicit LockedCatalog(const Context& ctx) : entry(entryFor(ctx.workspaceRoot)), lock(entry.mutex) {
        if (!entry.loaded) {
            LibraryCatalog::load(ctx.workspaceRoot, entry.catalog, warning);
            entry.loaded = true;
        } else {
            entry.catalog.refresh(warning);
        }
    }

    CatalogEntry& entry;
    std::lock_guard<std::mutex> lock;
    std::string warning;
};

rt::ToolResult payloadResult(Json payload, const LockedCatalog& locked) {
    if (!locked.warning.empty() && payload.is_object()) payload["warning"] = locked.warning;
    const bool isError = payload.is_object() && payload.contains("ok") && payload.at("ok").is_boolean() &&
                         !payload.at("ok").get<bool>();
    return rt::ToolResult{payload.dump(), isError};
}

rt::ToolResult fail(RuntimeLang lang, const std::string& pt, const std::string& en) {
    return rt::ToolResult{rt::langPick(lang, pt.c_str(), en.c_str()), true};
}

Tool makeListTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_list_assets";
    tool.description = "Lista a Biblioteca de Vertices e Agentes (nativa + Workspace) com filtros de biblioteca, tipo, dominio e subdominio.";
    tool.descriptionEn = "Lists the Arestrix Vertex and Agent Library (native + Workspace) with library, kind, domain and subdomain filters.";
    tool.inputSchema = schema({
        {"library", "string", "Id da biblioteca (opcional).", "Library id (optional)."},
        {"kind", "string", "Tipo: agent-template, pipeline, skill ou mcp (opcional).", "Kind: agent-template, pipeline, skill or mcp (optional)."},
        {"domain", "string", "Filtro por dominio (opcional).", "Domain filter (optional)."},
        {"subdomain", "string", "Filtro por subdominio (opcional).", "Subdomain filter (optional)."},
        {"limit", "number", "Maximo de itens (padrao 40, maximo 100).", "Maximum items (default 40, max 100)."},
        {"offset", "number", "Deslocamento para paginar (opcional).", "Paging offset (optional)."}
    }, {}, lang);
    tool.risk = "safe";
    tool.handler = [](const Json& args, const Context& ctx) {
        LockedCatalog locked(ctx);
        int limit = 0;
        int offset = 0;
        if (auto n = argNum(args, "limit")) limit = static_cast<int>(*n);
        if (auto n = argNum(args, "offset")) offset = static_cast<int>(*n);
        Json result = locked.entry.catalog.list(argStr(args, "library"), argStr(args, "kind"),
                                                argStr(args, "domain"), argStr(args, "subdomain"),
                                                limit, offset);
        return payloadResult(std::move(result), locked);
    };
    return tool;
}

Tool makeGetTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_get_asset";
    tool.description = "Le um ativo da biblioteca pelo id: metadados, auditoria, payload, template e agentSettings completos.";
    tool.descriptionEn = "Reads a library asset by id: metadata, audit, payload, template and full agentSettings.";
    tool.inputSchema = schema({
        {"id", "string", "Id do ativo (veja library_list_assets).", "Asset id (see library_list_assets)."}
    }, {"id"}, lang);
    tool.risk = "safe";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string id = argStr(args, "id");
        if (id.empty()) return fail(ctx.lang, "Informe o id do ativo.", "Provide the asset id.");
        LockedCatalog locked(ctx);
        auto asset = locked.entry.catalog.detail(id);
        if (!asset) {
            return fail(ctx.lang, "Ativo nao encontrado na biblioteca: " + id,
                        "Asset not found in the library: " + id);
        }
        return payloadResult(*asset, locked);
    };
    return tool;
}

Tool makeSearchTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_search_assets";
    tool.description = "Busca ativos da biblioteca por nome, descricao, categoria, dominio ou subdominio.";
    tool.descriptionEn = "Searches library assets by name, description, category, domain or subdomain.";
    tool.inputSchema = schema({
        {"query", "string", "Texto da busca.", "Search text."},
        {"kind", "string", "Tipo de ativo (opcional).", "Asset kind (optional)."},
        {"limit", "number", "Maximo de itens (padrao 40, maximo 100).", "Maximum items (default 40, max 100)."}
    }, {"query"}, lang);
    tool.risk = "safe";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string query = argStr(args, "query");
        if (query.empty()) return fail(ctx.lang, "Informe um texto para buscar.", "Provide a search text.");
        LockedCatalog locked(ctx);
        int limit = 0;
        if (auto n = argNum(args, "limit")) limit = static_cast<int>(*n);
        return payloadResult(locked.entry.catalog.search(query, argStr(args, "kind"), limit), locked);
    };
    return tool;
}

Tool makeInstallTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_install_asset";
    tool.description = "Instala uma skill ou MCP aprovado no Workspace fisico (.workspace/skills ou .workspace/mcps). REJECT/PENDING falham; CAUTION exige allow_caution=true.";
    tool.descriptionEn = "Installs an approved skill or MCP into the physical Workspace (.workspace/skills or .workspace/mcps). REJECT/PENDING fail; CAUTION requires allow_caution=true.";
    tool.inputSchema = schema({
        {"id", "string", "Id do ativo (veja library_list_assets).", "Asset id (see library_list_assets)."},
        {"allow_caution", "boolean", "Aceita explicitamente um veredito CAUTION.", "Explicitly accept a CAUTION verdict."}
    }, {"id"}, lang);
    tool.risk = "moderate";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string id = argStr(args, "id");
        if (id.empty()) return fail(ctx.lang, "Informe o id do ativo.", "Provide the asset id.");
        LockedCatalog locked(ctx);
        std::string error;
        Json result = locked.entry.catalog.installAsset(id, argBool(args, "allow_caution"), error);
        return payloadResult(std::move(result), locked);
    };
    return tool;
}

Tool makeDeriveTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_derive_asset";
    tool.description = "Cria um derivado editavel em \"Minha biblioteca\" (ou outra biblioteca do Workspace). O ativo de origem permanece imutavel.";
    tool.descriptionEn = "Creates an editable derivative in \"Minha biblioteca\" (or another workspace library). The source asset stays immutable.";
    tool.inputSchema = schema({
        {"asset_id", "string", "Id do ativo de origem.", "Source asset id."},
        {"name", "string", "Nome do derivado (opcional).", "Derivative name (optional)."},
        {"library_id", "string", "Biblioteca de destino (opcional).", "Target library id (optional)."}
    }, {"asset_id"}, lang);
    tool.risk = "moderate";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string id = argStr(args, "asset_id");
        if (id.empty()) return fail(ctx.lang, "Informe o asset_id de origem.", "Provide the source asset_id.");
        LockedCatalog locked(ctx);
        std::string error;
        Json result = locked.entry.catalog.deriveAsset(id, argStr(args, "name"), argStr(args, "library_id"), error);
        return payloadResult(std::move(result), locked);
    };
    return tool;
}

Tool makeRestoreTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_restore_asset";
    tool.description = "Restaura um ativo nativo para o padrao de instalacao de fabrica (payload, auditoria e status).";
    tool.descriptionEn = "Restores a native bundled asset to its factory install default (payload, audit and status).";
    tool.inputSchema = schema({
        {"asset_id", "string", "Id do ativo nativo (library-arestrix).", "Bundled asset id (library-arestrix)."}
    }, {"asset_id"}, lang);
    tool.risk = "moderate";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string id = argStr(args, "asset_id");
        if (id.empty()) return fail(ctx.lang, "Informe o asset_id nativo.", "Provide the bundled asset_id.");
        LockedCatalog locked(ctx);
        std::string error;
        Json result = locked.entry.catalog.restoreAsset(id, error);
        return payloadResult(std::move(result), locked);
    };
    return tool;
}

Tool makeAuditTool(RuntimeLang lang) {
    Tool tool;
    tool.name = "library_audit_asset";
    tool.description = "Reaudita um ativo com o scanner nativo C++ (esquema de template, fronteira de skill e supply chain de MCP), atualiza hash/veredito e persiste.";
    tool.descriptionEn = "Re-audits an asset with the native C++ scanner (template schema, skill boundary and MCP supply chain), updates hash/verdict and persists.";
    tool.inputSchema = schema({
        {"asset_id", "string", "Id do ativo (veja library_list_assets).", "Asset id (see library_list_assets)."}
    }, {"asset_id"}, lang);
    tool.risk = "moderate";
    tool.handler = [](const Json& args, const Context& ctx) {
        const std::string id = argStr(args, "asset_id");
        if (id.empty()) return fail(ctx.lang, "Informe o asset_id.", "Provide the asset_id.");
        LockedCatalog locked(ctx);
        std::string error;
        Json result = locked.entry.catalog.auditAsset(id, error);
        return payloadResult(std::move(result), locked);
    };
    return tool;
}

} // namespace

Server makeLibraryServer(RuntimeLang lang) {
    Server server;
    server.id = "arestrix.biblioteca";
    server.name = "Biblioteca de Vertices e Agentes (Nativo C++)";
    server.description = "Lista, le, instala, deriva, restaura e audita ativos da biblioteca de vertices, agentes, skills e MCPs.";
    server.descriptionEn = "Lists, reads, installs, derives, restores and audits assets from the vertex, agent, skill and MCP library.";
    server.category = "Biblioteca & Agentes";
    server.tools = {
        makeListTool(lang),
        makeGetTool(lang),
        makeSearchTool(lang),
        makeInstallTool(lang),
        makeDeriveTool(lang),
        makeRestoreTool(lang),
        makeAuditTool(lang)
    };
    return server;
}

} // namespace native
} // namespace taskforge
