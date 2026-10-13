// Testes do nucleo C++ do Arestrix (sem framework externo).
// Executar: ctest --test-dir build  (ou ./build/arestrix_tests)

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "compiler/TaskflowGenerator.hpp"
#include "mcp/McpClient.hpp"
#include "core/Graph.hpp"
#include "platform/Process.hpp"
#include "rt/Tools.hpp"
#include "search/ProjectSearch.hpp"
#include "search/SynonymAgent.hpp"
#include "search/Synonyms.hpp"
#include "sandbox/DockerSandboxDriver.hpp"
#include "sandbox/SandboxManager.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;
using json = taskforge::Json;

static int g_failed = 0;
static int g_total = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_total;                                                                    \
        if (!(cond)) {                                                                \
            ++g_failed;                                                               \
            std::cerr << "  FALHOU: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                             \
    } while (0)

#define TEST(name) static void name(); static struct Reg_##name { Reg_##name() { tests().push_back({#name, &name}); } } reg_##name; static void name()

struct TestCase { const char* name; void (*fn)(); };
static std::vector<TestCase>& tests() { static std::vector<TestCase> t; return t; }

static fs::path makeTempDir(const std::string& tag) {
    fs::path p = fs::temp_directory_path() / ("arestrix-test-" + tag + "-" + std::to_string(std::rand()));
    fs::create_directories(p);
    return p;
}

// ---------------------------------------------------------------------------
// JSON (simdjson)
// ---------------------------------------------------------------------------
TEST(json_parses_all_types) {
    auto j = taskforge::jsonutil::parse(R"({"a":1,"b":-2,"c":3.5,"d":"xé\n","e":[true,false,null],"f":{"g":18446744073709551615}})");
    CHECK(j["a"] == 1);
    CHECK(j["b"] == -2);
    CHECK(j["c"].get<double>() == 3.5);
    CHECK(j["d"].get<std::string>() == "x\xC3\xA9\n");
    CHECK(j["e"].size() == 3 && j["e"][0] == true && j["e"][2].is_null());
    CHECK(j["f"]["g"].get<uint64_t>() == 18446744073709551615ull);
}

TEST(json_rejects_invalid) {
    CHECK(!taskforge::jsonutil::tryParse("{\"a\":"));
    CHECK(!taskforge::jsonutil::tryParse(""));
    bool threw = false;
    try { taskforge::jsonutil::parse("{oops}"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST(json_graph_roundtrip) {
    taskforge::core::Graph g;
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "G"}, {"description", "d"}, {"targetStandard", "C++20"},
                      {"compilerConfig", {{"compiler", "msvc"}, {"cppStandard", "C++20"}, {"buildSystem", "cmake"},
                                          {"taskflowIncludePath", "x"}, {"outputBinaryName", "o"},
                                          {"enableOpenMP", false}, {"enableCuda", false}, {"extraFlags", json::array()}}},
                      {"version", "1"}, {"projectId", "p"}}},
        {"nodes", json::array({{{"id", "n1"}, {"label", "A"}, {"type", "static"}, {"position", {{"x", 1}, {"y", 2}}},
                                {"inputs", json::array()}, {"outputs", json::array()}, {"parameters", json::object()}}})},
        {"edges", json::array()}};
    g.fromJson(taskforge::jsonutil::parse(schema.dump()));
    CHECK(g.getNodes().size() == 1);
    CHECK(g.getNodes().at("n1").label == "A");
}

TEST(json_value_construction_and_dump) {
    using taskforge::Json;
    Json o = {{"b", 2}, {"a", "x"}, {"list", Json::array({1, 2, 3})}, {"nested", {{"k", true}}}, {"n", nullptr}};
    CHECK(o.is_object() && o.size() == 5);
    CHECK(o.dump() == R"({"a":"x","b":2,"list":[1,2,3],"n":null,"nested":{"k":true}})");   // chaves ordenadas
    CHECK(Json::parse(o.dump()) == o);                                                       // round-trip
    CHECK(Json::array({1, "a"}).is_array());
    CHECK(Json::array().dump() == "[]" && Json::object().dump() == "{}");
    CHECK(o.dump(2).find("\n  \"a\": \"x\"") != std::string::npos);

    Json d = 3.0;
    CHECK(d.dump() == "3.0");                         // preserva o tipo decimal
    CHECK(Json::parse(d.dump()).type() == Json::Type::Double);
    CHECK(Json(18446744073709551615ull).dump() == "18446744073709551615");
    CHECK(Json(-5).dump() == "-5");
}

