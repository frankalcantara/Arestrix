#pragma once

#include <cstddef>
#include <string_view>

namespace taskforge {
namespace rt {
namespace compress {

// Estimativa de tokens usada pela politica de compactacao enquanto o tokenizador real
// (ModernBERT, Etapa 3) nao existe. Aproximacao de 4 bytes por token, suficiente para
// decidir "comprimir ou nao" e reportar ganho; o tokenizador substitui esta funcao sem
// mudar a semantica da configuracao (tudo continua expresso em tokens).
inline size_t estimateTokens(std::string_view text) {
    return (text.size() + 3) / 4;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
