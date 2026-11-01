#pragma once

// E17: resolvedor de ancoras para a travessia. Combina dicionario de aliases,
// FTS lexical e candidatos vetoriais fornecidos pelo chamador. Pode abster-se.

#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct MemoryAnchor {
    std::string id;
    std::string kind;       // entity | edge | lesson | playbook
    std::string name;
    double score = 0.0;
    std::string reason;     // alias | lexical | vector
};

class MemoryAnchors {
public:
    static std::vector<MemoryAnchor> resolve(
        MemoryStore& store, const std::string& scopeId, const std::string& query,
        size_t limit = 5, const std::vector<std::string>& access = {},
        const std::vector<MemoryAnchor>& vectorAnchors = {});
};

} // namespace memory
} // namespace taskforge
