#pragma once

// Vertice MCP: chama UMA ferramenta de um servidor MCP do projeto, sem modelo de IA no meio.
//
// Spec (gerado a partir de node.mcpSettings):
//   { "server": "clientes", "tool": "lookup_customer",
//     "arguments": { "email": "{{input.email}}" },
//     "source": { "kind": "previous"|"file"|"none", "path": "...", "from": ["..."] },
//     "output": { "file": "workspace/cliente.json" } }
//
// Os argumentos podem usar {{input}} (o texto de entrada) e {{input.campo.sub}} (um campo, se a entrada for JSON).
// Uma string que e SOMENTE uma referencia ({{input.n}}) preserva o tipo do valor (numero, booleano, objeto).
// O resultado vai para .arestrix/out/<id>.txt (os vertices seguintes leem dali) e, se pedido, para um arquivo do projeto.
// Falha da ferramenta ou servidor inacessivel interrompem o pipeline com uma mensagem clara.

#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

// Aplica os modelos {{input}} / {{input.caminho}} aos valores string de `arguments`. Lanca std::runtime_error se
// o caminho nao existir na entrada ou se a entrada nao for JSON.
taskforge::Json renderMcpArguments(const taskforge::Json& arguments, const std::string& inputText);

} // namespace rt
} // namespace taskforge
