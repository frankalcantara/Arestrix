// Testes do nucleo C++ do Arestrix (sem framework externo).
// Executar: ctest --test-dir build  (ou ./build/arestrix_tests)

#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "compiler/TaskflowGenerator.hpp"
#include "mcp/McpClient.hpp"
#include "core/Graph.hpp"
#include "platform/Process.hpp"
#include "project/OpenableFiles.hpp"
#include "rt/Decision.hpp"
#include "rt/Http.hpp"
#include "rt/NetGuard.hpp"
#include "rt/McpTools.hpp"
#include "rt/McpVertex.hpp"
#include "native/NativeTools.hpp"
#include "rt/Runtime.hpp"
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

TEST(generator_switch_selects_case_with_default_fallback) {
    auto base = tutorialGraph();
    json schema = base.toJson();
    json& nodes = schema["nodes"];
    bool found = false;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]["id"] != "cond") continue;
        nodes[i]["type"] = "switch";
        nodes[i]["outputs"].push_back(json{{"id", "b2"}, {"name", "default"}, {"direction", "output"}, {"type", "flow"},
                                           {"dataType", ""}, {"conditionBranchIndex", 2}});
        found = true;
    }
    CHECK(found);
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);

    CHECK(src.find("auto arx_select = [&]() -> int {") != std::string::npos);        // o corpo do usuario vira o seletor
    CHECK(src.find("const int arx_cases = 3;") != std::string::npos);                // 2 casos + default
    CHECK(src.find("arx_case = arx_cases - 1;") != std::string::npos);               // fora do intervalo -> default
    CHECK(src.find("audit_decision.txt") != std::string::npos);                      // corpo preservado
    CHECK(src.find("task_cond.precede(task_done, task_gen)") != std::string::npos);  // casos 0 e 1 como no condition
    CHECK(src.find("task_bootstrap.precede(task_gen)") != std::string::npos);        // o retorno ainda e laco, nao entrada dupla

    // sem corpo: o seletor cai direto no default
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]["id"] == "cond") nodes[i]["customLambdaBody"] = "";
    taskforge::core::Graph g2;
    g2.fromJson(schema);
    std::string src2 = taskforge::compiler::TaskflowGenerator::generateSource(g2);
    CHECK(src2.find("return 2;") != std::string::npos);

    // o tipo "switch" faz roundtrip no JSON
    CHECK(g.toJson()["nodes"].dump().find("\"type\":\"switch\"") != std::string::npos);
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
#ifdef _WIN32
    // npx/npm/uvx viram cmd.exe no Windows. Argumentos com metacaracteres devem ser RECUSADOS sem spawn:
    // configs MCP vivem em arquivos de projeto e nao podem injetar comandos.
    taskforge::mcp::McpServerConfig evil;
    evil.id = "evil";
    evil.transport = taskforge::mcp::McpTransportType::Stdio;
    evil.command = "npx";
    evil.args = {"-y", "a\" & calc & \"b"};
    auto refused = client.callTool(evil, "x", {}, 2000);
    CHECK(refused.isError && !refused.success);
    CHECK(refused.errorCode == "invalid_arg");   // codigo estavel para o i18n do frontend
#endif
    // Transporte HTTP do MCP: http:// ou endereco interno e RECUSADO pela politica ANTES de conectar
    // (sempre falha, nunca finge sucesso; nenhuma conexao e tentada).
    taskforge::mcp::McpServerConfig sseNoServer;
    sseNoServer.id = "remote";
    sseNoServer.transport = taskforge::mcp::McpTransportType::Sse;
    sseNoServer.url = "http://127.0.0.1:1/mcp";
    auto bad = client.callTool(sseNoServer, "x", {}, 3000);
    CHECK(bad.isError && !bad.success);
    CHECK(bad.errorMessage.find("https") != std::string::npos);   // recusada pela politica, nao por falha de transporte
    CHECK(bad.errorCode == "url_not_https");
    taskforge::mcp::McpServerConfig rebind;
    rebind.id = "rebind";
    rebind.transport = taskforge::mcp::McpTransportType::Sse;
    rebind.url = "https://api.exemplo.com:8443/mcp";   // porta que nao e 443
    auto badPort = client.callTool(rebind, "x", {}, 3000);
    CHECK(badPort.isError && !badPort.success);
    CHECK(badPort.errorMessage.find("443") != std::string::npos);
    CHECK(badPort.errorCode == "url_bad_port");

    // listTools devolve o codigo estavel via McpErrorInfo; host interno e recusado (DNS rebinding).
    taskforge::mcp::McpServerConfig internal;
    internal.id = "internal";
    internal.transport = taskforge::mcp::McpTransportType::Sse;
    internal.url = "https://127.0.0.1:443/mcp";
    taskforge::mcp::McpErrorInfo info;
    CHECK(client.listTools(internal, 2000, &info).empty());
    CHECK(!info.empty() && info.codeName() == std::string("url_non_public_host"));
    CHECK(info.detail.find("127.0.0.1") != std::string::npos);

    // URL vazia no transporte HTTP: codigo proprio.
    taskforge::mcp::McpServerConfig noUrl;
    noUrl.id = "sem-url";
    noUrl.transport = taskforge::mcp::McpTransportType::Sse;
    noUrl.url = "";
    auto noUrlRes = client.callTool(noUrl, "x", {}, 1000);
    CHECK(noUrlRes.isError && noUrlRes.errorCode == "url_empty");
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

// ---------------------------------------------------------------------------
// Switch sem codigo (regras por palavras e decisao por modelo)
// ---------------------------------------------------------------------------
TEST(decision_keywords_ignore_accents_case_and_respect_word_boundaries) {
    using namespace taskforge::rt;
    std::vector<std::vector<std::string>> rules = {{"aprovado", "tudo certo"}, {"erro", "falha cr\xC3\xADtica"}};
    CHECK(pickByKeywords("STATUS: APROVADO.", rules, 2) == 0);
    CHECK(pickByKeywords("Revis\xC3\xA3o: tudo CERTO por aqui", rules, 2) == 0);   // frase, caixa mista
    CHECK(pickByKeywords("houve uma FALHA CRITICA", rules, 2) == 1);              // sem acento casa com acento
    CHECK(pickByKeywords("um terror de relatorio", rules, 2) == 2);              // "erro" dentro de "terror" nao casa
    CHECK(pickByKeywords("nada relevante", rules, 2) == 2);                      // default
    CHECK(pickByKeywords("aprovado mas com erro", rules, 2) == 0);               // ganha a ordem dos casos
}

TEST(decision_model_reply_is_matched_to_a_label) {
    using namespace taskforge::rt;
    std::vector<std::string> labels = {"alto", "m\xC3\xA9" "dio", "baixo", "default"};
    CHECK(matchLabel("alto", labels, 3) == 0);
    CHECK(matchLabel("  \"Medio\".\n", labels, 3) == 1);                        // aspas, caixa, acento
    CHECK(matchLabel("Acho que o caminho e baixo.", labels, 3) == 2);            // rotulo dentro de uma frase
    CHECK(matchLabel("nao sei dizer", labels, 3) == 3);                          // nao reconhecida: default
    std::vector<std::string> overlap = {"erro", "erro grave", "default"};
    CHECK(matchLabel("e um erro grave", overlap, 2) == 1);                       // o rotulo mais longo vence

    std::string prompt = decisionUserPrompt("Classifique o risco.", labels, "texto do relatorio");
    CHECK(prompt.find("Classifique o risco.") != std::string::npos);
    CHECK(prompt.find("- baixo") != std::string::npos);
    CHECK(prompt.find("none of the others fits") != std::string::npos);          // o default e explicado ao modelo
    CHECK(prompt.find("texto do relatorio") != std::string::npos);
}