TEST(json_access_and_conversion) {
    using taskforge::Json;
    Json j = Json::parse(R"({"s":"txt","i":7,"f":2.5,"b":true,"arr":["a","b"],"m":{"x":"1","y":"2"},"nul":null})");
    CHECK(j.value("s", "") == "txt");
    CHECK(j.value("ausente", "padrao") == "padrao");
    CHECK(j.value("nul", 9) == 9);                    // null -> padrao
    CHECK(j.value("i", 0) == 7 && j.value("f", 0.0) == 2.5 && j.value("b", false));
    CHECK(j["arr"][1] == "b" && j["arr"][0].get<std::string>() == "a");
    CHECK(j["arr"].get<std::vector<std::string>>().size() == 2);
    CHECK((j["m"].get<std::map<std::string, std::string>>().at("y") == "2"));
    CHECK(!j.contains("zzz") && j.contains("s"));
    CHECK(j["ausente"].is_null());                     // const: nao cria chave
    bool threw = false;
    try { j.at("zzz"); } catch (const taskforge::JsonError&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { j["s"].get<int>(); } catch (const taskforge::JsonError&) { threw = true; }
    CHECK(threw);

    int count = 0;
    for (const auto& e : j["arr"]) { (void)e; ++count; }
    for (auto it = j["m"].begin(); it != j["m"].end(); ++it) { CHECK(!it.key().empty()); ++count; }
    CHECK(count == 4);

    Json w;
    w["novo"]["fundo"] = 1;                            // null vira objeto
    w["lista"].push_back("x");
    CHECK(w.dump() == R"({"lista":["x"],"novo":{"fundo":1}})");
    CHECK(w.erase("lista") == 1 && !w.contains("lista"));
}

TEST(json_string_escaping_and_invalid_utf8) {
    using taskforge::Json;
    Json s = std::string("aspas\" barra\\ nl\n tab\t ctl\x01 acentu\xC3\xA7\xC3\xA3o");
    std::string text = s.dump();
    CHECK(Json::parse(text) == s);                    // volta identico
    CHECK(text.find("\\u0001") != std::string::npos);
    // bytes invalidos (ex.: saida binaria de um comando) viram U+FFFD em vez de gerar JSON invalido
    Json bad = std::string("ok\xFF\xFE fim \xC3");
    CHECK(taskforge::jsonutil::tryParse(bad.dump()).has_value());
    CHECK(bad.dump().find("\\ufffd") != std::string::npos);
}

TEST(json_enum_and_struct_conversion) {
    using taskforge::Json;
    taskforge::core::Port p;
    p.id = "i"; p.name = "n"; p.direction = taskforge::core::PortDirection::Output; p.type = taskforge::core::PortType::Data;
    Json j = p;                                        // to_json por ADL
    CHECK(j["direction"] == "output" && j["type"] == "data");
    auto back = j.get<taskforge::core::Port>();
    CHECK(back.direction == taskforge::core::PortDirection::Output && back.conditionBranchIndex == -1);
    std::vector<taskforge::core::Port> ports{p, p};
    CHECK(Json(ports).size() == 2);
    CHECK(Json(ports).get<std::vector<taskforge::core::Port>>().size() == 2);
}

// ---------------------------------------------------------------------------
// Confinamento de caminhos
// ---------------------------------------------------------------------------
TEST(workspace_confinement) {
    fs::path root = makeTempDir("ws");
    using taskforge::rt::resolveInWorkspace;
    CHECK(resolveInWorkspace(root, "workspace/scene.svg").has_value());
    CHECK(resolveInWorkspace(root, "a/b/../c.txt").has_value());
    CHECK(!resolveInWorkspace(root, "../fora.txt"));
    CHECK(!resolveInWorkspace(root, "a/../../fora.txt"));
    CHECK(!resolveInWorkspace(root, ""));
    CHECK(!resolveInWorkspace(root, (root.parent_path() / "outro.txt").string()));
    CHECK(resolveInWorkspace(root, (root / "dentro.txt").string()).has_value());
    CHECK(!resolveInWorkspace(root, "arquivo.txt:stream"));
    fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// Ferramentas dos agentes
// ---------------------------------------------------------------------------
TEST(tools_read_write_edit) {
    fs::path root = makeTempDir("tools");
    taskforge::rt::ToolContext ctx;
    ctx.workspaceRoot = root;
    ctx.allowed = {"read", "write", "edit"};

    auto w = taskforge::rt::executeTool(ctx, "write", {{"path", "workspace/a.txt"}, {"content", "ola mundo"}});
    CHECK(!w.isError);
    auto r = taskforge::rt::executeTool(ctx, "read", {{"path", "workspace/a.txt"}});
    CHECK(!r.isError && r.content == "ola mundo");
    auto e = taskforge::rt::executeTool(ctx, "edit", {{"path", "workspace/a.txt"}, {"old_text", "mundo"}, {"new_text", "Arestrix"}});
    CHECK(!e.isError);
    CHECK(taskforge::rt::executeTool(ctx, "read", {{"path", "workspace/a.txt"}}).content == "ola Arestrix");

    CHECK(taskforge::rt::executeTool(ctx, "write", {{"path", "../escape.txt"}, {"content", "x"}}).isError);
    CHECK(taskforge::rt::executeTool(ctx, "bash", {{"command", "echo oi"}}).isError);   // nao autorizada
    fs::remove_all(root);
}

TEST(tool_bash_host) {
    fs::path root = makeTempDir("bash");
    taskforge::rt::ToolContext ctx;
    ctx.workspaceRoot = root;
    ctx.allowed = {"bash"};
    ctx.sandbox.isolation = taskforge::sandbox::IsolationType::HostDirect;
    ctx.sandbox.timeoutSeconds = 20;
    auto r = taskforge::rt::executeTool(ctx, "bash", {{"command", "echo arestrix-ok"}});
    CHECK(!r.isError);
    CHECK(r.content.find("arestrix-ok") != std::string::npos);
    fs::remove_all(root);
}

TEST(sandbox_fails_closed_without_fallback) {
    taskforge::sandbox::ExecutionParams p;
    p.command = "echo oi";
    p.useShell = true;
    p.policy.isolation = taskforge::sandbox::IsolationType::MicroVM;   // WSL2 ausente em CI/Linux
    p.policy.allowFallbackToHost = false;
    auto& mgr = taskforge::sandbox::SandboxManager::instance();
    if (!mgr.isAvailable(taskforge::sandbox::IsolationType::MicroVM)) {
        auto res = mgr.execute(p);
        CHECK(!res.isSuccess());
        CHECK(!res.errorMessage.empty());
    }
}

TEST(docker_args_are_hardened) {
    taskforge::sandbox::DockerSandboxDriver d("img:test");
    taskforge::sandbox::ExecutionParams p;
    p.command = "ls";
    p.useShell = true;
    p.workingDir = fs::temp_directory_path().string();
    p.policy.network = taskforge::sandbox::NetworkAccess::EgressLlmOnly;
    p.policy.readWriteMounts = {"../fora"};      // tentativa de escapar do workspace
    auto args = d.buildDockerArgs(p);
    auto has = [&](const std::string& s) { for (auto& a : args) if (a == s) return true; return false; };
    CHECK(has("--cap-drop=ALL"));
    CHECK(has("--network=none"));
    CHECK(has("--read-only"));
    for (auto& a : args) CHECK(a.find("fora:/workspace") == std::string::npos);
}

TEST(process_streams_and_times_out) {
    taskforge::platform::ProcessSpec spec;
#ifdef _WIN32
    spec.shellCommand = "echo linha1 && echo linha2";
#else
    spec.shellCommand = "echo linha1; echo linha2";
#endif
    std::vector<std::string> lines;
    auto r = taskforge::platform::run(spec, 20000, [&](const std::string& l) { lines.push_back(l); });
    CHECK(r.ok());
    CHECK(lines.size() >= 2 && lines[0].find("linha1") != std::string::npos);

    taskforge::platform::ProcessSpec slow;
#ifdef _WIN32
    slow.shellCommand = "ping -n 30 127.0.0.1 >nul";
#else
    slow.shellCommand = "sleep 30";
#endif
    auto t = taskforge::platform::run(slow, 600);
    CHECK(t.timedOut);
    CHECK(t.seconds < 10.0);
}

TEST(process_does_not_deadlock_on_large_output) {
    taskforge::platform::ProcessSpec spec;
#ifdef _WIN32
    spec.shellCommand = "for /L %i in (1,1,3000) do @echo 0123456789012345678901234567890123456789012345678901234567890123456789";
#else
    spec.shellCommand = "i=0; while [ $i -lt 3000 ]; do echo 0123456789012345678901234567890123456789012345678901234567890123456789; i=$((i+1)); done";
#endif
    auto r = taskforge::platform::run(spec, 60000);
    CHECK(r.ok());
    CHECK(r.output.size() > 200000);
}

TEST(host_driver_scrubs_secrets) {
    taskforge::platform::setUserEnv("ARESTRIX_TEST_API_KEY", "segredo-123");
    taskforge::sandbox::HostProcessDriver h;
    taskforge::sandbox::ExecutionParams p;
    p.useShell = true;
#ifdef _WIN32
    p.command = "echo [%ARESTRIX_TEST_API_KEY%]";
#else
    p.command = "echo [$ARESTRIX_TEST_API_KEY]";
#endif
    p.policy.timeoutSeconds = 20;
    auto res = h.execute(p);
    CHECK(res.stdoutOutput.find("segredo-123") == std::string::npos);
#ifdef _WIN32
    // limpa o valor persistido no Registro (HKCU\Environment)
    taskforge::platform::run(taskforge::platform::ProcessSpec{{"reg", "delete", "HKCU\\Environment", "/v", "ARESTRIX_TEST_API_KEY", "/f"}}, 10000);
#endif
}

// ---------------------------------------------------------------------------
// SVG
// ---------------------------------------------------------------------------
TEST(svg_validator) {
    using taskforge::rt::validateSvg;
    CHECK(validateSvg(R"(<svg viewBox="0 0 10 10" xmlns="http://www.w3.org/2000/svg"><rect id="a" width="5" height="5"><animate id="x" begin="0s" dur="1s" fill="freeze"/></rect><text>ok</text></svg>)").empty());
    CHECK(!validateSvg("<svg viewBox=\"0 0 1 1\"><g></svg>").empty());                       // tag desbalanceada
    CHECK(!validateSvg("<html></html>").empty());                                           // raiz errada
    CHECK(!validateSvg("<svg><rect/></svg>").empty());                                      // sem viewBox
    CHECK(!validateSvg("<svg viewBox=\"0 0 1 1\"><rect id=\"a\"/><rect id=\"a\"/></svg>").empty());   // id duplicado
    auto orphan = validateSvg("<svg viewBox=\"0 0 1 1\"><rect id=\"a\"><animate begin=\"nada.end + 1s\"/></rect></svg>");
    CHECK(!orphan.empty() && orphan[0].find("nada") != std::string::npos);
    auto shadow = validateSvg("<svg viewBox=\"0 0 1 1\"><defs><filter id=\"f\"><feDropShadow/></filter></defs><text filter=\"url(#f)\">x</text></svg>");
    bool typo = false;
    for (auto& s : shadow) if (s.find("TIPOGRAFIA") != std::string::npos) typo = true;
    CHECK(typo);
}

// ---------------------------------------------------------------------------
// Gerador de codigo
// ---------------------------------------------------------------------------
static taskforge::core::Graph tutorialGraph() {
    json defaults = {
        {"sandbox", {{"isolationLevel", "host_direct"}, {"network", "egress_llm_only"}, {"maxMemoryMB", 512}, {"timeoutSeconds", 120},
                     {"readOnlyMounts", json::array()}, {"readWriteMounts", json::array()}}},
        {"model", {{"provider", "deepseek"}, {"model", "deepseek-flash"}, {"skillPath", ""}, {"promptTemplate", ""}}},
        {"codeMode", {{"allowedTools", {"write", "read", "validate_svg"}}, {"customScripts", json::array()}}}};

    auto port = [](const char* id, const char* dir, const char* type, int branch = -1) {
        return json{{"id", id}, {"name", id}, {"direction", dir}, {"type", type}, {"dataType", ""}, {"conditionBranchIndex", branch}};
    };
    auto node = [&](const char* id, const char* label, const char* type, std::vector<json> outs, const char* body = "") {
        return json{{"id", id}, {"label", label}, {"type", type}, {"position", {{"x", 0}, {"y", 0}}},
                    {"inputs", json::array({port("in", "input", "flow")})}, {"outputs", json(outs)}, {"parameters", json::object()},
                    {"customLambdaBody", body}};
    };
    json gen = node("gen", "Agente_Gerador_DeepSeek", "agent", {port("out", "output", "flow")});
    gen["agentSettings"] = {{"inheritModel", false}, {"model", {{"skillPath", "skills/gerador.md"}}}, {"inheritSandbox", true}, {"inheritCodeMode", true}};
    json jev = node("jev", "Agente_Auditor_JEV", "agent", {port("out", "output", "flow")});
    jev["agentSettings"] = {{"inheritModel", false}, {"model", {{"provider", "jev"}, {"model", "jev-visual-evaluator"}, {"skillPath", "skills/jev.md"}}},
                            {"inheritSandbox", true}, {"inheritCodeMode", true}};
    json cond = node("cond", "Decisor_Qualidade", "condition", {port("b0", "output", "flow", 0), port("b1", "output", "flow", 1)},
                     "std::ifstream f(\"workspace/audit_decision.txt\"); std::string s; if (f.is_open()) std::getline(f, s);\n"
                     "return s.find(\"STATUS: APROVADO\") != std::string::npos ? 0 : 1;");

    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "SVG \"Agent\" Pipeline"}, {"description", "linha1\nlinha2"}, {"targetStandard", "C++20"},
                      {"compilerConfig", {{"compiler", "msvc"}, {"cppStandard", "C++20"}, {"buildSystem", "cmake"},
                                          {"taskflowIncludePath", "third_party/taskflow"}, {"outputBinaryName", "o"},
                                          {"enableOpenMP", false}, {"enableCuda", false}, {"extraFlags", json::array()}}},
                      {"version", "1"}, {"projectId", "tut"}, {"globalDefaults", defaults}}},
        {"nodes", json::array({gen,
                   node("a", "Linter", "static", {port("out", "output", "flow")}, "std::cout << \"A\\n\";"),
                   node("b", "Auditor", "static", {port("out", "output", "flow")}, "std::cout << \"B\\n\";"),
                   node("join", "Consolidador", "static", {port("out", "output", "flow")}),
                   jev, cond,
                   node("done", "Entrega", "static", {port("out", "output", "flow")})})},
        {"edges", json::array({
                   json{{"id", "e1"}, {"sourceNodeId", "gen"}, {"sourcePortId", "out"}, {"targetNodeId", "a"}, {"targetPortId", "in"}},
                   json{{"id", "e2"}, {"sourceNodeId", "gen"}, {"sourcePortId", "out"}, {"targetNodeId", "b"}, {"targetPortId", "in"}},
                   json{{"id", "e3"}, {"sourceNodeId", "a"}, {"sourcePortId", "out"}, {"targetNodeId", "join"}, {"targetPortId", "in"}},
                   json{{"id", "e4"}, {"sourceNodeId", "b"}, {"sourcePortId", "out"}, {"targetNodeId", "join"}, {"targetPortId", "in"}},
                   json{{"id", "e5"}, {"sourceNodeId", "join"}, {"sourcePortId", "out"}, {"targetNodeId", "jev"}, {"targetPortId", "in"}},
                   json{{"id", "e6"}, {"sourceNodeId", "jev"}, {"sourcePortId", "out"}, {"targetNodeId", "cond"}, {"targetPortId", "in"}},
                   json{{"id", "e7"}, {"sourceNodeId", "cond"}, {"sourcePortId", "b0"}, {"targetNodeId", "done"}, {"targetPortId", "in"}},
                   // SEM isFeedbackLoop: o gerador deve detectar o ciclo sozinho
                   json{{"id", "e8"}, {"sourceNodeId", "cond"}, {"sourcePortId", "b1"}, {"targetNodeId", "gen"}, {"targetPortId", "in"}}
                   })}};
    taskforge::core::Graph g;
    g.fromJson(schema);
    return g;
}

