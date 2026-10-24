#pragma once

// Grafo de chamadas do runtime (mundo fechado).
//
// O codigo gerado chama setCallRegistry() uma vez com dois mapas JSON:
//   callables: { "<id>": { "kind": "agent", "label": "...", "spec": {...} } }
//   callers:   { "<id>": [ { "target": "...", "label": "...", "kind": "agent",
//                            "maxCalls": 1, "timeoutSeconds": 0,
//                            "onError": "fail", "description": "..." } ] }
//
// runAgent() consulta callTargetsFor(nodeId) e oferece a tool call_agent apenas para
// os alvos declarados. invokeCall() executa o alvo de forma sincrona, aplica os
// limites e devolve um JSON como resultado da tool.

#include <functional>
#include <string>
#include <vector>

#include "rt/RuntimeLang.hpp"
#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

struct CallTarget {
    std::string id;
    std::string label;
    std::string kind = "agent";
    int maxCalls = 1;
    int timeoutSeconds = 0;
    std::string onError = "fail";
    std::string description;
};

// Substitui o registro. O codigo gerado chama uma vez no inicio do pipeline.
// Lanca std::runtime_error se o JSON for invalido.
void setCallRegistry(const std::string& callablesJson, const std::string& callersJson);

// Alvos permitidos para um chamador (vazio quando ele nao pode chamar ninguem).
std::vector<CallTarget> callTargetsFor(const std::string& callerId);

// Definicoes da tool call_agent. Vazio quando targets esta vazio.
std::vector<ToolDef> callToolDefinitions(const std::vector<CallTarget>& targets, RuntimeLang lang);

// Registra a funcao nativa de um subflow chamavel (gerada no mesmo executavel).
// O codigo gerado chama isto depois de setCallRegistry().
void registerSubflow(const std::string& id, std::function<std::string(const std::string&)> runner);

// Executa uma chamada declarada. `callerRunId` identifica a execucao do chamador
// para o orcamento de maxCalls. Devolve o resultado JSON no conteudo da tool.
ToolResult invokeCall(const std::string& callerId, const std::string& callerRunId,
                      const std::vector<CallTarget>& targets, const taskforge::Json& args,
                      RuntimeLang lang);

// Profundidade de chamadas da thread atual (0 fora de uma chamada).
int currentCallDepth();

// Variante usada por chamadas paralelas: o novo thread herda a profundidade do
// chamador, para que maxDepth continue valendo.
ToolResult invokeCallAtDepth(const std::string& callerId, const std::string& callerRunId,
                             const std::vector<CallTarget>& targets, const taskforge::Json& args,
                             RuntimeLang lang, int parentDepth);

} // namespace rt
} // namespace taskforge