TEST(decision_rules_read_previous_output_or_a_file) {
    using namespace taskforge::rt;
    auto root = makeTempDir("decide");
    fs::path oldCwd = fs::current_path();
    fs::current_path(root);                       // o workspace do runtime e o diretorio atual

    const std::string spec = R"({"mode":"rules","labels":["aprovado","reprovado","default"],
        "rules":[["aprovado"],["reprovado","rejeitado"]],
        "source":{"kind":"previous","path":"","from":["agente_a"]}})";

    // sem saida do vertice anterior: erro explicito, nunca um desvio silencioso
    bool threw = false;
    try { decideCase("sw", "Roteador", spec); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    writeFile(root / ".arestrix" / "out" / "agente_a.txt", "Resultado: REJEITADO por inconsistencias.");
    CHECK(decideCase("sw", "Roteador", spec) == 1);

    writeFile(root / ".arestrix" / "out" / "agente_a.txt", "Sem conclusao.");
    CHECK(decideCase("sw", "Roteador", spec) == 2);                              // default

    // entrada por arquivo do projeto, confinada ao workspace
    writeFile(root / "report" / "audit.txt", "STATUS: APROVADO");
    const std::string fileSpec = R"({"mode":"rules","labels":["aprovado","reprovado","default"],
        "rules":[["aprovado"],["reprovado"]],"source":{"kind":"file","path":"report/audit.txt"}})";
    CHECK(decideCase("sw", "Roteador", fileSpec) == 0);
    const std::string escape = R"({"mode":"rules","labels":["a","default"],"rules":[["a"]],
        "source":{"kind":"file","path":"../fora.txt"}})";
    threw = false;
    try { decideCase("sw", "Roteador", escape); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    fs::current_path(oldCwd);
    fs::remove_all(root);
}

TEST(generator_switch_without_code_calls_the_runtime_decision) {
    auto base = tutorialGraph();
    json schema = base.toJson();
    json& nodes = schema["nodes"];
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]["id"] != "cond") continue;
        nodes[i]["type"] = "switch";
        nodes[i]["customLambdaBody"] = "";
        nodes[i]["outputs"] = json::array({
            json{{"id", "b0"}, {"name", "aprovado"}, {"direction", "output"}, {"type", "flow"}, {"dataType", ""}, {"conditionBranchIndex", 0}},
            json{{"id", "b1"}, {"name", "reprovado"}, {"direction", "output"}, {"type", "flow"}, {"dataType", ""}, {"conditionBranchIndex", 1}},
            json{{"id", "b2"}, {"name", "default"}, {"direction", "output"}, {"type", "flow"}, {"dataType", ""}, {"conditionBranchIndex", 2}}});
        nodes[i]["switchSettings"] = json{{"mode", "model"}, {"instruction", "Decida pelo relatorio."},
                                          {"source", {{"kind", "previous"}}}, {"rules", json::object()}};
    }
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);
    CHECK(src.find("taskforge::rt::decideCase(\"cond\"") != std::string::npos);
    CHECK(src.find("\"labels\":[\"aprovado\",\"reprovado\",\"default\"]") != std::string::npos);
    CHECK(src.find("\"from\":[\"jev\"]") != std::string::npos);                   // o vertice anterior alimenta a decisao
    CHECK(src.find("\"provider\":\"deepseek\"") != std::string::npos);            // modelo herdado dos padroes globais
    CHECK(src.find("\"instruction\":\"Decida pelo relatorio.\"") != std::string::npos);
    CHECK(src.find("arx_case = arx_cases - 1;") != std::string::npos);            // o fallback para o default continua

    // modo "rules": palavras por porta, sem modelo no spec
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i]["id"] == "cond")
            nodes[i]["switchSettings"] = json{{"mode", "rules"}, {"source", {{"kind", "file"}, {"path", "audit.txt"}}},
                                              {"rules", {{"b0", "aprovado, tudo certo"}, {"b1", "reprovado;rejeitado"}}}};
    taskforge::core::Graph g2;
    g2.fromJson(schema);
    std::string src2 = taskforge::compiler::TaskflowGenerator::generateSource(g2);
    CHECK(src2.find("\"rules\":[[\"aprovado\",\"tudo certo\"],[\"reprovado\",\"rejeitado\"]]") != std::string::npos);
    CHECK(src2.find("\"path\":\"audit.txt\"") != std::string::npos);
    auto callPos = src2.find("decideCase(\"cond\"");
    auto callEnd = src2.find(");", callPos);
    CHECK(callPos != std::string::npos && src2.substr(callPos, callEnd - callPos).find("\"provider\"") == std::string::npos);   // regras nao usam modelo
}

// ---------------------------------------------------------------------------
// MCP para agentes (ferramentas de servidores de .workspace/mcps)
// ---------------------------------------------------------------------------
static void setTestEnv(const char* k, const char* v) {
#ifdef _WIN32
    _putenv_s(k, v);
#else
    if (*v) setenv(k, v, 1); else unsetenv(k);
#endif
}

TEST(mcp_toolbox_expands_env_references_and_names_tools) {
    using taskforge::rt::McpToolbox;
    setTestEnv("ARX_TEST_REF", "valor-ok");
    CHECK(McpToolbox::expandEnv("a-${ARX_TEST_REF}-b") == "a-valor-ok-b");
    CHECK(McpToolbox::expandEnv("sem referencia $ e ${ invalido") == "sem referencia $ e ${ invalido");
    setTestEnv("ARX_TEST_UNSET2", "");
    CHECK(McpToolbox::expandEnv("${ARX_TEST_UNSET2:-python}") == "python");           // padrao quando ausente
    CHECK(McpToolbox::expandEnv("${ARX_TEST_REF:-python}") == "valor-ok");            // a variavel vence o padrao
    setTestEnv("ARX_TEST_REF", "");
    bool threw = false;
    try { McpToolbox::expandEnv("${ARX_TEST_REF}"); } catch (const std::exception& e) {
        threw = std::string(e.what()).find("ARX_TEST_REF") != std::string::npos;   // o erro diz QUAL variavel falta
    }
    CHECK(threw);

    CHECK(McpToolbox::agentToolName("clientes", "lookup_customer") == "mcp__clientes__lookup_customer");
    CHECK(McpToolbox::agentToolName("meu servidor", "a.b") == "mcp__meu_servidor__a_b");
    std::string longName = McpToolbox::agentToolName(std::string(40, 's'), std::string(60, 't'));
    CHECK(longName.size() <= 64 && longName.rfind("mcp__", 0) == 0);
    CHECK(longName != McpToolbox::agentToolName(std::string(40, 's'), std::string(59, 't') + "u"));   // truncar nao pode colidir
}

TEST(mcp_toolbox_loads_project_servers_and_runs_tools) {
    using taskforge::rt::McpToolbox;
    const char* py = taskforge::platform::commandExists("python") ? "python" : (taskforge::platform::commandExists("python3") ? "python3" : nullptr);
    if (!py) { std::cout << "  (python ausente: teste ignorado)" << std::endl; return; }

    auto root = makeTempDir("mcptools");
    fs::path script = fs::absolute(fs::path("CppCore") / "tests" / "e2e" / "fake_mcp_server.py");
    setTestEnv("ARX_TEST_MCP_SCRIPT", script.string().c_str());
    // a configuracao do projeto referencia a variavel; o caminho real nunca fica no arquivo
    writeFile(root / ".workspace" / "mcps" / "eco.json",
              std::string("{\"id\":\"eco\",\"name\":\"Eco\",\"transport\":\"stdio\",\"command\":\"") + py +
              "\",\"args\":[\"${ARX_TEST_MCP_SCRIPT}\"],\"env\":{},\"isEnabled\":true}");
    writeFile(root / ".workspace" / "mcps" / "off.json", R"({"id":"off","command":"python","isEnabled":false})");
    writeFile(root / ".workspace" / "mcps" / "falta.json",
              R"({"id":"falta","command":"python","args":["${ARX_TEST_DEFINITELY_UNSET}"]})");

    McpToolbox box(root, {"eco"}, {});
    CHECK(box.definitions().size() == 1);
    CHECK(box.definitions()[0].name == "mcp__eco__echo");
    CHECK(box.owns("mcp__eco__echo") && !box.owns("read"));
    CHECK(box.definitions()[0].parameters["type"] == "object");
    auto res = box.call("mcp__eco__echo", json{{"text", "ola"}});
    CHECK(!res.isError && res.content == "eco: ola");
    CHECK(box.call("mcp__eco__nao_existe", json::object()).isError);

    McpToolbox filtered(root, {"eco"}, {"outra_ferramenta"});               // lista de permitidas sem a ferramenta
    CHECK(filtered.definitions().empty());

    auto fails = [&](const char* id) {
        try { McpToolbox b(root, {id}, {}); } catch (const std::exception&) { return true; }
        return false;
    };
    CHECK(fails("off"));            // desabilitado
    CHECK(fails("falta"));          // variavel de ambiente ausente
    CHECK(fails("inexistente"));    // servidor que o projeto nao tem
    CHECK(fails("../fuga"));        // identificador invalido

    fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// Sessoes MCP persistentes (um processo por servidor, nao um por chamada)
// ---------------------------------------------------------------------------
static taskforge::mcp::McpServerConfig countingServer(const fs::path& countFile) {
    taskforge::mcp::McpServerConfig cfg;
    cfg.id = "counting";
    cfg.transport = taskforge::mcp::McpTransportType::Stdio;
    cfg.command = taskforge::platform::commandExists("python") ? "python" : "python3";
    cfg.args = {fs::absolute(fs::path("CppCore") / "tests" / "e2e" / "counting_mcp_server.py").string()};
    cfg.env["ARX_MCP_COUNT_FILE"] = countFile.string();
    return cfg;
}

static int countStarts(const fs::path& f) {
    std::ifstream in(f);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) if (line.rfind("start", 0) == 0) ++n;
    return n;
}

