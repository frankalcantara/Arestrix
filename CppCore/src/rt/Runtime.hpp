#pragma once

// Runtime do Arestrix: API usada pelo codigo C++ gerado a partir de um grafo.
//
//   * telemetria por no (status running / settled / error) para o Studio;
//   * execucao de nos "Agent": laco de tool-use contra um LLM (DeepSeek, OpenAI,
//     Anthropic, Gemini, Jev ou modelo local) com as ferramentas Code Mode.
//
// O codigo gerado nao depende de simdjson/nlohmann: a configuracao do agente viaja
// como uma string JSON.

#include <exception>
#include <filesystem>
#include <string>

namespace taskforge {
namespace rt {

// Prepara o processo: stdout sem buffer (telemetria em tempo real) e raiz do workspace.
void init();

// Raiz do workspace (diretorio de trabalho do pipeline).
std::filesystem::path workspaceRoot();

// Emite um evento de status de no no canal de telemetria.
void nodeEvent(const std::string& nodeId, const std::string& status, const std::string& detail = "");

// RAII: marca o no como "running" ao entrar e "settled"/"error" ao sair
// (error quando a saida ocorre por excecao).
class NodeScope {
public:
    NodeScope(std::string nodeId, std::string label);
    ~NodeScope();
    NodeScope(const NodeScope&) = delete;
    NodeScope& operator=(const NodeScope&) = delete;

private:
    std::string nodeId_;
    std::string label_;
    int uncaughtAtEntry_;
};

// Executa um no de agente. `specJson` descreve provider, modelo, skill, ferramentas e
// politica de sandbox (ver AgentSpec em Agent.cpp). Lanca std::runtime_error em falha.
// `runKey` identifica a execucao para o orcamento de repeticoes (padrao: nodeId); em lotes,
// use nodeId + item para que cada item tenha o proprio orcamento.
// `memoryContext` e um bloco de dados historicos (Blackboard/licoes) injetado no prompt de
// sistema como dado nao confiavel; vazio desativa a injecao.
void runAgent(const std::string& nodeId, const std::string& label, const std::string& specJson,
              const std::string& runKey = "", const std::string& memoryContext = "");

// Chamada unica a um modelo (sem ferramentas): provider e chaves seguem as mesmas regras dos agentes
// (variaveis de ambiente do usuario). Nunca lanca excecao; veja `ok` e `error`.
struct CompletionRequest {
    std::string provider;
    std::string model;
    std::string system;
    std::string user;
    int maxTokens = 4096;
    int maxAttempts = 2;
    bool hasTemperature = false;
    double temperature = 0.0;
};

struct CompletionResult {
    bool ok = false;
    std::string text;
    std::string error;
};

CompletionResult completeText(const CompletionRequest& req);

// Decide o caminho de um vertice switch sem codigo escrito pelo usuario. `specJson` traz o modo
// ("rules" ou "model"), os rotulos dos casos (o ultimo e o default), a origem do texto, as palavras de cada
// caso e o modelo. Devolve o indice do caso; lanca std::runtime_error se faltar entrada ou modelo.
int decideCase(const std::string& nodeId, const std::string& label, const std::string& specJson);

// Vertice MCP: chama uma ferramenta de um servidor MCP do projeto (sessao persistente) e grava o resultado em
// .arestrix/out/<nodeId>.txt. Lanca std::runtime_error se o servidor, a entrada ou a ferramenta falharem.
void callMcpTool(const std::string& nodeId, const std::string& label, const std::string& specJson);

} // namespace rt
} // namespace taskforge
