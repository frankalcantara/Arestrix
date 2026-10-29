#pragma once

#include <cstddef>
#include <string_view>

#include "rt/compress/ProseModel.hpp"
#include "rt/compress/ProseTokenizer.hpp"
#include "rt/compress/Result.hpp"

namespace taskforge {
namespace rt {
namespace compress {

struct ProseOptions {
    // Blocos acima disso nao sao comprimidos (o original fica inteiro, sem truncar o fim).
    size_t maxTokens = 8192;
    // Janela de inferencia: limita a memoria de atencao (2048 e seguro em GPUs de 4 GB).
    size_t windowTokens = 2048;
    // Probabilidade minima para manter o token.
    float threshold = 0.5f;
    double minGain = 0.10;
};

// Comprime prosa com o modelo (keep/drop por token), preservando pontuacao e trechos que nao
// podem ser quebrados (URL, e-mail, caminho, hash, data). Sem modelo disponivel, devolve o
// texto original: a politica nunca depende da compressao de prosa para funcionar.
CompressResult compressProse(std::string_view text, const ProseTokenizer& tokenizer,
                             ProseModel& model, const ProseOptions& options);

} // namespace compress
} // namespace rt
} // namespace taskforge
