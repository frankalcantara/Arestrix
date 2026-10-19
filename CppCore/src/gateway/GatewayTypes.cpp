#include "gateway/GatewayTypes.hpp"

#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "project/OpenableFiles.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace gateway {

namespace {

std::string utcStamp(const char* format) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, format);
    return out.str();
}

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

std::string nowIso() {
    std::ostringstream out;
    out << utcStamp("%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string periodWindow(const std::string& period) {
    if (period == "daily") return utcStamp("%Y-%m-%d");
    if (period == "monthly") return utcStamp("%Y-%m");
    return "total";
}

bool modelPrice(const GatewayConfig& config, const std::string& provider,
                const std::string& model, Price& price) {
    auto route = config.pricing.find(provider);
    if (route == config.pricing.end()) return false;

    auto exact = route->second.find(model);
    if (exact != route->second.end()) {
        price = exact->second;
        return true;
    }
    auto fallback = route->second.find("*");
    if (fallback != route->second.end()) {
        price = fallback->second;
        return true;
    }
    return false;
}

bool GatewayConfig::load(const std::filesystem::path& path, GatewayConfig& config, std::string& error) {
    error.clear();
    try {
        const Json root = jsonutil::parseFile(path);
        require(root.is_object(), "configuracao deve ser um objeto JSON");

        config.host = root.value("host", std::string("127.0.0.1"));
        config.port = root.value("port", 8787);
        config.database = root.value("database", std::string("arestrix-gateway.db"));
        config.defaultMaxTokens = root.value("defaultMaxTokens", 8192);
        config.requestTimeoutSeconds = root.value("requestTimeoutSeconds", 600);
        config.maxBodyBytes = root.value("maxBodyBytes", 32LL * 1024 * 1024);
        if (config.port <= 0 || config.port > 65535) throw std::runtime_error("port invalida");
        if (config.defaultMaxTokens <= 0) throw std::runtime_error("defaultMaxTokens invalido");
        if (config.requestTimeoutSeconds <= 0) throw std::runtime_error("requestTimeoutSeconds invalido");

        const std::filesystem::path baseDir = path.parent_path();
        if (!config.database.empty() && std::filesystem::path(config.database).is_relative()) {
            config.database = project::pathToUtf8(baseDir / project::pathFromUtf8(config.database));
        }

        config.routes.clear();
        const Json& routes = root["routes"];
        require(routes.is_object(), "routes ausente");
        for (auto it = routes.begin(); it != routes.end(); ++it) {
            const std::string id = it.key();
            const Json& value = it.value();
            require(value.is_object(), "rota invalida: " + id);
            ProviderRoute route;
            route.id = id;
            route.protocol = lower(value.value("protocol", std::string("openai")));
            route.baseUrl = value.value("baseUrl", std::string());
            route.keyEnv = value.value("keyEnv", std::string());
            route.enabled = value.value("enabled", true);
            require(route.protocol == "openai" || route.protocol == "anthropic",
                    "protocolo invalido na rota " + id);
            require(route.baseUrl.rfind("https://", 0) == 0 || route.baseUrl.rfind("http://", 0) == 0,
                    "baseUrl invalida na rota " + id);
            require(!route.keyEnv.empty(), "keyEnv ausente na rota " + id);
            while (!route.baseUrl.empty() && route.baseUrl.back() == '/') route.baseUrl.pop_back();
            config.routes[id] = std::move(route);
        }
        require(!config.routes.empty(), "nenhuma rota configurada");

        config.pricing.clear();
        const Json& pricing = root["pricing"];
        if (pricing.is_object()) {
            for (auto providerIt = pricing.begin(); providerIt != pricing.end(); ++providerIt) {
                const std::string provider = providerIt.key();
                const Json& models = providerIt.value();
                if (!models.is_object()) continue;
                for (auto modelIt = models.begin(); modelIt != models.end(); ++modelIt) {
                    const Json& entry = modelIt.value();
                    if (!entry.is_object()) continue;
                    Price price;
                    price.input = entry.value("input", 0.0);
                    price.output = entry.value("output", 0.0);
                    price.cachedInput = entry.value("cachedInput", entry.value("cached", 0.0));
                    config.pricing[provider][modelIt.key()] = price;
                }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

} // namespace gateway
} // namespace taskforge
