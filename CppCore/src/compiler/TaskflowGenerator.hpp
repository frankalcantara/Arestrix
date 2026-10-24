#pragma once

#include "../core/Graph.hpp"
#include <map>
#include <string>
#include <vector>

namespace taskforge {
namespace compiler {

class TaskflowGenerator {
public:
    // Gera o main.cpp C++20/Taskflow equivalente ao grafo. Nos "Agent" chamam o runtime
    // (rt/Runtime.hpp) em vez de apenas imprimir mensagens.
    // `inputContextExpr` e usado pelo gerador de subflow para injetar o payload do chamador
    // como inputContext dos agentes/interacoes; vazio mantem o fluxo normal.
    static std::string generateSource(const core::Graph& graph, const std::string& inputContextExpr = "");

    // Fontes das funcoes nativas dos subflows chamaveis (arquivo logico -> conteudo C++).
    static std::map<std::string, std::string> generateSubflowSources(const core::Graph& graph);

    static std::string generateBatchSource(
        const core::Graph& graph,
        const std::vector<std::string>& batchItems,
        const std::vector<std::string>& serializedStages,
        const std::string& stateFilePath = "pipeline-state.json"
    );

    // Skills embutidas nos nos (sem arquivo .md) precisam virar arquivos no workspace
    // para o runtime le-las: devolve caminho relativo -> conteudo Markdown.
    static std::map<std::string, std::string> collectSkillAssets(const core::Graph& graph);

    static std::string generateCMakeLists(const core::Graph& graph);
    static std::string generateMakefile(const core::Graph& graph);

    struct MsvcScripts {
        std::string ps1;
        std::string bat;
    };
    static MsvcScripts generateMsvcBuildScript(const core::Graph& graph);

    // Configuracao efetiva (heranca global resolvida) de um no de agente, no formato
    // consumido por taskforge::rt::runAgent.
    static taskforge::Json buildAgentSpec(const core::Graph& graph, const core::NodeData& node);

    // Configuracao efetiva de um no de interacao; em schemaSource=vertex preenche
    // provider/model/temperature/fallback a partir dos Padroes Globais quando vazios.
    static taskforge::Json buildInteractionSpec(const core::Graph& graph, const core::NodeData& node);

    // Utilitarios (publicos para teste).
    static std::string sanitizeIdentifier(const std::string& id);
    static std::string cppString(const std::string& text);   // literal C++ entre aspas, com escapes

private:
    static std::string indent(const std::string& text, int spaces);
};

} // namespace compiler
} // namespace taskforge
