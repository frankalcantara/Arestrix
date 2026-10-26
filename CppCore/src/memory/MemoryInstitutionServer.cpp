#include "MemoryInstitutionServer.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>

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

// Contadores do processo, sem labels de alta cardinalidade. O endpoint
// /metrics exige token: a telemetria nao abre uma excecao de autenticacao.
struct InstitutionServerMetrics {
    std::atomic<long long> requests{0};
    std::atomic<long long> restRequests{0};
    std::atomic<long long> mcpRequests{0};
    std::atomic<long long> mcpNotifications{0};
    std::atomic<long long> authFailures{0};
    std::atomic<long long> responses2xx{0};
    std::atomic<long long> responses4xx{0};
    std::atomic<long long> responses5xx{0};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    void observe(int status) {
        if (status >= 200 && status < 300) {
            responses2xx.fetch_add(1, std::memory_order_relaxed);
        } else if (status >= 400 && status < 500) {
            responses4xx.fetch_add(1, std::memory_order_relaxed);
            if (status == 401) authFailures.fetch_add(1, std::memory_order_relaxed);
        } else if (status >= 500) {
            responses5xx.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::string render() const {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        std::ostringstream out;
        out << "# HELP arestrix_memory_http_requests_total Total HTTP requests handled.\n"
            << "# TYPE arestrix_memory_http_requests_total counter\n"
            << "arestrix_memory_http_requests_total " << requests.load() << "\n"
            << "# HELP arestrix_memory_rest_requests_total REST requests handled.\n"
            << "# TYPE arestrix_memory_rest_requests_total counter\n"
            << "arestrix_memory_rest_requests_total " << restRequests.load() << "\n"
            << "# HELP arestrix_memory_mcp_requests_total MCP HTTP requests handled.\n"
            << "# TYPE arestrix_memory_mcp_requests_total counter\n"
            << "arestrix_memory_mcp_requests_total " << mcpRequests.load() << "\n"
            << "# HELP arestrix_memory_mcp_notifications_total MCP notifications without response.\n"
            << "# TYPE arestrix_memory_mcp_notifications_total counter\n"
            << "arestrix_memory_mcp_notifications_total " << mcpNotifications.load() << "\n"
            << "# HELP arestrix_memory_auth_failures_total Requests rejected with 401.\n"
            << "# TYPE arestrix_memory_auth_failures_total counter\n"
            << "arestrix_memory_auth_failures_total " << authFailures.load() << "\n"
            << "# HELP arestrix_memory_http_responses_total HTTP responses by status class.\n"
            << "# TYPE arestrix_memory_http_responses_total counter\n"
            << "arestrix_memory_http_responses_total{class=\"2xx\"} " << responses2xx.load() << "\n"
            << "arestrix_memory_http_responses_total{class=\"4xx\"} " << responses4xx.load() << "\n"
            << "arestrix_memory_http_responses_total{class=\"5xx\"} " << responses5xx.load() << "\n"
            << "# HELP arestrix_memory_uptime_seconds Process uptime in seconds.\n"
            << "# TYPE arestrix_memory_uptime_seconds gauge\n"
            << "arestrix_memory_uptime_seconds " << uptime << "\n"
            << "# HELP arestrix_memory_info Build information.\n"
            << "# TYPE arestrix_memory_info gauge\n"
            << "arestrix_memory_info{version=\"" << institutionServiceVersion() << "\"} 1\n";
        return out.str();
    }
};

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

    InstitutionServerMetrics metrics;

    auto rest = [this, &metrics](const httplib::Request& request, httplib::Response& response) {
        metrics.requests.fetch_add(1, std::memory_order_relaxed);
        metrics.restRequests.fetch_add(1, std::memory_order_relaxed);
        try {
            const auto result = handleInstitutionRest(service_, request.method, request.path,
                                                      bearerToken(request), request.body);
            metrics.observe(result.status);
            writeResponse(response, result);
        } catch (const std::exception& error) {
            metrics.observe(500);
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

    server.Get("/metrics", [this, &metrics](const httplib::Request& request,
                                            httplib::Response& response) {
        metrics.requests.fetch_add(1, std::memory_order_relaxed);
        MemoryPrincipal principal;
        if (!service_.authenticate(bearerToken(request), principal)) {
            metrics.observe(401);
            response.status = 401;
            response.set_content("nao autenticado\n", "text/plain; charset=utf-8");
            response.set_header("WWW-Authenticate", "Bearer");
            response.set_header("Cache-Control", "no-store");
            return;
        }
        metrics.observe(200);
        response.status = 200;
        response.set_content(metrics.render(), "text/plain; version=0.0.4; charset=utf-8");
        response.set_header("Cache-Control", "no-store");
    });

    server.Post("/mcp", [this, &metrics](const httplib::Request& request, httplib::Response& response) {
        metrics.requests.fetch_add(1, std::memory_order_relaxed);
        metrics.mcpRequests.fetch_add(1, std::memory_order_relaxed);
        try {
            auto parsed = taskforge::jsonutil::tryParse(request.body);
            if (!parsed) {
                metrics.observe(400);
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
            if (!result.hasResponse) {
                metrics.mcpNotifications.fetch_add(1, std::memory_order_relaxed);
            }
            response.status = result.hasResponse ? result.httpStatus : 202;
            metrics.observe(response.status);
            if (result.hasResponse) {
                response.set_content(result.response.dump(), "application/json; charset=utf-8");
            } else {
                response.set_content("", "application/json; charset=utf-8");
            }
            response.set_header("Cache-Control", "no-store");
            if (result.httpStatus == 401) response.set_header("WWW-Authenticate", "Bearer");
        } catch (const std::exception& error) {
            metrics.observe(500);
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