TEST(generator_tutorial_topology) {
    auto g = tutorialGraph();
    std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);

    CHECK(src.find("#include \"rt/Runtime.hpp\"") != std::string::npos);
    CHECK(src.find("#include <fstream>") != std::string::npos);                  // o tutorial usa std::ifstream
    CHECK(src.find("taskforge::rt::runAgent(\"gen\"") != std::string::npos);
    CHECK(src.find("taskforge::rt::runAgent(\"jev\"") != std::string::npos);
    CHECK(src.find("\"provider\":\"jev\"") != std::string::npos);                // modelo proprio do no auditor
    CHECK(src.find("\"provider\":\"deepseek\"") != std::string::npos);           // gerador herda o padrao global
    CHECK(src.find("\"skillPath\":\"skills/gerador.md\"") != std::string::npos);
    CHECK(src.find("\"isolationLevel\":\"host_direct\"") != std::string::npos);

    // fork / join
    CHECK(src.find("task_gen.precede(task_a)") != std::string::npos);
    CHECK(src.find("task_gen.precede(task_b)") != std::string::npos);
    CHECK(src.find("task_a.precede(task_join)") != std::string::npos);
    CHECK(src.find("task_b.precede(task_join)") != std::string::npos);
    // condicao: ramo 0 -> entrega, ramo 1 -> gerador (laco de retorno)
    CHECK(src.find("task_cond.precede(task_done, task_gen)") != std::string::npos);
    // o gerador e ponto de entrada (o laco nao conta como dependencia forte)
    CHECK(src.find("task_bootstrap.precede(task_gen)") != std::string::npos);
    CHECK(src.find("executor.run(taskflow).get()") != std::string::npos);
    // escapes: nome com aspas e quebra de linha na descricao nao quebram o codigo
    CHECK(src.find("SVG \\\"Agent\\\" Pipeline") != std::string::npos);
    CHECK(src.find("// Descricao: linha1 linha2") != std::string::npos);
}

