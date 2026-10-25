#include "MemoryInstitutionServer.hpp"

#include <iostream>

#include "util/Json.hpp"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4996)
#endif
#include "httplib.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace taskforge {
namespace memory {

namespace {

std::string bearerToken(const httplib::Request& request) {
    const std::string auth = request.get_header_value("Authorization");
    if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7);
    const std::string direct = request.get_header_value("X-Arestrix-Token");
    if (!direct.empty()) return direct;
    return "";
}

void writeResponse(httplib::Response& response, const InstitutionHttpResponse& result) {
    response.status = result.status;
    response.set_content(result.body, "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Content-Type-Options", "nosniff");
    if (result.status == 401) response.set_header("WWW-Authenticate", "Bearer");
}

} // namespace

MemoryInstitutionServer::MemoryInstitutionServer(MemoryInstitutionService& service)
    : service_(service) {}

bool MemoryInstitutionServer::run(const std::string& host, int port, std::string& error) {
    error.clear();
    httplib::Server server;
    server.set_payload_max_length(1024 * 1024);
    server.set_read_timeout(30, 0);

    auto rest = [this](const httplib::Request& request, httplib::Response& response) {
        try {
            writeResponse(response, handleInstitutionRest(service_, request.method, request.path,
                                                          bearerToken(request), request.body));
        } catch (const std::exception& error) {
            writeResponse(response, InstitutionHttpResponse{
                500, taskforge::Json{{"ok", false}, {"error", std::string("erro interno: ") + error.what()}}.dump()
            });
        }
    };

    server.Get("/health", rest);
    server.Get("/v1/status", rest);
    server.Post("/v1/context", rest);
    server.Post("/v1/lessons", rest);
    server.Post("/v1/policies", rest);

    server.Post("/mcp", [this](const httplib::Request& request, httplib::Response& response) {
        try {
            auto parsed = taskforge::jsonutil::tryParse(request.body);
            if (!parsed) {
                response.status = 400;
                response.set_content(
                    taskforge::Json{{"jsonrpc", "2.0"},
                                    {"id", nullptr},
                                    {"error", taskforge::Json{{"code", -32700}, {"message", "JSON invalido"}}}}
                        .dump(),
                    "application/json; charset=utf-8");
                return;
            }
            const auto result = handleInstitutionMcp(service_, bearerToken(request), *parsed);
            response.status = result.hasResponse ? result.httpStatus : 202;
            if (result.hasResponse) {
                response.set_content(result.response.dump(), "application/json; charset=utf-8");
            } else {
                response.set_content("", "application/json; charset=utf-8");
            }
            response.set_header("Cache-Control", "no-store");
            if (result.httpStatus == 401) response.set_header("WWW-Authenticate", "Bearer");
        } catch (const std::exception& error) {
            response.status = 500;
            response.set_content(
                taskforge::Json{{"jsonrpc", "2.0"},
                                {"id", nullptr},
                                {"error", taskforge::Json{{"code", -32603},
                                                          {"message", std::string("erro interno: ") + error.what()}}}}
                    .dump(),
                "application/json; charset=utf-8");
        }
    });

    server.set_error_handler([](const httplib::Request&, httplib::Response& response) {
        if (response.body.empty()) {
            taskforge::Json body{{"ok", false}, {"error", "rota nao encontrada"}};
            response.set_content(body.dump(), "application/json; charset=utf-8");
        }
    });

    std::cout << "[arestrix-memory] ouvindo em http://" << host << ":" << port << "\n";
    if (!server.listen(host, port)) {
        error = "nao foi possivel escutar em " + host + ":" + std::to_string(port);
        return false;
    }
    return true;
}

} // namespace memory
} // namespace taskforge
