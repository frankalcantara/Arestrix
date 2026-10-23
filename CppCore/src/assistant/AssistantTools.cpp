#include "assistant/AssistantTools.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#include "util/Json.hpp"

namespace taskforge {
namespace assistant {

namespace {

namespace fs = std::filesystem;
using rt::langPick;
using rt::RuntimeLang;

constexpr size_t kMaxDocChars = 24000;

Json prop(const char* type, const std::string& description) {
    return Json{{"type", type}, {"description", description}};
}

Json object(Json properties, std::vector<std::string> required = {}) {
    Json s = Json::object();
    s["type"] = "object";
    s["properties"] = std::move(properties);
    Json req = Json::array();
    for (const auto& r : required) req.push_back(r);
    s["required"] = req;
    return s;
}

AssistantToolDef tool(const char* name, const std::string& description, Json schema, ToolRisk risk) {
    AssistantToolDef t;
    t.def.name = name;
    t.def.description = description;
    t.def.parameters = std::move(schema);
    t.risk = risk;
    return t;
}

std::string argStr(const Json& args, const char* key) {
    return args.is_object() && args.contains(key) && args.at(key).is_string() ? args.at(key).get<std::string>() : std::string();
}

std::optional<double> argNum(const Json& args, const char* key) {
    if (!args.is_object() || !args.contains(key) || !args.at(key).is_number()) return std::nullopt;
    return args.at(key).get<double>();
}

ToolOutcome fail(const std::string& message, const std::string& toolName) {
    ToolOutcome o;
    o.result.content = message;
    o.result.isError = true;
    o.activity = Json{{"tool", toolName}, {"params", Json::object()}, {"status", "error"}, {"detail", message}};
    return o;
}

ToolOutcome okOutcome(const Json& result, const std::string& toolName, Json params) {
    ToolOutcome o;
    o.result.content = result.dump();
    o.activity = Json{{"tool", toolName}, {"params", std::move(params)}, {"status", "ok"}};
    return o;
}

std::string labelOf(const GraphDocument& doc, const std::string& id) {
    auto n = doc.node(id);
    return n ? n->value("label", id) : id;
}

// Documentos .md da pasta, em ordem; o titulo e a primeira linha "# ".
std::vector<std::pair<std::string, std::string>> listDocs(const fs::path& dir) {
    std::vector<std::pair<std::string, std::string>> docs;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return docs;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".md") continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::string line, title;
        while (std::getline(in, line)) {
            if (line.rfind("# ", 0) == 0) { title = line.substr(2); break; }
        }
        while (!title.empty() && (title.back() == '\r' || title.back() == ' ')) title.pop_back();
        auto stem = e.path().stem().u8string();
        docs.emplace_back(std::string(stem.begin(), stem.end()), title);
    }
    std::sort(docs.begin(), docs.end());
    return docs;
}

Json nodeTypesCatalog(RuntimeLang lang) {
    struct T { const char* type; const char* pt; const char* en; };
    static const T types[] = {
        {"agent", "Agente de IA: executa uma skill com um modelo e ferramentas (Code Mode, MCP). Entradas: in (fluxo), prompt_in (dado). Saidas: out (fluxo), artifact_out (dado).",
                  "AI agent: runs a skill with a model and tools (Code Mode, MCP). Inputs: in (flow), prompt_in (data). Outputs: out (flow), artifact_out (data)."},
        {"static", "Tarefa C++ deterministica (corpo lambda). Uma entrada e uma saida de fluxo.",
                   "Deterministic C++ task (lambda body). One flow input and one flow output."},
        {"condition", "Decisao binaria: o corpo devolve 0 ou 1 e escolhe a saida 'branch 0' ou 'branch 1'. Unico tipo, junto com switch, que permite ciclos (lacos de correcao).",
                      "Binary decision: the body returns 0 or 1 and picks output 'branch 0' or 'branch 1'. Together with switch, the only type that allows cycles (correction loops)."},
        {"switch", "Selecao multivia: um caso por saida, o ultimo e 'default'. Decide por codigo, por palavras ou por um modelo de IA.",
                   "Multi-way selection: one case per output, the last is 'default'. Decides by code, by keywords or by an AI model."},
        {"fork", "Dispara ramos em paralelo.", "Fans out parallel branches."},
        {"join", "Barreira: espera os ramos paralelos terminarem.", "Barrier: waits for parallel branches to finish."},
        {"subflow", "Subgrafo aninhado (duplo clique entra nele).", "Nested subgraph (double-click to enter it)."},
        {"module", "Modulo reutilizavel composto de outro grafo.", "Reusable module composed from another graph."},
        {"critical", "Secao critica: acesso exclusivo a recurso compartilhado.", "Critical section: exclusive access to a shared resource."},
        {"cuda", "Tarefa com kernel CUDA na GPU.", "Task with a CUDA kernel on the GPU."},
        {"mcp", "Chama UMA ferramenta de um servidor MCP do projeto, sem IA.", "Calls ONE tool of a project MCP server, without AI."},
        {"interaction", "Interação humana: formulário seguro, canal Studio/Runner/CLI, timeout e resposta de volta ao fluxo ou ao chamador.",
                        "Human interaction: safe form, Studio/Runner/CLI channel, timeout and answer back to the flow or caller."},
        {"output", "Vertice coletor de saida e entrega de artefatos: define arquivos gerados (SVG, planilha, PDF, Markdown, texto, JSON) para visualizacao e exportacao.",
                   "Output / Artifact deliverable collector: defines deliverable files (SVG, spreadsheet, PDF, Markdown, text, JSON) for preview and export."},
    };
    Json out = Json::array();
    for (const auto& t : types) out.push_back(Json{{"type", t.type}, {"description", langPick(lang, t.pt, t.en)}});
    return out;
}

} // namespace

