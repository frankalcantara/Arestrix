#pragma once

#include <cstdint>
#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

// A7 v2: contrato do protocolo A/B pareado.
//
// O conjunto curado tem 20 tarefas: 12 com conhecimento relevante, 4 de
// controle e 4 de falha conhecida. Cada tarefa carrega um input_hash estavel.
// As observacoes sao produzidas externamente (runner/validador); este modulo
// valida o conjunto, sorteia a ordem e calcula os deltas pareados com IC 95%.
class MemoryAbProtocol {
public:
    static taskforge::Json validateSuite(const taskforge::Json& suite, std::string& error);
    static taskforge::Json buildRunOrder(const taskforge::Json& suite, int repetitions,
                                         uint32_t seed, std::string& error);
    static taskforge::Json analyze(const taskforge::Json& suite,
                                   const taskforge::Json& observations,
                                   int repetitions, std::string& error);
};

} // namespace memory
} // namespace taskforge
