#include "MemoryInstitutionApi.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int statusForError(const std::string& error) {
    if (error.rfind("nao autenticado", 0) == 0) return 401;
    if (error.rfind("permissao insuficiente", 0) == 0) return 403;
    if (error.rfind("instituicao nao configurada", 0) == 0) return 503;
    if (error.find("invalido") != std::string::npos ||
        error.find("obrigatorio") != std::string::npos) {
        return 400;
    }
    return 500;
}

InstitutionHttpResponse jsonResponse(int status, const taskforge::Json& body) {
    return InstitutionHttpResponse{status, body.dump()};
}

InstitutionHttpResponse errorResponse(int status, const std::string& error) {
    return jsonResponse(status, taskforge::Json{{"ok", false}, {"error", error}});
}

bool parseObject(const std::string& body, taskforge::Json& out, std::string& error) {
    auto parsed = taskforge::jsonutil::tryParse(body);
    if (!parsed || !parsed->is_object()) {
        error = "corpo JSON invalido: esperado um objeto";
        return false;
    }
    out = std::move(*parsed);
    return true;
}

bool readString(const taskforge::Json& input, const std::string& key,
                bool required, std::string& out, std::string& error) {
    out.clear();
    if (!input.contains(key) || input[key].is_null()) {
        if (required) error = "campo obrigatorio: " + key;
        return !required;
    }
    if (!input[key].is_string()) {
        error = "campo " + key + " invalido: esperado string";
        return false;
    }
    out = input[key].str();
    if (required && out.empty()) {
        error = "campo obrigatorio: " + key;
        return false;
    }
    return true;
}

bool readInt(const taskforge::Json& input, const std::string& key,
             int fallback, int minimum, int maximum, int& out, std::string& error) {
    out = fallback;
    if (!input.contains(key) || input[key].is_null()) return true;
    if (!input[key].is_number()) {
        error = "campo " + key + " invalido: esperado numero";
        return false;
    }
    try {
        const double value = input[key].get<double>();
        if (!std::isfinite(value) || value < -2147483648.0 || value > 2147483647.0 ||
            value != std::floor(value)) {
            error = "campo " + key + " invalido: esperado inteiro";
            return false;
        }
        out = std::max(minimum, std::min(maximum, static_cast<int>(value)));
    } catch (...) {
        error = "campo " + key + " invalido: esperado numero";
        return false;
    }
    return true;
}

bool readBool(const taskforge::Json& input, const std::string& key,
              bool fallback, bool& out, std::string& error) {
    out = fallback;
    if (!input.contains(key) || input[key].is_null()) return true;
    if (!input[key].is_boolean()) {
        error = "campo " + key + " invalido: esperado booleano";
        return false;
    }
    out = input[key].get<bool>();
    return true;
}

bool readDomains(const taskforge::Json& input, std::vector<std::string>& out,
                 std::string& error) {
    out.clear();
    if (!input.contains("domains") || input["domains"].is_null()) return true;
    if (!input["domains"].is_array()) {
        error = "campo domains invalido: esperado array de strings";
        return false;
    }
    for (const auto& item : input["domains"].arr()) {
        if (!item.is_string()) {
            error = "campo domains invalido: esperado array de strings";
            return false;
        }
        out.push_back(item.str());
    }
    return true;
}

taskforge::Json mcpTextResult(const std::string& text, bool isError) {
    return taskforge::Json{
        {"content", taskforge::Json::array({taskforge::Json{
                        {"type", "text"},
                        {"text", text}}})},
        {"isError", isError}};
}