const char* riskName(ToolRisk risk) {
    switch (risk) {
        case ToolRisk::Edit: return "edit";
        case ToolRisk::Confirm: return "confirm";
        default: return "read";
    }
}

std::vector<AssistantToolDef> assistantTools(RuntimeLang lang) {
    auto L = [lang](const char* pt, const char* en) { return langPick(lang, pt, en); };
    std::vector<AssistantToolDef> tools;

    // --- Aprender o Studio ---
    tools.push_back(tool("studio_docs_list",
        L("Lista os capitulos da documentacao do Arestrix Studio (id e titulo). Use para responder duvidas sobre como o Studio funciona.",
          "Lists the chapters of the Arestrix Studio documentation (id and title). Use it to answer questions about how the Studio works."),
        object(Json::object()), ToolRisk::Read));
    tools.push_back(tool("studio_docs_read",
        L("Le um capitulo da documentacao do Studio pelo id devolvido por studio_docs_list.",
          "Reads a Studio documentation chapter by the id returned by studio_docs_list."),
        object(Json{{"id", prop("string", L("Id do capitulo (ex.: interface).", "Chapter id (e.g. interface)."))}}, {"id"}),
        ToolRisk::Read));
    tools.push_back(tool("list_node_types",
        L("Lista os tipos de vertice disponiveis, com portas e finalidade.", "Lists the available vertex types, with ports and purpose."),
        object(Json::object()), ToolRisk::Read));

    // --- Ler o estado ---
    tools.push_back(tool("get_project",
        L("Projeto ativo (nome, pasta, pipelines) e projetos recentes.", "Active project (name, folder, pipelines) and recent projects."),
        object(Json::object()), ToolRisk::Read));
    tools.push_back(tool("get_graph",
        L("Resumo do grafo aberto no canvas: vertices (id, tipo, rotulo, posicao, portas) e arestas.",
          "Summary of the graph open on the canvas: vertices (id, type, label, position, ports) and edges."),
        object(Json::object()), ToolRisk::Read));
    tools.push_back(tool("get_node",
        L("Todos os detalhes de um vertice (corpo lambda, configuracao de agente, parametros).",
          "Full details of a vertex (lambda body, agent settings, parameters)."),
        object(Json{{"id", prop("string", L("Id do vertice.", "Vertex id."))}}, {"id"}), ToolRisk::Read));
    tools.push_back(tool("validate_graph",
        L("Valida o grafo: arestas orfas, ciclos sem vertice de decisao, saidas sem destino, vertices isolados.",
          "Validates the graph: dangling edges, cycles without a decision vertex, outputs leading nowhere, isolated vertices."),
        object(Json::object()), ToolRisk::Read));
    tools.push_back(tool("list_artifacts",
        L("Lista os artefatos e arquivos entregaveis gerados pelo pipeline (registrados em vertices de saida ou presentes em exports/ e workspace/).",
          "Lists the artifacts and deliverable files produced by the pipeline (registered in output vertices or present in exports/ and workspace/)."),
        object(Json::object()), ToolRisk::Read));
    tools.push_back(tool("get_artifact",
        L("Le o conteudo (se texto, SVG, Markdown ou JSON) ou metadados de um artefato gerado pelo pipeline.",
          "Reads the content (if text, SVG, Markdown, or JSON) or metadata of a pipeline artifact."),
        object(Json{{"path", prop("string", L("Caminho do artefato ou id do vertice output.", "Artifact path or output vertex id."))}}, {"path"}),
        ToolRisk::Read));

    // --- Editar o grafo (aparece no canvas e pode ser desfeito) ---
    tools.push_back(tool("add_node",
        L("Adiciona um vertice ao grafo. Sem x/y, ele vai a direita do ultimo vertice. Devolve o id e as portas.",
          "Adds a vertex to the graph. Without x/y it goes to the right of the last vertex. Returns the id and ports."),
        object(Json{{"type", prop("string", L("Tipo do vertice (veja list_node_types).", "Vertex type (see list_node_types)."))},
                    {"label", prop("string", L("Rotulo exibido no cartao.", "Label shown on the card."))},
                    {"x", prop("number", L("Posicao horizontal (opcional).", "Horizontal position (optional)."))},
                    {"y", prop("number", L("Posicao vertical (opcional).", "Vertical position (optional)."))},
                    {"lambda", prop("string", L("Corpo C++ do vertice (opcional).", "C++ body of the vertex (optional)."))}},
               {"type", "label"}),
        ToolRisk::Edit));
    tools.push_back(tool("update_node",
        L("Altera um vertice. Campos aceitos: label, customLambdaBody, criticalSectionId (texto); parameters, agentSettings, "
          "switchSettings, mcpSettings, outputSettings (objetos, mesclados no existente); requiredHeaders (lista).",
          "Changes a vertex. Accepted fields: label, customLambdaBody, criticalSectionId (text); parameters, agentSettings, "
          "switchSettings, mcpSettings, outputSettings (objects, merged into the existing one); requiredHeaders (list)."),
        object(Json{{"id", prop("string", L("Id do vertice.", "Vertex id."))},
                    {"patch", prop("object", L("Campos a alterar.", "Fields to change."))}},
               {"id", "patch"}),
        ToolRisk::Edit));
    tools.push_back(tool("remove_node",
        L("Remove um vertice e as arestas ligadas a ele.", "Removes a vertex and the edges attached to it."),
        object(Json{{"id", prop("string", L("Id do vertice.", "Vertex id."))}}, {"id"}), ToolRisk::Edit));
    tools.push_back(tool("connect",
        L("Liga a saida de um vertice a entrada de outro. Portas por id ou nome; sem porta, usa a primeira porta de fluxo.",
          "Connects an output of one vertex to an input of another. Ports by id or name; without a port, the first flow port is used."),
        object(Json{{"from", prop("string", L("Id do vertice de origem.", "Source vertex id."))},
                    {"to", prop("string", L("Id do vertice de destino.", "Target vertex id."))},
                    {"from_port", prop("string", L("Porta de saida (opcional).", "Output port (optional)."))},
                    {"to_port", prop("string", L("Porta de entrada (opcional).", "Input port (optional)."))}},
               {"from", "to"}),
        ToolRisk::Edit));
    tools.push_back(tool("disconnect",
        L("Remove uma aresta pelo id (veja get_graph).", "Removes an edge by id (see get_graph)."),
        object(Json{{"edge_id", prop("string", L("Id da aresta.", "Edge id."))}}, {"edge_id"}), ToolRisk::Edit));
    tools.push_back(tool("move_node",
        L("Move um vertice para a posicao (x, y).", "Moves a vertex to position (x, y)."),
        object(Json{{"id", prop("string", L("Id do vertice.", "Vertex id."))},
                    {"x", prop("number", "x")}, {"y", prop("number", "y")}},
               {"id", "x", "y"}),
        ToolRisk::Edit));

    // --- Acoes que pedem confirmacao do usuario ---
    tools.push_back(tool("create_project",
        L("Cria um projeto novo (pasta, manifesto e grafo principal vazio) e o abre no Studio. Pede confirmacao ao usuario.",
          "Creates a new project (folder, manifest and empty main graph) and opens it in the Studio. Asks the user to confirm."),
        object(Json{{"name", prop("string", L("Nome do projeto.", "Project name."))},
                    {"directory", prop("string", L("Pasta do projeto (opcional; padrao: projects/<nome>).",
                                                   "Project folder (optional; default: projects/<name>)."))},
                    {"description", prop("string", L("Descricao (opcional).", "Description (optional)."))}},
               {"name"}),
        ToolRisk::Confirm));
    tools.push_back(tool("open_project",
        L("Abre um projeto existente pela pasta (ou pelo arestrix.project.json). Pede confirmacao ao usuario.",
          "Opens an existing project by its folder (or its arestrix.project.json). Asks the user to confirm."),
        object(Json{{"path", prop("string", L("Pasta do projeto.", "Project folder."))}}, {"path"}), ToolRisk::Confirm));
    tools.push_back(tool("clear_graph",
        L("Comeca um grafo novo e vazio no projeto atual, removendo todos os vertices. Pede confirmacao ao usuario.",
          "Starts a new, empty graph in the current project by removing every vertex. Asks the user to confirm."),
        object(Json::object()), ToolRisk::Confirm));
    tools.push_back(tool("save_project",
        L("Grava o grafo do canvas no projeto ativo (pipelines/main.json). Pede confirmacao ao usuario.",
          "Saves the canvas graph into the active project (pipelines/main.json). Asks the user to confirm."),
        object(Json::object()), ToolRisk::Confirm));
    tools.push_back(tool("run_pipeline",
        L("Compila e executa o pipeline do grafo atual (pode gastar tokens e rodar codigo). Valida antes. Pede confirmacao ao usuario.",
          "Compiles and runs the current graph's pipeline (may spend tokens and run code). Validates first. Asks the user to confirm."),
        object(Json::object()), ToolRisk::Confirm));
    return tools;
}

