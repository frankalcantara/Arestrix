#pragma once

// Extrator deterministico de propostas (M1 do L2).
//
// Le um memory_episode sanitizado e produz candidatos a licao, fato, entidade,
// playbook e sumario. Nao usa LLM e nao promove nada: apenas grava propostas
// pendentes para revisao/promocao.

#include <cstddef>
#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct MemoryProposalDraft {
    std::string kind;
    taskforge::Json payload = taskforge::Json::object();
    taskforge::Json evidence = taskforge::Json::array();
    double confidence = 0.5;
};

class MemoryExtractor {
public:
    static std::vector<MemoryProposalDraft> extract(const taskforge::Json& episode);

    // Extrai e grava as propostas para um episodio existente. Devolve quantas
    // propostas novas/atualizadas foram registradas (dedup pendente conta 0).
    static size_t extractAndStore(MemoryStore& store, const std::string& scopeId,
                                  const std::string& episodeId);
};

} // namespace memory
} // namespace taskforge
