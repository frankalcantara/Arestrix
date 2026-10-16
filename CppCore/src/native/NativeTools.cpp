#include "native/NativeTools.hpp"

#include <algorithm>
#include <map>

namespace taskforge {
namespace native {

namespace {
constexpr size_t kMaxResultBytes = 64 * 1024;

std::string clip(const std::string& s, rt::RuntimeLang lang) {
    if (s.size() <= kMaxResultBytes) return s;
    size_t cut = kMaxResultBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + rt::langPick(lang,
                                           "\n[... resultado truncado em 64 KB ...]",
                                           "\n[... result truncated at 64 KB ...]");
}
} // namespace

bool isNativeServer(const std::string& id) {
    return id.rfind(kIdPrefix, 0) == 0;
}

const std::vector<Server>& serversForLang(rt::RuntimeLang lang) {
    // O catalogo e construido uma vez por idioma (os handlers sao os mesmos lambdas; so os textos mudam).
    static std::map<rt::RuntimeLang, std::vector<Server>> cache;
    auto it = cache.find(lang);
    if (it == cache.end()) {
        std::vector<Server> v;
        v.push_back(makeFilesServer(lang));
        v.push_back(makeHttpServer(lang));
        v.push_back(makeDataServer(lang));
        v.push_back(makeMemoryServer(lang));
        v.push_back(makeTimeServer(lang));
        v.push_back(makeGitServer(lang));
        v.push_back(makeSqliteServer(lang));
        if (lang == rt::RuntimeLang::En) {
            // make*Server monta description (pt) e descriptionEn (en); para o catalogo em ingles
            // a descricao principal passa a ser a inglesa.
            for (auto& s : v) {
                if (!s.descriptionEn.empty()) std::swap(s.description, s.descriptionEn);
                for (auto& t : s.tools)
                    if (!t.descriptionEn.empty()) std::swap(t.description, t.descriptionEn);
            }
        }
        it = cache.emplace(lang, std::move(v)).first;
    }
    return it->second;
}

const std::vector<Server>& servers() {
    return serversForLang(rt::RuntimeLang::Pt);
}

const Server* findServer(const std::string& id) {
    for (const auto& s : servers())
        if (s.id == id) return &s;
    return nullptr;
}

const Tool* findTool(const Server& server, const std::string& toolName) {
    for (const auto& t : server.tools)
        if (t.name == toolName) return &t;
    return nullptr;
}

Json schema(const std::vector<Prop>& properties, const std::vector<std::string>& required, rt::RuntimeLang lang) {
    Json props = Json::object();
    for (const auto& p : properties) {
        Json item;
        item["type"] = p.type;
        const std::string desc = rt::langPick(lang, p.description.c_str(),
                                              p.descriptionEn.empty() ? p.description.c_str() : p.descriptionEn.c_str());
        if (!desc.empty()) item["description"] = desc;
        props[p.name] = item;
    }
    Json out;
    out["type"] = "object";
    out["properties"] = props;
    Json req = Json::array();
    for (const auto& r : required) req.push_back(r);
    out["required"] = req;
    return out;
}

rt::ToolResult call(const std::string& serverId, const std::string& toolName, const Json& args, const Context& ctx) {
    const Server* server = findServer(serverId);
    if (!server) return {rt::langPick(ctx.lang, "servidor nativo desconhecido: ", "unknown native server: ") + serverId, true};
    const Tool* tool = findTool(*server, toolName);
    if (!tool)
        return {rt::langPick(ctx.lang, "ferramenta desconhecida em ", "unknown tool on ") + serverId + ": " + toolName, true};

    const Json& a = args.is_object() ? args : Json::object();
    const Json& required = tool->inputSchema["required"];
    for (size_t i = 0; i < required.size(); ++i) {
        const std::string key = required[i].get<std::string>();
        if (!a.contains(key) || a[key].is_null())
            return {rt::langPick(ctx.lang, "argumento obrigatorio ausente: ", "missing required argument: ") + key, true};
    }
    try {
        rt::ToolResult r = tool->handler(a, ctx);
        r.content = clip(r.content, ctx.lang);
        return r;
    } catch (const std::exception& ex) {
        return {rt::langPick(ctx.lang, "erro em ", "error on ") + serverId + "." + toolName + ": " + ex.what(), true};
    }
}

} // namespace native
} // namespace taskforge
