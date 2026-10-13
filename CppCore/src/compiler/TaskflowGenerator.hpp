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
    static std::string generateSource(const core::Graph& graph);

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

    // Utilitarios (publicos para teste).
    static std::string sanitizeIdentifier(const std::string& id);
    static std::string cppString(const std::string& text);   // literal C++ entre aspas, com escapes

private:
    static std::string indent(const std::string& text, int spaces);
};

} // namespace compiler
} // namespace taskforge
