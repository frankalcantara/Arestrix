#pragma once

// Sanitizacao de segredos antes de persistir memoria.
//
// A memoria e dado historico, mas nao pode virar um cofre involuntario de chaves.
// Esta camada remove padroes conhecidos (OpenAI, Google, AWS, GitHub, Slack, Bearer,
// query strings e chaves privadas PEM) e limita o tamanho de textos persistidos.
//
// Ela e aplicada no MemoryStore (licoes, episodios, arestas e Blackboard) e no
// DatabaseBackupService antes de gravar o snapshot que vai para o Git.

#include <cstddef>
#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct SanitizeResult {
    std::string text;
    bool redacted = false;
    bool truncated = false;
    size_t redactions = 0;
};

// maxChars == 0 significa sem limite de tamanho.
SanitizeResult sanitizeText(const std::string& input, size_t maxChars = 0);

// Percorre objetos, arrays e strings recursivamente. Devolve o numero de redacoes.
size_t sanitizeJsonInPlace(taskforge::Json& value);

} // namespace memory
} // namespace taskforge
