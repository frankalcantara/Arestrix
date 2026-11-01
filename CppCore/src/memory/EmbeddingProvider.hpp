#pragma once

// Providers de embeddings do L2/M2.
//
// O Arestrix nao embute pesos nem inicia processos auxiliares. O provider local
// fala com um endpoint OpenAI-compatible /v1/embeddings ja existente (llama.cpp,
// Ollama, LM Studio, vLLM...). Sem endpoint/modelo, o provider nulo mantem a
// recuperacao 100% lexical.

#include <memory>
#include <string>
#include <vector>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct Embedding {
    std::vector<float> values;
    std::string model;
    int dim = 0;
};

struct EmbeddingConfig {
    bool enabled = false;
    std::string provider = "local";   // local | none
    std::string model = "embeddinggemma-2";
    std::string baseUrl;              // vazio = ARESTRIX_EMBEDDING_BASE_URL/LOCAL_LLM_BASE_URL
    std::string apiKeyEnv = "ARESTRIX_EMBEDDING_API_KEY";
    int timeoutSeconds = 60;
    int batchSize = 8;
    int maxChars = 8000;
    int dim = 768;                    // dimensao armazenada; 0 = descobrir na primeira resposta
    std::string revision;             // revisao/digest do modelo; entra no fingerprint
    int truncateDim = 0;              // MRL: 0 = vetor completo; >0 trunca e renormaliza
};

class IEmbeddingProvider {
public:
    virtual ~IEmbeddingProvider() = default;
    virtual bool available() const = 0;
    virtual std::string modelId() const = 0;
    virtual int dim() const = 0;
    // Revisao/digest efetivo (usuario ou descoberto no servidor). Entra no fingerprint.
    virtual std::string revision() const { return ""; }
    // query=true aplica o prefixo/instrucao de consulta do modelo.
    virtual bool embed(const std::vector<std::string>& texts, bool query,
                       std::vector<Embedding>& out, std::string& error) = 0;
};

class NullEmbeddingProvider : public IEmbeddingProvider {
public:
    bool available() const override { return false; }
    std::string modelId() const override { return "none"; }
    int dim() const override { return 0; }
    bool embed(const std::vector<std::string>&, bool,
               std::vector<Embedding>&, std::string& error) override {
        error = "embeddings desativados";
        return false;
    }
};

class LocalLlamaEmbeddingProvider : public IEmbeddingProvider {
public:
    explicit LocalLlamaEmbeddingProvider(EmbeddingConfig config);

    bool available() const override;
    std::string modelId() const override { return config_.model; }
    int dim() const override { return dim_; }
    std::string revision() const override {
        return config_.revision.empty() ? serverRevision_ : config_.revision;
    }
    bool embed(const std::vector<std::string>& texts, bool query,
               std::vector<Embedding>& out, std::string& error) override;

private:
    std::string endpoint() const;
    std::string prepareText(const std::string& text, bool query) const;

    EmbeddingConfig config_;
    int dim_ = 0;
    std::string serverRevision_;
};

EmbeddingConfig defaultEmbeddingConfig();
EmbeddingConfig embeddingConfigFromJson(const taskforge::Json& value);
taskforge::Json embeddingConfigToJson(const EmbeddingConfig& config);
// Prefixos/instrucoes por modelo (documentados e versionados no fingerprint).
std::string embeddingQueryPrefix(const std::string& model);
std::string embeddingDocumentPrefix(const std::string& model);
// Prepara um vetor recebido do servidor: valida dimensoes aceitas pelo modelo,
// trunca MRL, renormaliza L2 e rejeita NaN/Inf ou vetor degenerado.
bool prepareEmbeddingVector(const std::string& model, std::vector<float>& values,
                            int truncateDim, int expectedDim, int& effectiveDim,
                            std::string& error);
// Fingerprint canonico do indice: endpoint, modelo, revisao, dimensao efetiva,
// truncamento, prefixos, normalizacao, metrica e versao.
std::string embeddingFingerprint(const EmbeddingConfig& config, int effectiveDim = 0,
                                 const std::string& revisionOverride = "");
std::shared_ptr<IEmbeddingProvider> makeEmbeddingProvider(const EmbeddingConfig& config);

} // namespace memory
} // namespace taskforge
