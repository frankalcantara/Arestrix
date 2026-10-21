#include "gateway/GatewayServer.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <random>
#include <regex>
#include <sstream>
#include <syncstream>

#include "platform/Process.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "httplib.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace taskforge {
namespace gateway {

namespace {

std::string randomHex(std::size_t bytes) {
    thread_local std::mt19937_64 rng{std::random_device{}()};
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < bytes; ++i) {
        const unsigned value = static_cast<unsigned>(rng() & 0xff);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0f]);
    }
    return out;
}

std::string headerText(const httplib::Request& request, const char* name, size_t maxLen = 200) {
    std::string value = request.get_header_value(name);
    if (value.size() > maxLen) value.resize(maxLen);
    for (char& c : value) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = ' ';
    }
    return value;
}

long long headerNumber(const httplib::Request& request, const char* name) {
    const std::string value = headerText(request, name, 32);
    if (value.empty()) return 0;
    try {
        return std::max<long long>(0, std::stoll(value));
    } catch (...) {
        return 0;
    }
}

std::string bearerToken(const httplib::Request& request) {
    const std::string auth = request.get_header_value("Authorization");
    if (auth.rfind("Bearer ", 0) == 0) return auth.substr(7);
    const std::string apiKey = request.get_header_value("x-api-key");
    if (!apiKey.empty()) return apiKey;
    return std::string();
}

void setJson(httplib::Response& response, int status, const Json& body) {
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

void jsonError(httplib::Response& response, int status, const std::string& type,
               const std::string& message, const std::string& requestId) {
    Json error = Json::object();
    error["message"] = message;
    error["type"] = type;
    error["code"] = type;
    Json body = Json::object();
    body["error"] = error;
    if (!requestId.empty()) body["requestId"] = requestId;
    setJson(response, status, body);
    if (!requestId.empty()) response.set_header("X-Arestrix-Request-Id", requestId);
    if (status == 401) response.set_header("WWW-Authenticate", "Bearer");
}

} // namespace

GatewayServer::GatewayServer(GatewayConfig config, GatewayStore& store)
    : config_(std::move(config)), store_(store) {}

long long GatewayServer::estimateInputTokens(const std::string& body) {
    return std::max<long long>(1, static_cast<long long>(body.size() / 4));
}

TokenUsage GatewayServer::parseUsage(const ProviderRoute& route, const std::string& body) const {
    TokenUsage usage;
    const auto parsed = jsonutil::tryParse(body);
    if (!parsed || !parsed->is_object()) return usage;
    const Json& root = *parsed;
    const Json& value = root["usage"];
    if (!value.is_object()) return usage;

    if (route.protocol == "anthropic") {
        usage.input = value.value("input_tokens", 0LL) +
                      value.value("cache_creation_input_tokens", 0LL);
        usage.output = value.value("output_tokens", 0LL);
        usage.cached = value.value("cache_read_input_tokens", 0LL);
    } else {
        usage.input = value.value("prompt_tokens", 0LL);
        usage.output = value.value("completion_tokens", 0LL);
        const Json& details = value["prompt_tokens_details"];
        if (details.is_object()) usage.cached = details.value("cached_tokens", 0LL);
    }
    return usage;
}

double GatewayServer::computeCost(const ProviderRoute& route, const Price& price,
                                  const TokenUsage& usage) const {
    const double inputPrice = price.input / 1000000.0;
    const double outputPrice = price.output / 1000000.0;
    const double cachedPrice = (price.cachedInput > 0 ? price.cachedInput : price.input) / 1000000.0;

    if (route.protocol == "anthropic") {
        return static_cast<double>(usage.input) * inputPrice +
               static_cast<double>(usage.cached) * cachedPrice +
               static_cast<double>(usage.output) * outputPrice;
    }

    const long long nonCached = std::max<long long>(0, usage.input - usage.cached);
    return static_cast<double>(nonCached) * inputPrice +
           static_cast<double>(usage.cached) * cachedPrice +
           static_cast<double>(usage.output) * outputPrice;
}

