#pragma once

// Listagem de pastas para a tela Abrir do Studio: subpastas (marcando as que sao projetos),
// pacotes .arestrix e arquivos .json. Os caminhos cruzam a fronteira com a interface em UTF-8.

#include <cstddef>
#include <filesystem>
#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace project {

// Caminho vindo da interface (UTF-8) -> std::filesystem::path, inclusive no Windows.
std::filesystem::path pathFromUtf8(const std::string& s);

// std::filesystem::path -> UTF-8 com barras normais (formato generico).
std::string pathToUtf8(const std::filesystem::path& p);

// true se a pasta contem arestrix.project.json (ou o legado taskforge.project.json).
bool isProjectDirectory(const std::filesystem::path& dir);

// Conteudo de `dir` que a tela Abrir sabe abrir:
//   {"path", "parent", "exists", "truncated",
//    "entries": [{"name", "path", "kind": "folder"|"project"|"package"|"manifest"|"json", "modified": epoch ms}]}
// Pastas antes de arquivos, ordem alfabetica sem diferenciar maiusculas; itens ocultos (".x") ficam de fora.
// `parent` e vazio na raiz de uma unidade. Caminho relativo e resolvido a partir do diretorio atual.
Json listOpenableEntries(const std::filesystem::path& dir, std::size_t limit = 2000);

} // namespace project
} // namespace taskforge