taskforge::Json mcpResult(const taskforge::Json& id, const taskforge::Json& result) {
    return taskforge::Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

taskforge::Json mcpError(const taskforge::Json& id, int code, const std::string& message) {
    return taskforge::Json{{"jsonrpc", "2.0"},
                           {"id", id},
                           {"error", taskforge::Json{{"code", code}, {"message", message}}}};
}

taskforge::Json toolList() {
    using taskforge::Json;
    const Json querySchema = Json{
        {"type", "object"},
        {"properties", Json{{"query", Json{{"type", "string"}}},
                            {"limit", Json{{"type", "integer"}, {"minimum", 1}, {"maximum", 50}}},
                            {"domains", Json{{"type", "array"}, {"items", Json{{"type", "string"}}}}}}},
        {"required", Json::array({Json("query")})}};

    const Json lessonSchema = Json{
        {"type", "object"},
        {"properties", Json{{"domain", Json{{"type", "string"}}},
                            {"triggerError", Json{{"type", "string"}}},
                            {"lessonRule", Json{{"type", "string"}}}}},
        {"required", Json::array({Json("domain"), Json("triggerError"), Json("lessonRule")})}};

    const Json policySchema = Json{
        {"type", "object"},
        {"properties", Json{{"key", Json{{"type", "string"}}},
                            {"value", Json::object()},
                            {"immutable", Json{{"type", "boolean"}}}}},
        {"required", Json::array({Json("key")})}};

    return Json::array({
        Json{{"name", "memory_institution_context"},
             {"description", "Busca memoria institucional autorizada (viewer)."},
             {"inputSchema", querySchema}},
        Json{{"name", "memory_institution_write_lesson"},
             {"description", "Registra uma licao institucional com evidencia do principal (editor)."},
             {"inputSchema", lessonSchema}},
        Json{{"name", "memory_institution_status"},
             {"description", "Mostra instituicao, principal, papel e quantidade de licoes."},
             {"inputSchema", Json{{"type", "object"}, {"properties", Json::object()}}}},
        Json{{"name", "memory_institution_set_policy"},
             {"description", "Define uma politica institucional (admin)."},
             {"inputSchema", policySchema}}
    });
}

} // namespace

std::string institutionServiceVersion() {
#ifdef ARESTRIX_MEMORY_VERSION
    return ARESTRIX_MEMORY_VERSION;
#else
    return "dev";
#endif
}

bool loadInstitutionTokens(MemoryInstitutionService& service,
                           const std::string& tokensFile,
                           const std::string& tokensJson,
                           std::string& error) {
    if (!tokensJson.empty()) {
        auto parsed = taskforge::jsonutil::tryParse(tokensJson);
        if (!parsed || !parsed->is_array()) {
            error = "tokens JSON invalido: esperado um array";
            return false;
        }
        for (const auto& item : parsed->arr()) {
            if (!item.is_object()) {
                error = "tokens JSON invalido: cada item deve ser um objeto";
                return false;
            }
            std::string token;
            std::string principal;
            std::string role;
            std::string fieldError;
            if (!readString(item, "token", true, token, fieldError) ||
                !readString(item, "principal", true, principal, fieldError) ||
                !readString(item, "role", true, role, fieldError)) {
                error = "tokens JSON invalido: " + fieldError;
                return false;
            }
            if (!service.addToken(token, principal, role)) {
                error = "token invalido no JSON (token, principal e role viewer|editor|admin)";
                return false;
            }
        }
    }

    if (!tokensFile.empty()) {
        std::ifstream in(tokensFile);
        if (!in) {
            error = "nao foi possivel abrir o arquivo de tokens: " + tokensFile;
            return false;
        }
        std::string line;
        size_t lineNumber = 0;
        while (std::getline(in, line)) {
            ++lineNumber;
            const std::string clean = trim(line);
            if (clean.empty() || clean[0] == '#') continue;
            std::istringstream parts(clean);
            std::string token;
            std::string principal;
            std::string role;
            if (!(parts >> token >> principal >> role)) {
                error = "linha " + std::to_string(lineNumber) +
                        " invalida: use token principal viewer|editor|admin";
                return false;
            }
            if (!service.addToken(token, principal, role)) {
                error = "linha " + std::to_string(lineNumber) +
                        " invalida: papel deve ser viewer, editor ou admin";
                return false;
            }
        }
    }

    return true;
}

