#include "EmbeddingProvider.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <sstream>

#include "rt/Http.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string envOr(const char* name, const std::string& fallback = "") {
    if (const char* value = std::getenv(name); value && *value) return std::string(value);
    return fallback;
}

int envInt(const char* name, int fallback) {
    const std::string value = envOr(name);
    if (value.empty()) return fallback;
    try { return std::stoi(value); } catch (...) { return fallback; }
}

std::string resolvedBaseUrl(const EmbeddingConfig& config) {
    std::string base = config.baseUrl;
    if (base.empty()) base = envOr("ARESTRIX_EMBEDDING_BASE_URL");
    if (base.empty()) base = envOr("LOCAL_LLM_BASE_URL");
    if (base.empty()) base = "http://127.0.0.1:11434/v1";
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

std::string jsonError(const taskforge::Json& root, const std::string& fallback) {
    if (root.is_object() && root.contains("error")) {
        const auto& error = root["error"];
        if (error.is_object()) {
            const std::string message = error.value("message", std::string());
            if (!message.empty()) return message;
        }
        if (error.is_string()) return error.str();
    }
    return fallback;
}

bool asBool(const std::string& text, bool fallback) {
    const std::string v = lower(text);
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return fallback;
}

} // namespace

EmbeddingConfig defaultEmbeddingConfig() {
    EmbeddingConfig config;
    config.enabled = false;
    config.provider = "local";
    config.model = "embeddinggemma-2";
    config.baseUrl.clear();
    config.apiKeyEnv = "ARESTRIX_EMBEDDING_API_KEY";
    config.timeoutSeconds = 60;
    config.batchSize = 8;
    config.maxChars = 8000;
    config.dim = 768;
    return config;
}

EmbeddingConfig embeddingConfigFromJson(const taskforge::Json& value) {
    EmbeddingConfig config = defaultEmbeddingConfig();
    const taskforge::Json root = value.is_object() ? value : taskforge::Json::object();

    // Aceita tanto o bloco memory completo quanto um objeto "embedding".
    const taskforge::Json source = root.contains("embedding") && root["embedding"].is_object()
        ? root["embedding"] : root;

    config.enabled = source.value("embeddingEnabled", source.value("enabled", config.enabled));
    config.provider = source.value("embeddingProvider", source.value("provider", config.provider));
    config.model = source.value("embeddingModel", source.value("model", config.model));
    config.baseUrl = source.value("embeddingBaseUrl", source.value("baseUrl", config.baseUrl));
    config.apiKeyEnv = source.value("embeddingApiKeyEnv", source.value("apiKeyEnv", config.apiKeyEnv));
    config.timeoutSeconds = source.value("embeddingTimeoutSeconds",
                                         source.value("timeoutSeconds", config.timeoutSeconds));
    config.batchSize = source.value("embeddingBatchSize",
                                    source.value("batchSize", config.batchSize));
    config.maxChars = source.value("embeddingMaxChars",
                                   source.value("maxChars", config.maxChars));
    config.dim = source.value("embeddingDimensions",
                              source.value("dimensions", source.value("dim", config.dim)));

    // Variaveis de ambiente vencem o arquivo do projeto: sao uteis para testes
    // pontuais sem alterar o grafo salvo.
    if (const std::string enabled = envOr("ARESTRIX_EMBEDDINGS_ENABLED"); !enabled.empty()) {
        config.enabled = asBool(enabled, config.enabled);
    }
    if (const std::string base = envOr("ARESTRIX_EMBEDDING_BASE_URL"); !base.empty()) config.baseUrl = base;
    if (const std::string model = envOr("ARESTRIX_EMBEDDING_MODEL"); !model.empty()) config.model = model;
    if (const std::string provider = envOr("ARESTRIX_EMBEDDING_PROVIDER"); !provider.empty()) config.provider = provider;
    config.timeoutSeconds = std::max(1, std::min(600, envInt("ARESTRIX_EMBEDDING_TIMEOUT", config.timeoutSeconds)));
    config.batchSize = std::max(1, std::min(128, envInt("ARESTRIX_EMBEDDING_BATCH", config.batchSize)));
    config.maxChars = std::max(64, std::min(32000, envInt("ARESTRIX_EMBEDDING_MAX_CHARS", config.maxChars)));
    config.dim = std::max(0, std::min(8192, envInt("ARESTRIX_EMBEDDING_DIM", config.dim)));

    if (config.provider.empty()) config.provider = "local";
    if (config.model.empty()) config.model = "embeddinggemma-2";
    if (config.timeoutSeconds <= 0) config.timeoutSeconds = 60;
    if (config.batchSize <= 0) config.batchSize = 8;
    if (config.maxChars <= 0) config.maxChars = 8000;
    return config;
}

