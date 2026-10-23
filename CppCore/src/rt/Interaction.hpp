#pragma once

// Vertice de interacao humana (runtime). No 2B1 o canal e um arquivo de respostas
// (ARESTRIX_INTERACTION_ANSWERS), usado pelo CLI `--answers` e pelos testes. O Studio
// passa a usar o canal bidirecional na 2B2.
//
// specJson = interactionSettings do vertice:
//   { "schemaSource": "static", "form": { "fields": [...] },
//     "channel": "any", "timeoutSeconds": 300, "onTimeout": "fail",
//     "defaultAnswers": {...}, "sensitive": false }

#include <string>

namespace taskforge {
namespace rt {

// Le as respostas, valida o formulario, grava .arestrix/out/<nodeId>.txt e .json e
// devolve o resultado como JSON. Lanca std::runtime_error se nao houver canal,
// o arquivo for invalido, um campo obrigatorio faltar ou um tipo/opcao for invalido.
std::string runInteraction(const std::string& nodeId, const std::string& label,
                           const std::string& specJson,
                           const std::string& inputContext);

} // namespace rt
} // namespace taskforge
