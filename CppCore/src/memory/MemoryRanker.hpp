#pragma once

#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"
#include "memory/MemoryVectorIndex.hpp"

namespace taskforge {
namespace memory {

struct RankedMemory {
    std::string id;
    std::string kind;          // lesson | fact | playbook | entity | episode
    std::string text;
    std::string domain;
    double confidence = 0.5;
    std::string createdAt;
    double score = 0.0;
    std::string sources;       // lexical | vector | lexical+vector
};

class MemoryRanker {
public:
    // Reciprocal Rank Fusion (RRF). Sem pesos arbitrarios na primeira versao.
    static std::vector<RankedMemory> fuse(const std::vector<MemorySearchCandidate>& lexical,
                                          const std::vector<MemoryVectorHit>& vector,
                                          size_t limit, double k = 60.0);
};

} // namespace memory
} // namespace taskforge
