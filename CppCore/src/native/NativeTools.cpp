#include "native/NativeTools.hpp"

#include <algorithm>

namespace taskforge {
namespace native {

namespace {
constexpr size_t kMaxResultBytes = 64 * 1024;

std::string clip(const std::string& s) {
    if (s.size() <= kMaxResultBytes) return s;
    size_t cut = kMaxResultBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\n[... resultado truncado em 64 KB ...]";
}
} // namespace

bool isNativeServer(const std::string& id) {
    return id.rfind(kIdPrefix, 0) == 0;
}

const std::vector<Server>& servers() {
    static const std::vector<Server> all = [] {
        std::vector<Server> v;
        v.push_back(makeFilesServer());
        v.push_back(makeHttpServer());
        v.push_back(makeDataServer());
        v.push_back(makeMemoryServer());
        v.push_back(makeTimeServer());
        v.push_back(makeGitServer());
        v.push_back(makeSqliteServer());
        return v;
    }();
    return all;
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

Json schema(const std::vector<Prop>& properties, const std::vector<std::string>& required) {
    Json props = Json::object();
    for (const auto& p : properties) {
        Json item;
        item["type"] = p.type;
        if (!p.description.empty()) item["description"] = p.description;
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
    if (!server) return {"servidor nativo desconhecido: " + serverId, true};
    const Tool* tool = findTool(*server, toolName);
    if (!tool) return {"ferramenta desconhecida em " + serverId + ": " + toolName, true};

    const Json& a = args.is_object() ? args : Json::object();
    const Json& required = tool->inputSchema["required"];
    for (size_t i = 0; i < required.size(); ++i) {
        const std::string key = required[i].get<std::string>();
        if (!a.contains(key) || a[key].is_null()) return {"argumento obrigatorio ausente: " + key, true};
    }
    try {
        rt::ToolResult r = tool->handler(a, ctx);
        r.content = clip(r.content);
        return r;
    } catch (const std::exception& ex) {
        return {std::string("erro em ") + serverId + "." + toolName + ": " + ex.what(), true};
    }
}

} // namespace native
} // namespace taskforge
