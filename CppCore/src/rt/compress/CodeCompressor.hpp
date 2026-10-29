#pragma once

#include <cstddef>
#include <string_view>

#include "rt/compress/Result.hpp"

namespace taskforge {
namespace rt {
namespace compress {

struct CodeOptions {
    // Linhas iniciais preservadas (cabecalho/licenca).
    size_t keepHeaderLines = 8;
    // Ganho minimo para aceitar a compressao (20%).
    double minGain = 0.20;
};

// Comprime codigo-fonte mantendo a "planta" do arquivo: imports, assinaturas, tipos e
// estrutura de blocos. Corpos sao substituidos por um marcador com a contagem de linhas.
// Nao tenta gerar codigo valido: e uma visao de navegacao; o original vive no CCR.
CompressResult compressCode(std::string_view input, const CodeOptions& options);

} // namespace compress
} // namespace rt
} // namespace taskforge