TEST(mcp_sessions_are_persistent_and_recover) {
    using taskforge::mcp::McpClient;
    if (!taskforge::platform::commandExists("python") && !taskforge::platform::commandExists("python3")) {
        std::cout << "  (python ausente: teste ignorado)" << std::endl;
        return;
    }
    McpClient::shutdownSessions();
    auto root = makeTempDir("mcp-pool");
    auto cfg = countingServer(root / "starts.log");
    McpClient client;

    // varias chamadas, UM processo
    auto first = client.callTool(cfg, "pid", {}, 20000);
    CHECK(first.success && !first.isError);
    for (int i = 0; i < 5; ++i) CHECK(client.callTool(cfg, "pid", {}, 20000).content == first.content);
    CHECK(client.listTools(cfg, 20000).size() == 3);
    CHECK(countStarts(root / "starts.log") == 1);
    CHECK(McpClient::activeSessions() == 1);
    CHECK(client.callTool(cfg, "echo", {{"text", "oi"}}, 20000).content == "eco: oi");

    // threads em paralelo no mesmo servidor: serializadas, sem abrir outro processo
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
        threads.emplace_back([&] {
            taskforge::mcp::McpClient c;
            for (int i = 0; i < 5; ++i)
                if (c.callTool(cfg, "echo", {{"text", "x"}}, 20000).content == "eco: x") ++ok;
        });
    for (auto& th : threads) th.join();
    CHECK(ok == 40);
    CHECK(countStarts(root / "starts.log") == 1);

    // o servidor morre no meio de uma chamada: a chamada falha (nunca e repetida) e a proxima abre uma sessao nova
    auto died = client.callTool(cfg, "die", {}, 20000);
    CHECK(died.isError);
    auto after = client.callTool(cfg, "pid", {}, 20000);
    CHECK(after.success && !after.isError && after.content != first.content);
    CHECK(countStarts(root / "starts.log") == 2);

    // mudar a configuracao recria a sessao; encerrar tudo fecha os processos
    auto changed = cfg;
    changed.env["EXTRA"] = "1";
    auto third = client.callTool(changed, "pid", {}, 20000);
    CHECK(third.success && third.content != after.content);
    McpClient::shutdownSessions();
    CHECK(McpClient::activeSessions() == 0);

    fs::remove_all(root);
}

TEST(mcp_persistent_sessions_are_much_faster_than_one_process_per_call) {
    using taskforge::mcp::McpClient;
    if (!taskforge::platform::commandExists("python") && !taskforge::platform::commandExists("python3")) return;
    McpClient::shutdownSessions();
    auto root = makeTempDir("mcp-bench");
    auto cfg = countingServer(root / "starts.log");
    McpClient client;
    const int calls = 15;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) {            // antes: um processo e um handshake por chamada
        client.callTool(cfg, "echo", {{"text", "x"}}, 20000);
        McpClient::shutdownSessions();
    }
    double perCall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < calls; ++i) client.callTool(cfg, "echo", {{"text", "x"}}, 20000);   // agora: sessao reaproveitada
    double persistent = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    McpClient::shutdownSessions();

    std::cout << "  " << calls << " chamadas: processo por chamada = " << perCall << " s, sessao persistente = " << persistent
              << " s (" << (persistent > 0 ? perCall / persistent : 0) << "x mais rapido)" << std::endl;
    CHECK(persistent * 3 < perCall);
    fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// Vertice MCP (chamada direta de uma ferramenta, sem modelo de IA)
// ---------------------------------------------------------------------------
TEST(mcp_vertex_renders_arguments_from_the_input) {
    using namespace taskforge::rt;
    json args = {{"nome", "Oi {{input.cliente.nome}}!"}, {"id", "{{input.cliente.id}}"}, {"todo", "{{input}}"},
                 {"lista", json::array({"{{input.ativo}}", "fixo"})}, {"n", 7}};
    const std::string input = R"({"cliente":{"nome":"Ana","id":42},"ativo":true})";
    json out = renderMcpArguments(args, input);
    CHECK(out["nome"] == "Oi Ana!");
    CHECK(out["id"].is_number() && out["id"] == 42);              // referencia pura preserva o tipo
    CHECK(out["todo"] == input);
    CHECK(out["lista"][0].is_boolean() && out["lista"][0] == true && out["lista"][1] == "fixo");
    CHECK(out["n"] == 7);

    bool threw = false;
    try { renderMcpArguments(json{{"x", "{{input.nao_existe}}"}}, input); } catch (const std::exception&) { threw = true; }
    CHECK(threw);                                                  // campo ausente: erro claro, nunca valor vazio
    threw = false;
    try { renderMcpArguments(json{{"x", "{{input.a}}"}}, "texto solto"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);                                                  // entrada nao JSON
    CHECK(renderMcpArguments(json{{"x", "{{input}}"}}, "texto solto")["x"] == "texto solto");
}

TEST(mcp_vertex_calls_a_tool_and_hands_the_result_to_the_next_vertex) {
    using namespace taskforge::rt;
    const char* py = taskforge::platform::commandExists("python") ? "python" : (taskforge::platform::commandExists("python3") ? "python3" : nullptr);
    if (!py) { std::cout << "  (python ausente: teste ignorado)" << std::endl; return; }
    taskforge::mcp::McpClient::shutdownSessions();

    auto root = makeTempDir("mcpvertex");
    fs::path oldCwd = fs::current_path();
    fs::path script = fs::absolute(fs::path("CppCore") / "tests" / "e2e" / "counting_mcp_server.py");
    setTestEnv("ARX_TEST_VERTEX_SCRIPT", script.string().c_str());
    fs::current_path(root);                                         // o workspace do runtime e o diretorio atual

    writeFile(root / ".workspace" / "mcps" / "eco.json",
              std::string("{\"id\":\"eco\",\"transport\":\"stdio\",\"command\":\"") + py +
              "\",\"args\":[\"${ARX_TEST_VERTEX_SCRIPT}\"],\"isEnabled\":true}");
    writeFile(root / "entrada.json", R"({"nome":"Ana","n":3})");

    // a entrada vem de um arquivo do projeto; o resultado vai para .arestrix/out e para um arquivo escolhido
    callMcpTool("v1", "Consultar", R"({"server":"eco","tool":"echo","arguments":{"text":"ola {{input.nome}}"},
        "source":{"kind":"file","path":"entrada.json"},"output":{"file":"saida/resultado.txt"}})");
    CHECK(taskforge::jsonutil::readFile(root / ".arestrix" / "out" / "v1.txt") == "eco: ola Ana");
    CHECK(taskforge::jsonutil::readFile(root / "saida" / "resultado.txt") == "eco: ola Ana");

    // o vertice seguinte (por exemplo um switch) enxerga o resultado como "saida do vertice anterior"
    callMcpTool("v2", "Repetir", R"({"server":"eco","tool":"echo","arguments":{"text":"[{{input}}]"},
        "source":{"kind":"previous","from":["v1"]},"output":{}})");
    CHECK(taskforge::jsonutil::readFile(root / ".arestrix" / "out" / "v2.txt") == "eco: [eco: ola Ana]");

    auto fails = [&](const std::string& spec) {
        try { callMcpTool("vx", "Falha", spec); } catch (const std::exception&) { return true; }
        return false;
    };
    CHECK(fails(R"({"server":"","tool":"echo"})"));                                          // sem servidor
    CHECK(fails(R"({"server":"nao_existe","tool":"echo","source":{"kind":"none"}})"));      // servidor ausente no projeto
    CHECK(fails(R"({"server":"eco","tool":"echo","arguments":{"t":"{{input.x}}"},"source":{"kind":"none"}})"));   // campo ausente
    CHECK(fails(R"({"server":"eco","tool":"echo","source":{"kind":"previous","from":["nunca_rodou"]}})"));       // sem saida anterior
    CHECK(fails(R"({"server":"eco","tool":"echo","source":{"kind":"none"},"output":{"file":"../fora.txt"}})"));  // fuga do projeto
    CHECK(fails(R"({"server":"eco","tool":"die","source":{"kind":"none"}})"));                                   // ferramenta falha: pipeline para

    taskforge::mcp::McpClient::shutdownSessions();
    fs::current_path(oldCwd);
    fs::remove_all(root);
}

