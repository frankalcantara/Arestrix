#pragma once

// E12: ingestao deterministica de fontes de texto sem chamar modelo de
// linguagem. A segmentacao depende apenas do tipo do arquivo e do conteudo.

#include <cstddef>
#include <string>
#include <vector>

namespace taskforge {
namespace memory {

struct MemoryPassageDraft {
    long long ordinal = 0;
    long long start = 0;
    long long end = 0;
    std::string text;
};

class MemorySources {
public:
    static std::string normalizeUri(const std::string& uri);
    static std::string kindForUri(const std::string& uri);
    static bool isSupported(const std::string& uri, size_t size, size_t maxBytes);
    static bool isExcluded(const std::string& relativeUri);

    // Segmentacao deterministica: Markdown por titulos/paragrafos, codigo por
    // blocos de topo e demais textos em janelas de 1.000 caracteres com 200 de
    // sobreposicao.
    static std::vector<MemoryPassageDraft> segment(const std::string& text,
                                                   const std::string& kind);

    static std::string sourceId(const std::string& scopeId, const std::string& uri,
                                const std::string& contentHash);
    static std::string passageId(const std::string& contentHash, long long ordinal);
};

} // namespace memory
} // namespace taskforge
