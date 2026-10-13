#pragma once

// Ponte de leitura JSON do Arestrix.
//
// TODO parse de JSON do projeto passa por aqui e e feito pelo simdjson (third_party/simdjson).
// O valor resultante e um taskforge::Json (util/JsonValue.hpp).

#include <filesystem>
#include <istream>
#include <optional>
#include <string_view>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace jsonutil {

// Faz o parse de `text`. Lanca JsonError com a mensagem do simdjson se o documento for invalido.
Json parse(std::string_view text);

// Variante sem excecao: devolve std::nullopt se o documento for invalido.
std::optional<Json> tryParse(std::string_view text);

// Le todo o stream e faz o parse.
Json parseStream(std::istream& in);

// Le e faz o parse de um arquivo inteiro. Lanca std::runtime_error se nao puder ser lido.
Json parseFile(const std::filesystem::path& path);

// Le o conteudo de um arquivo texto inteiro (utilitario compartilhado).
std::string readFile(const std::filesystem::path& path);

} // namespace jsonutil
} // namespace taskforge