InstitutionHttpResponse handleInstitutionRest(MemoryInstitutionService& service,
                                              const std::string& method,
                                              const std::string& path,
                                              const std::string& token,
                                              const std::string& body) {
    using taskforge::Json;

    std::string route = path;
    const auto query = route.find('?');
    if (query != std::string::npos) route = route.substr(0, query);
    route = lower(route);
    if (route.size() > 1 && route.back() == '/') route.pop_back();
    const std::string verb = lower(method);

    if (route == "/health" && verb == "get") {
        return jsonResponse(200, Json{{"ok", true},
                                      {"service", "arestrix-memory"},
                                      {"version", institutionServiceVersion()},
                                      {"authRequired", true}});
    }

    if (route == "/v1/status" && verb == "get") {
        std::string error;
        Json status = service.status(token, error);
        if (!error.empty()) return errorResponse(statusForError(error), error);
        return jsonResponse(200, status);
    }

    if (route == "/v1/context" && verb == "post") {
        Json input;
        std::string error;
        if (!parseObject(body, input, error)) return errorResponse(400, error);
        std::string search;
        if (!readString(input, "query", true, search, error)) return errorResponse(400, error);
        int limit = 10;
        if (!readInt(input, "limit", 10, 1, 50, limit, error)) return errorResponse(400, error);
        std::vector<std::string> domains;
        if (!readDomains(input, domains, error)) return errorResponse(400, error);
        const std::string context = service.readContext(token, search, static_cast<size_t>(limit), domains, error);
        if (!error.empty()) return errorResponse(statusForError(error), error);
        return jsonResponse(200, Json{{"ok", true}, {"context", context}});
    }

    if (route == "/v1/lessons" && verb == "post") {
        Json input;
        std::string error;
        if (!parseObject(body, input, error)) return errorResponse(400, error);
        std::string domain;
        std::string triggerError;
        std::string lessonRule;
        if (!readString(input, "domain", true, domain, error) ||
            !readString(input, "triggerError", true, triggerError, error) ||
            !readString(input, "lessonRule", true, lessonRule, error)) {
            return errorResponse(400, error);
        }
        const std::string id = service.writeLesson(token, domain, triggerError, lessonRule, error);
        if (!error.empty()) return errorResponse(statusForError(error), error);
        return jsonResponse(200, Json{{"ok", true}, {"id", id}});
    }

    if (route == "/v1/policies" && verb == "post") {
        Json input;
        std::string error;
        if (!parseObject(body, input, error)) return errorResponse(400, error);
        std::string key;
        if (!readString(input, "key", true, key, error)) return errorResponse(400, error);
        bool immutable = false;
        if (!readBool(input, "immutable", false, immutable, error)) return errorResponse(400, error);
        if (!input.contains("value")) return errorResponse(400, "campo obrigatorio: value");
        const Json value = input["value"];
        if (!service.setPolicy(token, key, value, immutable, error)) {
            return errorResponse(statusForError(error), error);
        }
        return jsonResponse(200, Json{{"ok", true}, {"key", key}});
    }

    if (route == "/v1/status" || route == "/v1/context" ||
        route == "/v1/lessons" || route == "/v1/policies") {
        return errorResponse(405, "metodo nao permitido");
    }

    return errorResponse(404, "rota nao encontrada");
}

