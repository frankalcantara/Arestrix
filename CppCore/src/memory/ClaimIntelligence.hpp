#pragma once

// Fronteira única entre a memória e a inteligência do Studio.
//
// A memória não conhece provedor, protocolo, URL, chave ou modelo. Ela envia
// apenas o trecho, o catálogo resumido e o contrato de saída; o agente local do
// Studio escolhe como obter a resposta e devolve o JSON de claims.

#include <functional>
#include <string>

namespace taskforge {
namespace memory {

// Alvo de inteligencia escolhido pelo projeto. Vazio significa o modelo
// padrao do Studio. `graph`/`node` permitem que o Studio execute um grafo em vez
// de um agente logico; a resolucao continua inteiramente no Studio.
struct ClaimAgentRef {
    std::string id;
    std::string graph;
    std::string node;

    bool configured() const { return !id.empty() || !graph.empty(); }
};

struct ClaimIntelligenceRequest {
    std::string passageId;
    std::string passageText;
    std::string catalog;
    std::string schemaJson;
    ClaimAgentRef agent;
    int maxItems = 5;
};

struct ClaimIntelligenceResult {
    bool ok = false;
    std::string content;
    std::string error;
};

using ClaimIntelligenceFn =
    std::function<ClaimIntelligenceResult(const ClaimIntelligenceRequest&)>;

} // namespace memory
} // namespace taskforge