TEST(generator_mcp_vertex_calls_the_native_client) {
    auto base = tutorialGraph();
    json schema = base.toJson();
    json& nodes = schema["nodes"];
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i]["id"] != "a") continue;
        nodes[i]["type"] = "mcp";
        nodes[i]["customLambdaBody"] = "";
        nodes[i]["mcpSettings"] = json{{"server", "clientes"}, {"tool", "lookup_customer"},
                                       {"arguments", {{"email", "{{input.email}}"}}},
                                       {"source", {{"kind", "previous"}}}, {"output", {{"file", "workspace/cliente.json"}}}};
    }
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);
    CHECK(src.find("taskforge::rt::callMcpTool(\"a\"") != std::string::npos);
    CHECK(src.find("\"server\":\"clientes\"") != std::string::npos);
    CHECK(src.find("\"tool\":\"lookup_customer\"") != std::string::npos);
    CHECK(src.find("\"from\":[\"gen\"]") != std::string::npos);                        // o vertice anterior alimenta a entrada
    CHECK(src.find("\"file\":\"workspace/cliente.json\"") != std::string::npos);
    CHECK(src.find("task_gen.precede(task_a)") != std::string::npos);                  // continua sendo uma tarefa Taskflow normal
}

// ---------------------------------------------------------------------------
// Ferramentas nativas (servidores em C++ dentro do processo)
// ---------------------------------------------------------------------------
static taskforge::rt::ToolResult nativeCall(const fs::path& root, const std::string& server, const std::string& tool, const json& args) {
    return taskforge::native::call("arestrix." + server, tool, args, taskforge::native::Context{root});
}

TEST(native_registry_lists_the_servers_and_validates_calls) {
    namespace nat = taskforge::native;
    std::vector<std::string> ids;
    for (const auto& sv : nat::servers()) {
        ids.push_back(sv.id);
        CHECK(!sv.tools.empty() && sv.id.rfind("arestrix.", 0) == 0);
        for (const auto& t : sv.tools) CHECK(t.inputSchema["type"] == "object" && !t.description.empty() && (t.risk == "safe" || t.risk == "moderate" || t.risk == "high"));
    }
    for (const char* want : {"arquivos", "http", "dados", "memoria", "tempo", "git", "sqlite"})
        CHECK(std::find(ids.begin(), ids.end(), std::string("arestrix.") + want) != ids.end());
    auto root = makeTempDir("native-reg");
    CHECK(nativeCall(root, "arquivos", "read_file", json::object()).isError);             // argumento obrigatorio ausente
    CHECK(nativeCall(root, "arquivos", "nao_existe", json::object()).isError);
    CHECK(nat::call("arestrix.inexistente", "x", json::object(), nat::Context{root}).isError);
    fs::remove_all(root);
}

TEST(native_files_are_confined_and_protect_code_paths) {
    auto root = makeTempDir("native-files");
    CHECK(!nativeCall(root, "arquivos", "write_file", {{"path", "notas/a.txt"}, {"content", "ola\nmundo ARESTRIX"}}).isError);
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", "notas/a.txt"}}).content == "ola\nmundo ARESTRIX");
    CHECK(!nativeCall(root, "arquivos", "write_file", {{"path", "notas/a.txt"}, {"content", "!"}, {"append", true}}).isError);
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", "notas/a.txt"}}).content == "ola\nmundo ARESTRIX!");

    // fuga do projeto
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", "../fora.txt"}}).isError);
    CHECK(nativeCall(root, "arquivos", "write_file", {{"path", "../fora.txt"}, {"content", "x"}}).isError);
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", (fs::temp_directory_path() / "qualquer.txt").string()}}).isError);

    // .git ilegivel; pipelines, configuracao e pastas internas nao sao gravaveis (gravar ali injetaria codigo)
    writeFile(root / ".git" / "config", "[remote]\nurl = https://usuario:segredo@exemplo.com/r.git\n");
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", ".git/config"}}).isError);
    for (const char* path : {"pipelines/main.json", "arestrix.project.json", ".workspace/mcps/x.json", ".git/hooks/pre-commit",
                             ".arestrix/memory.json", ".githooks/pre-commit", "arestrix-mcp-servers.json", "PIPELINES/outro.json"}) {
        auto r = nativeCall(root, "arquivos", "write_file", {{"path", path}, {"content", "x"}});
        CHECK(r.isError);
        CHECK(!fs::exists(root / path));
    }
    CHECK(nativeCall(root, "arquivos", "make_directory", {{"path", "pipelines/novo"}}).isError);
    CHECK(!nativeCall(root, "arquivos", "make_directory", {{"path", "dados/2026"}}).isError && fs::is_directory(root / "dados" / "2026"));

    // listar, informar e buscar
    CHECK(nativeCall(root, "arquivos", "list_directory", {{"path", "notas"}}).content.find("a.txt") != std::string::npos);
    CHECK(nativeCall(root, "arquivos", "list_directory", {{"recursive", true}}).content.find(".git") == std::string::npos);
    CHECK(nativeCall(root, "arquivos", "file_info", {{"path", "notas/a.txt"}}).content.find("\"type\":\"file\"") != std::string::npos);
    CHECK(nativeCall(root, "arquivos", "file_info", {{"path", "nada.txt"}}).content.find("\"exists\":false") != std::string::npos);
    auto found = nativeCall(root, "arquivos", "search_text", {{"query", "arestrix"}});      // sem diferenciar maiusculas
    CHECK(found.content.find("notas/a.txt:2:") != std::string::npos);
    CHECK(nativeCall(root, "arquivos", "search_text", {{"query", "segredo"}}).content == "(nenhum resultado)");   // nao entra em .git
    CHECK(nativeCall(root, "arquivos", "search_text", {{"query", "a"}}).isError);                                 // consulta curta demais

    // binarios e arquivos grandes
    { std::ofstream(root / "bin.dat", std::ios::binary) << std::string("abc\0def", 7); }
    CHECK(nativeCall(root, "arquivos", "read_file", {{"path", "bin.dat"}}).isError);
    CHECK(nativeCall(root, "arquivos", "write_file", {{"path", "grande.txt"}, {"content", std::string(2 * 1024 * 1024 + 1, 'x')}}).isError);

    // as ferramentas write/edit dos AGENTES tambem protegem os caminhos que levam a execucao de codigo
    taskforge::rt::ToolContext tctx;
    tctx.workspaceRoot = root;
    tctx.allowed = {"read", "write", "edit"};
    CHECK(taskforge::rt::executeTool(tctx, "write", {{"path", "pipelines/main.json"}, {"content", "x"}}).isError);
    CHECK(taskforge::rt::executeTool(tctx, "write", {{"path", "arestrix.project.json"}, {"content", "x"}}).isError);
    CHECK(!taskforge::rt::executeTool(tctx, "write", {{"path", "workspace/ok.txt"}, {"content", "x"}}).isError);
    writeFile(root / "pipelines" / "p.json", "abc");
    CHECK(taskforge::rt::executeTool(tctx, "edit", {{"path", "pipelines/p.json"}, {"old_text", "abc"}, {"new_text", "xyz"}}).isError);
    CHECK(taskforge::jsonutil::readFile(root / "pipelines" / "p.json") == "abc");
    fs::remove_all(root);
}

