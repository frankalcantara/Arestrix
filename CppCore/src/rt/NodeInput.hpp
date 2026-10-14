#pragma once

// Entrada de texto de um vertice (switch, MCP...): a resposta final dos vertices anteriores ou um arquivo do projeto.
//
//   {"kind": "previous", "from": ["id_do_vertice_anterior", ...]}   le .arestrix/out/<id>.txt de cada um (agentes e
//                                                                    vertices MCP gravam ali o que produziram)
//   {"kind": "file", "path": "workspace/relatorio.txt"}             arquivo do projeto, confinado a ele
//   {"kind": "none"}                                                sem entrada (texto vazio)

#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

// Devolve o texto (limitado a 12000 bytes). Lanca std::runtime_error com uma mensagem clara se faltar entrada.
std::string gatherNodeInput(const taskforge::Json& source, const std::string& label);

// Grava o que um vertice produziu em .arestrix/out/<nodeId>.txt, para os vertices seguintes lerem.
void writeNodeOutput(const std::string& nodeId, const std::string& text);

} // namespace rt
} // namespace taskforge
