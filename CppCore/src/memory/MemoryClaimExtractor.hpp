#pragma once

// E13: extracao de afirmacoes por um modelo local, sempre fora da execucao do
// agente e sempre como proposta. O modelo responde JSON; a validacao e
// deterministica e a citacao precisa existir literalmente no trecho.

#include <string>
#include <vector>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct ClaimExtractorConfig {
    bool enabled = false;
    std::string baseUrl = "http://127.0.0.1:11434/v1";
    std::string model = "gemma-4-e2b";
    std::string revision;
    std::string promptVersion = "claim-v1";
    std::string schemaVersion = "claim-schema-v1";
    std::string apiKeyEnv = "ARESTRIX_EXTRACTOR_API_KEY";
    int maxItems = 5;
    int timeoutSeconds = 60;

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

    // Parse/valida a resposta JSON do modelo. `diagnostics` recebe contagens e
    // rejeicoes para o log de manutencao.
    static std::vector<ClaimDraft> parseResponse(const std::string& content,
                                                 const std::string& passageText,
                                                 const ClaimExtractorConfig& config,
                                                 std::string& error,
                                                 taskforge::Json* diagnostics = nullptr);

    // Executa a chamada OpenAI-compatible e valida a resposta.
    static std::vector<ClaimDraft> extract(const std::string& passageText,
                                           const ClaimExtractorConfig& config,
                                           std::string& error,
                                           taskforge::Json* diagnostics = nullptr);
};

} // namespace memory
} // namespace taskforge
