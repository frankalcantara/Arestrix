#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace taskforge {
namespace rt {
namespace compress {

// Limites do cache de compressao reversivel. Padroes: 7 dias e 256 MB por projeto.
struct CcrLimits {
    std::chrono::hours maxAge{24 * 7};
    std::uintmax_t maxBytes = 256ull * 1024ull * 1024ull;
};

// Raiz do CCR fora do repositorio: %LOCALAPPDATA%\Arestrix\ccr no Windows,
// ~/.local/share/Arestrix/ccr (ou XDG_DATA_HOME) no Linux e
// ~/Library/Application Support/Arestrix/ccr no macOS. ARESTRIX_CCR_DIR sobrepoe.
std::filesystem::path defaultCcrRoot();

// CCR (cache de compressao reversivel): guarda o texto original de cada bloco comprimido em
// <raiz>/<projectId>/<runId>/<token>.txt. A ferramenta `retrieve` devolve o original byte a byte.
// A poda e do projeto inteiro (todas as execucoes), por idade e por tamanho total.
class CcrStore {
public:
    CcrStore(std::filesystem::path baseRoot, std::string projectId, std::string runId,
             CcrLimits limits = {});

    // Guarda o original e devolve o token (16 hex). Vazio em falha de escrita.
    std::string store(std::string_view original);

    // Le o original pelo token. nullopt se o token for invalido ou o arquivo nao existir.
    std::optional<std::string> retrieve(const std::string& token) const;

    const std::filesystem::path& directory() const { return dir_; }

    // Remove arquivos vencidos e, se o projeto passar do teto, os mais antigos.
    // Publico para o runtime poder limpar no inicio/fim de uma execucao.
    void prune() const;

private:
    std::filesystem::path dir_;
    std::filesystem::path projectDir_;
    CcrLimits limits_;
};

} // namespace compress
} // namespace rt
} // namespace taskforge