InstitutionMcpResponse handleInstitutionMcp(MemoryInstitutionService& service,
                                            const std::string& token,
                                            const taskforge::Json& request) {
    using taskforge::Json;

    InstitutionMcpResponse result;
    result.hasResponse = false;

    if (!request.is_object()) {
        result.hasResponse = true;
        result.response = mcpError(Json(), -32600, "requisicao JSON-RPC invalida");
        return result;
    }

    const bool hasId = request.contains("id") && !request["id"].is_null();
    const Json id = hasId ? request["id"] : Json();
    if (request.contains("method") && !request["method"].is_string()) {
        result.hasResponse = true;
        result.response = mcpError(id, -32600, "campo method invalido");
        return result;
    }
    const std::string method = request.value("method", std::string());

    if (method.rfind("notifications/", 0) == 0) return result;

    if (method.empty()) {
        result.hasResponse = true;
        result.response = mcpError(id, -32600, "metodo ausente");
        return result;
    }

    // Toda requisicao MCP (inclusive initialize e tools/list) exige token valido.
    // Notificacoes nao tem id e sao ignoradas acima, sem resposta.
    if (hasId) {
        MemoryPrincipal principal;
        if (!service.authenticate(token, principal)) {
            result.hasResponse = true;
            result.httpStatus = 401;
            result.response = mcpError(id, -32001, "nao autenticado");
            return result;
        }
    }

    if (method == "initialize") {
        result.hasResponse = true;
        result.response = mcpResult(id, Json{
            {"protocolVersion", "2025-03-26"},
            {"capabilities", Json{{"tools", Json::object()}}},
            {"serverInfo", Json{{"name", "arestrix-memory"}, {"version", institutionServiceVersion()}}}});
        return result;
    }

    if (method == "ping") {
        result.hasResponse = true;
        result.response = mcpResult(id, Json::object());
        return result;
    }

    if (method == "tools/list") {
        result.hasResponse = true;
        result.response = mcpResult(id, Json{{"tools", toolList()}});
        return result;
    }

    if (method != "tools/call") {
        result.hasResponse = true;
        result.response = mcpError(id, -32601, "metodo nao encontrado: " + method);
        return result;
    }

    const Json params = request.contains("params") && request["params"].is_object()
                            ? request["params"]
                            : Json::object();
    if (!params.contains("name") || !params["name"].is_string()) {
        result.hasResponse = true;
        result.response = mcpResult(id, mcpTextResult("campo params.name obrigatorio", true));
        return result;
    }
    const std::string tool = params["name"].str();
    if (params.contains("arguments") && !params["arguments"].is_object()) {
        result.hasResponse = true;
        result.response = mcpResult(id, mcpTextResult("campo params.arguments invalido", true));
        return result;
    }
    const Json args = params.contains("arguments") ? params["arguments"] : Json::object();

    result.hasResponse = true;

    if (tool == "memory_institution_status") {
        std::string error;
        const Json status = service.status(token, error);
        if (!error.empty()) {
            result.httpStatus = statusForError(error);
            result.response = mcpResult(id, mcpTextResult(error, true));
        } else {
            result.response = mcpResult(id, mcpTextResult(status.dump(2), false));
        }
        return result;
    }

    if (tool == "memory_institution_context") {
        std::string error;
        std::string query;
        int limit = 10;
        std::vector<std::string> domains;
        if (!readString(args, "query", true, query, error) ||
            !readInt(args, "limit", 10, 1, 50, limit, error) ||
            !readDomains(args, domains, error)) {
            result.response = mcpResult(id, mcpTextResult(error, true));
            return result;
        }
        const std::string context = service.readContext(token, query, static_cast<size_t>(limit), domains, error);
        if (!error.empty()) {
            result.httpStatus = statusForError(error);
            result.response = mcpResult(id, mcpTextResult(error, true));
        } else {
            result.response = mcpResult(id, mcpTextResult(context, false));
        }
        return result;
    }

    if (tool == "memory_institution_write_lesson") {
        std::string error;
        std::string domain;
        std::string triggerError;
        std::string lessonRule;
        if (!readString(args, "domain", true, domain, error) ||
            !readString(args, "triggerError", true, triggerError, error) ||
            !readString(args, "lessonRule", true, lessonRule, error)) {
            result.response = mcpResult(id, mcpTextResult(error, true));
            return result;
        }
        const std::string lessonId = service.writeLesson(token, domain, triggerError, lessonRule, error);
        if (!error.empty()) {
            result.httpStatus = statusForError(error);
            result.response = mcpResult(id, mcpTextResult(error, true));
        } else {
            result.response = mcpResult(id, mcpTextResult(lessonId, false));
        }
        return result;
    }

    if (tool == "memory_institution_set_policy") {
        std::string error;
        std::string key;
        bool immutable = false;
        if (!readString(args, "key", true, key, error) ||
            !readBool(args, "immutable", false, immutable, error)) {
            result.response = mcpResult(id, mcpTextResult(error, true));
            return result;
        }
        if (!args.contains("value")) {
            result.response = mcpResult(id, mcpTextResult("campo obrigatorio: value", true));
            return result;
        }
        const Json value = args["value"];
        if (!service.setPolicy(token, key, value, immutable, error)) {
            result.httpStatus = statusForError(error);
            result.response = mcpResult(id, mcpTextResult(error, true));
        } else {
            result.response = mcpResult(id, mcpTextResult("politica atualizada: " + key, false));
        }
        return result;
    }

    result.response = mcpResult(id, mcpTextResult("ferramenta desconhecida: " + tool, true));
    return result;
}

} // namespace memory
} // namespace taskforge
