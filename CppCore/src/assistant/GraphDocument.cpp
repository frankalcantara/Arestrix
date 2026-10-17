#include "assistant/GraphDocument.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace taskforge {
namespace assistant {

namespace {

using rt::RuntimeLang;
using rt::langPick;

const std::vector<std::string>& nodeTypes() {
    static const std::vector<std::string> types = {"static", "condition", "subflow", "module", "critical", "cuda",
                                                   "agent", "fork", "join", "switch", "mcp", "output"};
    return types;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string str(const Json& j, const char* key) {
    return j.is_object() ? j.value(key, std::string()) : std::string();
}

Json port(const std::string& id, const std::string& name, const char* direction, const char* type) {
    Json p = Json::object();
    p["id"] = id;
    p["name"] = name;
    p["direction"] = direction;
    p["type"] = type;
    return p;
}

// Mesmo texto padrao de Graph.getDefaultLambdaBody no Studio.
std::string defaultLambda(const std::string& type, const std::string& label) {
    if (type == "mcp") return "";
    if (type == "switch") {
        return "// Switch " + label + ": devolva o índice do caso (0, 1, 2...).\n"
               "// O último caso é o \"default\": qualquer valor fora do intervalo vai para ele.\nreturn 0;";
    }
    if (type == "condition") {
        return "// Decisão de branch do agente " + label + "\n// Retorna 0 para ramo 0, 1 para ramo 1\nreturn (rand() % 2);";
    }
    if (type == "subflow") {
        return "// Subfluxo gerado dinamicamente para " + label +
               "\nauto sub_a = sf.emplace([]() { std::cout << \"Subtask A\\n\"; }).name(\"SubA\");"
               "\nauto sub_b = sf.emplace([]() { std::cout << \"Subtask B\\n\"; }).name(\"SubB\");\nsub_a.precede(sub_b);";
    }
    if (type == "module") return "// Módulo reutilizável composto de outro subgrafo";
    if (type == "critical") {
        return "// Seção crítica com acesso exclusivo a recursos compartilhados\nstd::cout << \"Acesso exclusivo seguro pelo agente " +
               label + "\\n\";";
    }
    if (type == "cuda") {
        return "// Kernel CUDA acelerado na GPU: " + label + "\nstd::cout << \"[CUDA: " + label + "] Executando kernel na GPU...\\n\";";
    }
    if (type == "agent") {
        return "// Agente Autônomo Code Mode: " + label + "\n// Invocação em sandbox/VM isolada com streaming de eventos\n"
               "std::cout << \"[Agent: " + label + "] Disparando no sandbox isolado...\\n\";";
    }
    if (type == "output") {
        return "// Coletor de Entrega / Artefato: " + label + "\n"
               "// Em runtime, este vertice registra e disponibiliza o artefato produzido na aba Artefatos e na pasta de saida.\n"
               "std::cout << \"[OUTPUT: " + label + "] Entregavel registrado com sucesso!\\n\";";
    }
    return "// Execução computacional determinística do agente: " + label + "\nstd::cout << \"Executando agente: " + label + "\\n\";";
}

// Mesma estrutura de Graph.buildNode no Studio.
Json buildNode(const std::string& id, const std::string& label, const std::string& type, double x, double y) {
    Json inputs = Json::array({port(id + "_in_0", "in", "input", "flow")});
    Json outputs = Json::array();
    if (type == "condition") {
        for (int i = 0; i < 2; ++i) {
            Json p = port(id + "_out_" + std::to_string(i), "branch " + std::to_string(i), "output", "flow");
            p["conditionBranchIndex"] = i;
            outputs.push_back(p);
        }
    } else if (type == "switch") {
        const char* names[] = {"case 1", "case 2"};
        for (int i = 0; i < 2; ++i) {
            Json p = port(id + "_out_" + std::to_string(i), names[i], "output", "flow");
            p["conditionBranchIndex"] = i;
            outputs.push_back(p);
        }
        Json def = port(id + "_out_default", "default", "output", "flow");
        def["conditionBranchIndex"] = 2;
        outputs.push_back(def);
    } else if (type == "agent") {
        Json prompt = port(id + "_in_prompt", "prompt_in", "input", "data");
        prompt["dataType"] = "std::string";
        inputs.push_back(prompt);
        outputs.push_back(port(id + "_out_0", "out", "output", "flow"));
        Json artifact = port(id + "_out_data", "artifact_out", "output", "data");
        artifact["dataType"] = "std::string";
        outputs.push_back(artifact);
    } else if (type == "output") {
        Json dataIn = port(id + "_in_data", "data_in", "input", "data");
        dataIn["dataType"] = "std::string";
        inputs.push_back(dataIn);
        outputs.push_back(port(id + "_out_0", "out", "output", "flow"));
    } else {
        outputs.push_back(port(id + "_out_0", "out", "output", "flow"));
    }

    Json node = Json::object();
    node["id"] = id;
    node["label"] = label;
    node["type"] = type;
    node["position"] = Json{{"x", x}, {"y", y}, {"z", 0}};
    node["inputs"] = inputs;
    node["outputs"] = outputs;
    node["parameters"] = Json::object();
    node["customLambdaBody"] = defaultLambda(type, label);
    if (type == "agent" || type == "switch") {
        node["agentSettings"] = Json{{"inheritSandbox", true}, {"inheritModel", true},
                                     {"inheritCodeMode", true}, {"inheritMemory", true}};
    }
    if (type == "switch") {
        node["switchSettings"] = Json{{"mode", "model"}, {"source", Json{{"kind", "previous"}}},
                                      {"rules", Json::object()}, {"instruction", ""}};
    }
    if (type == "mcp") {
        node["mcpSettings"] = Json{{"server", ""}, {"tool", ""}, {"arguments", Json::object()},
                                   {"source", Json{{"kind", "previous"}}}, {"output", Json::object()}};
    }
    if (type == "output") {
        node["outputSettings"] = Json{{"sourcePath", ""}, {"destinationPath", ""},
                                      {"artifactType", "auto"}, {"displayName", label},
                                      {"autoOpenPreview", true}, {"description", ""}};
    }
    return node;
}

// Porta por id ou nome (sem diferenciar maiusculas); vazio = primeira porta de fluxo da direcao.
const Json* findPort(const Json& node, const std::string& idOrName, bool output) {
    const char* list = output ? "outputs" : "inputs";
    if (!node.contains(list) || !node.at(list).is_array()) return nullptr;
    const Json& ports = node.at(list);
    if (idOrName.empty()) {
        for (const auto& p : ports) if (str(p, "type") == "flow") return &p;
        return ports.size() > 0 ? &ports.at(0) : nullptr;
    }
    for (const auto& p : ports) if (str(p, "id") == idOrName) return &p;
    std::string wanted = lower(idOrName);
    for (const auto& p : ports) if (lower(str(p, "name")) == wanted) return &p;
    return nullptr;
}

Json portList(const Json& node, const char* list) {
    Json out = Json::array();
    if (!node.contains(list) || !node.at(list).is_array()) return out;
    for (const auto& p : node.at(list)) {
        out.push_back(Json{{"id", str(p, "id")}, {"name", str(p, "name")}, {"type", str(p, "type")}});
    }
    return out;
}

std::string portName(const Json* node, const std::string& portId) {
    if (!node) return portId;
    for (const char* list : {"inputs", "outputs"}) {
        if (!node->contains(list)) continue;
        for (const auto& p : node->at(list)) if (str(p, "id") == portId) return str(p, "name");
    }
    return portId;
}

} // namespace

bool isKnownNodeType(const std::string& type) {
    const auto& t = nodeTypes();
    return std::find(t.begin(), t.end(), type) != t.end();
}

GraphDocument::GraphDocument(Json schema) { reset(std::move(schema)); }

void GraphDocument::reset(Json schema) {
    schema_ = schema.is_object() ? std::move(schema) : emptySchema("", "");
    if (!schema_.contains("nodes") || !schema_["nodes"].is_array()) schema_["nodes"] = Json::array();
    if (!schema_.contains("edges") || !schema_["edges"].is_array()) schema_["edges"] = Json::array();
}

Json GraphDocument::emptySchema(const std::string& graphName, const std::string& projectId) {
    Json s = Json::object();
    s["version"] = "1.0.0";
    s["metadata"] = Json{{"graphName", graphName}, {"projectId", projectId}, {"description", ""}, {"targetStandard", "C++20"}};
    s["nodes"] = Json::array();
    s["edges"] = Json::array();
    return s;
}

Json* GraphDocument::findNode(const std::string& id) {
    for (auto& n : schema_["nodes"]) if (str(n, "id") == id) return &n;
    return nullptr;
}

const Json* GraphDocument::findNodeConst(const std::string& id) const {
    for (const auto& n : schema_.at("nodes")) if (str(n, "id") == id) return &n;
    return nullptr;
}

std::string GraphDocument::newId(const std::string& prefix) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    for (;;) {
        std::string id = prefix + "_" + std::to_string(ms) + "_" + std::to_string(++counter_);
        if (!findNodeConst(id)) return id;
    }
}

size_t GraphDocument::nodeCount() const { return schema_.at("nodes").size(); }

Json GraphDocument::summary() const {
    Json nodes = Json::array();
    for (const auto& n : schema_.at("nodes")) {
        Json item = Json::object();
        item["id"] = str(n, "id");
        item["type"] = str(n, "type");
        item["label"] = str(n, "label");
        if (n.contains("position") && n.at("position").is_object()) {
            item["x"] = n.at("position").value("x", 0.0);
            item["y"] = n.at("position").value("y", 0.0);
        }
        item["inputs"] = portList(n, "inputs");
        item["outputs"] = portList(n, "outputs");
        if (n.contains("childGraph") && n.at("childGraph").is_object()) {
            item["childNodes"] = n.at("childGraph").contains("nodes") ? n.at("childGraph").at("nodes").size() : 0;
        }
        nodes.push_back(item);
    }
    Json edges = Json::array();
    for (const auto& e : schema_.at("edges")) {
        const Json* src = findNodeConst(str(e, "sourceNodeId"));
        const Json* dst = findNodeConst(str(e, "targetNodeId"));
        edges.push_back(Json{{"id", str(e, "id")},
                             {"from", str(e, "sourceNodeId") + ":" + portName(src, str(e, "sourcePortId"))},
                             {"to", str(e, "targetNodeId") + ":" + portName(dst, str(e, "targetPortId"))}});
    }
    Json out = Json::object();
    out["graphName"] = schema_.contains("metadata") ? str(schema_.at("metadata"), "graphName") : std::string();
    out["nodes"] = nodes;
    out["edges"] = edges;
    return out;
}

std::optional<Json> GraphDocument::node(const std::string& id) const {
    const Json* n = findNodeConst(id);
    if (!n) return std::nullopt;
    return *n;
}

EditResult GraphDocument::addNode(const std::string& type, const std::string& label, std::optional<double> x,
                                  std::optional<double> y, const std::string& lambda, RuntimeLang lang) {
    EditResult r;
    if (!isKnownNodeType(type)) {
        r.error = langPick(lang, "Tipo de vertice desconhecido: '", "Unknown vertex type: '") + type + "'.";
        return r;
    }
    // Sem posicao: a direita do vertice mais a direita, na mesma altura dele.
    double px = 0, py = 0;
    bool first = true;
    for (const auto& n : schema_.at("nodes")) {
        if (!n.contains("position")) continue;
        double nx = n.at("position").value("x", 0.0);
        if (first || nx > px) { px = nx; py = n.at("position").value("y", 0.0); first = false; }
    }
    double fx = x ? *x : (first ? 0.0 : px + 300.0);
    double fy = y ? *y : py;

    std::string id = newId("node");
    std::string name = label.empty() ? type : label;
    Json n = buildNode(id, name, type, fx, fy);
    if (!lambda.empty()) n["customLambdaBody"] = lambda;
    schema_["nodes"].push_back(n);

    r.ok = true;
    r.result = Json{{"id", id}, {"type", type}, {"label", name}, {"x", fx}, {"y", fy},
                    {"inputs", portList(n, "inputs")}, {"outputs", portList(n, "outputs")}};
    r.uiOp = Json{{"op", "add_node"}, {"node", n}};
    return r;
}

EditResult GraphDocument::updateNode(const std::string& id, const Json& patch, RuntimeLang lang) {
    EditResult r;
    Json* n = findNode(id);
    if (!n) {
        r.error = langPick(lang, "Vertice nao encontrado: '", "Vertex not found: '") + id + "'.";
        return r;
    }
    static const std::set<std::string> scalarFields = {"label", "customLambdaBody", "criticalSectionId"};
    static const std::set<std::string> objectFields = {"parameters", "agentSettings", "switchSettings", "mcpSettings", "outputSettings"};
    Json applied = Json::object();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const std::string& key = it.key();
        const Json& val = it.value();
        if (scalarFields.count(key)) {
            if (!val.is_string()) {
                r.error = langPick(lang, "O campo '", "Field '") + key + langPick(lang, "' deve ser texto.", "' must be a string.");
                return r;
            }
            applied[key] = val;
        } else if (objectFields.count(key)) {
            if (!val.is_object()) {
                r.error = langPick(lang, "O campo '", "Field '") + key + langPick(lang, "' deve ser um objeto.", "' must be an object.");
                return r;
            }
            Json merged = n->contains(key) && n->at(key).is_object() ? n->at(key) : Json::object();
            for (auto f = val.begin(); f != val.end(); ++f) merged[f.key()] = f.value();
            applied[key] = merged;
        } else if (key == "requiredHeaders") {
            if (!val.is_array()) {
                r.error = langPick(lang, "O campo 'requiredHeaders' deve ser uma lista.", "Field 'requiredHeaders' must be a list.");
                return r;
            }
            applied[key] = val;
        } else {
            r.error = langPick(lang, "Campo nao editavel: '", "Field cannot be edited: '") + key + "'.";
            return r;
        }
    }
    if (applied.size() == 0) {
        r.error = langPick(lang, "Nada para alterar.", "Nothing to change.");
        return r;
    }
    for (auto it = applied.begin(); it != applied.end(); ++it) (*n)[it.key()] = it.value();
    r.ok = true;
    r.result = Json{{"id", id}, {"updated", applied}};
    r.uiOp = Json{{"op", "update_node"}, {"id", id}, {"patch", applied}};
    return r;
}

