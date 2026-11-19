#pragma once

// Catalogo de assets da Biblioteca de Vertices e Agentes.
//
// Duas fontes, na mesma ordem de precedencia:
//   1. catalogo embutido (bundledAgentVertices.json), com os vertices/agentes nativos;
//   2. estado do Workspace (.workspace/libraries.json), com assets instalados,
//      derivados e bibliotecas criadas pelo usuario.
//
// Assets customizados/derivados do Workspace substituem o item embutido de mesmo id;
// stubs compactados apenas atualizam status, sem perder o payload completo de fabrica.
//
// O mesmo modulo alimenta o Assistente do Studio (ferramentas nativas) e o servidor
// MCP `arestrix-mcp-biblioteca`, com indice em memoria e recarga por mtime.

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "rt/RuntimeLang.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace libraries {

class LibraryCatalog {
public:
    // Carrega o catalogo do projeto ativo. Nunca falha por ausencia do catalogo
    // embutido: nesse caso `warning` explica a ausencia e o Workspace continua valido.
    static bool load(const std::filesystem::path& projectRoot, LibraryCatalog& out, std::string& warning);

    // Recarrega apenas o estado do Workspace quando `.workspace/libraries.json` mudou.
    // O catalogo embutido e o indice de busca permanecem em cache (O(1) quando nada mudou).
    bool refresh(std::string& warning);

    const std::vector<Json>& libraries() const { return libraries_; }
    const std::vector<Json>& assets() const { return assets_; }
    const std::filesystem::path& projectRoot() const { return projectRoot_; }
    const std::filesystem::path& workspaceFile() const { return workspaceFile_; }
    const std::filesystem::path& bundledFile() const { return bundledFile_; }
    bool hasBundledCatalog() const { return !bundledFile_.empty(); }

    const Json* find(const std::string& assetId) const;
    const Json* findLibrary(const std::string& libraryId) const;

    // Lista compacta para o modelo, com filtros opcionais.
    Json list(const std::string& libraryId, const std::string& kind,
              const std::string& domain, const std::string& subdomain,
              int limit, int offset) const;

    // Busca textual em nome, descricao, categoria, dominio e subdominio.
    Json search(const std::string& query, const std::string& kind, int limit) const;

    // Asset completo, com textos longos truncados para caber no contexto do modelo.
    std::optional<Json> detail(const std::string& assetId) const;

    // Converte um asset em dados de vertice prontos para GraphDocument::addNodeJson.
    // Aceita agent-template, pipeline e skill; MCP exige attach_mcp e devolve erro.
    bool buildNodeFromAsset(const Json& asset, const std::string& label, const Json& parameterOverrides,
                            rt::RuntimeLang lang, Json& node, std::string& error) const;

    // --- Escritas na biblioteca do Workspace ---------------------------------
    // Todas recarregam o estado, alteram o asset e gravam `.workspace/libraries.json`
    // de forma atomica. Erros sao fail-closed e voltam em `error`.

    Json installAsset(const std::string& assetId, bool allowCaution, std::string& error);
    Json deriveAsset(const std::string& assetId, const std::string& name,
                     const std::string& libraryId, std::string& error);
    Json restoreAsset(const std::string& assetId, std::string& error);
    Json auditAsset(const std::string& assetId, std::string& error);

private:
    bool rebuildFromWorkspace(std::string& warning);
    void reindex();
    Json summary(const Json& asset) const;
    std::string libraryName(const std::string& libraryId) const;
    Json* mutableAsset(const std::string& assetId);

    bool saveState(std::string& error);
    bool writeFileAtomic(const std::filesystem::path& path, const std::string& content, std::string& error);
    std::string uniqueId(const std::string& prefix, const std::vector<Json>& entries) const;
    std::string slugify(const std::string& text) const;

    std::vector<Json> libraries_;
    std::vector<Json> assets_;
    std::vector<Json> factoryAssets_;
    std::vector<std::string> searchIndex_;              // alinhado a assets_
    std::unordered_map<std::string, size_t> assetIndex_;
    std::unordered_map<std::string, size_t> libraryIndex_;
    Json workspaceSources_ = Json::array();
    std::filesystem::path projectRoot_;
    std::filesystem::path workspaceFile_;
    std::filesystem::path bundledFile_;
    std::filesystem::file_time_type workspaceMtime_{};
    bool workspaceMtimeValid_ = false;
};

} // namespace libraries
} // namespace taskforge
