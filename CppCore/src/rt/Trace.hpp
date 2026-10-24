#pragma once

// Trace aberto da execucao: arvore resumida de tasks, chamadas e interacoes em
// .arestrix/runs/<runId>/trace.json.
//
// O trace nao guarda input, output, respostas de formulario nem mensagens de erro:
// apenas IDs, labels, tipos, status, profundidade e tempos. A UI usa `calls` para
// destacar no canvas as arestas de chamada realmente percorridas.

#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

void traceTaskStart(const std::string& nodeId, const std::string& label);
void traceTaskEnd(const std::string& nodeId, const std::string& status);

void traceCallStart(const std::string& callId, const std::string& callerId,
                    const std::string& targetId, const std::string& label,
                    const std::string& kind, int depth);
void traceCallEnd(const std::string& callId, const std::string& status);

void traceInteraction(const std::string& requestId, const std::string& nodeId,
                      const std::string& status);

// Fecha o run e grava trace.json. Chamado pelo main gerado, depois de drenar
// chamadas assincronas.
void finishTrace(bool success);

} // namespace rt
} // namespace taskforge
