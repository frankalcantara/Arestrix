#pragma once

// Compilador de playbooks (M4). Trajetórias bem-sucedidas viram passos
// reutilizáveis por task_type; a primeira evidência cria um candidato e a
// segunda o ativa. Falhas só atualizam playbooks existentes.

#include <string>

#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

class MemoryPlaybookCompiler {
public:
    static bool observe(MemoryStore& store, const std::string& scopeId,
                        const taskforge::Json& episode);
};

} // namespace memory
} // namespace taskforge