TEST(generator_condition_with_gap_uses_noop) {
    auto g = tutorialGraph();
    // remove o ramo 0 (entrega): o ramo 1 deve continuar sendo o indice 1
    g.removeEdge("e7");
    std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);
    auto pos = src.find("task_cond.precede(branch_noop_0, task_gen)");
    CHECK(pos != std::string::npos);
}

TEST(generator_is_deterministic) {
    auto g = tutorialGraph();
    CHECK(taskforge::compiler::TaskflowGenerator::generateSource(g) == taskforge::compiler::TaskflowGenerator::generateSource(g));
}

TEST(generator_cpp_string_escapes) {
    using taskforge::compiler::TaskflowGenerator;
    CHECK(TaskflowGenerator::cppString("a\"b") == "\"a\\\"b\"");
    CHECK(TaskflowGenerator::cppString("a\\b") == "\"a\\\\b\"");
    CHECK(TaskflowGenerator::cppString("x\ny") == "\"x\\ny\"");
    CHECK(TaskflowGenerator::cppString("??=") == "\"\\?\\?=\"");
}

// ---------------------------------------------------------------------------
// MCP (stdio real contra um servidor Python minimo)
// ---------------------------------------------------------------------------
TEST(mcp_stdio_roundtrip) {
    const char* py = taskforge::platform::commandExists("python") ? "python" : (taskforge::platform::commandExists("python3") ? "python3" : nullptr);
    if (!py) { std::cout << "  (python ausente: teste ignorado)" << std::endl; return; }
    taskforge::mcp::McpServerConfig cfg;
    cfg.id = "fake";
    cfg.transport = taskforge::mcp::McpTransportType::Stdio;
    cfg.command = py;
    cfg.args = {(fs::path("CppCore") / "tests" / "e2e" / "fake_mcp_server.py").string()};

    taskforge::mcp::McpClient client;
    auto tools = client.listTools(cfg, 15000);
    CHECK(tools.size() == 1 && tools[0].name == "echo");
    auto res = client.callTool(cfg, "echo", {{"text", "ola"}}, 15000);
    CHECK(res.success && !res.isError);
    CHECK(res.content == "eco: ola");

    taskforge::mcp::McpServerConfig mock;
    mock.id = "mock-x";
    mock.command = "mock";
    CHECK(client.callTool(mock, "read_file", {}, 1000).content.find("simulada") != std::string::npos);
    taskforge::mcp::McpServerConfig sseNoServer;
    sseNoServer.id = "remote";
    sseNoServer.transport = taskforge::mcp::McpTransportType::Sse;
    sseNoServer.url = "http://127.0.0.1:1/mcp";   // porta fechada: deve FALHAR, nunca fingir sucesso
    auto bad = client.callTool(sseNoServer, "x", {}, 3000);
    CHECK(bad.isError && !bad.success);
}


