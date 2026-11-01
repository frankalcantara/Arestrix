#pragma once

// E15: caminhos canonicos sobre afirmacoes. A consolidacao e deterministica,
// idempotente por trace_id e nao altera o estado das afirmacoes.

#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct PathOptions {
    long long minDistinctInputs = 3;
    double prior = 1.0;
    // Meia-vida padrao de 30 dias, em segundos (ln 2 / 2.592.000 s).
    double mu = 2.674e-7;
};

class MemoryPaths {
public:
    // Utilidade Beta com prior e desconto temporal. `nowTau`/`lastTau` em
    // segundos; um traco fora de ordem nao produz desconto negativo.
    static double utility(long long success, long long failure, long long lastTau,
                          long long nowTau, const PathOptions& options);

    static taskforge::Json consolidate(MemoryStore& store, const std::string& scopeId,
                                       const std::vector<MemoryTraceRecord>& traces,
                                       const PathOptions& options = {});

    // Suspende caminhos que dependem de afirmacao inexistente, inativa,
    // expirada ou contestada. Reativa um caminho suspenso quando todas as suas
    // dependencias voltam a estar ativas.
    static taskforge::Json reconcile(MemoryStore& store, const std::string& scopeId,
                                     const std::string& reason = "claim_inactive");
    static taskforge::Json suspendForClaim(MemoryStore& store, const std::string& scopeId,
                                           const std::string& claimId,
                                           const std::string& reason = "claim_inactive");

    // E16: seleciona caminhos ativos e elegiveis antes da busca no grafo.
    // `access` e a politica do chamador (ex.: {"project"} ou {"local"}).
    static taskforge::Json serve(MemoryStore& store, const std::string& scopeId,
                                 const std::string& taskType, const std::string& intent,
                                 const std::vector<std::string>& access,
                                 size_t maxPaths, double explorationRate);
    static bool pathEligible(MemoryStore& store, const std::string& scopeId,
                             const MemoryPathRecord& path,
                             const std::vector<std::string>& access,
                             std::string* reason = nullptr);
    static double thompsonSample(long long success, long long failure, uint64_t seed);
    static bool shouldExplore(double rate, uint64_t seed);

    // E16: atalhos sao projecoes reconstruiveis; nao sao evidencia nem entram
    // em conflitos, e herdam o estado do caminho.
    static taskforge::Json materializeShortcuts(MemoryStore& store, const std::string& scopeId,
                                                double minUtility = 0.6,
                                                long long minUses = 3);
};

} // namespace memory
} // namespace taskforge
