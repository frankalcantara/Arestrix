#pragma once

// E14: lentes e facetas. A faceta de uma entidade/lente e uma descricao
// deterministica das afirmacoes ativas que a sustentam; nenhum modelo de
// linguagem e chamado para gera-la.

#include <string>

#include "memory/EmbeddingProvider.hpp"
#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

class MemoryFacets {
public:
    // Garante lente em todas as arestas ativas e regenera as facetas cujo
    // claims_hash mudou. Idempotente por (entidade, lente, revision).
    static taskforge::Json rebuild(MemoryStore& store, const std::string& scopeId,
                                   const EmbeddingConfig& embedding, bool facetsEnabled);
};

} // namespace memory
} // namespace taskforge
