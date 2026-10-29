#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "rt/compress/Result.hpp"

namespace taskforge {
namespace rt {
namespace compress {

struct JsonOptions {
    // Arrays maiores que isso guardam o inicio e um marcador com o total.
    size_t maxArrayItems = 20;
    // Strings acima deste tamanho (em tokens estimados) sao cortadas com marcador.
    size_t maxStringTokens = 1000;
    // Ganho minimo para aceitar a compressao (10%).
    double minGain = 0.10;
};

// Comprime JSON preservando chaves, tipos e identificadores: arrays longos sao amostrados
// e strings gigantes cortadas, sempre com marcador explicito. JSON invalido ou ganho
// pequeno devolve o texto original.
CompressResult crushJson(std::string_view input, const JsonOptions& options);

} // namespace compress
} // namespace rt
} // namespace taskforge