taskforge::Json embeddingConfigToJson(const EmbeddingConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"provider", config.provider},
        {"model", config.model},
        {"baseUrl", config.baseUrl},
        {"localBaseUrl", resolvedBaseUrl(config)},
        {"apiKeyEnv", config.apiKeyEnv},
        {"timeoutSeconds", config.timeoutSeconds},
        {"batchSize", config.batchSize},
        {"maxChars", config.maxChars},
        {"dim", config.dim}};
}

std::shared_ptr<IEmbeddingProvider> makeEmbeddingProvider(const EmbeddingConfig& config) {
    if (lower(config.provider) == "none") return std::make_shared<NullEmbeddingProvider>();
    if (config.model.empty()) return std::make_shared<NullEmbeddingProvider>();
    auto provider = std::make_shared<LocalLlamaEmbeddingProvider>(config);
    if (!provider->available()) return std::make_shared<NullEmbeddingProvider>();
    return provider;
}

LocalLlamaEmbeddingProvider::LocalLlamaEmbeddingProvider(EmbeddingConfig config)
    : config_(std::move(config)) {
    config_.baseUrl = resolvedBaseUrl(config_);
    dim_ = std::max(0, config_.dim);
}

bool LocalLlamaEmbeddingProvider::available() const {
    return !config_.baseUrl.empty() && !config_.model.empty();
}

std::string LocalLlamaEmbeddingProvider::endpoint() const {
    std::string base = config_.baseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.size() >= 11 && base.compare(base.size() - 11, 11, "/embeddings") == 0) return base;
    if (base.size() >= 3 && base.compare(base.size() - 3, 3, "/v1") == 0) return base + "/embeddings";
    return base + "/v1/embeddings";
}

std::string LocalLlamaEmbeddingProvider::prepareText(const std::string& text, bool query) const {
    std::string clipped = text;
    if (static_cast<int>(clipped.size()) > config_.maxChars) clipped.resize(config_.maxChars);
    if (clipped.empty()) clipped = " ";
    const std::string model = lower(config_.model);
    // EmbeddingGemma 2 usa instrucoes assimetricas de busca.
    if (model.find("embeddinggemma") != std::string::npos) {
        return query
            ? "task: search result | query: " + clipped
            : "title: none | text: " + clipped;
    }
    // E5 exige prefixos distintos para consulta e passagem. Outros modelos
    // multilinguais (MiniLM, BGE) funcionam sem prefixo.
    if (model.find("e5") != std::string::npos) {
        return std::string(query ? "query: " : "passage: ") + clipped;
    }
    return clipped;
}

