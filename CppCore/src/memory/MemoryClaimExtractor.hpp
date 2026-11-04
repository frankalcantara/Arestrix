#pragma once

// E13: extração de afirmações como proposta, sempre fora da execução do agente.
// A inteligência pertence ao agente local do Studio; este módulo só envia o
// trecho, o catálogo e o contrato de saída e valida a resposta.

#include <string>
#include <vector>

#include "memory/ClaimIntelligence.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct ClaimExtractorConfig {
    bool enabled = false;
    std::string promptVersion = "claim-v1";
    std::string schemaVersion = "claim-schema-v1";
    std::string catalogMode = "warn";
    int maxItems = 5;

    std::string fingerprint() const;
};

struct ClaimDraft {
    std::string subject;
    std::string subjectType = "entity";
    std::string predicate;
    std::string object;
    std::string value;
    std::string unit;
    std::string polarity = "positive";
    std::string qualifiersJson = "{}";
    std::string validFrom;
    std::string validTo;
    std::string lens;
    std::string quote;
    bool knownPredicate = true;
};

class MemoryClaimExtractor {
public:
    static ClaimExtractorConfig configFromJson(const taskforge::Json& memory);

    // O agente local do Studio registra aqui a sua capacidade de extração.
    // Nenhum provedor, protocolo, URL ou chave entra no módulo de memória.
    static void setIntelligence(ClaimIntelligenceFn intelligence);
    static bool hasIntelligence();

    static std::vector<ClaimDraft> parseResponse(const std::string& content,
                                                 const std::string& passageText,
                                                 const ClaimExtractorConfig& config,
                                                 std::string& error,
                                                 taskforge::Json* diagnostics = nullptr);

    static std::vector<ClaimDraft> extract(const std::string& passageText,
                                           const ClaimExtractorConfig& config,
                                           std::string& error,
                                           taskforge::Json* diagnostics = nullptr);
};

} // namespace memory
} // namespace taskforge
