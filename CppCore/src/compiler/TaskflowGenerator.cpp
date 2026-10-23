#include "TaskflowGenerator.hpp"
#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <sstream>
#include <stdexcept>

namespace taskforge {
namespace compiler {

using json = taskforge::Json;

// ---------------------------------------------------------------------------
// Utilitarios
// ---------------------------------------------------------------------------
std::string TaskflowGenerator::sanitizeIdentifier(const std::string& id) {
    std::string sanitized = id;
    for (char& c : sanitized) {
        if (!std::isalnum(static_cast<unsigned char>(c))) {
            c = '_';
        }
    }
    return sanitized;
}

std::string TaskflowGenerator::cppString(const std::string& text) {
    static const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '?':  out += "\\?"; break;   // evita trigraphs
            default:
                if (c < 0x20) {
                    out += "\\x";
                    out += hex[c >> 4];
                    out += hex[c & 0xF];
                    out += "\"\"";   // encerra a sequencia hexadecimal
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

std::string TaskflowGenerator::indent(const std::string& text, int spaces) {
    std::string pad(spaces, ' ');
    std::stringstream ss(text);
    std::string line;
    std::string result;
    bool first = true;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!first) result += "\n";
        result += pad + line;
        first = false;
    }
    return result;
}

namespace {

// Vertices cujo corpo devolve o indice do sucessor (condition) ou do caso (switch): unicos que podem fechar lacos.
bool isBranching(const core::NodeData& node) {
    return node.type == core::TaskflowNodeType::Condition || node.type == core::TaskflowNodeType::Switch;
}

// Texto seguro para comentarios de uma linha (sem quebras de linha).
std::string commentSafe(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    return s;
}

// Literal de string bruta dividido em pedacos (limite de ~16 KB por literal no MSVC).
std::string rawLiteral(const std::string& text) {
    std::string delim = "ARX";
    while (text.find(")" + delim + "\"") != std::string::npos) delim += "X";
    const size_t chunk = 6000;
    std::string out;
    for (size_t pos = 0; pos < text.size() || pos == 0; pos += chunk) {
        if (pos) out += "\n      ";
        out += "R\"" + delim + "(" + text.substr(pos, chunk) + ")" + delim + "\"";
        if (text.empty()) break;
    }
    return out;
}

std::vector<std::string> sortedNodeIds(const core::Graph& graph) {
    std::vector<std::string> ids;
    for (const auto& p : graph.getNodes()) ids.push_back(p.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<const core::EdgeData*> sortedEdges(const core::Graph& graph) {
    std::vector<const core::EdgeData*> edges;
    for (const auto& p : graph.getEdges()) edges.push_back(&p.second);
    std::sort(edges.begin(), edges.end(), [](const core::EdgeData* a, const core::EdgeData* b) { return a->id < b->id; });
    return edges;
}

// Nome de variavel unico por no (ids diferentes podem sanitizar para o mesmo texto).
std::map<std::string, std::string> buildVarNames(const std::vector<std::string>& ids) {
    std::map<std::string, std::string> names;
    std::set<std::string> used;
    for (const auto& id : ids) {
        std::string base = "task_" + TaskflowGenerator::sanitizeIdentifier(id);
        std::string name = base;
        int n = 2;
        while (!used.insert(name).second) name = base + "_" + std::to_string(n++);
        names[id] = name;
    }
    return names;
}

// Arestas de retorno: marcadas pela UI ou que fecham um ciclo (DFS). Somente arestas
// que saem de nos de condicao podem formar lacos validos em Taskflow.
std::set<std::string> computeLoopEdges(const core::Graph& graph, const std::vector<std::string>& ids) {
    std::set<std::string> loops;
    const auto edges = sortedEdges(graph);
    std::map<std::string, std::vector<const core::EdgeData*>> out;
    std::map<std::string, int> incoming;
    for (const auto* e : edges) {
        if (e->kind == core::EdgeKind::Call) continue;   // chamada nao e dependencia de fluxo
        if (e->isFeedbackLoop) { loops.insert(e->id); continue; }
        out[e->sourceNodeId].push_back(e);
        incoming[e->targetNodeId]++;
    }

    // Passo 1: arestas que saem de uma condicao e voltam para um no que alcanca essa mesma
    // condicao sao lacos validos de Taskflow (independe de qual no e a raiz do grafo).
    auto reaches = [&](const std::string& from, const std::string& to, const std::string& skipEdge) {
        std::set<std::string> seen{from};
        std::vector<std::string> stack{from};
        while (!stack.empty()) {
            std::string u = stack.back();
            stack.pop_back();
            if (u == to) return true;
            for (const auto* e : edges) {
                if (e->kind == core::EdgeKind::Call) continue;
                if (e->sourceNodeId != u || e->id == skipEdge || loops.count(e->id)) continue;
                if (seen.insert(e->targetNodeId).second) stack.push_back(e->targetNodeId);
            }
        }
        return false;
    };
    for (const auto& id : ids) {
        const auto& node = graph.getNodes().at(id);
        if (!isBranching(node)) continue;
        for (const auto* e : edges) {
            if (e->kind == core::EdgeKind::Call) continue;
            if (e->sourceNodeId != id || loops.count(e->id)) continue;
            if (reaches(e->targetNodeId, id, e->id)) loops.insert(e->id);
        }
    }
    out.clear();
    incoming.clear();
    for (const auto* e : edges) {
        if (e->kind == core::EdgeKind::Call) continue;
        if (loops.count(e->id)) continue;
        out[e->sourceNodeId].push_back(e);
        incoming[e->targetNodeId]++;
    }

    // Passo 2: qualquer ciclo restante (invalido) e quebrado por DFS para nao travar a execucao.
    std::map<std::string, int> color;   // 0 branco, 1 cinza, 2 preto
    std::function<void(const std::string&)> dfs = [&](const std::string& u) {
        color[u] = 1;
        for (const auto* e : out[u]) {
            int c = color[e->targetNodeId];
            if (c == 1) loops.insert(e->id);          // aresta de retorno
            else if (c == 0) dfs(e->targetNodeId);
        }
        color[u] = 2;
    };
    for (const auto& id : ids) if (incoming[id] == 0 && color[id] == 0) dfs(id);
    for (const auto& id : ids) if (color[id] == 0) dfs(id);
    return loops;
}

const char* kStandardIncludes =
    "#include <taskflow/taskflow.hpp>\n"
    "#include <algorithm>\n"
    "#include <chrono>\n"
    "#include <cstdlib>\n"
    "#include <filesystem>\n"
    "#include <fstream>\n"
    "#include <iostream>\n"
    "#include <memory>\n"
    "#include <mutex>\n"
    "#include <queue>\n"
    "#include <random>\n"
    "#include <regex>\n"
    "#include <sstream>\n"
    "#include <string>\n"
    "#include <syncstream>\n"
    "#include <unordered_map>\n"
    "#include <vector>\n";

const json& objectOrEmpty(const json& j, const char* key) {
    static const json empty = json::object();
    if (j.is_object() && j.contains(key) && j[key].is_object()) return j[key];
    return empty;
}

// Secao efetiva: padrao global; se o no nao herda, sobrepoe com os campos do no.
json effectiveSection(const json& globalDefaults, const json& nodeSettings, const char* inheritKey, const char* section) {
    json eff = objectOrEmpty(globalDefaults, section);
    if (nodeSettings.is_object()) {
        bool inherit = true;
        if (nodeSettings.contains(inheritKey) && nodeSettings[inheritKey].is_boolean()) inherit = nodeSettings[inheritKey].get<bool>();
        if (!inherit && nodeSettings.contains(section) && nodeSettings[section].is_object()) {
            for (auto it = nodeSettings[section].begin(); it != nodeSettings[section].end(); ++it) eff[it.key()] = it.value();
        }
    }
    return eff;
}

json effectiveMemory(const core::Graph& graph, const core::NodeData& node) {
    return effectiveSection(graph.getMetadata().globalDefaults, node.agentSettings, "inheritMemory", "memory");
}

bool anyMemoryPersistent(const core::Graph& graph) {
    json global = objectOrEmpty(graph.getMetadata().globalDefaults, "memory");
    bool persistent = global.value("enabled", false) && global.value("persistent", false);
    for (const auto& pair : graph.getNodes()) {
        if (pair.second.type != core::TaskflowNodeType::Agent) continue;
        json m = effectiveMemory(graph, pair.second);
        if (m.value("enabled", false) && m.value("persistent", false)) return true;
    }
    return persistent;
}

bool memoryMeasurementOff(const core::Graph& graph) {
    json global = objectOrEmpty(graph.getMetadata().globalDefaults, "memory");
    return global.value("measurementMode", std::string("normal")) == "memory_off";
}

bool memoryRecordMetrics(const core::Graph& graph) {
    json global = objectOrEmpty(graph.getMetadata().globalDefaults, "memory");
    return global.value("recordMetrics", false);
}

std::string cppStringList(const json& values) {
    std::string out = "std::vector<std::string>{";
    bool first = true;
    if (values.is_array()) {
        for (const auto& v : values) {
            if (!v.is_string()) continue;
            if (!first) out += ", ";
            out += TaskflowGenerator::cppString(v.get<std::string>());
            first = false;
        }
    }
    out += "}";
    return out;
}


std::string skillAssetPath(const std::string& nodeId) {
    return ".arestrix/skills/" + TaskflowGenerator::sanitizeIdentifier(nodeId) + ".md";
}

} // namespace

// ---------------------------------------------------------------------------
// Agentes
// ---------------------------------------------------------------------------
json TaskflowGenerator::buildAgentSpec(const core::Graph& graph, const core::NodeData& node) {
    const json& defaults = graph.getMetadata().globalDefaults;
    const json& settings = node.agentSettings;

    json model = effectiveSection(defaults, settings, "inheritModel", "model");
    json sandbox = effectiveSection(defaults, settings, "inheritSandbox", "sandbox");
    json codeMode = effectiveSection(defaults, settings, "inheritCodeMode", "codeMode");

    json spec = json::object();
    spec["provider"] = model.value("provider", "deepseek");
    spec["model"] = model.value("model", "");
    if (model.contains("temperature") && model["temperature"].is_number()) spec["temperature"] = model["temperature"];
    spec["promptTemplate"] = model.value("promptTemplate", "");

    std::string skillPath = model.value("skillPath", "");
    if (skillPath.empty()) {
        auto assets = collectSkillAssets(graph);
        if (assets.count(skillAssetPath(node.id))) skillPath = skillAssetPath(node.id);
    }
    spec["skillPath"] = skillPath;

    json memory = effectiveSection(defaults, settings, "inheritMemory", "memory");
    const bool forceMemoryOff = memoryMeasurementOff(graph);
    const bool effectiveMemoryEnabled = memory.value("enabled", false) && !forceMemoryOff;
    spec["memoryEnabled"] = effectiveMemoryEnabled;
    spec["memoryForceDisabled"] = forceMemoryOff;
    spec["memoryProjectId"] = graph.getMetadata().projectId;
    spec["memoryWriteEnabled"] = effectiveMemoryEnabled && memory.value("recordLessons", false);
    if (memory.contains("domains") && memory["domains"].is_array()) spec["memoryDomains"] = memory["domains"];

    json tools = json::array();
    if (codeMode.contains("allowedTools") && codeMode["allowedTools"].is_array()) tools = codeMode["allowedTools"];
    if (effectiveMemoryEnabled) {
        auto addTool = [&](const std::string& name) {
            for (const auto& tool : tools) if (tool.is_string() && tool.get<std::string>() == name) return;
            tools.push_back(name);
        };
        addTool("memory_search");
        if (memory.value("recordLessons", false)) addTool("memory_record_lesson");
    }
    spec["tools"] = tools;

    // Ferramentas MCP: servidores (ids em .workspace/mcps) e, opcionalmente, quais ferramentas deles sao permitidas.
    json mcpSpec = json::object();
    mcpSpec["servers"] = codeMode.contains("mcpServers") && codeMode["mcpServers"].is_array() ? codeMode["mcpServers"] : json::array();
    mcpSpec["allowedTools"] = codeMode.contains("allowedMcpTools") && codeMode["allowedMcpTools"].is_array() ? codeMode["allowedMcpTools"] : json::array();
    if (!mcpSpec["servers"].empty()) spec["mcp"] = mcpSpec;

    json sb = json::object();
    sb["isolationLevel"] = sandbox.value("isolationLevel", "container");
    sb["network"] = sandbox.value("network", "egress_llm_only");
    sb["maxMemoryMB"] = sandbox.value("maxMemoryMB", 512);
    sb["timeoutSeconds"] = sandbox.value("timeoutSeconds", 300);
    sb["readOnlyMounts"] = sandbox.contains("readOnlyMounts") ? sandbox["readOnlyMounts"] : json::array();
    sb["readWriteMounts"] = sandbox.contains("readWriteMounts") ? sandbox["readWriteMounts"] : json::array();
    sb["allowFallbackToHost"] = sandbox.value("allowFallbackToHost", false);
    spec["sandbox"] = sb;
    return spec;
}

std::map<std::string, std::string> TaskflowGenerator::collectSkillAssets(const core::Graph& graph) {
    std::map<std::string, std::string> assets;
    for (const auto& p : graph.getNodes()) {
        const auto& node = p.second;
        if (node.type != core::TaskflowNodeType::Agent) continue;
        const json& s = node.agentSettings;
        if (!s.is_object() || !s.contains("skills") || !s["skills"].is_array()) continue;
        std::string activeId = s.value("activeSkillId", "");
        for (const auto& skill : s["skills"]) {
            if (!skill.is_object()) continue;
            if (!activeId.empty() && skill.value("id", "") != activeId) continue;
            std::string md = skill.value("markdownContent", "");
            if (md.empty()) md = skill.value("systemPrompt", "");
            if (!md.empty()) {
                assets[skillAssetPath(node.id)] = md;
                break;
            }
        }
    }
    return assets;
}

// ---------------------------------------------------------------------------
// Pipeline simples
// ---------------------------------------------------------------------------
std::string TaskflowGenerator::generateSource(const core::Graph& graph) {
    const auto& meta = graph.getMetadata();
    const auto& nodes = graph.getNodes();
    const auto ids = sortedNodeIds(graph);
    const auto names = buildVarNames(ids);
    const auto loopEdges = computeLoopEdges(graph, ids);
    const auto edges = sortedEdges(graph);

    for (const auto& pair : nodes) {
        if (pair.second.type == core::TaskflowNodeType::Interaction) {
            throw std::runtime_error("Vertice de interacao '" + pair.second.id +
                                     "' ainda nao executavel: o canal humano chega na Fase 2B.");
        }
    }

    // Grafo de chamadas (fechado): arestas "call" nao entram no fluxo. O gerador emite um
    // registro de callables e a tabela de alvos de cada chamador para o runtime.
    std::map<std::string, std::vector<const core::EdgeData*>> callsFrom;
    std::set<std::string> callTargets;
    for (const auto* e : edges) {
        if (e->kind != core::EdgeKind::Call) continue;
        if (!nodes.count(e->sourceNodeId) || !nodes.count(e->targetNodeId)) {
            throw std::runtime_error("Aresta de chamada aponta para vertice inexistente: " + e->id);
        }
        const auto& src = nodes.at(e->sourceNodeId);
        const auto& dst = nodes.at(e->targetNodeId);
        if (src.type != core::TaskflowNodeType::Agent) {
            throw std::runtime_error("Aresta de chamada deve sair de um vertice Agent: " + e->sourceNodeId);
        }
        if (dst.type != core::TaskflowNodeType::Agent) {
            throw std::runtime_error("Destino de chamada nao suportado no v1 (use Agent): " + e->targetNodeId);
        }
        if (e->callSettings.maxCalls < 1) {
            throw std::runtime_error("maxCalls deve ser >= 1 na chamada " + e->sourceNodeId + " -> " + e->targetNodeId);
        }
        callsFrom[e->sourceNodeId].push_back(e);
        callTargets.insert(e->targetNodeId);
    }
    // v1: o alvo de uma chamada nao participa do fluxo (evita execucao dupla silenciosa).
    for (const auto& target : callTargets) {
        for (const auto* e : edges) {
            if (e->kind == core::EdgeKind::Call) continue;
            if (e->sourceNodeId == target || e->targetNodeId == target) {
                throw std::runtime_error("Vertice chamavel tambem participa do fluxo: " + target);
            }
        }
    }

    auto callEdgesJson = [&nodes](const std::vector<const core::EdgeData*>& list) {
        json out = json::array();
        for (const auto* e : list) {
            const auto& targetNode = nodes.at(e->targetNodeId);
            out.push_back(json{
                {"target", e->targetNodeId},
                {"label", targetNode.label.empty() ? targetNode.id : targetNode.label},
                {"kind", "agent"},
                {"maxCalls", e->callSettings.maxCalls},
                {"timeoutSeconds", e->callSettings.timeoutSeconds},
                {"onError", e->callSettings.onError},
                {"description", e->callSettings.description}
            });
        }
        return out;
    };

    json callers = json::object();
    for (const auto& pair : callsFrom) callers[pair.first] = callEdgesJson(pair.second);

    json registry = json::object();
    for (const auto& target : callTargets) {
        const auto& node = nodes.at(target);
        json entry = json::object();
        entry["kind"] = "agent";
        entry["label"] = node.label.empty() ? node.id : node.label;
        entry["spec"] = buildAgentSpec(graph, node);
        auto it = callsFrom.find(target);
        entry["calls"] = it == callsFrom.end() ? json::array() : callEdgesJson(it->second);
        registry[target] = entry;
    }

    bool hasCritical = false;
    bool hasCuda = false;
    for (const auto& pair : nodes) {
        if (pair.second.type == core::TaskflowNodeType::Critical) hasCritical = true;
        if (pair.second.type == core::TaskflowNodeType::Cuda) hasCuda = true;
    }

    std::string code = "// ============================================================================\n";
    code += "// Gerado automaticamente por Arestrix - Visual Taskflow IDE\n";
    code += "// Pipeline: " + commentSafe(meta.graphName) + "\n";
    code += "// Descricao: " + commentSafe(meta.description) + "\n";
    code += "// Padrao C++: " + commentSafe(meta.targetStandard) + "\n";
    code += "// ============================================================================\n\n";

    code += kStandardIncludes;
    if (hasCuda) code += "#include <taskflow/cuda/cudaflow.hpp>\n";
    code += "#include \"rt/Runtime.hpp\"\n";
    code += "#include \"memory/MemoryService.hpp\"\n\n";

    code += "struct AgentContext {\n";
    code += "  std::string pipeline_name = " + cppString(meta.graphName) + ";\n";
    code += "  uint64_t tick_count = 0;\n";
    code += "  bool abort_requested = false;\n\n";
    code += "  std::queue<std::string> task_queue;\n";
    code += "  std::mutex queue_mutex;\n\n";
    code += "  std::unordered_map<std::string, std::string> persistent_store;\n";
    code += "  std::mutex db_mutex;\n\n";
    code += "  double last_confidence_score = 0.0;\n";
    code += "  int retry_counter = 0;\n";
    code += "};\n\n";

    // Registro de chamadas fechado. O runtime consulta estes mapas para saber quais alvos
    // cada agente pode invocar e com quais limites; nao vira dependencia de Taskflow.
    if (!callTargets.empty()) {
        code += "[[maybe_unused]] static const char* arx_callables = " + rawLiteral(registry.dump()) + ";\n";
        code += "[[maybe_unused]] static const char* arx_callers = " + rawLiteral(callers.dump()) + ";\n\n";
    }

    code += "int main(int argc, char* argv[]) {\n";
    code += "  (void)argc; (void)argv;\n";
    code += "  taskforge::rt::init();\n";
    code += "  const std::string projectId = " + cppString(meta.projectId.empty() ? "default" : meta.projectId) + ";\n";
    code += "  auto& memory = taskforge::memory::MemoryService::getInstance();\n";
    code += "  const bool arx_mem_persistent = " + std::string(anyMemoryPersistent(graph) ? "true" : "false") +
            " || " + std::string(memoryRecordMetrics(graph) ? "true" : "false") +
            " || std::getenv(\"ARESTRIX_MEMORY_METRICS\") != nullptr;\n";
    code += "  if (!memory.configure(projectId, arx_mem_persistent)) {\n";
    code += "    std::cerr << \"[MEMORIA] nao foi possivel abrir o banco do projeto\\n\";\n";
    code += "  }\n";
    code += "  auto arx_mem_flush = [&]() {\n";
    code += "    if (!arx_mem_persistent) memory.clearSession(projectId);\n";
    code += "  };\n";
    code += "  try {\n";
    code += "  std::osyncstream(std::cout) << \">>> Iniciando pipeline Taskflow: \" << " + cppString(meta.graphName) + " << \"...\\n\";\n\n";
    code += "  AgentContext ctx;\n";
    code += "  tf::Executor executor;\n";
    code += "  tf::Taskflow taskflow(" + cppString(meta.graphName) + ");\n\n";
    if (!callTargets.empty()) {
        code += "  taskforge::rt::setCallRegistry(arx_callables, arx_callers);\n\n";
    }
    if (hasCritical) code += "  tf::Semaphore critical_sec(1);\n\n";

    code += "  auto task_bootstrap = taskflow.emplace([&ctx]() {\n";
    code += "    std::osyncstream(std::cout) << \">>> [Pipeline \" << ctx.pipeline_name << \"] Ambiente ativado.\\n\";\n";
    code += "  }).name(\"Pipeline_Bootstrap\");\n\n";

    // --- Tarefas ---------------------------------------------------------------------------
    for (const auto& id : ids) {
        if (callTargets.count(id)) continue;   // chamavel puro: nao vira task de fluxo
        const auto& node = nodes.at(id);
        const std::string& var = names.at(id);
        std::string label = node.label.empty() ? node.id : node.label;
        std::string scope = "    taskforge::rt::NodeScope arx_scope(" + cppString(node.id) + ", " + cppString(label) + ");\n";

        std::string body;
        std::string lambdaHead = "[&ctx, &memory, &projectId]()";
        if (node.type == core::TaskflowNodeType::Condition) {
            lambdaHead = "[&ctx, &memory, &projectId]() -> int";
            body = node.customLambdaBody.empty() ? "return 0;" : node.customLambdaBody;
        } else if (node.type == core::TaskflowNodeType::Switch) {
            // Switch: o corpo devolve o indice do caso (0..N-1). Fora do intervalo cai no ultimo
            // caso ("default"), nunca encerra o fluxo em silencio como uma condition faria.
            const int caseCount = static_cast<int>(node.outputs.size());
            std::string selector = node.customLambdaBody.empty() ? "return " + std::to_string(std::max(0, caseCount - 1)) + ";"
                                                                 : node.customLambdaBody;
            // Sem codigo: o runtime decide por palavras no texto ou perguntando a um modelo de IA.
            const json& ss = node.switchSettings;
            const std::string switchMode = ss.is_object() ? ss.value("mode", std::string("code")) : std::string("code");
            if (switchMode == "rules" || switchMode == "model") {
                json spec = json::object();
                spec["mode"] = switchMode;
                json labels = json::array();
                for (const auto& port : node.outputs) labels.push_back(port.name);
                spec["labels"] = labels;

                json rules = json::array();   // um item por caso, sem o default
                const json& ruleMap = ss["rules"];
                for (size_t pi = 0; pi + 1 < node.outputs.size(); ++pi) {
                    json kws = json::array();
                    std::string raw = ruleMap.is_object() ? ruleMap.value(node.outputs[pi].id, std::string()) : std::string();
                    std::string cur;
                    auto flush = [&]() {
                        size_t a = cur.find_first_not_of(" \t\r");
                        size_t b = cur.find_last_not_of(" \t\r");
                        if (a != std::string::npos) kws.push_back(cur.substr(a, b - a + 1));
                        cur.clear();
                    };
                    for (char c : raw) { if (c == ',' || c == ';' || c == '\n') flush(); else cur += c; }
                    flush();
                    rules.push_back(kws);
                }
                spec["rules"] = rules;
                spec["instruction"] = ss.value("instruction", std::string());

                json source = json::object();
                const json& srcCfg = ss["source"];
                source["kind"] = srcCfg.is_object() ? srcCfg.value("kind", std::string("previous")) : std::string("previous");
                source["path"] = srcCfg.is_object() ? srcCfg.value("path", std::string()) : std::string();
                json from = json::array();
                for (const auto* e : edges) if (e->targetNodeId == id) from.push_back(e->sourceNodeId);
                source["from"] = from;
                spec["source"] = source;

                if (switchMode == "model") {
                    json agentSpec = buildAgentSpec(graph, node);
                    spec["provider"] = agentSpec.value("provider", std::string("deepseek"));
                    spec["model"] = agentSpec.value("model", std::string());
                    if (agentSpec.contains("temperature")) spec["temperature"] = agentSpec["temperature"];
                }
                selector = "return taskforge::rt::decideCase(" + cppString(node.id) + ", " + cppString(label) + ",\n    " +
                           rawLiteral(spec.dump()) + ");";
            }
            lambdaHead = "[&ctx, &memory, &projectId]() -> int";
            body = "auto arx_select = [&]() -> int {\n" + indent(selector, 4) + "\n};\n" +
                   "const int arx_cases = " + std::to_string(caseCount) + ";\n" +
                   "int arx_case = arx_select();\n" +
                   "if (arx_case < 0 || arx_case >= arx_cases) arx_case = arx_cases - 1;   // default\n" +
                   "return arx_case;";
        } else if (node.type == core::TaskflowNodeType::Mcp && node.customLambdaBody.empty()) {
            // Vertice MCP: uma chamada de ferramenta pelo cliente MCP nativo (sessao persistente), sem codigo do usuario.
            const json& ms = node.mcpSettings;
            json spec = json::object();
            spec["server"] = ms.is_object() ? ms.value("server", std::string()) : std::string();
            spec["tool"] = ms.is_object() ? ms.value("tool", std::string()) : std::string();
            spec["arguments"] = (ms.is_object() && ms["arguments"].is_object()) ? ms["arguments"] : json::object();
            json source = json::object();
            const json& srcCfg = ms["source"];
            source["kind"] = srcCfg.is_object() ? srcCfg.value("kind", std::string("none")) : std::string("none");
            source["path"] = srcCfg.is_object() ? srcCfg.value("path", std::string()) : std::string();
            json from = json::array();
            for (const auto* e : edges) if (e->targetNodeId == id) from.push_back(e->sourceNodeId);
            source["from"] = from;
            spec["source"] = source;
            json output = json::object();
            output["file"] = (ms.is_object() && ms["output"].is_object()) ? ms["output"].value("file", std::string()) : std::string();
            spec["output"] = output;
            body = "taskforge::rt::callMcpTool(" + cppString(node.id) + ", " + cppString(label) + ",\n    " +
                   rawLiteral(spec.dump()) + ");";
        } else if (node.type == core::TaskflowNodeType::Cuda) {
            body = node.customLambdaBody.empty()
                ? "// Taskflow 4.1+ Native CUDA Graph (tf::cudaGraph / tf::cudaGraphExec)\n"
                  "tf::cudaGraph cg;\n"
                  "tf::cudaGraphExec exec(cg);\n"
                  "tf::cudaStream stream;\n"
                  "stream.run(exec);\n"
                  "stream.synchronize();\n"
                  "std::osyncstream(std::cout) << \"[CUDA] Grafo GPU executado.\\n\";"
                : node.customLambdaBody;
        } else if (node.type == core::TaskflowNodeType::Subflow || node.type == core::TaskflowNodeType::Module) {
            lambdaHead = "[&ctx, &memory, &projectId](tf::Subflow& sf)";
            body = node.customLambdaBody.empty() ? "(void)sf; // subflow hierarquico" : node.customLambdaBody;
        } else if (node.type == core::TaskflowNodeType::Agent && node.customLambdaBody.empty()) {
            json mem = effectiveMemory(graph, node);
            const bool memEnabled = mem.value("enabled", false) && !memoryMeasurementOff(graph);
            if (memEnabled) {
                json domains = mem.contains("domains") && mem["domains"].is_array() ? mem["domains"] : json::array();
                json keys = mem.contains("allowedKeys") && mem["allowedKeys"].is_array() ? mem["allowedKeys"] : json::array();
                const int budget = mem.value("contextBudgetTokens", 800);
                body = "    taskforge::memory::MemoryBriefInfo arx_brief_info;\n";
                body += "    std::string arx_mem_ctx;\n";
                body += "    if (std::getenv(\"ARESTRIX_MEMORY_DISABLE\") == nullptr) {\n";
                body += "      arx_mem_ctx = memory.buildBrief(projectId, \"pipeline\", " +
                        cppStringList(domains) + ", " + cppStringList(keys) + ", " +
                        std::to_string(std::max(0, budget)) + ", " +
                        (mem.value("semanticLessons", false) ? "true" : "false") + ", " +
                        (mem.value("readBlackboard", false) ? "true" : "false") + ", &arx_brief_info);\n";
                body += "      memory.recordBriefHits(projectId, " + cppString(node.id) + ", " +
                        cppString(node.id) + ", arx_brief_info);\n";
                body += "    }\n";
                body += "    if (!arx_mem_ctx.empty()) std::osyncstream(std::cout) << \"[MEMORIA] contexto injetado em \" << " + cppString(label) + " << \"\\n\";\n";
                std::string runCall = "taskforge::rt::runAgent(" + cppString(node.id) + ", " + cppString(label) + ",\n      " +
                                      rawLiteral(buildAgentSpec(graph, node).dump()) + ", \"\", arx_mem_ctx);";
                if (mem.value("recordLessons", false)) {
                    std::string domain = "geral";
                    if (domains.is_array()) {
                        for (const auto& d : domains) if (d.is_string()) { domain = d.get<std::string>(); break; }
                    }
                    std::string failPrefix = "Falha no agente " + label + ": ";
                    body += "    try {\n      " + runCall + "\n    } catch (const std::exception& arx_mem_err) {\n";
                    body += "      memory.recordLesson(projectId, " + cppString(domain) +
                            ", \"agent_failure\",\n          std::string(" + cppString(failPrefix) + ") + arx_mem_err.what(), " + cppString(node.id) + ");\n";
                    body += "      throw;\n    }\n";
                } else {
                    body += "    " + runCall + "\n";
                }
                if (mem.value("writeBlackboard", false)) {
                    body += "    memory.setWorking(projectId, \"pipeline\", \"last_stage\", taskforge::Json(" + cppString(node.id) + "));\n";
                }
            } else {
                body = "taskforge::rt::runAgent(" + cppString(node.id) + ", " + cppString(label) + ",\n      " +
                       rawLiteral(buildAgentSpec(graph, node).dump()) + ");";
            }
        } else if (node.type == core::TaskflowNodeType::Output) {
            std::string src = "";
            std::string dst = "";
            std::string atype = "auto";
            if (node.outputSettings.is_object()) {
                src = node.outputSettings.value("sourcePath", "");
                dst = node.outputSettings.value("destinationPath", "");
                atype = node.outputSettings.value("artifactType", "auto");
            }
            // A entrega vem das configuracoes do vertice (origem/destino), nao do corpo: um vertice
            // criado na interface traz o corpo padrao preenchido e ainda assim precisa copiar o artefato.
            body = "try {\n"
                   "    std::string arx_out_src = " + cppString(src) + ";\n"
                   "    std::string arx_out_dst = " + cppString(dst) + ";\n"
                   "    if (!arx_out_dst.empty() && !arx_out_src.empty() && std::filesystem::exists(arx_out_src)) {\n"
                   "        auto arx_out_parent = std::filesystem::path(arx_out_dst).parent_path();\n"
                   "        if (!arx_out_parent.empty()) std::filesystem::create_directories(arx_out_parent);\n"
                   "        std::filesystem::copy_file(arx_out_src, arx_out_dst, std::filesystem::copy_options::overwrite_existing);\n"
                   "    }\n"
                   "} catch (const std::exception& e) {\n"
                   "    std::cerr << \"[OUTPUT Erro] \" << e.what() << \"\\n\";\n"
                   "}\n";
            if (!node.customLambdaBody.empty()) {
                body += node.customLambdaBody + "\n";
            }
            if (body.find("[OUTPUT_DELIVERY]") == std::string::npos) {
                body += "std::cout << \"[OUTPUT_DELIVERY] {\\\"nodeId\\\": \\\"" + node.id + "\\\", \\\"label\\\": \\\"" + label + "\\\", \\\"source\\\": \\\"" + src + "\\\", \\\"destination\\\": \\\"" + dst + "\\\", \\\"type\\\": \\\"" + atype + "\\\"}\\n\";";
            }
        } else {
            body = node.customLambdaBody.empty()
                ? "std::cout << \"Executando \" << " + cppString(label) + " << \"\\n\";"
                : node.customLambdaBody;
        }

        code += "  auto " + var + " = taskflow.emplace(" + lambdaHead + " {\n";
        code += scope;
        code += indent(body, 4) + "\n";
        code += "  }).name(" + cppString(label) + ");\n";
        if (node.type == core::TaskflowNodeType::Critical) {
            code += "  " + var + ".acquire(critical_sec);\n  " + var + ".release(critical_sec);\n";
        }
        code += "\n";
    }

    // --- Dependencias ----------------------------------------------------------------------
    code += "  // Dependencias e Precedencias\n";
    std::map<std::string, int> strongIncoming;
    for (const auto* e : edges) {
        if (e->kind == core::EdgeKind::Call) continue;   // chamada nao e precedencia
        if (!loopEdges.count(e->id)) strongIncoming[e->targetNodeId]++;
    }
    std::vector<std::string> entries;
    for (const auto& id : ids) {
        if (!callTargets.count(id) && strongIncoming[id] == 0) entries.push_back(names.at(id));
    }
    if (!entries.empty()) {
        std::string targets;
        for (size_t i = 0; i < entries.size(); ++i) targets += (i ? ", " : "") + entries[i];
        code += "  task_bootstrap.precede(" + targets + ");\n";
    }
    code += "\n";

    int noopCounter = 0;
    for (const auto& id : ids) {
        if (callTargets.count(id)) continue;   // chamavel puro nao tem task de fluxo
        const auto& node = nodes.at(id);
        const std::string& var = names.at(id);

        std::vector<const core::EdgeData*> outEdges;
        for (const auto* e : edges) {
            if (e->sourceNodeId != id || e->kind == core::EdgeKind::Call) continue;
            outEdges.push_back(e);
        }
        if (outEdges.empty()) continue;

        if (isBranching(node)) {
            // O indice retornado pela condicao e a posicao do sucessor em precede():
            // monta o vetor por indice de ramo, preenchendo lacunas com tarefas vazias.
            std::map<int, std::vector<std::string>> byBranch;
            int nextAuto = 0;
            int maxIdx = -1;
            for (const auto* e : outEdges) {
                int idx = -1;
                for (const auto& port : node.outputs) if (port.id == e->sourcePortId) idx = port.conditionBranchIndex;
                if (idx < 0) idx = nextAuto;
                nextAuto = std::max(nextAuto, idx + 1);
                byBranch[idx].push_back(names.at(e->targetNodeId));
                maxIdx = std::max(maxIdx, idx);
            }
            std::vector<std::string> slots;
            for (int i = 0; i <= maxIdx; ++i) {
                auto it = byBranch.find(i);
                if (it == byBranch.end()) {
                    std::string noop = "branch_noop_" + std::to_string(noopCounter++);
                    code += "  auto " + noop + " = taskflow.emplace([]() {}).name(\"_ramo_vazio\");\n";
                    slots.push_back(noop);
                } else if (it->second.size() == 1) {
                    slots.push_back(it->second[0]);
                } else {
                    std::string fan = "branch_fan_" + std::to_string(noopCounter++);
                    code += "  auto " + fan + " = taskflow.emplace([]() {}).name(\"_ramo_multiplo\");\n";
                    for (const auto& t : it->second) code += "  " + fan + ".precede(" + t + ");\n";
                    slots.push_back(fan);
                }
            }
            std::string targets;
            for (size_t i = 0; i < slots.size(); ++i) targets += (i ? ", " : "") + slots[i];
            code += "  " + var + ".precede(" + targets + ");\n";
        } else {
            for (const auto* e : outEdges) {
                if (loopEdges.count(e->id)) {
                    code += "  // Aresta de retorno ignorada (" + commentSafe(id) + " -> " + commentSafe(e->targetNodeId) +
                            "): lacos em Taskflow exigem um no de condicao.\n";
                    continue;
                }
                code += "  " + var + ".precede(" + names.at(e->targetNodeId) + ");\n";
            }
        }
    }

    code += "\n  std::osyncstream(std::cout) << \">>> Disparando execucao no pool de threads...\\n\";\n";
    code += "  auto start = std::chrono::high_resolution_clock::now();\n\n";
    code += "  executor.run(taskflow).get();\n\n";
    code += "  auto end = std::chrono::high_resolution_clock::now();\n";
    code += "  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n";
    code += "  arx_mem_flush();\n";
    code += "  std::cout << \">>> Pipeline concluido com sucesso em \" << elapsed_ms << \" ms.\\n\";\n";
    code += "  } catch (const std::exception& ex) {\n";
    code += "    arx_mem_flush();\n";
    code += "    std::cerr << \">>> [ERRO] Pipeline interrompido: \" << ex.what() << std::endl;\n";
    code += "    return 1;\n";
    code += "  }\n";
    code += "  return 0;\n";
    code += "}\n";

    return code;
}

// ---------------------------------------------------------------------------
// Pipeline em lote (Resume Engine)
// ---------------------------------------------------------------------------
std::string TaskflowGenerator::generateBatchSource(
    const core::Graph& graph,
    const std::vector<std::string>& batchItems,
    const std::vector<std::string>& serializedStages,
    const std::string& stateFilePath
) {
    const auto& meta = graph.getMetadata();
    const auto& nodes = graph.getNodes();
    const auto ids = sortedNodeIds(graph);
    const auto loopEdges = computeLoopEdges(graph, ids);
    const auto edges = sortedEdges(graph);

    for (const auto* e : edges) {
        if (e->kind == core::EdgeKind::Call) {
            throw std::runtime_error("Pipeline em lote ainda nao suporta arestas de chamada no v1.");
        }
    }
    for (const auto& pair : nodes) {
        if (pair.second.type == core::TaskflowNodeType::Interaction) {
            throw std::runtime_error("Pipeline em lote nao suporta vertices de interacao no v1.");
        }
    }

    std::string code = "// ============================================================================\n";
    code += "// Gerado por Arestrix (Batch DAG & Resume Engine)\n";
    code += "// Pipeline: " + commentSafe(meta.graphName) + " (Lote de " + std::to_string(batchItems.size()) + " itens)\n";
    code += "// Arquivo de Checkpoint: " + commentSafe(stateFilePath) + "\n";
    code += "// ============================================================================\n\n";

    code += kStandardIncludes;
    code += "#include \"rt/Runtime.hpp\"\n";
    code += "#include \"sandbox/PipelineResumeEngine.hpp\"\n";
    code += "#include \"sandbox/AgentTelemetryEmitter.hpp\"\n";
    code += "#include \"memory/AgentBlackboard.hpp\"\n";
    code += "#include \"memory/SemanticMemoryEngine.hpp\"\n";
    code += "#include \"mcp/McpRegistry.hpp\"\n\n";

    std::string projId = meta.projectId.empty() ? "default" : meta.projectId;

    code += "static std::string arx_subst(std::string s, const std::string& item) {\n";
    code += "  const std::string key = \"{{itemId}}\";\n";
    code += "  size_t p = 0;\n";
    code += "  while ((p = s.find(key, p)) != std::string::npos) { s.replace(p, key.size(), item); p += item.size(); }\n";
    code += "  return s;\n";
    code += "}\n\n";

    code += "int main() {\n";
    code += "  taskforge::rt::init();\n";
    code += "  try {\n";
    code += "  std::cout << \">>> Inicializando Pipeline em Lote Arestrix...\\n\";\n";
    code += "  tf::Executor executor;\n";
    code += "  tf::Taskflow taskflow(\"BatchMultiAgentPipeline\");\n\n";
    code += "  auto& resumeEngine = taskforge::sandbox::PipelineResumeEngine::getInstance();\n";
    code += "  resumeEngine.setStateFilePath(" + cppString(stateFilePath) + ");\n\n";
    code += "  const std::string projectId = " + cppString(projId) + ";\n";
    code += "  auto& blackboard = taskforge::memory::AgentBlackboard::getInstance();\n";
    code += "  auto& semanticMemory = taskforge::memory::SemanticMemoryEngine::getInstance();\n\n";

    auto itemVar = [&](size_t idx, const std::string& nodeId) {
        return "task_i" + std::to_string(idx) + "_" + sanitizeIdentifier(nodeId);
    };

    for (size_t itemIdx = 0; itemIdx < batchItems.size(); ++itemIdx) {
        const std::string& itemId = batchItems[itemIdx];
        code += "  // --- Tarefas para o Item: " + commentSafe(itemId) + " ---\n";
        for (const auto& id : ids) {
            const auto& node = nodes.at(id);
            std::string label = node.label.empty() ? node.id : node.label;
            std::string taskVar = itemVar(itemIdx, node.id);

            code += "  auto " + taskVar + " = taskflow.emplace([&resumeEngine, &blackboard, &semanticMemory, &projectId, itemId = std::string(" +
                    cppString(itemId) + ")]() {\n";

            std::string ruleTargetPattern = "aulas/{itemId}.md";
            int ruleMinBytes = 200;
            std::string ruleContentPattern;
            bool ruleCheckExitCode = true;
            if (node.agentSettings.is_object() && node.agentSettings.contains("validationRules") &&
                node.agentSettings["validationRules"].is_array() && !node.agentSettings["validationRules"].empty()) {
                const auto& vr = node.agentSettings["validationRules"][0];
                ruleTargetPattern = vr.value("targetPathPattern", "aulas/{itemId}.md");
                ruleMinBytes = vr.value("minSizeBytes", 200);
                ruleContentPattern = vr.value("contentPattern", "");
                ruleCheckExitCode = vr.value("checkLogExitCode", true);
            }

            code += "    taskforge::rt::NodeScope arx_scope(" + cppString(node.id) + ", " + cppString(label) + ");\n";
            code += "    taskforge::sandbox::ArtifactValidationRule rule;\n";
            code += "    rule.targetPathPattern = " + cppString(ruleTargetPattern) + ";\n";
            code += "    rule.minSizeBytes = " + std::to_string(ruleMinBytes) + ";\n";
            if (!ruleContentPattern.empty()) code += "    rule.contentPattern = " + cppString(ruleContentPattern) + ";\n";
            code += "    rule.checkLogExitCode = " + std::string(ruleCheckExitCode ? "true" : "false") + ";\n\n";
            code += "    if (resumeEngine.isStageCompleted(itemId, " + cppString(node.id) + ", rule)) {\n";
            code += "      std::cout << \"[CHECKPOINT:RESUME] \" << itemId << \" :: \" << " + cppString(label) + " << \" ja concluido. Reaproveitando artefato.\\n\";\n";
            code += "      return;\n";
            code += "    }\n";
            code += "    resumeEngine.markStageStarted(itemId, " + cppString(node.id) + ");\n";
            code += "    std::cout << \"[EXECUTANDO] \" << itemId << \" :: \" << " + cppString(label) + " << \"\\n\";\n";

            // Memoria heterogenea (opt-in por agente)
            bool memEnabled = false, memReadBb = false, memWriteBb = false, memSemLessons = false;
            if (node.agentSettings.is_object()) {
                bool inherit = node.agentSettings.value("inheritMemory", true);
                json m = json::object();
                if (!inherit && node.agentSettings.contains("memory") && node.agentSettings["memory"].is_object()) {
                    m = node.agentSettings["memory"];
                } else if (meta.globalDefaults.is_object() && meta.globalDefaults.contains("memory")) {
                    m = meta.globalDefaults["memory"];
                }
                memEnabled = m.value("enabled", false);
                memReadBb = m.value("readBlackboard", false);
                memWriteBb = m.value("writeBlackboard", false);
                memSemLessons = m.value("semanticLessons", false);
            }
            if (memEnabled && memSemLessons) {
                code += "    auto semCtx = semanticMemory.formatPromptContext(projectId);\n";
                code += "    if (!semCtx.empty()) { std::cout << \"[MEMORIA:SEMANTICA] Contexto injetado para \" << itemId << \"\\n\"; }\n";
            }
            if (memEnabled && memReadBb) {
                code += "    auto bbSnap = blackboard.exportSnapshot(projectId, itemId);\n";
                code += "    if (!bbSnap.empty()) { std::cout << \"[MEMORIA:BLACKBOARD] Snapshot carregado para \" << itemId << \"\\n\"; }\n";
            }

            if (!node.customLambdaBody.empty()) {
                code += indent(node.customLambdaBody, 4) + "\n";
            } else if (node.type == core::TaskflowNodeType::Agent) {
                json spec = buildAgentSpec(graph, node);
                code += "    taskforge::rt::runAgent(" + cppString(node.id) + ", " + cppString(label) + ",\n      arx_subst(" +
                        rawLiteral(spec.dump()) + ", itemId), " + cppString(node.id) + " + \"#\" + itemId);\n";
            } else if (node.type == core::TaskflowNodeType::Static &&
                       node.parameters.find("action") != node.parameters.end() &&
                       node.parameters.at("action").is_string() &&
                       node.parameters.at("action").get<std::string>() == "publish_jekyll_post") {
                std::string destDir = "../matematica/_posts/matematica";
                auto itDest = node.parameters.find("destinationDir");
                if (itDest != node.parameters.end() && itDest->second.is_string()) destDir = itDest->second.get<std::string>();
                code += "    std::error_code ec;\n";
                code += "    std::filesystem::create_directories(" + cppString(destDir) + ", ec);\n";
                code += "    std::string srcFile = \"aulas/\" + itemId + \".md\";\n";
                code += "    std::string dstFile = " + cppString(destDir + "/") + " + itemId + \".md\";\n";
                code += "    if (std::filesystem::exists(srcFile)) {\n";
                code += "      std::filesystem::copy_file(srcFile, dstFile, std::filesystem::copy_options::overwrite_existing, ec);\n";
                code += "    }\n";
                code += "    std::cout << \"[PUBLICAR] Artigo \" << itemId << \" enviado para \" << " + cppString(destDir) + " << \"\\n\";\n";
            }

            if (memEnabled && memWriteBb) {
                code += "    blackboard.set(projectId, itemId, \"last_stage\", " + cppString(node.id) + ");\n";
            }
            code += "    resumeEngine.markStageSuccess(itemId, " + cppString(node.id) + ", rule.targetPathPattern);\n";
            code += "  }).name(" + cppString(itemId + " :: " + label) + ");\n\n";
        }

        code += "  // Dependencias internas do Item: " + commentSafe(itemId) + "\n";
        for (const auto* e : edges) {
            if (loopEdges.count(e->id)) continue;
            code += "  " + itemVar(itemIdx, e->sourceNodeId) + ".precede(" + itemVar(itemIdx, e->targetNodeId) + ");\n";
        }
        code += "\n";
    }

    if (!serializedStages.empty() && batchItems.size() > 1) {
        code += "  // --- Encadeamento serial entre itens (etapas pesadas) ---\n";
        for (const auto& stageId : serializedStages) {
            if (!nodes.count(stageId)) continue;
            for (size_t i = 0; i + 1 < batchItems.size(); ++i) {
                code += "  " + itemVar(i, stageId) + ".precede(" + itemVar(i + 1, stageId) + ");\n";
            }
        }
        code += "\n";
    }

    code += "  auto start = std::chrono::high_resolution_clock::now();\n";
    code += "  executor.run(taskflow).get();\n";
    code += "  auto end = std::chrono::high_resolution_clock::now();\n";
    code += "  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n";
    code += "  std::cout << \">>> Lote de " + std::to_string(batchItems.size()) + " itens finalizado em \" << elapsed_ms << \" ms.\\n\";\n\n";
    code += "  blackboard.saveToDisk(projectId);\n";
    code += "  semanticMemory.saveToDisk(projectId);\n";
    code += "  blackboard.clearSession(projectId);\n";
    code += "  semanticMemory.clearSession(projectId);\n";
    code += "  } catch (const std::exception& ex) {\n";
    code += "    std::cerr << \">>> [ERRO] Lote interrompido: \" << ex.what() << std::endl;\n";
    code += "    return 1;\n";
    code += "  }\n";
    code += "  return 0;\n";
    code += "}\n";
    return code;
}

// ---------------------------------------------------------------------------
// Exportacao (CMake / Makefile / scripts MSVC)
// ---------------------------------------------------------------------------
std::string TaskflowGenerator::generateCMakeLists(const core::Graph& graph) {
    const auto& meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;
    std::string proj = sanitizeIdentifier(meta.graphName.empty() ? "pipeline" : meta.graphName);

    return "cmake_minimum_required(VERSION 3.20)\n"
           "project(" + proj + " CXX)\n\n"
           "set(CMAKE_CXX_STANDARD 20)\n"
           "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
           "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
           "# Raiz de uma instalacao do Arestrix (contem third_party/, CppCore/src e runtime/lib).\n"
           "set(ARESTRIX_HOME \"$ENV{ARESTRIX_HOME}\" CACHE PATH \"Raiz do Arestrix\")\n"
           "if(NOT ARESTRIX_HOME)\n"
           "  message(FATAL_ERROR \"Defina ARESTRIX_HOME (cmake -DARESTRIX_HOME=...)\")\n"
           "endif()\n\n"
           "if(MSVC)\n"
           "  add_compile_options(/EHsc /W4 /O2 /permissive- /utf-8)\n"
           "else()\n"
           "  add_compile_options(-Wall -Wextra -O3 -pthread)\n"
           "endif()\n\n"
           "find_library(ARESTRIX_CORE arestrix_core PATHS \"${ARESTRIX_HOME}/runtime/lib\" NO_DEFAULT_PATH REQUIRED)\n"
           "add_executable(" + binName + " main.cpp)\n"
           "target_include_directories(" + binName + " PRIVATE\n"
           "  \"${ARESTRIX_HOME}/third_party/taskflow\"\n"
           "  \"${ARESTRIX_HOME}/CppCore/src\"\n"
           "  \"${ARESTRIX_HOME}/third_party/simdjson/singleheader\")\n"
           "find_package(Threads REQUIRED)\n"
           "target_link_libraries(" + binName + " PRIVATE ${ARESTRIX_CORE} Threads::Threads)\n"
           "if(WIN32)\n"
           "  target_link_libraries(" + binName + " PRIVATE winhttp advapi32 ole32 shell32 user32)\n"
           "endif()\n";
}

std::string TaskflowGenerator::generateMakefile(const core::Graph& graph) {
    const auto& meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;

    return "# Makefile para " + commentSafe(meta.graphName) + " (C++20)\n"
           "# Use: make ARESTRIX_HOME=/caminho/do/arestrix\n"
           "ARESTRIX_HOME ?= $(shell echo $$ARESTRIX_HOME)\n"
           "CXX = g++\n"
           "CXXFLAGS = -std=c++20 -O3 -Wall -pthread \\\n"
           "  -I$(ARESTRIX_HOME)/third_party/taskflow -I$(ARESTRIX_HOME)/CppCore/src \\\n"
           "  -I$(ARESTRIX_HOME)/third_party/simdjson/singleheader\n"
           "TARGET = " + binName + "\n\n"
           "all: $(TARGET)\n\n"
           "$(TARGET): main.cpp\n"
           "\t$(CXX) $(CXXFLAGS) main.cpp $(ARESTRIX_HOME)/runtime/lib/libarestrix_core.a -o $(TARGET)\n\n"
           "clean:\n"
           "\trm -f $(TARGET) $(TARGET).exe\n";
}

TaskflowGenerator::MsvcScripts TaskflowGenerator::generateMsvcBuildScript(const core::Graph& graph) {
    const auto& meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;

    const std::string flags =
        "/nologo /std:c++20 /EHsc /O2 /MD /utf-8 /permissive- /Zc:__cplusplus "
        "/I\"%ARESTRIX_HOME%\\third_party\\taskflow\" /I\"%ARESTRIX_HOME%\\CppCore\\src\" "
        "/I\"%ARESTRIX_HOME%\\third_party\\simdjson\\singleheader\" "
        "main.cpp /Fe:\"" + binName + ".exe\" "
        "/link \"%ARESTRIX_HOME%\\runtime\\lib\\arestrix_core.lib\" winhttp.lib ws2_32.lib advapi32.lib ole32.lib shell32.lib user32.lib";

    MsvcScripts scripts;
    scripts.bat = "@echo off\r\n"
                  "if \"%ARESTRIX_HOME%\"==\"\" (echo Defina ARESTRIX_HOME & exit /b 1)\r\n"
                  "echo >>> Compilando " + commentSafe(meta.graphName) + " com MSVC (C++20)...\r\n"
                  "cl.exe " + flags + "\r\n"
                  "if %errorlevel% equ 0 (echo >>> Compilacao concluida) else (echo >>> Erro na compilacao & exit /b 1)\r\n";
    scripts.ps1 = "# Execute em um 'Developer PowerShell for VS' (cl.exe no PATH).\n"
                  "if (-not $env:ARESTRIX_HOME) { Write-Error 'Defina ARESTRIX_HOME'; exit 1 }\n"
                  "Write-Host '>>> Compilando " + commentSafe(meta.graphName) + " com MSVC (C++20)...' -ForegroundColor Cyan\n"
                  "cmd /c \"build.bat\"\n"
                  "exit $LASTEXITCODE\n";
    return scripts;
}

} // namespace compiler
} // namespace taskforge