bool GatewayServer::checkRateLimit(const VirtualKey& key, const std::string& requestId,
                                   long long estimatedTokens, std::string& error) {
    if (key.rpmLimit <= 0 && key.tpmLimit <= 0) return true;

    std::lock_guard<std::mutex> lock(rateMu_);
    auto& events = rateEvents_[key.id];
    const auto now = std::chrono::steady_clock::now();
    while (!events.empty() && now - events.front().at > std::chrono::seconds(60)) {
        events.pop_front();
    }

    if (key.rpmLimit > 0 && static_cast<int>(events.size()) >= key.rpmLimit) {
        error = "rate_limit_rpm";
        return false;
    }
    if (key.tpmLimit > 0) {
        long long used = 0;
        for (const auto& event : events) used += event.tokens;
        if (used + estimatedTokens > key.tpmLimit) {
            error = "rate_limit_tpm";
            return false;
        }
    }
    events.push_back(RateEvent{now, estimatedTokens, requestId});
    return true;
}

void GatewayServer::finishRateLimit(const std::string& keyId, const std::string& requestId,
                                    long long tokens) {
    std::lock_guard<std::mutex> lock(rateMu_);
    auto it = rateEvents_.find(keyId);
    if (it == rateEvents_.end()) return;
    for (auto& event : it->second) {
        if (event.requestId == requestId) {
            event.tokens = tokens;
            return;
        }
    }
}

void GatewayServer::logEvent(const UsageEvent& event, const BudgetDecision& budget) {
    Json line = Json::object();
    line["ts"] = nowIso();
    line["requestId"] = event.context.requestId;
    line["keyId"] = event.context.keyId;
    line["project"] = event.context.project;
    line["user"] = event.context.user;
    line["runner"] = event.context.runner;
    line["provider"] = event.context.provider;
    line["model"] = event.context.model;
    line["inputTokens"] = event.usage.input;
    line["outputTokens"] = event.usage.output;
    line["cachedTokens"] = event.usage.cached;
    line["costUsd"] = event.costUsd;
    line["latencyMs"] = event.latencyMs;
    line["status"] = event.status;
    if (!event.agent.empty()) line["agent"] = event.agent;
    if (!event.runId.empty()) line["runId"] = event.runId;
    if (!event.memoryScope.empty()) line["memoryScope"] = event.memoryScope;
    line["memoryEnabled"] = event.memoryEnabled;
    line["memoryContextChars"] = event.memoryContextChars;
    line["memoryTokens"] = event.memoryTokens;
    if (!event.errorCode.empty()) line["errorCode"] = event.errorCode;
    if (budget.warning) line["budgetWarning"] = true;
    std::osyncstream(std::cout) << line.dump() << '\n';
}