// ---------------------------------------------------------------------------
// Busca no projeto
// ---------------------------------------------------------------------------
static void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << content;
}

static const char* kSearchPipeline = R"({
  "version": "1", "metadata": {"graphName": "Principal", "description": "Pipeline de teste"},
  "nodes": [
    {"id": "n_fila", "label": "Fila Buffer", "type": "static", "parameters": {"capacidade": 128},
     "customLambdaBody": "// consome itens da fila"},
    {"id": "n_llm", "label": "Decis\u00e3o do LLM", "type": "condition", "customLambdaBody": "return 0;"},
    {"id": "n_visao", "label": "Vis\u00e3o Paralela", "type": "subflow",
     "childGraph": {"nodes": [
        {"id": "n_filtro", "label": "Filtro de Imagem", "type": "static", "customLambdaBody": "aplica o kernel gaussiano"}
     ], "edges": []}},
    {"id": "n_agente", "label": "Agente Revisor", "type": "agent",
     "agentSettings": {"model": {"provider": "deepseek", "promptTemplate": "Valide o SVG gerado e corrija os erros de tipografia"}}}
  ], "edges": []})";

TEST(search_tokenize_folds_accents_and_splits_identifiers) {
    using namespace taskforge::search;
    CHECK(foldText("Decisão") == "decisao");
    CHECK(foldText("Ç" "AÇÃO") == "cacao");
    auto t = tokenize("validarSvg snake_case");
    CHECK(t.size() == 4);
    CHECK(t[0] == "validar" && t[1] == "svg" && t[2] == "snake" && t[3] == "case");
    // plurais e acentos convergem para o mesmo termo
    CHECK(tokenize("Opera\xC3\xA7\xC3\xB5""es")[0] == tokenize("operacao")[0]);
    CHECK(tokenize("itens")[0] == tokenize("item")[0]);
    CHECK(tokenize("o a de the of").empty()); // stopwords
}