EditResult GraphDocument::removeNode(const std::string& id, RuntimeLang lang) {
    EditResult r;
    if (!findNode(id)) {
        r.error = langPick(lang, "Vertice nao encontrado: '", "Vertex not found: '") + id + "'.";
        return r;
    }
    Json nodes = Json::array();
    for (auto& n : schema_["nodes"]) if (str(n, "id") != id) nodes.push_back(n);
    Json edges = Json::array();
    Json removedEdges = Json::array();
    for (auto& e : schema_["edges"]) {
        if (str(e, "sourceNodeId") == id || str(e, "targetNodeId") == id) removedEdges.push_back(str(e, "id"));
        else edges.push_back(e);
    }
    schema_["nodes"] = nodes;
    schema_["edges"] = edges;
    r.ok = true;
    r.result = Json{{"removed", id}, {"removedEdges", removedEdges}};
    r.uiOp = Json{{"op", "remove_node"}, {"id", id}};
    return r;
}

EditResult GraphDocument::moveNode(const std::string& id, double x, double y, RuntimeLang lang) {
    EditResult r;
    Json* n = findNode(id);
    if (!n) {
        r.error = langPick(lang, "Vertice nao encontrado: '", "Vertex not found: '") + id + "'.";
        return r;
    }
    (*n)["position"] = Json{{"x", x}, {"y", y}, {"z", 0}};
    r.ok = true;
    r.result = Json{{"id", id}, {"x", x}, {"y", y}};
    r.uiOp = Json{{"op", "move_node"}, {"id", id}, {"x", x}, {"y", y}};
    return r;
}