bool GatewayServer::run(std::string& error) {
    error.clear();
    httplib::Server server;
    server.set_payload_max_length(static_cast<size_t>(config_.maxBodyBytes));
    server.set_read_timeout(config_.requestTimeoutSeconds, 0);
    server.set_write_timeout(config_.requestTimeoutSeconds, 0);
    server.set_keep_alive_max_count(100);
    server.set_keep_alive_timeout(5);

    server.Get("/health", [this](const httplib::Request&, httplib::Response& response) {
        std::string dbError;
        const bool dbOk = store_.healthy(dbError);
        Json body = Json::object();
        body["ok"] = dbOk;
#ifdef ARESTRIX_GATEWAY_VERSION
        body["version"] = ARESTRIX_GATEWAY_VERSION;
#else
        body["version"] = "dev";
#endif
        if (!dbOk) body["error"] = dbError;
        setJson(response, dbOk ? 200 : 503, body);
    });

    server.Get("/metrics", [this](const httplib::Request&, httplib::Response& response) {
        std::ostringstream out;
        out << "# HELP arestrix_gateway_requests_total Requisicoes recebidas\n";
        out << "# TYPE arestrix_gateway_requests_total counter\n";
        out << "arestrix_gateway_requests_total " << metrics_.requests.load() << "\n";
        out << "# HELP arestrix_gateway_success_total Requisicoes concluidas com sucesso\n";
        out << "# TYPE arestrix_gateway_success_total counter\n";
        out << "arestrix_gateway_success_total " << metrics_.success.load() << "\n";
        out << "# HELP arestrix_gateway_errors_total Erros\n";
        out << "# TYPE arestrix_gateway_errors_total counter\n";
        out << "arestrix_gateway_errors_total " << metrics_.errors.load() << "\n";
        out << "# HELP arestrix_gateway_budget_rejections_total Bloqueios por budget\n";
        out << "# TYPE arestrix_gateway_budget_rejections_total counter\n";
        out << "arestrix_gateway_budget_rejections_total " << metrics_.budgetRejections.load() << "\n";
        out << "# HELP arestrix_gateway_rate_limited_total Bloqueios por rate limit\n";
        out << "# TYPE arestrix_gateway_rate_limited_total counter\n";
        out << "arestrix_gateway_rate_limited_total " << metrics_.rateLimited.load() << "\n";
        out << "# HELP arestrix_gateway_input_tokens_total Tokens de entrada\n";
        out << "# TYPE arestrix_gateway_input_tokens_total counter\n";
        out << "arestrix_gateway_input_tokens_total " << metrics_.inputTokens.load() << "\n";
        out << "# HELP arestrix_gateway_output_tokens_total Tokens de saida\n";
        out << "# TYPE arestrix_gateway_output_tokens_total counter\n";
        out << "arestrix_gateway_output_tokens_total " << metrics_.outputTokens.load() << "\n";
        out << "# HELP arestrix_gateway_cost_usd_total Custo estimado em USD\n";
        out << "# TYPE arestrix_gateway_cost_usd_total counter\n";
        out << "arestrix_gateway_cost_usd_total " << (static_cast<double>(metrics_.costMicros.load()) / 1000000.0) << "\n";
        response.set_content(out.str(), "text/plain; version=0.0.4");
    });

    server.Get("/v1/usage", [this](const httplib::Request& request, httplib::Response& response) {
        const std::string token = bearerToken(request);
        if (token.empty()) {
            jsonError(response, 401, "unauthorized", "chave virtual ausente", "");
            return;
        }
        Json body;
        std::string usageError;
        if (!store_.usageForToken(token, body, usageError)) {
            jsonError(response, 401, "unauthorized", usageError, "");
            return;
        }
        setJson(response, 200, body);
    });

    server.Post(R"(^/([A-Za-z0-9_-]+)/(.+)$)",
        [this](const httplib::Request& request, httplib::Response& response) {
            const auto started = std::chrono::steady_clock::now();
            metrics_.requests.fetch_add(1);

            if (request.matches.size() < 3) {
                jsonError(response, 404, "not_found", "rota invalida", "");
                return;
            }
            const std::string provider = request.matches[1].str();
            std::string rest = request.matches[2].str();
            while (!rest.empty() && rest.front() == '/') rest.erase(rest.begin());

            const auto routeIt = config_.routes.find(provider);
            if (routeIt == config_.routes.end() || !routeIt->second.enabled) {
                jsonError(response, 404, "provider_not_found", "provedor nao configurado: " + provider, "");
                return;
            }
            const ProviderRoute& route = routeIt->second;
            // Superficie minima: o gateway nao e um proxy generico para endpoints administrativos.
            if (route.protocol == "anthropic" && rest != "v1/messages") {
                jsonError(response, 404, "path_not_allowed", "caminho nao permitido: " + rest, "");
                return;
            }
            if (route.protocol == "openai" && rest != "chat/completions" && rest != "completions") {
                jsonError(response, 404, "path_not_allowed", "caminho nao permitido: " + rest, "");
                return;
            }

            const std::string token = bearerToken(request);
            if (token.empty()) {
                jsonError(response, 401, "unauthorized", "chave virtual ausente", "");
                return;
            }
            VirtualKey key;
            std::string authError;
            if (!store_.authenticate(token, key, authError)) {
                jsonError(response, 401, "unauthorized", authError, "");
                return;
            }

            const std::string requestId = "req_" + randomHex(8);
            const auto parsed = jsonutil::tryParse(request.body);
            if (!parsed || !parsed->is_object()) {
                jsonError(response, 400, "invalid_json", "corpo JSON invalido", requestId);
                return;
            }
            const Json& body = *parsed;
            if (body.value("stream", false)) {
                jsonError(response, 400, "streaming_unsupported",
                          "streaming ainda nao suportado pelo gateway", requestId);
                return;
            }
            const std::string model = body.value("model", std::string());
            if (model.empty()) {
                jsonError(response, 400, "model_missing", "campo model obrigatorio", requestId);
                return;
            }

            RequestContext context;
            context.requestId = requestId;
            context.keyId = key.id;
            context.project = key.project;
            context.user = key.user;
            context.provider = provider;
            context.model = model;
            context.runner = request.get_header_value("X-Arestrix-Runner");

            Price price;
            const bool priced = modelPrice(config_, provider, model, price);
            if (!priced) {
                std::cerr << "[gateway] aviso: preco ausente para " << provider << "/" << model << "\n";
            }

            const long long estimatedInput = estimateInputTokens(request.body);
            long long maxOutput = body.value("max_tokens", 0LL);
            if (maxOutput <= 0) maxOutput = body.value("max_completion_tokens", 0LL);
            if (maxOutput <= 0) maxOutput = config_.defaultMaxTokens;
            // Buffer de 10% para reduzir overrun por estimativa de tokenizacao.
            const long long estimatedTokens = (estimatedInput + maxOutput) * 11 / 10;
            const double estimatedUsd = priced
                ? ((static_cast<double>(estimatedInput) * price.input +
                    static_cast<double>(maxOutput) * price.output) / 1000000.0) * 1.1
                : 0.0;

            UsageEvent event;
            event.context = context;
            event.agent = headerText(request, "X-Arestrix-Agent");
            event.runId = headerText(request, "X-Arestrix-Run-Id");
            event.memoryScope = headerText(request, "X-Arestrix-Memory-Scope");
            event.memoryEnabled = headerText(request, "X-Arestrix-Memory-Enabled", 8) == "1";
            event.memoryContextChars = std::min<long long>(
                headerNumber(request, "X-Arestrix-Memory-Context-Chars"), 10'000'000);
            event.memoryTokens = (event.memoryContextChars + 3) / 4;
            BudgetDecision budget;

            std::string rateError;
            if (!checkRateLimit(key, requestId, estimatedTokens, rateError)) {
                metrics_.rateLimited.fetch_add(1);
                event.status = "rate_limited";
                event.errorCode = rateError;
                event.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                store_.recordUsage(event, authError);
                logEvent(event, budget);
                jsonError(response, 429, rateError, "limite de taxa excedido", requestId);
                response.set_header("Retry-After", "60");
                return;
            }

            std::string budgetError;
            if (!store_.checkAndReserve(context, estimatedUsd, estimatedTokens, budget, budgetError)) {
                finishRateLimit(key.id, requestId, 0);
                metrics_.errors.fetch_add(1);
                event.status = "gateway_error";
                event.errorCode = "budget_store_error";
                event.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                store_.recordUsage(event, authError);
                logEvent(event, budget);
                jsonError(response, 500, "gateway_error", budgetError, requestId);
                return;
            }
            if (!budget.allowed) {
                finishRateLimit(key.id, requestId, 0);
                metrics_.budgetRejections.fetch_add(1);
                event.status = "budget_rejected";
                event.errorCode = budget.reason;
                event.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                store_.recordUsage(event, authError);
                logEvent(event, budget);
                Json payload = Json::object();
                Json errorBody = Json::object();
                errorBody["message"] = "orcamento excedido";
                errorBody["type"] = "budget_exceeded";
                errorBody["code"] = budget.reason;
                payload["error"] = errorBody;
                payload["requestId"] = requestId;
                payload["budgets"] = budget.budgets;
                setJson(response, 402, payload);
                response.set_header("X-Arestrix-Request-Id", requestId);
                return;
            }

            const std::string providerKey = platform::getEnv(route.keyEnv);
            if (providerKey.empty()) {
                store_.reconcile(context, TokenUsage{}, 0.0, estimatedUsd, estimatedTokens, budgetError);
                finishRateLimit(key.id, requestId, 0);
                metrics_.errors.fetch_add(1);
                event.status = "gateway_error";
                event.errorCode = "provider_key_missing";
                event.latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                store_.recordUsage(event, authError);
                logEvent(event, budget);
                jsonError(response, 500, "provider_key_missing",
                          "chave do provedor ausente no gateway: " + route.keyEnv, requestId);
                return;
            }

            std::string upstreamPath = route.baseUrl + "/" + rest;
            rt::HttpRequest upstream;
            upstream.method = "POST";
            upstream.url = upstreamPath;
            upstream.timeoutSeconds = config_.requestTimeoutSeconds;
            upstream.headers["Content-Type"] = "application/json";
            for (const char* header : {"anthropic-version", "anthropic-beta", "OpenAI-Beta", "Accept"}) {
                const std::string value = request.get_header_value(header);
                if (!value.empty()) upstream.headers[header] = value;
            }
            if (route.protocol == "anthropic") {
                upstream.headers["x-api-key"] = providerKey;
                if (upstream.headers.find("anthropic-version") == upstream.headers.end()) {
                    upstream.headers["anthropic-version"] = "2023-06-01";
                }
            } else {
                upstream.headers["Authorization"] = "Bearer " + providerKey;
            }
            upstream.body = request.body;

            const rt::HttpResponse upstreamResponse = rt::httpSend(upstream);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();

            TokenUsage usage;
            double actualUsd = 0.0;
            std::string status = "ok";
            std::string errorCode;
            if (upstreamResponse.status == 200) {
                usage = parseUsage(route, upstreamResponse.body);
                actualUsd = computeCost(route, price, usage);
                metrics_.success.fetch_add(1);
                metrics_.inputTokens.fetch_add(usage.input);
                metrics_.outputTokens.fetch_add(usage.output);
                metrics_.costMicros.fetch_add(static_cast<long long>(actualUsd * 1000000.0));
            } else if (upstreamResponse.status == 0) {
                status = "upstream_error";
                errorCode = "transport_error";
                metrics_.errors.fetch_add(1);
            } else {
                status = "upstream_error";
                errorCode = "http_" + std::to_string(upstreamResponse.status);
                metrics_.errors.fetch_add(1);
            }

            store_.reconcile(context, usage, actualUsd, estimatedUsd, estimatedTokens, budgetError);
            finishRateLimit(key.id, requestId, usage.total());

            event.usage = usage;
            event.costUsd = actualUsd;
            event.latencyMs = elapsed;
            event.status = status;
            event.errorCode = errorCode;
            store_.recordUsage(event, authError);
            logEvent(event, budget);

            if (upstreamResponse.status == 0) {
                jsonError(response, 502, "upstream_error",
                          upstreamResponse.error.empty() ? "falha de transporte" : upstreamResponse.error,
                          requestId);
                return;
            }

            response.status = upstreamResponse.status;
            std::string contentType = "application/json";
            auto contentTypeIt = upstreamResponse.headers.find("content-type");
            if (contentTypeIt != upstreamResponse.headers.end() && !contentTypeIt->second.empty()) {
                contentType = contentTypeIt->second;
            }
            response.set_content(upstreamResponse.body, contentType);
            response.set_header("X-Arestrix-Request-Id", requestId);
            if (budget.warning) response.set_header("X-Arestrix-Budget-Warning", "1");
        });

    server.set_error_handler([](const httplib::Request&, httplib::Response& response) {
        if (response.body.empty()) {
            Json body = Json::object();
            Json errorBody = Json::object();
            errorBody["message"] = "rota nao encontrada";
            errorBody["type"] = "not_found";
            errorBody["code"] = "not_found";
            body["error"] = errorBody;
            response.status = 404;
            response.set_content(body.dump(), "application/json");
        }
    });

    server.set_exception_handler([](const httplib::Request&, httplib::Response& response,
                                    std::exception_ptr) {
        Json body = Json::object();
        Json errorBody = Json::object();
        errorBody["message"] = "erro interno do gateway";
        errorBody["type"] = "internal_error";
        errorBody["code"] = "internal_error";
        body["error"] = errorBody;
        response.status = 500;
        response.set_content(body.dump(), "application/json");
    });

    std::cout << "[gateway] ouvindo em http://" << config_.host << ":" << config_.port << "\n";
    if (!server.listen(config_.host, config_.port)) {
        error = "nao foi possivel escutar em " + config_.host + ":" + std::to_string(config_.port);
        return false;
    }
    return true;
}

} // namespace gateway
} // namespace taskforge