TEST(search_finds_nodes_subflows_and_agent_prompts) {
    using namespace taskforge::search;
    auto root = makeTempDir("search");
    writeFile(root / "pipelines" / "main.json", kSearchPipeline);
    ProjectSearchIndex idx;
    auto st = idx.refresh(root.string(), {{"main", "Pipeline Principal", "pipelines/main.json"}});
    CHECK(st.rebuilt && st.filesTotal == 1 && st.filesReindexed == 1);
    CHECK(st.documents == 6); // pipeline + 4 vertices + 1 vertice do subfluxo

    auto hits = idx.query("fila");
    CHECK(!hits.empty() && hits[0].nodeId == "n_fila");

    // sem acento acha com acento; sinonimo (decisao ~ condicao)
    hits = idx.query("decisao llm");
    CHECK(!hits.empty() && hits[0].nodeId == "n_llm");
    hits = idx.query("condicao");
    CHECK(!hits.empty() && hits[0].nodeId == "n_llm");

    // vertice dentro de subfluxo traz o caminho
    hits = idx.query("filtro imagem");
    CHECK(!hits.empty() && hits[0].nodeId == "n_filtro");
    CHECK(hits[0].nodePath.size() == 1 && hits[0].nodePath[0] == "n_visao");
    CHECK(hits[0].subtitle.find("Vis") != std::string::npos);

    // texto do prompt do agente, com snippet mostrando a linha que casou
    hits = idx.query("tipografia svg");
    CHECK(!hits.empty() && hits[0].nodeId == "n_agente");
    CHECK(hits[0].snippet.find("tipografia") != std::string::npos);

    // prefixo (digitando) e erro de digitacao
    hits = idx.query("agen");
    CHECK(!hits.empty() && hits[0].nodeId == "n_agente");
    hits = idx.query("revizor");
    CHECK(!hits.empty() && hits[0].nodeId == "n_agente");

    CHECK(idx.query("zzzqqq").empty());
    fs::remove_all(root);
}

