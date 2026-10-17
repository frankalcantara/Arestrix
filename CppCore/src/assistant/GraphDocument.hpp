#pragma once

// Documento do grafo editado pelo Assistente do Studio.
//
// Trabalha direto sobre o JSON do grafo raiz (o mesmo de graph.getRootSchema() no Studio e de
// pipelines/main.json), sem passar pelo core::Graph: assim nenhum campo que o C++ nao modela se perde.
// Cada edicao devolve, alem do resultado para o modelo, a operacao que a interface aplica no canvas
// (e registra para desfazer). Opera sempre no nivel raiz do grafo.

#include <optional>
#include <string>

#include "rt/RuntimeLang.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace assistant {

// Resultado de uma edicao: `result` vai para o modelo; `uiOp` (se nao nulo) vai para a interface.
struct EditResult {
    bool ok = false;
    std::string error;   // texto para o modelo, no idioma da sessao
    Json result;
    Json uiOp;
};

// Tipos de vertice conhecidos pelo Studio.
bool isKnownNodeType(const std::string& type);

class GraphDocument {
public:
    explicit GraphDocument(Json schema = Json());

    void reset(Json schema);
    const Json& schema() const { return schema_; }

    // Visao compacta para o modelo: vertices (id, tipo, rotulo, posicao, portas) e arestas.
    Json summary() const;
    std::optional<Json> node(const std::string& id) const;
    size_t nodeCount() const;

    EditResult addNode(const std::string& type, const std::string& label, std::optional<double> x,
                       std::optional<double> y, const std::string& lambda, rt::RuntimeLang lang);
    // Campos aceitos: label, customLambdaBody, parameters, agentSettings, switchSettings, mcpSettings,
    // requiredHeaders, criticalSectionId (objetos sao mesclados no existente).
    EditResult updateNode(const std::string& id, const Json& patch, rt::RuntimeLang lang);
    EditResult removeNode(const std::string& id, rt::RuntimeLang lang);
    EditResult moveNode(const std::string& id, double x, double y, rt::RuntimeLang lang);
    // Portas por id ou nome; vazias = primeira porta de fluxo de saida / entrada.
    EditResult connect(const std::string& fromNode, const std::string& fromPort, const std::string& toNode,
                       const std::string& toPort, rt::RuntimeLang lang);
    EditResult disconnect(const std::string& edgeId, rt::RuntimeLang lang);

    // {"ok", "errors": [...], "warnings": [...]}: arestas orfas, ciclos sem vertice de decisao,
    // saidas de decisao sem destino, vertices isolados e arestas duplicadas.
    Json validate(rt::RuntimeLang lang) const;

    // Grafo vazio com metadados do projeto.
    static Json emptySchema(const std::string& graphName, const std::string& projectId);

private:
    Json* findNode(const std::string& id);
    const Json* findNodeConst(const std::string& id) const;
    std::string newId(const std::string& prefix);

    Json schema_;
    unsigned counter_ = 0;
};

} // namespace assistant
} // namespace taskforge