std::optional<ToolRisk> toolRisk(const std::string& name) {
    static const std::map<std::string, ToolRisk> risks = [] {
        std::map<std::string, ToolRisk> m;
        for (const auto& t : assistantTools(RuntimeLang::Pt)) m[t.def.name] = t.risk;
        return m;
    }();
    auto it = risks.find(name);
    if (it == risks.end()) return std::nullopt;
    return it->second;
}

std::string validateArgs(const Json& schema, const Json& args, RuntimeLang lang) {
    if (!args.is_object()) return langPick(lang, "Os argumentos devem ser um objeto JSON.", "Arguments must be a JSON object.");
    if (args.contains("__invalid_arguments")) {
        return langPick(lang, "Argumentos em JSON invalido: ", "Arguments are not valid JSON: ") + args.at("__invalid_arguments").dump();
    }
    if (schema.contains("required")) {
        for (const auto& r : schema.at("required")) {
            std::string key = r.get<std::string>();
            if (!args.contains(key) || args.at(key).is_null()) {
                return langPick(lang, "Falta o argumento obrigatorio '", "Missing required argument '") + key + "'.";
            }
        }
    }
    if (schema.contains("properties")) {
        const Json& props = schema.at("properties");
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (!props.contains(it.key())) continue;
            std::string type = props.at(it.key()).value("type", std::string());
            const Json& v = it.value();
            bool ok = type.empty() || (type == "string" && v.is_string()) || (type == "number" && v.is_number()) ||
                      (type == "object" && v.is_object()) || (type == "array" && v.is_array()) ||
                      (type == "boolean" && v.is_boolean());
            if (!ok) {
                return langPick(lang, "O argumento '", "Argument '") + it.key() +
                       langPick(lang, "' deve ser do tipo ", "' must be of type ") + type + ".";
            }
        }
    }
    return "";
}

