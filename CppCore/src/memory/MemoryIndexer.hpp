#pragma once

#include <cstddef>
#include <string>

#include "memory/EmbeddingProvider.hpp"
#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct EmbeddingIndexStats {
    size_t candidates = 0;
    size_t indexed = 0;
    size_t skipped = 0;
    size_t removed = 0;
    size_t failed = 0;
    size_t pending = 0;          // objetos ainda sem vetor depois desta rodada
    bool stoppedByBudget = false;
};

class MemoryIndexer {
public:
    // Indexa objetos pendentes. maxObjects=0 indexa tudo; force=true reembeda tudo.
    // timeBudgetMs=0 nao limita o tempo; >0 interrompe entre lotes.
    static EmbeddingIndexStats index(MemoryStore& store, IEmbeddingProvider& provider,
                                     const EmbeddingConfig& config, const std::string& scopeId,
                                     bool force, size_t maxObjects, std::string& error,
                                     int timeBudgetMs = 0);

    static std::string contentHash(const std::string& objectType, const std::string& objectId,
                                   const std::string& text, const std::string& model,
                                   const std::string& fingerprint = "");
};

} // namespace memory
} // namespace taskforge
