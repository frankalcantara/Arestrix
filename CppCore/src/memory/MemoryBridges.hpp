#pragma once

// E19: pontes interdominio sao hipoteses, nunca arestas automaticas. O detector
// propoe combinacoes de lentes com a mesma entidade a partir de tracos aprovados.

#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct BridgeOptions {
    size_t maxProposals = 5;
    size_t maxPerEntity = 1;
    size_t maxDiscarded = 1000;
};

class MemoryBridges {
public:
    static taskforge::Json detect(MemoryStore& store, const std::string& scopeId,
                                  const std::vector<MemoryTraceRecord>& traces,
                                  const BridgeOptions& options = {});
};

} // namespace memory
} // namespace taskforge
