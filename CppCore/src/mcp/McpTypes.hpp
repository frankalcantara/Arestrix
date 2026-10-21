#pragma once

#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace mcp {

enum class McpTransportType {
    Stdio,
    Sse
};

ARX_JSON_ENUM(McpTransportType, {
    {McpTransportType::Stdio, "stdio"},
    {McpTransportType::Sse, "sse"}
})

struct McpTool {
    std::string name;
    std::string description;
    taskforge::Json inputSchema = taskforge::Json::object();
    std::string serverId;
};

inline void to_json(taskforge::Json& j, const McpTool& t) {
    j = taskforge::Json{
        {"name", t.name},
        {"description", t.description},
        {"inputSchema", t.inputSchema},
        {"serverId", t.serverId}
    };
}

inline void from_json(const taskforge::Json& j, McpTool& t) {
    t.name = j.value("name", "");
    t.description = j.value("description", "");
    t.inputSchema = j.value("inputSchema", taskforge::Json::object());
    t.serverId = j.value("serverId", "");
}

struct McpServerConfig {
    std::string id;
    std::string name;
    McpTransportType transport = McpTransportType::Stdio;
    std::string command;                       // Para stdio (ex: "npx", "uvx", "python")
    std::vector<std::string> args;             // Argumentos do comando
    std::map<std::string, std::string> env;    // Variáveis de ambiente opcionais
    std::string url;                           // Para transporte HTTP/SSE (ex: "https://host/mcp")
    // Cabecalhos HTTP opcionais (ex: X-API-Key). Os valores aceitam ${VARIAVEL} e sao expandidos
    // no momento da chamada; o arquivo de configuracao nunca deve guardar a chave literal.
    std::map<std::string, std::string> headers;
    bool isEnabled = true;
    std::vector<McpTool> cachedTools;
};

inline void to_json(taskforge::Json& j, const McpServerConfig& s) {
    j = taskforge::Json{
        {"id", s.id},
        {"name", s.name},
        {"transport", s.transport},
        {"command", s.command},
        {"args", s.args},
        {"env", s.env},
        {"url", s.url},
        {"headers", s.headers},
        {"isEnabled", s.isEnabled},
        {"cachedTools", s.cachedTools}
    };
}

inline void from_json(const taskforge::Json& j, McpServerConfig& s) {
    s.id = j.value("id", "");
    s.name = j.value("name", "");
    s.command = j.value("command", "");
    s.url = j.value("url", "");

    // Formatos aceitos: "stdio", "sse" e "http"/"streamable-http" (o cliente usa o mesmo
    // caminho Streamable HTTP para os dois ultimos). Sem "transport", uma URL implica HTTP.
    s.transport = s.url.empty() ? McpTransportType::Stdio : McpTransportType::Sse;
    if (j.contains("transport") && j["transport"].is_string()) {
        std::string t = j["transport"].str();
        for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (t == "http" || t == "streamable-http" || t == "streamable_http" || t == "sse") {
            s.transport = McpTransportType::Sse;
        } else if (t == "stdio") {
            s.transport = McpTransportType::Stdio;
        }
    }

    if (j.contains("args")) s.args = j["args"].get<std::vector<std::string>>();
    if (j.contains("env")) s.env = j["env"].get<std::map<std::string, std::string>>();
    if (j.contains("headers") && j["headers"].is_object()) {
        for (auto it = j["headers"].begin(); it != j["headers"].end(); ++it) {
            if (!it->is_string()) throw std::invalid_argument("valor de header MCP deve ser string");
            const std::string name = it.key();
            const std::string value = it->str();
            if (name.empty() || name.size() > 100) throw std::invalid_argument("nome de header MCP invalido");
            for (unsigned char c : name)
                if (!(std::isalnum(c) || c == '-' || c == '_')) throw std::invalid_argument("nome de header MCP invalido");
            if (value.size() > 4096 || value.find('\r') != std::string::npos || value.find('\n') != std::string::npos)
                throw std::invalid_argument("valor de header MCP invalido");
            s.headers[name] = value;
        }
    }
    s.isEnabled = j.value("isEnabled", true);
    if (j.contains("cachedTools")) s.cachedTools = j["cachedTools"].get<std::vector<McpTool>>();
}

struct McpCallResult {
    bool success = false;
    std::string content;
    bool isError = false;
    std::string errorMessage;
    std::string errorCode;    // codigo estavel para o i18n do frontend (mcp/McpErrors.hpp)
    std::string errorDetail;  // parte dinamica (id do servidor, host, variavel...)
    double durationSeconds = 0.0;
};

inline void to_json(taskforge::Json& j, const McpCallResult& r) {
    j = taskforge::Json{
        {"success", r.success},
        {"content", r.content},
        {"isError", r.isError},
        {"errorMessage", r.errorMessage},
        {"errorCode", r.errorCode},
        {"errorDetail", r.errorDetail},
        {"durationSeconds", r.durationSeconds}
    };
}

inline void from_json(const taskforge::Json& j, McpCallResult& r) {
    r.success = j.value("success", false);
    r.content = j.value("content", "");
    r.isError = j.value("isError", false);
    r.errorMessage = j.value("errorMessage", "");
    r.errorCode = j.value("errorCode", "");
    r.errorDetail = j.value("errorDetail", "");
    r.durationSeconds = j.value("durationSeconds", 0.0);
}

} // namespace mcp
} // namespace taskforge