TEST(native_http_requires_project_authorization_and_blocks_internal_targets) {
    namespace nat = taskforge::native;
    auto root = makeTempDir("native-http");
    // sem politica: nada sai, e a mensagem ensina o que fazer
    std::string refusal = nat::http::checkUrl(root, "https://api.exemplo.com/v1");
    CHECK(refusal.find("api.exemplo.com") != std::string::npos && refusal.find("http.json") != std::string::npos);

    writeFile(root / ".workspace" / "native" / "http.json", R"({"allowedHosts":["api.exemplo.com","*.exemplo.org","*","10.0.0.5","localhost"]})");
    CHECK(nat::http::checkUrl(root, "https://api.exemplo.com/v1?q=1").empty());
    CHECK(nat::http::checkUrl(root, "HTTPS://API.EXEMPLO.COM:443/x").empty());
    CHECK(nat::http::checkUrl(root, "https://sub.exemplo.org/x").empty());                    // curinga *.dominio
    CHECK(!nat::http::checkUrl(root, "https://exemplo.org/x").empty());                       // o proprio dominio nao e subdominio
    CHECK(!nat::http::checkUrl(root, "https://outro.com/x").empty());                         // "*" sozinho e ignorado
    CHECK(!nat::http::checkUrl(root, "http://api.exemplo.com/v1").empty());                   // so HTTPS
    CHECK(!nat::http::checkUrl(root, "https://api.exemplo.com:8443/v1").empty());             // so a porta 443
    CHECK(!nat::http::checkUrl(root, std::string("https://usuario") + ":" + "ficticia" + "@api.exemplo.com/").empty());   // credenciais na URL (texto de teste, montado em partes)
    CHECK(!nat::http::checkUrl(root, "https://api.exemplo.com.atacante.com/").empty());       // nao basta "conter" o host
    CHECK(!nat::http::checkUrl(root, "https://api.exemplo.com/a b").empty());

    // destinos locais e privados sao recusados MESMO se alguem os colocar na lista (SSRF)
    for (const char* u : {"https://10.0.0.5/x", "https://localhost/x", "https://127.0.0.1/x", "https://192.168.1.10/x",
                          "https://172.20.0.1/x", "https://169.254.169.254/latest/meta-data", "https://2130706433/x",
                          "https://[::1]/x", "https://servidor.local/x", "https://db.internal/x"}) {
        CHECK(!nat::http::checkUrl(root, u).empty());
    }

    // validacoes que acontecem antes de qualquer rede
    auto call = [&](const json& args) { return nat::call("arestrix.http", "http_get", args, nat::Context{root}); };
    CHECK(call({{"url", "https://api.exemplo.com/"}, {"headers", {{"Authorization", "Bearer abc"}}}}).content.find("bearer_env") != std::string::npos);
    CHECK(call({{"url", "https://api.exemplo.com/"}, {"headers", {{"Cookie", "a=b"}}}}).isError);
    CHECK(call({{"url", "https://api.exemplo.com/"}, {"headers", {{"X-Teste", "a\r\nHost: evil"}}}}).isError);
    CHECK(call({{"url", "https://api.exemplo.com/"}, {"bearer_env", "meu token"}}).isError);       // nao e um nome de variavel
    CHECK(call({{"url", "https://api.exemplo.com/"}, {"bearer_env", "ARX_TEST_TOKEN_NAO_DEFINIDO"}}).content.find("nao esta definida") != std::string::npos);
    fs::remove_all(root);
}

TEST(native_http_policy_is_validated_when_saved_and_tool_filter_accepts_qualified_names) {
    namespace nat = taskforge::native;
    auto root = makeTempDir("native-policy");
    std::string err;
    CHECK(nat::http::readPolicy(root).empty());
    CHECK(nat::http::writePolicy(root, {"API.Exemplo.com", " *.exemplo.org ", "api.exemplo.com", ""}, err) && err.empty());
    auto hosts = nat::http::readPolicy(root);
    CHECK(hosts.size() == 2 && hosts[0] == "api.exemplo.com" && hosts[1] == "*.exemplo.org");
    CHECK(nat::http::checkUrl(root, "https://api.exemplo.com/x").empty() && nat::http::checkUrl(root, "https://v1.exemplo.org/x").empty());
    CHECK(!nat::http::checkUrl(root, "https://outro.com/x").empty());
    for (const char* bad : {"https://a.com", "a.com/caminho", "a.com:443", "localhost", "192.168.0.1", "10.0.0.1", "2130706433", "*.com", "*", "*.*", "a..com", ".a.com", "a b.com", "a_b.com"}) {
        CHECK(!nat::http::writePolicy(root, {bad}, err) && !err.empty());
        err.clear();
    }
    CHECK(nat::http::readPolicy(root).size() == 2);                                    // as recusas nao alteraram o arquivo
    CHECK(nat::http::writePolicy(root, {}, err) && nat::http::readPolicy(root).empty());   // esvaziar fecha o acesso
    CHECK(!nat::http::checkUrl(root, "https://api.exemplo.com/x").empty());
    fs::remove_all(root);

    using TB = taskforge::rt::McpToolbox;
    CHECK(TB::isToolAllowed({}, "arestrix.dados", "json_query"));                        // vazia = todas
    CHECK(TB::isToolAllowed({"json_query"}, "arestrix.dados", "json_query"));            // nome simples (formato antigo)
    CHECK(!TB::isToolAllowed({"json_query"}, "arestrix.dados", "regex_extract"));
    CHECK(TB::isToolAllowed({"arestrix.dados/regex_extract"}, "arestrix.dados", "regex_extract"));
    CHECK(!TB::isToolAllowed({"arestrix.dados/regex_extract"}, "clientes", "regex_extract"));   // outro servidor, mesmo nome
    CHECK(TB::isToolAllowed({"clientes/*"}, "clientes", "qualquer"));
    CHECK(!TB::isToolAllowed({"clientes/*"}, "arestrix.dados", "json_query"));
}

TEST(net_guard_classifies_addresses_and_blocks_dns_rebinding) {
    namespace net = taskforge::rt::net;
    for (const char* ip : {"8.8.8.8", "1.1.1.1", "93.184.216.34", "172.32.0.1", "172.15.255.255", "100.63.0.1", "2606:4700:4700::1111", "2001:4860:4860::8888",
                           "::ffff:8.8.8.8", "64:ff9b::808:808", "2002:808:808::1"})
        CHECK(net::isPublicAddress(ip));
    for (const char* ip : {"127.0.0.1", "127.255.255.254", "0.0.0.0", "10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.1", "169.254.169.254",
                           "100.64.0.1", "100.127.255.255", "224.0.0.1", "255.255.255.255", "240.0.0.1", "192.0.2.1", "198.51.100.7", "203.0.113.9",
                           "198.18.0.1", "::", "::1", "fe80::1", "fc00::1", "fd12:3456::1", "ff02::1", "2001:db8::1", "::ffff:127.0.0.1", "::ffff:10.0.0.1",
                           "::ffff:169.254.169.254", "64:ff9b::7f00:1", "64:ff9b::a00:1", "2002:7f00:1::1", "2002:a9fe:a9fe::1", "2001::1", "100::1", "::127.0.0.1",
                           "nao e um ip", "", "999.1.1.1", "1.2.3"})
        CHECK(!net::isPublicAddress(ip));

    CHECK(net::hostOfUrl("https://API.Exemplo.com/x?y=1") == "api.exemplo.com");
    CHECK(net::hostOfUrl(std::string("https://u") + ":" + "x" + "@api.exemplo.com:443/x") == "api.exemplo.com");
    CHECK(net::hostOfUrl("https://[::1]:443/") == "::1");

    // resolvedor falso: sem rede e sem depender de DNS real
    std::map<std::string, net::Resolution> table;
    table["bom.exemplo.com"].addresses = {"93.184.216.34", "2606:4700:4700::1111"};
    table["interno.exemplo.com"].addresses = {"10.0.0.7"};
    table["misto.exemplo.com"].addresses = {"93.184.216.34", "127.0.0.1"};           // um publico e um interno: recusa
    table["mapeado.exemplo.com"].addresses = {"::ffff:192.168.0.1"};
    table["metadados.exemplo.com"].addresses = {"169.254.169.254"};
    table["vazio.exemplo.com"].error = "o nome 'vazio.exemplo.com' nao foi resolvido";
    net::setResolverForTest([&](const std::string& h) { auto it = table.find(h); return it == table.end() ? net::Resolution{{}, "sem registro"} : it->second; });

    std::vector<std::string> addrs;
    CHECK(net::checkHostIsPublic("bom.exemplo.com", &addrs).empty() && addrs.size() == 2);
    for (const char* h : {"interno.exemplo.com", "misto.exemplo.com", "mapeado.exemplo.com", "metadados.exemplo.com", "vazio.exemplo.com", "desconhecido.exemplo.com"})
        CHECK(!net::checkHostIsPublic(h).empty());
    CHECK(net::checkHostIsPublic("misto.exemplo.com").find("127.0.0.1") != std::string::npos);
    CHECK(net::checkHostIsPublic("8.8.8.8").empty() && !net::checkHostIsPublic("10.0.0.1").empty());   // literal: sem consulta de DNS

    // ponta a ponta nas ferramentas nativas: o host esta AUTORIZADO, mas o DNS aponta para dentro
    auto root = makeTempDir("native-dns");
    std::string err;
    CHECK(taskforge::native::http::writePolicy(root, {"interno.exemplo.com", "misto.exemplo.com", "metadados.exemplo.com"}, err));
    for (const char* h : {"interno.exemplo.com", "misto.exemplo.com", "metadados.exemplo.com"}) {
        auto r = nativeCall(root, "http", "http_get", {{"url", std::string("https://") + h + "/"}});
        CHECK(r.isError && r.content.find("recusad") != std::string::npos);
        auto p = nativeCall(root, "http", "http_post_json", {{"url", std::string("https://") + h + "/"}, {"json", json{{"a", 1}}}});
        CHECK(p.isError && p.content.find("recusad") != std::string::npos);
    }
    net::setResolverForTest(nullptr);
    fs::remove_all(root);

    // o resolvedor do sistema tambem recusa o que e local (funciona sem internet)
    CHECK(!net::checkHostIsPublic("localhost").empty());
}

