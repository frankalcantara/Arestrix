#pragma once

// E17: travessia em feixe para consultas que nenhum caminho cobre. A busca
// apenas le o grafo e devolve um pacote de contexto mais o vetor de cobertura.

#include <string>
#include <vector>

#include "memory/MemoryAnchors.hpp"
#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct TraversalOptions {
    size_t candidateLimit = 40;   // C na primeira etapa
    size_t beamWidth = 4;         // B
    size_t maxDepth = 3;          // H
    size_t expandedLimit = 200;
    size_t maxResults = 8;
    size_t maxChars = 4000;
    int timeBudgetMs = 50;
    double minScore = 0.02;
    bool exact = false;           // C = todos os incidentes
};

class MemoryTraversal {
public:
    static taskforge::Json run(MemoryStore& store, const std::string& scopeId,
                               const std::string& query, const std::string& taskType,
                               const TraversalOptions& options = {},
                               const std::vector<std::string>& access = {},
                               const std::vector<MemoryAnchor>& vectorAnchors = {});
};

} // namespace memory
} // namespace taskforge
