#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace taskforge {
namespace mcp {

enum class McpTransportType {
    Stdio,
    Sse
};

NLOHMANN_JSON_SERIALIZE_ENUM(McpTransportType, {
    {McpTransportType::Stdio, "stdio"},
    {McpTransportType::Sse, "sse"}
})

struct McpTool {
    std::string name;
    std::string description;
    nlohmann::json inputSchema = nlohmann::json::object();
    std::string serverId;
};

inline void to_json(nlohmann::json& j, const McpTool& t) {
    j = nlohmann::json{
        {"name", t.name},
        {"description", t.description},
        {"inputSchema", t.inputSchema},
        {"serverId", t.serverId}
    };
}

inline void from_json(const nlohmann::json& j, McpTool& t) {
    t.name = j.value("name", "");
    t.description = j.value("description", "");
    t.inputSchema = j.value("inputSchema", nlohmann::json::object());
    t.serverId = j.value("serverId", "");
}

struct McpServerConfig {
    std::string id;
    std::string name;
    McpTransportType transport = McpTransportType::Stdio;
    std::string command;                       // Para stdio (ex: "npx", "uvx", "python")
    std::vector<std::string> args;             // Argumentos do comando
    std::map<std::string, std::string> env;    // Variáveis de ambiente opcionais
    std::string url;                           // Para transporte SSE (ex: "http://localhost:8000/sse")
    bool isEnabled = true;
    std::vector<McpTool> cachedTools;
};

inline void to_json(nlohmann::json& j, const McpServerConfig& s) {
    j = nlohmann::json{
        {"id", s.id},
        {"name", s.name},
        {"transport", s.transport},
        {"command", s.command},
        {"args", s.args},
        {"env", s.env},
        {"url", s.url},
        {"isEnabled", s.isEnabled},
        {"cachedTools", s.cachedTools}
    };
}

inline void from_json(const nlohmann::json& j, McpServerConfig& s) {
    s.id = j.value("id", "");
    s.name = j.value("name", "");
    s.transport = j.value("transport", McpTransportType::Stdio);
    s.command = j.value("command", "");
    if (j.contains("args")) s.args = j["args"].get<std::vector<std::string>>();
    if (j.contains("env")) s.env = j["env"].get<std::map<std::string, std::string>>();
    s.url = j.value("url", "");
    s.isEnabled = j.value("isEnabled", true);
    if (j.contains("cachedTools")) s.cachedTools = j["cachedTools"].get<std::vector<McpTool>>();
}

struct McpCallResult {
    bool success = false;
    std::string content;
    bool isError = false;
    std::string errorMessage;
    double durationSeconds = 0.0;
};

inline void to_json(nlohmann::json& j, const McpCallResult& r) {
    j = nlohmann::json{
        {"success", r.success},
        {"content", r.content},
        {"isError", r.isError},
        {"errorMessage", r.errorMessage},
        {"durationSeconds", r.durationSeconds}
    };
}

inline void from_json(const nlohmann::json& j, McpCallResult& r) {
    r.success = j.value("success", false);
    r.content = j.value("content", "");
    r.isError = j.value("isError", false);
    r.errorMessage = j.value("errorMessage", "");
    r.durationSeconds = j.value("durationSeconds", 0.0);
}

} // namespace mcp
} // namespace taskforge