TEST(native_http_reaches_a_real_public_host_when_online) {
    namespace net = taskforge::rt::net;
    net::setResolverForTest(nullptr);
    std::string e;
    if (!net::checkHostIsPublic("example.com").empty()) { std::cout << "  (sem internet ou DNS: teste ignorado)" << std::endl; return; }
    auto root = makeTempDir("native-online");
    CHECK(taskforge::native::http::writePolicy(root, {"example.com"}, e));
    auto r = nativeCall(root, "http", "http_get", {{"url", "https://example.com/"}});
    if (r.isError && r.content.find("transporte") != std::string::npos) { std::cout << "  (sem conexao: teste ignorado)" << std::endl; fs::remove_all(root); return; }
    CHECK(!r.isError && r.content.find("Example Domain") != std::string::npos);   // a verificacao do endereco conectado nao atrapalha o uso normal
    taskforge::rt::HttpRequest req;
    req.method = "GET";
    req.url = "https://example.com/";
    req.requirePublicPeer = true;
    req.noRedirects = true;
    auto resp = taskforge::rt::httpSend(req);
#ifdef _WIN32
    CHECK(resp.status == 200 && !resp.peer.empty() && net::isPublicAddress(resp.peer));    // o Windows informa o endereco conectado
#else
    CHECK(resp.status == 200);
#endif
    fs::remove_all(root);
}

TEST(native_data_tools_transform_json_csv_regex_and_base64) {
    auto root = makeTempDir("native-data");
    auto q = [&](const json& j, const std::string& path) { return nativeCall(root, "dados", "json_query", {{"json", j}, {"path", path}}); };
    json doc = {{"cliente", {{"nome", "Ana"}, {"enderecos", json::array({{{"cidade", "Recife"}}, {{"cidade", "Olinda"}}})}}}, {"n", 3}};
    CHECK(q(doc, "cliente.nome").content == "Ana");
    CHECK(q(doc, "cliente.enderecos[1].cidade").content == "Olinda");
    CHECK(q(doc, "cliente.enderecos.0.cidade").content == "Recife");
    CHECK(q(doc, "n").content == "3");
    CHECK(q(doc, "cliente.telefone").isError);
    CHECK(q(json(R"({"a":{"b":2}})"), "a.b").content == "2");                      // aceita o JSON como texto

    auto csv = nativeCall(root, "dados", "csv_to_json", {{"csv", "nome,obs\r\nAna,\"disse \"\"oi\"\", depois\nsaiu\"\nBruno,\r\n"}});
    json rows = taskforge::jsonutil::parse(csv.content);
    CHECK(rows.size() == 2 && rows[0]["nome"] == "Ana" && rows[0]["obs"] == "disse \"oi\", depois\nsaiu" && rows[1]["obs"] == "");
    auto back = nativeCall(root, "dados", "json_to_csv", {{"json", rows}});
    json again = taskforge::jsonutil::parse(nativeCall(root, "dados", "csv_to_json", {{"csv", back.content}}).content);
    CHECK(again == rows);                                                           // ida e volta preserva aspas, virgulas e quebras de linha
    CHECK(taskforge::jsonutil::parse(nativeCall(root, "dados", "csv_to_json", {{"csv", "a;b\n1;2"}, {"delimiter", ";"}}).content)[0]["b"] == "2");

    auto rx = [&](const json& args) { return nativeCall(root, "dados", "regex_extract", args); };
    CHECK(rx({{"text", "pedido 123 e 456"}, {"pattern", "\\d+"}}).content == "[\"123\"]");
    CHECK(rx({{"text", "pedido 123 e 456"}, {"pattern", "\\d+"}, {"all", true}}).content == "[\"123\",\"456\"]");
    CHECK(rx({{"text", "id=42;"}, {"pattern", "id=(\\d+)"}, {"group", 1}}).content == "[\"42\"]");
    CHECK(rx({{"text", "x"}, {"pattern", "(["}}).isError);                          // regex invalida: erro claro
    CHECK(rx({{"text", std::string(40, 'a') + "!"}, {"pattern", "(a+)+$"}}).isError);   // backtracking catastrofico: recusado
    CHECK(rx({{"text", "x"}, {"pattern", "(.*)*"}}).isError);
    CHECK(rx({{"text", "abc"}, {"pattern", "a"}, {"group", 5}}).isError);

    for (const std::string text : {std::string(""), std::string("a"), std::string("ab"), std::string("abc"), std::string("Ol\xC3\xA1, mundo!")}) {
        auto enc = nativeCall(root, "dados", "base64_encode", {{"text", text}});
        CHECK(nativeCall(root, "dados", "base64_decode", {{"data", enc.content}}).content == text);
    }
    CHECK(nativeCall(root, "dados", "base64_encode", {{"text", "Man"}}).content == "TWFu");
    CHECK(nativeCall(root, "dados", "base64_decode", {{"data", "@@@"}}).isError);
    fs::remove_all(root);
}

TEST(native_memory_and_time) {
    auto root = makeTempDir("native-mem");
    CHECK(!nativeCall(root, "memoria", "memory_set", {{"key", "cliente:ana"}, {"value", "plano ouro"}}).isError);
    CHECK(nativeCall(root, "memoria", "memory_get", {{"key", "cliente:ana"}}).content == "plano ouro");
    CHECK(nativeCall(root, "memoria", "memory_get", {{"key", "nao_existe"}}).isError);
    nativeCall(root, "memoria", "memory_set", {{"key", "cliente:bruno"}, {"value", "basico"}});
    nativeCall(root, "memoria", "memory_set", {{"key", "outro"}, {"value", "x"}});
    CHECK(nativeCall(root, "memoria", "memory_list", {{"prefix", "cliente:"}}).content == "[\"cliente:ana\",\"cliente:bruno\"]");
    CHECK(fs::exists(root / ".arestrix" / "memory.json"));                       // persiste no projeto
    CHECK(!nativeCall(root, "memoria", "memory_delete", {{"key", "outro"}}).isError);
    CHECK(nativeCall(root, "memoria", "memory_delete", {{"key", "outro"}}).isError);
    CHECK(nativeCall(root, "memoria", "memory_set", {{"key", "chave invalida!"}, {"value", "x"}}).isError);
    CHECK(nativeCall(root, "memoria", "memory_set", {{"key", "grande"}, {"value", std::string(70 * 1024, 'x')}}).isError);

    auto add = [&](const std::string& iso, const json& d) {
        json args = d;
        args["iso"] = iso;
        return taskforge::jsonutil::parse(nativeCall(root, "tempo", "add_duration", args).content)["iso"].str();
    };
    CHECK(add("2024-02-28T10:00:00Z", {{"days", 1}}) == "2024-02-29T10:00:00Z");              // ano bissexto
    CHECK(add("2024-02-28T10:00:00Z", {{"days", 2}}) == "2024-03-01T10:00:00Z");
    CHECK(add("2025-02-28", {{"days", 1}}) == "2025-03-01T00:00:00Z");                        // so data
    CHECK(add("2026-12-31T23:30:00-03:00", {{"minutes", 45}}) == "2027-01-01T00:15:00-03:00");   // mantem o fuso
    CHECK(add("2026-10-13T09:00:00Z", {{"hours", -10}}) == "2026-10-12T23:00:00Z");
    CHECK(nativeCall(root, "tempo", "add_duration", {{"iso", "ontem"}}).isError);
    json d = taskforge::jsonutil::parse(nativeCall(root, "tempo", "diff", {{"from", "2026-10-13T00:00:00Z"}, {"to", "2026-10-15T12:00:00Z"}}).content);
    CHECK(d["seconds"] == 216000 && d["days"].get<double>() == 2.5);
    CHECK(nativeCall(root, "tempo", "add_duration", {{"iso", "2026-10-13"}}).content.find("\"weekday\":\"terca\"") != std::string::npos);
    json now = taskforge::jsonutil::parse(nativeCall(root, "tempo", "now", json::object()).content);
    CHECK(now["iso"].str().size() == 20 && now["iso"].str().back() == 'Z' && now["epoch"].get<int64_t>() > 1700000000);
    json local = taskforge::jsonutil::parse(nativeCall(root, "tempo", "now", {{"timezone", "local"}}).content);
    CHECK(local["epoch"].get<int64_t>() > 1700000000);
    fs::remove_all(root);
}