EditResult GraphDocument::connect(const std::string& fromNode, const std::string& fromPort, const std::string& toNode,
                                  const std::string& toPort, RuntimeLang lang) {
    EditResult r;
    const Json* src = findNodeConst(fromNode);
    const Json* dst = findNodeConst(toNode);
    if (!src || !dst) {
        r.error = langPick(lang, "Vertice nao encontrado: '", "Vertex not found: '") + (!src ? fromNode : toNode) + "'.";
        return r;
    }
    if (fromNode == toNode) {
        r.error = langPick(lang, "Um vertice nao pode ser ligado a ele mesmo.", "A vertex cannot be connected to itself.");
        return r;
    }
    const Json* sp = findPort(*src, fromPort, true);
    const Json* tp = findPort(*dst, toPort, false);
    if (!sp) {
        r.error = langPick(lang, "Porta de saida nao encontrada em '", "Output port not found on '") + fromNode +
                  "': '" + fromPort + "'. " + langPick(lang, "Saidas: ", "Outputs: ") + portList(*src, "outputs").dump();
        return r;
    }
    if (!tp) {
        r.error = langPick(lang, "Porta de entrada nao encontrada em '", "Input port not found on '") + toNode +
                  "': '" + toPort + "'. " + langPick(lang, "Entradas: ", "Inputs: ") + portList(*dst, "inputs").dump();
        return r;
    }
    std::string sourcePortId = str(*sp, "id");
    std::string targetPortId = str(*tp, "id");
    std::string edgeId = "edge_" + fromNode + "_" + sourcePortId + "_to_" + toNode + "_" + targetPortId;
    for (const auto& e : schema_.at("edges")) {
        if (str(e, "id") == edgeId ||
            (str(e, "sourcePortId") == sourcePortId && str(e, "targetPortId") == targetPortId &&
             str(e, "sourceNodeId") == fromNode && str(e, "targetNodeId") == toNode)) {
            r.error = langPick(lang, "Essas portas ja estao ligadas (aresta '", "These ports are already connected (edge '") +
                      str(e, "id") + "').";
            return r;
        }
    }
    Json edge = Json{{"id", edgeId}, {"sourceNodeId", fromNode}, {"sourcePortId", sourcePortId},
                     {"targetNodeId", toNode}, {"targetPortId", targetPortId}};
    schema_["edges"].push_back(edge);
    r.ok = true;
    r.result = Json{{"edgeId", edgeId}, {"from", fromNode + ":" + str(*sp, "name")}, {"to", toNode + ":" + str(*tp, "name")}};
    r.uiOp = Json{{"op", "add_edge"}, {"edge", edge}};
    return r;
}

