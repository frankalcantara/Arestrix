#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "rt/ModelClient.hpp"
#include "rt/RuntimeLang.hpp"
#include "rt/compress/CcrStore.hpp"
#include "rt/compress/ProseModel.hpp"
#include "rt/compress/ProseTokenizer.hpp"

namespace taskforge {
namespace rt {
namespace compress {

struct CompressionSettings {
    bool enabled = false;
    // Bloco menor que isso (em tokens estimados) nao e tocado.
    size_t minBlockTokens = 2000;
    // Run inteiro menor que isso também nao (custo > ganho).
    size_t minRunTokens = 8000;
    size_t maxArrayItems = 20;
    // Strings acima disso sao cortadas com marcador.
    size_t maxStringTokens = 1000;
    size_t maxLogLines = 200;
    // Etapa 2: compressao de codigo (mantem assinaturas e estrutura).
    bool code = true;
    // Etapa 3: compressao de prosa via ONNX. Desligada ate o modelo existir.
    bool prose = false;
};

struct CompressionStats {
    size_t blocks = 0;
    size_t tokensBefore = 0;   // estimativa (Tokens.hpp)
    size_t tokensAfter = 0;
    std::string kinds;         // "json,log" (sem repeticao)
};

// Compacta as observacoes de ferramenta dentro do loop do agente.
//
// Política: so mensagens `tool`, nunca a ultima mensagem, so blocos acima do limiar e so
// quando o run inteiro passa do orcamento. Toda substituicao e reversivel pelo CCR.
class ContextCompressor {
public:
    ContextCompressor(CompressionSettings settings, CcrStore* ccr, RuntimeLang lang,
                      const ProseTokenizer* proseTokenizer = nullptr,
                      ProseModel* proseModel = nullptr);

    CompressionStats apply(std::vector<ChatMessage>& messages);

private:
    CompressionSettings settings_;
    CcrStore* ccr_;
    RuntimeLang lang_;
    const ProseTokenizer* proseTokenizer_;
    ProseModel* proseModel_;
};

} // namespace compress
} // namespace rt
} // namespace taskforge
