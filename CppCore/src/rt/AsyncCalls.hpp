#pragma once

// Chamadas assincronas do runtime: o modelo inicia uma chamada, recebe um callId
// pendente e consulta o resultado depois com a tool call_result.
//
// A semantica do alvo continua a mesma de invokeCall (mundo fechado, maxCalls,
// maxDepth, maxTotalCalls, notificacao), mas a task do agente nao espera a
// conclusao. No fim do pipeline, drainAsyncCalls() espera as chamadas pendentes
// para que notificacoes/auditoria nao sejam perdidas na saida do processo.

#include <string>
#include <vector>

#include "rt/CallGraph.hpp"
#include "rt/RuntimeLang.hpp"
#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

// Inicia uma chamada declarada com `async: true`. Devolve JSON com status
// "pending" e o callId que deve ser usado em call_result.
ToolResult invokeCallAsync(const std::string& callerId, const std::string& callerRunId,
                           const std::vector<CallTarget>& targets, const taskforge::Json& args,
                           RuntimeLang lang);

// Consulta o resultado de uma chamada assincrona iniciada por este chamador/run.
// Enquanto pendente, devolve status "pending" e nao bloqueia.
ToolResult fetchAsyncCallResult(const std::string& callerId, const std::string& callerRunId,
                                const taskforge::Json& args, RuntimeLang lang);

// Tool call_result. Vazio quando nenhum alvo permite chamadas assincronas.
std::vector<ToolDef> asyncCallToolDefinitions(const std::vector<CallTarget>& targets, RuntimeLang lang);

// Espera todas as chamadas pendentes (best effort). Chamado pelo main gerado
// antes de encerrar o processo.
void drainAsyncCalls();

} // namespace rt
} // namespace taskforge