EditResult GraphDocument::disconnect(const std::string& edgeId, RuntimeLang lang) {
    EditResult r;
    Json edges = Json::array();
    bool found = false;
    for (auto& e : schema_["edges"]) {
        if (str(e, "id") == edgeId) found = true;
        else edges.push_back(e);
    }
    if (!found) {
        r.error = langPick(lang, "Aresta nao encontrada: '", "Edge not found: '") + edgeId + "'.";
        return r;
    }
    schema_["edges"] = edges;
    r.ok = true;
    r.result = Json{{"removed", edgeId}};
    r.uiOp = Json{{"op", "remove_edge"}, {"id", edgeId}};
    return r;
}

Json GraphDocument::validate(RuntimeLang lang) const {
    Json errors = Json::array();
    Json warnings = Json::array();
    const Json& nodes = schema_.at("nodes");
    const Json& edges = schema_.at("edges");

    std::map<std::string, const Json*> byId;
    for (const auto& n : nodes) byId[str(n, "id")] = &n;

    auto hasPort = [](const Json* n, const std::string& portId, const char* list) {
        if (!n || !n->contains(list)) return false;
        for (const auto& p : n->at(list)) if (str(p, "id") == portId) return true;
        return false;
    };

    std::map<std::string, std::vector<std::string>> adj;
    std::map<std::string, int> degree;
    std::set<std::string> seen;
    std::set<std::string> usedOutputs;
    for (const auto& e : edges) {
        std::string s = str(e, "sourceNodeId"), t = str(e, "targetNodeId");
        const Json* sn = byId.count(s) ? byId[s] : nullptr;
        const Json* tn = byId.count(t) ? byId[t] : nullptr;
        if (!sn || !tn) {
            errors.push_back(langPick(lang, "Aresta '", "Edge '") + str(e, "id") +
                             langPick(lang, "' aponta para um vertice inexistente.", "' points to a missing vertex."));
            continue;
        }
        if (!hasPort(sn, str(e, "sourcePortId"), "outputs") || !hasPort(tn, str(e, "targetPortId"), "inputs")) {
            errors.push_back(langPick(lang, "Aresta '", "Edge '") + str(e, "id") +
                             langPick(lang, "' usa uma porta inexistente.", "' uses a missing port."));
            continue;
        }
        std::string key = s + "|" + str(e, "sourcePortId") + "|" + t + "|" + str(e, "targetPortId");
        if (!seen.insert(key).second) {
            warnings.push_back(langPick(lang, "Aresta duplicada entre '", "Duplicate edge between '") + s + "' -> '" + t + "'.");
        }
        adj[s].push_back(t);
        degree[s]++;
        degree[t]++;
        usedOutputs.insert(s + "|" + str(e, "sourcePortId"));
    }

    // Componentes fortemente conexos (Tarjan): ciclo so e valido se passar por condition ou switch.
    std::map<std::string, int> index, low;
    std::set<std::string> onStack;
    std::vector<std::string> stack;
    int counter = 0;
    std::function<void(const std::string&)> strong = [&](const std::string& v) {
        index[v] = low[v] = counter++;
        stack.push_back(v);
        onStack.insert(v);
        for (const auto& w : adj[v]) {
            if (!index.count(w)) {
                strong(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack.count(w)) {
                low[v] = std::min(low[v], index[w]);
            }
        }
        if (low[v] == index[v]) {
            std::vector<std::string> comp;
            for (;;) {
                std::string w = stack.back();
                stack.pop_back();
                onStack.erase(w);
                comp.push_back(w);
                if (w == v) break;
            }
            if (comp.size() > 1) {
                bool hasDecision = false;
                std::string names;
                for (const auto& id : comp) {
                    std::string type = str(*byId[id], "type");
                    if (type == "condition" || type == "switch") hasDecision = true;
                    names += (names.empty() ? "" : ", ") + id;
                }
                if (!hasDecision) {
                    errors.push_back(langPick(lang, "Ciclo sem vertice de decisao (condition ou switch): ",
                                              "Cycle without a decision vertex (condition or switch): ") + names + ".");
                }
            }
        }
    };
    for (const auto& n : nodes) {
        std::string id = str(n, "id");
        if (!index.count(id)) strong(id);
    }

    for (const auto& n : nodes) {
        std::string id = str(n, "id");
        std::string type = str(n, "type");
        if ((type == "condition" || type == "switch") && n.contains("outputs")) {
            for (const auto& p : n.at("outputs")) {
                if (!usedOutputs.count(id + "|" + str(p, "id"))) {
                    warnings.push_back(langPick(lang, "Saida '", "Output '") + str(p, "name") +
                                       langPick(lang, "' do vertice de decisao '", "' of decision vertex '") + id +
                                       langPick(lang, "' nao leva a lugar nenhum.", "' leads nowhere."));
                }
            }
        }
        if (nodes.size() > 1 && !degree.count(id)) {
            warnings.push_back(langPick(lang, "Vertice isolado (sem arestas): '", "Isolated vertex (no edges): '") + id + "'.");
        }
    }

    Json out = Json::object();
    out["ok"] = errors.size() == 0;
    out["errors"] = errors;
    out["warnings"] = warnings;
    return out;
}

} // namespace assistant
} // namespace taskforge
