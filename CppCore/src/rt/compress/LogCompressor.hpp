#pragma once

#include <cstddef>
#include <string_view>

#include "rt/compress/Result.hpp"

namespace taskforge {
namespace rt {
namespace compress {

struct LogOptions {
    // Teto de linhas no resultado. Linhas FATAL/CRITICAL nunca sao descartadas.
    size_t maxLines = 200;
    // A partir de quantas repeticoes o grupo vira marcador.
    size_t keepRepeats = 3;
    // Teto de linhas de erro comuns (ERROR/Exception) preservadas.
    size_t maxErrorLines = 80;
    double minGain = 0.10;
};

// Comprime logs: agrupa linhas repetidas e corta o miolo quando passa do teto, sempre
// preservando FATAL/CRITICAL e as primeiras linhas de cada grupo. Nao reordena linhas.
CompressResult compressLog(std::string_view input, const LogOptions& options);

} // namespace compress
} // namespace rt
} // namespace taskforge