TEST(search_indexes_skills_of_the_workspace) {
    using namespace taskforge::search;
    auto root = makeTempDir("search-skill");
    writeFile(root / ".workspace" / "skills" / "svg-validator" / "SKILL.md",
              "---\nname: svg-validator\ndescription: Valida a tipografia de arquivos SVG\n---\n# Passos\nConfira as fontes.\n");
    ProjectSearchIndex idx;
    idx.refresh(root.string(), {});
    auto hits = idx.query("validar tipografia");
    CHECK(!hits.empty() && hits[0].kind == "skill" && hits[0].title == "svg-validator");
    fs::remove_all(root);
}

TEST(search_reindexes_only_when_the_project_hash_changes) {
    using namespace taskforge::search;
    auto root = makeTempDir("search-hash");
    writeFile(root / "pipelines" / "main.json", kSearchPipeline);
    writeFile(root / "pipelines" / "segundo.json",
              R"({"nodes":[{"id":"x1","label":"Exportador PDF","type":"static"}],"edges":[]})");
    std::vector<PipelineSource> src = {{"main", "Principal", "pipelines/main.json"}};

    ProjectSearchIndex idx;
    auto a = idx.refresh(root.string(), src);
    CHECK(a.rebuilt && a.filesTotal == 2 && a.filesReindexed == 2);
    // todos os pipelines da pasta entram, mesmo fora do manifesto
    CHECK(!idx.query("exportador").empty());

    auto b = idx.refresh(root.string(), src);
    CHECK(!b.rebuilt && b.filesReindexed == 0 && b.projectHash == a.projectHash);

    // muda so um arquivo: so ele e reindexado e o hash do projeto muda
    writeFile(root / "pipelines" / "segundo.json",
              R"({"nodes":[{"id":"x1","label":"Gerador Relatorio","type":"static"}],"edges":[]})");
    auto c = idx.refresh(root.string(), src);
    CHECK(c.rebuilt && c.filesReindexed == 1 && c.projectHash != a.projectHash);
    CHECK(idx.query("exportador").empty());
    CHECK(!idx.query("relatorio").empty());

    // outra instancia (reinicio do aplicativo) reaproveita o cache em disco
    ProjectSearchIndex fresh;
    auto d = fresh.refresh(root.string(), src);
    CHECK(d.filesReindexed == 0 && d.projectHash == c.projectHash);
    CHECK(!fresh.query("relatorio").empty());

    // arquivo removido sai do indice
    fs::remove(root / "pipelines" / "segundo.json");
    auto e = idx.refresh(root.string(), src);
    CHECK(e.rebuilt && e.filesTotal == 1);
    CHECK(idx.query("relatorio").empty());
    fs::remove_all(root);
}