ToolOutcome executeAssistantTool(GraphDocument& doc, AssistantHost& host, RuntimeLang lang, const std::string& name,
                                 const Json& args) {
    const auto tools = assistantTools(lang);
    auto def = std::find_if(tools.begin(), tools.end(), [&](const AssistantToolDef& t) { return t.def.name == name; });
    if (def == tools.end()) {
        return fail(langPick(lang, "Ferramenta desconhecida: ", "Unknown tool: ") + name, name);
    }
    std::string invalid = validateArgs(def->def.parameters, args, lang);
    if (!invalid.empty()) return fail(invalid, name);

    auto fromEdit = [&](const EditResult& r, Json params) {
        if (!r.ok) return fail(r.error, name);
        ToolOutcome o = okOutcome(r.result, name, std::move(params));
        if (!r.uiOp.is_null()) o.uiOps.push_back(r.uiOp);
        return o;
    };

    try {
        if (name == "studio_docs_list") {
            Json list = Json::array();
            for (const auto& [id, title] : listDocs(host.docsDirectory(lang))) list.push_back(Json{{"id", id}, {"title", title}});
            return okOutcome(Json{{"chapters", list}}, name, Json::object());
        }
        if (name == "studio_docs_read") {
            std::string id = argStr(args, "id");
            if (id.size() > 3 && id.substr(id.size() - 3) == ".md") id.resize(id.size() - 3);
            fs::path dir = host.docsDirectory(lang);
            for (const auto& [docId, title] : listDocs(dir)) {
                if (docId != id) continue;   // so ids listados: nada de caminhos arbitrarios
                std::string text = jsonutil::readFile(dir / (docId + ".md"));
                bool cut = text.size() > kMaxDocChars;
                if (cut) text.resize(kMaxDocChars);
                return okOutcome(Json{{"id", docId}, {"title", title}, {"text", text}, {"truncated", cut}}, name,
                                 Json{{"title", title}});
            }
            return fail(langPick(lang, "Capitulo nao encontrado: '", "Chapter not found: '") + id +
                            langPick(lang, "'. Use studio_docs_list.", "'. Use studio_docs_list."), name);
        }
        if (name == "list_node_types") return okOutcome(Json{{"types", nodeTypesCatalog(lang)}}, name, Json::object());
        if (name == "get_project") {
            Json p = host.currentProject();
            Json slim = Json::object();
            for (const char* k : {"projectId", "name", "description", "rootPath", "pipelines"}) {
                if (p.is_object() && p.contains(k)) slim[k] = p.at(k);
            }
            return okOutcome(Json{{"project", slim}, {"recent", host.recentProjects()}}, name, Json::object());
        }
        if (name == "get_graph") return okOutcome(doc.summary(), name, Json::object());
        if (name == "get_node") {
            auto n = doc.node(argStr(args, "id"));
            if (!n) return fail(langPick(lang, "Vertice nao encontrado: '", "Vertex not found: '") + argStr(args, "id") + "'.", name);
            return okOutcome(*n, name, Json{{"label", n->value("label", std::string())}});
        }
        if (name == "validate_graph") {
            Json v = doc.validate(lang);
            return okOutcome(v, name, Json{{"errors", v.at("errors").size()}, {"warnings", v.at("warnings").size()}});
        }
        if (name == "list_artifacts") {
            Json list = Json::array();
            std::string root = host.currentProject().value("rootPath", std::string());
            fs::path prjRoot = root.empty() ? fs::current_path() : fs::path(root);

            // 1. Vértices output do grafo
            for (const auto& n : doc.schema().at("nodes")) {
                if (n.value("type", "") != "output") continue;
                std::string nid = n.value("id", "");
                std::string label = n.value("label", nid);
                Json s = n.contains("outputSettings") && n.at("outputSettings").is_object() ? n.at("outputSettings") : Json::object();
                std::string src = s.value("sourcePath", "");
                std::string dst = s.value("destinationPath", "");
                std::string atype = s.value("artifactType", "auto");
                std::string dname = s.value("displayName", label);
                if (dname.empty()) dname = label;

                std::string chosenPath = !dst.empty() ? dst : src;
                fs::path fullP = prjRoot / chosenPath;
                std::error_code ec;
                bool exists = fs::exists(fullP, ec);
                size_t sizeBytes = exists ? (size_t)fs::file_size(fullP, ec) : 0;

                list.push_back(Json{
                    {"id", nid},
                    {"nodeId", nid},
                    {"name", dname},
                    {"path", chosenPath},
                    {"sourcePath", src},
                    {"destinationPath", dst},
                    {"type", atype},
                    {"sizeBytes", sizeBytes},
                    {"exists", exists}
                });
            }

            // 2. Arquivos em exports/
            std::error_code ec;
            fs::path expDir = prjRoot / "exports";
            if (fs::is_directory(expDir, ec)) {
                for (const auto& entry : fs::directory_iterator(expDir, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    std::string rel = "exports/" + entry.path().filename().string();
                    bool already = false;
                    for (const auto& item : list) {
                        if (item.value("path", "") == rel) { already = true; break; }
                    }
                    if (!already) {
                        list.push_back(Json{
                            {"id", "export_" + entry.path().filename().string()},
                            {"name", entry.path().filename().string()},
                            {"path", rel},
                            {"type", "auto"},
                            {"sizeBytes", (size_t)entry.file_size(ec)},
                            {"exists", true}
                        });
                    }
                }
            }

            return okOutcome(Json{{"artifacts", list}}, name, Json{{"count", list.size()}});
        }
        if (name == "get_artifact") {
            std::string req = argStr(args, "path");
            std::string root = host.currentProject().value("rootPath", std::string());
            fs::path prjRoot = root.empty() ? fs::current_path() : fs::path(root);

            std::string filePath = req;
            auto n = doc.node(req);
            if (n && n->value("type", "") == "output" && n->contains("outputSettings")) {
                const auto& s = n->at("outputSettings");
                std::string dst = s.value("destinationPath", "");
                std::string src = s.value("sourcePath", "");
                filePath = !dst.empty() ? dst : src;
            }

            fs::path fullP = prjRoot / filePath;
            std::error_code ec;
            if (!fs::exists(fullP, ec) || !fs::is_regular_file(fullP, ec)) {
                return fail(langPick(lang, "Artefato nao encontrado no disco: '", "Artifact not found on disk: '") + filePath + "'.", name);
            }

            size_t size = fs::file_size(fullP, ec);
            std::string ext = fullP.extension().string();
            bool isText = (ext == ".svg" || ext == ".txt" || ext == ".md" || ext == ".json" ||
                           ext == ".csv" || ext == ".tsv" || ext == ".xml" || ext == ".html");

            std::string content;
            if (isText && size < 1024 * 1024) {
                content = jsonutil::readFile(fullP);
            }

            return okOutcome(Json{
                {"path", filePath},
                {"sizeBytes", size},
                {"isText", isText},
                {"content", content}
            }, name, Json{{"path", filePath}});
        }

        if (name == "add_node") {
            auto r = doc.addNode(argStr(args, "type"), argStr(args, "label"), argNum(args, "x"), argNum(args, "y"),
                                 argStr(args, "lambda"), lang);
            return fromEdit(r, Json{{"label", argStr(args, "label")}, {"type", argStr(args, "type")}});
        }
        if (name == "update_node") {
            std::string id = argStr(args, "id");
            std::string before = labelOf(doc, id);
            return fromEdit(doc.updateNode(id, args.at("patch"), lang), Json{{"label", before}});
        }
        if (name == "remove_node") {
            std::string id = argStr(args, "id");
            std::string label = labelOf(doc, id);
            return fromEdit(doc.removeNode(id, lang), Json{{"label", label}});
        }
        if (name == "connect") {
            std::string from = argStr(args, "from"), to = argStr(args, "to");
            auto r = doc.connect(from, argStr(args, "from_port"), to, argStr(args, "to_port"), lang);
            return fromEdit(r, Json{{"from", labelOf(doc, from)}, {"to", labelOf(doc, to)}});
        }
        if (name == "disconnect") {
            std::string edgeId = argStr(args, "edge_id");
            return fromEdit(doc.disconnect(edgeId, lang), Json{{"edge", edgeId}});
        }
        if (name == "move_node") {
            std::string id = argStr(args, "id");
            return fromEdit(doc.moveNode(id, *argNum(args, "x"), *argNum(args, "y"), lang), Json{{"label", labelOf(doc, id)}});
        }

        if (name == "create_project" || name == "open_project") {
            std::string error;
            std::optional<Json> mainGraph = name == "create_project"
                ? host.createProject(argStr(args, "name"), argStr(args, "directory"), argStr(args, "description"), error)
                : host.openProject(argStr(args, "path"), error);
            if (!mainGraph) {
                return fail(langPick(lang, "Falha: ", "Failed: ") + (error.empty() ? name : error), name);
            }
            doc.reset(*mainGraph);
            Json project = host.currentProject();
            std::string projectName = project.value("name", std::string());
            std::string root = project.value("rootPath", std::string());
            ToolOutcome o = okOutcome(Json{{"name", projectName}, {"rootPath", root}, {"graph", doc.summary()}}, name,
                                      Json{{"name", projectName}, {"path", root}});
            o.uiOps.push_back(Json{{"op", "load_project"}, {"rootPath", root}});
            return o;
        }
        if (name == "clear_graph") {
            ToolOutcome o;
            Json removed = Json::array();
            for (const auto& n : doc.summary().at("nodes")) {
                auto r = doc.removeNode(n.value("id", std::string()), lang);
                if (r.ok) {
                    o.uiOps.push_back(r.uiOp);
                    removed.push_back(n.value("id", std::string()));
                }
            }
            o.result.content = Json{{"removedNodes", removed}}.dump();
            o.activity = Json{{"tool", name}, {"params", Json{{"count", removed.size()}}}, {"status", "ok"}};
            return o;
        }
        if (name == "save_project") {
            std::string error;
            if (!host.savePipeline(doc.schema(), error)) return fail(langPick(lang, "Falha ao salvar: ", "Save failed: ") + error, name);
            Json project = host.currentProject();
            return okOutcome(Json{{"saved", true}, {"rootPath", project.value("rootPath", std::string())}}, name,
                             Json{{"name", project.value("name", std::string())}});
        }
        if (name == "run_pipeline") {
            Json v = doc.validate(lang);
            if (!v.value("ok", false)) {
                return fail(langPick(lang, "O grafo tem erros e nao foi executado: ", "The graph has errors and was not run: ") +
                                v.at("errors").dump(), name);
            }
            std::string error;
            if (!host.runPipeline(doc.schema(), error)) return fail(langPick(lang, "Falha ao executar: ", "Run failed: ") + error, name);
            return okOutcome(Json{{"started", true},
                                  {"note", langPick(lang, "A execucao segue em segundo plano; o progresso aparece no console do Studio.",
                                                    "The run continues in the background; progress shows in the Studio console.")}},
                             name, Json::object());
        }
    } catch (const std::exception& ex) {
        return fail(langPick(lang, "Erro interno: ", "Internal error: ") + std::string(ex.what()), name);
    }
    return fail(langPick(lang, "Ferramenta sem implementacao: ", "Tool not implemented: ") + name, name);
}

} // namespace assistant
} // namespace taskforge