TEST(native_git_is_read_only_and_rejects_option_injection) {
    if (!taskforge::platform::commandExists("git")) { std::cout << "  (git ausente: teste ignorado)" << std::endl; return; }
    auto root = makeTempDir("native-git");
    auto git = [&](std::vector<std::string> args) {
        taskforge::platform::ProcessSpec spec;
        spec.argv = {"git", "-C", root.string(), "-c", "user.email=t@example.com", "-c", "user.name=Teste", "-c", "commit.gpgsign=false"};
        for (auto& a : args) spec.argv.push_back(a);
        return taskforge::platform::run(spec, 20000).ok();
    };
    CHECK(git({"init", "-q"}));
    writeFile(root / "a.txt", "primeira versao\n");
    CHECK(git({"add", "."}) && git({"commit", "-q", "-m", "inicio"}));
    writeFile(root / "a.txt", "segunda versao\n");

    CHECK(nativeCall(root, "git", "git_status", json::object()).content.find("a.txt") != std::string::npos);
    CHECK(nativeCall(root, "git", "git_log", {{"max_count", 5}}).content.find("inicio") != std::string::npos);
    CHECK(nativeCall(root, "git", "git_diff", {{"path", "a.txt"}}).content.find("+segunda versao") != std::string::npos);
    CHECK(nativeCall(root, "git", "git_show_file", {{"rev", "HEAD"}, {"path", "a.txt"}}).content == "primeira versao\n");

    // injecao de opcao, caminhos invalidos e .git
    CHECK(nativeCall(root, "git", "git_show_file", {{"rev", "--output=pwned.txt"}, {"path", "a.txt"}}).isError);
    CHECK(nativeCall(root, "git", "git_show_file", {{"rev", "-p"}, {"path", "a.txt"}}).isError);
    CHECK(nativeCall(root, "git", "git_show_file", {{"rev", "HEAD"}, {"path", "../fora.txt"}}).isError);
    CHECK(nativeCall(root, "git", "git_show_file", {{"rev", "HEAD"}, {"path", ".git/config"}}).isError);
    CHECK(nativeCall(root, "git", "git_log", {{"path", "../fora"}}).isError);
    CHECK(!fs::exists(root / "pwned.txt"));
    fs::remove_all(root);
}

TEST(native_sqlite_works_and_is_locked_down) {
    auto root = makeTempDir("native-sql");
    auto exec = [&](const std::string& sql, const json& params = json::array(), const std::string& db = "") {
        json a = {{"sql", sql}, {"params", params}};
        if (!db.empty()) a["database"] = db;
        return nativeCall(root, "sqlite", "sql_execute", a);
    };
    auto query = [&](const std::string& sql, const json& params = json::array(), const std::string& db = "") {
        json a = {{"sql", sql}, {"params", params}};
        if (!db.empty()) a["database"] = db;
        return nativeCall(root, "sqlite", "sql_query", a);
    };
    CHECK(query("SELECT 1").isError);                                       // banco ainda nao existe: consulta nao o cria
    CHECK(!fs::exists(root / "data" / "arestrix.db"));
    CHECK(!exec("CREATE TABLE clientes (id INTEGER PRIMARY KEY, nome TEXT NOT NULL, email TEXT, saldo REAL)").isError);
    CHECK(fs::exists(root / "data" / "arestrix.db"));
    auto ins = exec("INSERT INTO clientes (nome, email, saldo) VALUES (?, ?, ?)", {"Ana", "ana@exemplo.com", 10.5});
    CHECK(!ins.isError && taskforge::jsonutil::parse(ins.content)["changes"] == 1 && taskforge::jsonutil::parse(ins.content)["last_insert_rowid"] == 1);
    exec("INSERT INTO clientes (nome, email) VALUES (?, ?)", {"Bruno", nullptr});
    json q = taskforge::jsonutil::parse(query("SELECT id, nome, email, saldo FROM clientes WHERE nome = ?", {"Ana"}).content);
    CHECK(q["row_count"] == 1 && q["rows"][0]["email"] == "ana@exemplo.com" && q["rows"][0]["saldo"].get<double>() == 10.5 && q["rows"][0]["id"] == 1);
    json all = taskforge::jsonutil::parse(query("SELECT nome, email FROM clientes ORDER BY id").content);
    CHECK(all["row_count"] == 2 && all["rows"][1]["email"].is_null());
    CHECK(taskforge::jsonutil::parse(nativeCall(root, "sqlite", "sql_query", {{"sql", "SELECT * FROM clientes"}, {"max_rows", 1}}).content)["truncated"] == true);
    CHECK(nativeCall(root, "sqlite", "list_tables", json::object()).content.find("\"clientes\"") != std::string::npos);
    CHECK(nativeCall(root, "sqlite", "describe_table", {{"table", "clientes"}}).content.find("\"nome\"") != std::string::npos);
    CHECK(nativeCall(root, "sqlite", "describe_table", {{"table", "nao_existe"}}).isError);

    // injecao: o texto vai como parametro, nunca vira SQL
    exec("INSERT INTO clientes (nome) VALUES (?)", {"x'); DROP TABLE clientes; --"});
    CHECK(taskforge::jsonutil::parse(query("SELECT COUNT(*) AS n FROM clientes").content)["rows"][0]["n"] == 3);

    // regras de uso
    CHECK(query("INSERT INTO clientes (nome) VALUES ('a')").isError);                 // consulta nao escreve
    CHECK(query("DELETE FROM clientes").isError);
    CHECK(query("SELECT 1; DROP TABLE clientes").isError);                             // um comando por chamada
    CHECK(exec("SELECT 1; SELECT 2").isError);
    CHECK(query("SELECT * FROM clientes WHERE id = ?", json::array()).isError);        // numero de parametros confere
    CHECK(query("SELECT * FROM nao_existe").isError);
    CHECK(exec("").isError);

    // o que o autorizador recusa: sair do arquivo, mudar configuracao, gatilhos, transacoes
    CHECK(exec("ATTACH DATABASE 'outro.db' AS o").isError);
    CHECK(exec("ATTACH DATABASE '../fora.db' AS o").isError);
    CHECK(exec("PRAGMA writable_schema = ON").isError);
    CHECK(exec("PRAGMA journal_mode = OFF").isError);
    CHECK(query("PRAGMA table_info(clientes)").isError);
    CHECK(exec("CREATE VIRTUAL TABLE v USING fts5(a)").isError);
    CHECK(exec("CREATE TRIGGER t AFTER INSERT ON clientes BEGIN DELETE FROM clientes; END").isError);
    CHECK(exec("BEGIN").isError);
    CHECK(exec("CREATE TEMP TABLE t (a)").isError);
    CHECK(query("SELECT load_extension('x')").isError);
    CHECK(!fs::exists(root / "outro.db") && !fs::exists(root.parent_path() / "fora.db"));
    CHECK(query("SELECT \"clientes\" FROM clientes").isError);                          // aspas duplas nao viram texto (DQS=0)

    // o arquivo: so dentro do projeto, so extensoes de banco, nunca em lugares protegidos
    CHECK(exec("CREATE TABLE t (a)", json::array(), "../fora.db").isError);
    CHECK(exec("CREATE TABLE t (a)", json::array(), "dados.txt").isError);
    CHECK(exec("CREATE TABLE t (a)", json::array(), "pipelines/x.db").isError);
    CHECK(exec("CREATE TABLE t (a)", json::array(), ".git/x.db").isError);
    CHECK(exec("CREATE TABLE t (a)", json::array(), ".workspace/x.db").isError);
    CHECK(!exec("CREATE TABLE t (a)", json::array(), "dados/outro.sqlite").isError);   // outro banco no projeto: permitido
    CHECK(fs::exists(root / "dados" / "outro.sqlite"));

    // limite de tempo: uma consulta que nunca termina e interrompida
    auto t0 = std::chrono::steady_clock::now();
    auto slow = query("WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c) SELECT count(*) FROM c");
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(slow.isError && secs < 8.0);
    fs::remove_all(root);
}