bool LocalLlamaEmbeddingProvider::embed(const std::vector<std::string>& texts, bool query,
                                        std::vector<Embedding>& out, std::string& error) {
    out.clear();
    if (!available()) {
        error = "provider de embeddings nao configurado";
        return false;
    }
    if (texts.empty()) return true;

    const int batchSize = std::max(1, config_.batchSize);
    std::string apiKey = envOr(config_.apiKeyEnv.c_str());
    if (apiKey.empty()) {
        const std::string endpointLower = lower(endpoint());
        const bool loopback = endpointLower.find("127.0.0.1") != std::string::npos ||
                              endpointLower.find("localhost") != std::string::npos ||
                              endpointLower.find("[::1]") != std::string::npos;
        if (loopback) apiKey = envOr("LOCAL_LLM_API_KEY");
    }

    for (size_t start = 0; start < texts.size(); start += static_cast<size_t>(batchSize)) {
        const size_t end = std::min(texts.size(), start + static_cast<size_t>(batchSize));
        taskforge::Json inputs = taskforge::Json::array();
        for (size_t i = start; i < end; ++i) inputs.push_back(taskforge::Json(prepareText(texts[i], query)));
        taskforge::Json body = taskforge::Json{{"model", config_.model}, {"input", inputs}};

        rt::HttpRequest request;
        request.method = "POST";
        request.url = endpoint();
        request.timeoutSeconds = config_.timeoutSeconds;
        request.headers["Content-Type"] = "application/json";
        if (!apiKey.empty()) request.headers["Authorization"] = "Bearer " + apiKey;
        request.body = body.dump();

        const rt::HttpResponse response = rt::httpSend(request);
        if (response.status == 0) {
            error = response.error.empty() ? "falha de transporte ao chamar embeddings" : response.error;
            if (endpoint().find("11434") != std::string::npos) {
                error += " (Ollama indisponivel; inicie o Ollama e rode: ollama pull embeddinggemma-2)";
            }
            out.clear();
            return false;
        }
        auto parsed = taskforge::jsonutil::tryParse(response.body);
        if (!parsed || !parsed->is_object()) {
            std::ostringstream message;
            message << "resposta invalida do endpoint de embeddings (HTTP " << response.status << ")";
            error = message.str();
            out.clear();
            return false;
        }
        if (response.status < 200 || response.status >= 300) {
            error = jsonError(*parsed, "HTTP " + std::to_string(response.status) +
                                       " do endpoint de embeddings");
            out.clear();
            return false;
        }

        const auto& data = parsed->contains("data") ? (*parsed)["data"] : taskforge::Json();
        if (!data.is_array() || data.empty()) {
            error = "resposta de embeddings sem data[]";
            out.clear();
            return false;
        }

        std::vector<std::pair<long long, Embedding>> batch;
        batch.reserve(data.size());
        long long fallbackIndex = 0;
        for (const auto& item : data) {
            if (!item.is_object() || !item.contains("embedding") || !item["embedding"].is_array()) {
                error = "item de embedding invalido";
                out.clear();
                return false;
            }
            Embedding embedding;
            embedding.model = config_.model;
            for (const auto& number : item["embedding"]) {
                if (!number.is_number()) {
                    error = "vetor de embedding contem valor nao numerico";
                    out.clear();
                    return false;
                }
                const double value = number.get<double>();
                if (!std::isfinite(value)) {
                    error = "vetor de embedding contem NaN/Inf";
                    out.clear();
                    return false;
                }
                embedding.values.push_back(static_cast<float>(value));
            }
            if (embedding.values.empty()) {
                error = "vetor de embedding vazio";
                out.clear();
                return false;
            }
            embedding.dim = static_cast<int>(embedding.values.size());
            if (dim_ <= 0) dim_ = embedding.dim;
            if (embedding.dim != dim_) {
                error = "dimensao de embedding inconsistente: " + std::to_string(embedding.dim) +
                        " (esperado " + std::to_string(dim_) + ")";
                out.clear();
                return false;
            }
            const long long index = item.value("index", fallbackIndex++);
            batch.push_back({index, std::move(embedding)});
        }
        std::sort(batch.begin(), batch.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
        for (auto& entry : batch) out.push_back(std::move(entry.second));
    }

    if (out.size() != texts.size()) {
        error = "endpoint devolveu " + std::to_string(out.size()) + " embeddings para " +
                std::to_string(texts.size()) + " textos";
        out.clear();
        return false;
    }
    return true;
}

} // namespace memory
} // namespace taskforge
