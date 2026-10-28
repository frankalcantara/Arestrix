#pragma once

// Fonte GitHub da biblioteca de assets.
//
// O endpoint C++ faz apenas leitura (GET) e separa dois modos:
//  - manifest: metadados + arvore, sem baixar o conteudo dos assets;
//  - asset: baixa um unico blob sob demanda para a quarentena do frontend.
//
// Nada aqui grava arquivos, executa codigo do repositorio ou devolve tokens.

#include <string>
#include <vector>

namespace taskforge {
namespace libraries {

struct GitHubAssetCandidate {
    std::string path;
    std::string sha;
    std::string kind;
    std::string name;
    long long size = 0;
};

struct GitHubManifest {
    std::string repo;
    std::string ref;
    std::string commit;
    std::string license;
    std::string defaultBranch;
    bool isPrivate = false;
    bool archived = false;
    bool truncated = false;
    std::vector<GitHubAssetCandidate> assets;
};

struct GitHubAssetResult {
    std::string repo;
    std::string ref;
    std::string commit;
    std::string path;
    std::string kind;
    std::string content;
    long long size = 0;
};

// Porta de entrada usada pelo WebUI. Recebe e devolve JSON.
std::string fetchLibrarySourceJson(const std::string& requestJson);

// Helpers puros, expostos para testes e para o frontend validar antes de chamar.
bool isValidRepo(const std::string& repo);
bool isValidRef(const std::string& ref);
bool isValidAssetPath(const std::string& path);
bool isValidCommit(const std::string& commit);
std::string classifyAssetKind(const std::string& path);
std::string deriveAssetName(const std::string& path, const std::string& repo);

} // namespace libraries
} // namespace taskforge