TEST(native_servers_work_as_mcp_vertices_and_agent_tools_without_any_process) {
    namespace nat = taskforge::native;
    using namespace taskforge::rt;
    taskforge::mcp::McpClient::shutdownSessions();
    auto root = makeTempDir("native-wire");
    fs::path oldCwd = fs::current_path();
    fs::current_path(root);

    // vertice MCP chamando uma ferramenta nativa, com a entrada vinda de um arquivo
    writeFile(root / "entrada.json", R"({"cliente":{"nome":"Ana"}})");
    callMcpTool("n1", "Nome", R"({"server":"arestrix.dados","tool":"json_query",
        "arguments":{"json":"{{input}}","path":"cliente.nome"},"source":{"kind":"file","path":"entrada.json"},"output":{}})");
    CHECK(taskforge::jsonutil::readFile(root / ".arestrix" / "out" / "n1.txt") == "Ana");
    CHECK(taskforge::mcp::McpClient::activeSessions() == 0);                 // nenhum processo foi aberto

    // erro de ferramenta nativa interrompe o pipeline
    bool threw = false;
    try { callMcpTool("n2", "Falha", R"({"server":"arestrix.arquivos","tool":"read_file","arguments":{"path":"../fora"},"source":{"kind":"none"}})"); }
    catch (const std::exception& ex) { threw = std::string(ex.what()).find("falhou") != std::string::npos; }
    CHECK(threw);

    // caixa de ferramentas dos agentes: os nomes seguem mcp__<servidor>__<ferramenta>
    McpToolbox box(root, {"arestrix.tempo", "arestrix.dados"}, {"now", "base64_encode"});
    CHECK(box.definitions().size() == 2);
    CHECK(box.owns("mcp__arestrix_tempo__now") && box.owns("mcp__arestrix_dados__base64_encode") && !box.owns("mcp__arestrix_dados__json_query"));
    CHECK(box.call("mcp__arestrix_dados__base64_encode", {{"text", "Man"}}).content == "TWFu");
    McpToolbox all(root, {"arestrix.arquivos"}, {});
    CHECK(all.definitions().size() == nat::findServer("arestrix.arquivos")->tools.size());
    CHECK(all.definitions()[0].description.find("[Arestrix") == 0);
    CHECK(taskforge::mcp::McpClient::activeSessions() == 0);

    // a lista que a interface mostra: nativos primeiro, e o prefixo e reservado
    json listed = McpToolbox::listProjectServers(root);
    CHECK(listed.size() >= 6 && listed[0]["isNative"] == true && listed[0]["transport"] == "native");
    bool reserved = false;
    try { McpToolbox::loadServerConfig(root, "arestrix.arquivos"); } catch (const std::exception&) { reserved = true; }
    CHECK(reserved);
    bool unknown = false;
    try { McpToolbox bad(root, {"arestrix.nao_existe"}, {}); } catch (const std::exception&) { unknown = true; }
    CHECK(unknown);

    fs::current_path(oldCwd);
    fs::remove_all(root);
}

TEST(native_calls_are_orders_of_magnitude_faster_than_mcp_over_a_process) {
    using taskforge::mcp::McpClient;
    namespace nat = taskforge::native;
    auto root = makeTempDir("native-bench");
    const int nativeCalls = 3000;
    json args = {{"json", R"({"a":{"b":[1,2,3]}})"}, {"path", "a.b[2]"}};
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < nativeCalls; ++i) nat::call("arestrix.dados", "json_query", args, nat::Context{root});
    double nativeUs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / nativeCalls * 1e6;
    std::cout << "  ferramenta nativa: " << nativeUs << " us por chamada" << std::endl;
    CHECK(nativeUs < 500);

    if (taskforge::platform::commandExists("python") || taskforge::platform::commandExists("python3")) {
        McpClient::shutdownSessions();
        auto cfg = countingServer(root / "starts.log");
        McpClient client;
        client.callTool(cfg, "echo", {{"text", "x"}}, 20000);                       // abre a sessao fora da medicao
        const int mcpCalls = 100;
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < mcpCalls; ++i) client.callTool(cfg, "echo", {{"text", "x"}}, 20000);
        double mcpUs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / mcpCalls * 1e6;
        McpClient::shutdownSessions();
        std::cout << "  MCP externo (sessao persistente): " << mcpUs << " us por chamada (" << mcpUs / nativeUs << "x mais lento)" << std::endl;
        CHECK(nativeUs * 10 < mcpUs);
    }
    fs::remove_all(root);
}

TEST(open_screen_lists_folders_projects_packages_and_graphs) {
    namespace prj = taskforge::project;
    auto root = makeTempDir("open-list");
    fs::create_directories(root / "zeta-pasta");
    fs::create_directories(root / "Alfa-projeto" / "pipelines");
    { std::ofstream(root / "Alfa-projeto" / "arestrix.project.json") << "{}"; }
    fs::create_directories(root / prj::pathFromUtf8("Programação"));
    fs::create_directories(root / ".oculta");
    { std::ofstream(root / "pacote.arestrix") << "x"; }
    { std::ofstream(root / "grafo.json") << "{}"; }
    { std::ofstream(root / "arestrix.project.json") << "{}"; }
    { std::ofstream(root / "notas.txt") << "x"; }

    auto listing = prj::listOpenableEntries(root);
    CHECK(listing["exists"].get<bool>());
    CHECK(!listing["truncated"].get<bool>());
    CHECK(listing["path"].get<std::string>() == prj::pathToUtf8(root.lexically_normal()));
    CHECK(listing["parent"].get<std::string>() == prj::pathToUtf8(root.lexically_normal().parent_path()));

    // Pastas primeiro, depois arquivos; ocultos e extensoes desconhecidas ficam de fora.
    std::vector<std::string> names, kinds;
    std::string accentedPath;
    for (const auto& e : listing["entries"]) {
        names.push_back(e.value("name", std::string()));
        kinds.push_back(e.value("kind", std::string()));
        CHECK(e.value("modified", int64_t(0)) > 0);
        if (e.value("name", std::string()) == "Programação") accentedPath = e.value("path", std::string());
    }
    CHECK((names == std::vector<std::string>{"Alfa-projeto", "Programação", "zeta-pasta",
                                             "arestrix.project.json", "grafo.json", "pacote.arestrix"}));
    CHECK((kinds == std::vector<std::string>{"project", "folder", "folder", "manifest", "json", "package"}));
    CHECK(prj::isProjectDirectory(root / "Alfa-projeto"));
    CHECK(!prj::isProjectDirectory(root / "zeta-pasta"));

    // O caminho com acento volta da interface em UTF-8 e continua navegavel.
    auto inner = prj::listOpenableEntries(prj::pathFromUtf8(accentedPath));
    CHECK(inner["exists"].get<bool>());
    CHECK(inner["entries"].size() == 0);

    // Barra final, pasta inexistente, limite de itens e raiz da unidade.
    auto withSlash = prj::listOpenableEntries(prj::pathFromUtf8(prj::pathToUtf8(root) + "/"));
    CHECK(withSlash["path"].get<std::string>() == listing["path"].get<std::string>());
    auto missing = prj::listOpenableEntries(root / "nao-existe");
    CHECK(!missing["exists"].get<bool>());
    CHECK(missing["entries"].size() == 0);
    auto limited = prj::listOpenableEntries(root, 2);
    CHECK(limited["truncated"].get<bool>());
    CHECK(limited["entries"].size() == 2);
    CHECK(prj::listOpenableEntries(root.root_path())["parent"].get<std::string>().empty());

    fs::remove_all(root);
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