TEST(search_synonym_response_is_validated) {
    using namespace taskforge::search;
    const std::string reply =
        "Claro! Aqui esta:\n```json\n{\"groups\": [[\"queue\", \"cola\", \"Fila\", \"duas palavras\", 7],"
        " [\"solo\"], \"x\", [\"database\", \"base-de-dados\", \"banco\", \"<script>\"]]}\n```";
    auto g = parseSynonymResponse(reply);
    CHECK(g.size() == 2);                    // o grupo de uma palavra e o item nao-lista caem
    CHECK(g[0].size() == 3 && g[0][0] == "queue" && g[0][2] == "fila");   // frase, numero: descartados; caixa baixa
    CHECK(g[1].size() == 2 && g[1][1] == "banco");   // "base-de-dados" (varias palavras) e "<script>" caem
    CHECK(parseSynonymResponse("sem json nenhum").empty());
    CHECK(parseSynonymResponse("{\"groups\": 5}").empty());
}

TEST(search_installed_synonyms_expand_queries) {
    using namespace taskforge::search;
    auto dir = makeTempDir("synonyms");
    setSynonymDirForTesting(dir.string());
    CHECK(!hasSynonymFile("es-ES"));

    auto root = makeTempDir("search-syn");
    writeFile(root / "pipelines" / "main.json",
              R"({"nodes":[{"id":"a","label":"Cola de mensajes","type":"static"}],"edges":[]})");
    ProjectSearchIndex idx;
    idx.refresh(root.string(), {{"main", "Principal", "pipelines/main.json"}});
    CHECK(idx.query("queue").empty());      // sem dicionario, "queue" nao chega a "cola"

    std::string err;
    CHECK(writeSynonymFile("es-ES", "teste/modelo", {{"queue", "cola", "buffer"}, {"error", "fallo"}}, &err));
    CHECK(hasSynonymFile("es-ES"));
    auto hits = idx.query("queue");         // o dicionario do idioma instalado liga ingles e espanhol
    CHECK(!hits.empty() && hits[0].nodeId == "a");
    CHECK(!writeSynonymFile("../fora", "m", {{"a", "b"}}, &err));   // idioma invalido nao grava fora da pasta

    setSynonymDirForTesting("");
    fs::remove_all(dir);
    fs::remove_all(root);
}

TEST(search_studio_default_model_comes_from_the_environment) {
    using namespace taskforge::search;
    auto setv = [](const char* k, const char* v) {
#ifdef _WIN32
        _putenv_s(k, v);
#else
        if (*v) setenv(k, v, 1); else unsetenv(k);
#endif
    };
    const char* vars[] = {"ARESTRIX_DEFAULT_PROVIDER", "ARESTRIX_DEFAULT_MODEL", "DEEPSEEK_API_KEY", "OPENAI_API_KEY",
                          "ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "LOCAL_LLM_BASE_URL"};
    std::vector<std::pair<std::string, std::string>> saved;
    for (auto v : vars) { const char* c = std::getenv(v); saved.push_back({v, c ? c : ""}); setv(v, ""); }

    CHECK(!studioDefaultModel().has_value());                        // nada configurado
    setv("OPENAI_API_KEY", "x");
    auto m = studioDefaultModel();
    CHECK(m && m->provider == "openai" && !m->model.empty());       // primeiro provedor com chave
    setv("ARESTRIX_DEFAULT_PROVIDER", "local");
    CHECK(!studioDefaultModel().has_value());                        // local exige o nome do modelo
    setv("ARESTRIX_DEFAULT_MODEL", "llama3");
    m = studioDefaultModel();
    CHECK(m && m->provider == "local" && m->model == "llama3");      // escolha explicita do usuario

    setv("ARESTRIX_DEFAULT_PROVIDER", "auto");                       // "auto" = nao definido
    setv("ARESTRIX_DEFAULT_MODEL", "auto");
    m = studioDefaultModel();
    CHECK(m && m->provider == "openai" && m->model != "auto");

    for (auto& [k, v] : saved) setv(k.c_str(), v.c_str());
}

int main() {
    std::srand((unsigned)std::time(nullptr));
    for (auto& t : tests()) {
        int before = g_failed;
        std::cout << "[ RUN  ] " << t.name << std::endl;
        try {
            t.fn();
        } catch (const std::exception& ex) {
            ++g_failed;
            std::cerr << "  EXCECAO: " << ex.what() << "\n";
        }
        std::cout << (g_failed == before ? "[  OK  ] " : "[ FAIL ] ") << t.name << std::endl;
    }
    std::cout << "\n" << (g_total - g_failed) << "/" << g_total << " verificacoes passaram\n";
    return g_failed == 0 ? 0 : 1;
}
