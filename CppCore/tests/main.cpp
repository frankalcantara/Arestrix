// Testes do nucleo C++ do Arestrix (sem framework externo).
// Executar: ctest --test-dir build  (ou ./build/arestrix_tests)

#include <algorithm>
#include <atomic>
#include <cstdlib>
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
#include "memory/MemoryStore.hpp"
#include "memory/MemoryService.hpp"
#include "platform/Process.hpp"
#include "project/OpenableFiles.hpp"
#include "project/GitSyncService.hpp"
#include "project/RunnerService.hpp"
#include "project/CorporateVault.hpp"
#include "project/RunnerSecurity.hpp"
#include "project/RunnerPackaging.hpp"
#include "assistant/AssistantSession.hpp"
#include "assistant/AssistantTools.hpp"
#include "assistant/GraphDocument.hpp"
#include "rt/ModelClient.hpp"
#include "rt/CallGraph.hpp"
#include "rt/Interaction.hpp"
#include "local/LocalRuntime.hpp"
#include "gateway/GatewayStore.hpp"
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
// ---------------------------------------------------------------------------
// Idioma do agente em runtime: skill + prompt definem o idioma do scaffolding
// ---------------------------------------------------------------------------
TEST(runtime_lang_detection_and_bilingual_tools) {
    using namespace taskforge::rt;

    // Detector: sinais fortes de cada lingua; vazio, sem sinais ou empate = Pt (padrao do projeto).
    CHECK(detectRuntimeLang("") == RuntimeLang::Pt);
    CHECK(detectRuntimeLang("Voce deve ler o arquivo e corrigir a tarefa conforme as instrucoes desta skill.") == RuntimeLang::Pt);
    CHECK(detectRuntimeLang("You must read the file and fix the task according to the instructions of this skill.") == RuntimeLang::En);
    CHECK(detectRuntimeLang(std::vector<std::string>{}) == RuntimeLang::Pt);
    CHECK(detectRuntimeLang(std::vector<std::string>{"", "You should read the file and write the output."}) == RuntimeLang::En);
    CHECK(detectRuntimeLang(std::vector<std::string>{"deve ler o arquivo", "the file and"}) == RuntimeLang::Pt);   // empate: Pt

    // Ferramentas built-in: descricoes no idioma do agente.
    auto ptTools = toolDefinitions({"read", "bash"}, RuntimeLang::Pt);
    CHECK(ptTools.size() == 2 && ptTools[0].description.rfind("Le um arquivo", 0) == 0);
    auto enTools = toolDefinitions({"read", "bash"}, RuntimeLang::En);
    CHECK(enTools.size() == 2 && enTools[0].description.rfind("Reads a text file", 0) == 0);

    // validate_svg: mensagens no idioma pedido.
    CHECK(!validateSvg("<svg><rect/></svg>", RuntimeLang::Pt).empty());
    CHECK(!validateSvg("<svg><rect/></svg>", RuntimeLang::En).empty());

    // Catalogo nativo por idioma (descricoes e esquemas localizados).
    const std::string httpId = std::string(taskforge::native::kIdPrefix) + "http";
    const auto* ptHttp = taskforge::native::findServer(httpId);
    CHECK(ptHttp && ptHttp->description.find("autorizou") != std::string::npos);
    bool enDesc = false;
    for (const auto& s : taskforge::native::serversForLang(RuntimeLang::En))
        if (s.id == httpId) enDesc = s.description.find("authorized") != std::string::npos;
    CHECK(enDesc);

    // Ferramenta nativa com contexto em ingles devolve o erro em ingles.
    taskforge::native::Context enCtx;
    enCtx.workspaceRoot = fs::current_path();
    enCtx.lang = RuntimeLang::En;
    auto r = taskforge::native::call(httpId, "http_get", json{{"url", "http://x"}}, enCtx);
    CHECK(r.isError && r.content.find("https") != std::string::npos);
    auto rf = taskforge::native::call(std::string(taskforge::native::kIdPrefix) + "arquivos", "read_file",
                                      json{{"path", "nao-existe-x.txt"}}, enCtx);
    CHECK(rf.isError && rf.content.find("File not found") != std::string::npos);
}

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

TEST(generator_call_edges_are_not_flow) {
    using taskforge::compiler::TaskflowGenerator;
    auto base = tutorialGraph();
    json schema = base.toJson();

    auto port = [](const char* id, const char* dir, const char* type) {
        return json{{"id", id}, {"name", id}, {"direction", dir}, {"type", type},
                    {"dataType", ""}, {"conditionBranchIndex", -1}};
    };
    json specialist = {
        {"id", "specialist"}, {"label", "Specialist"}, {"type", "agent"},
        {"position", {{"x", 1200}, {"y", 400}}},
        {"inputs", json::array({port("specialist_in", "input", "flow")})},
        {"outputs", json::array({port("specialist_out", "output", "flow")})},
        {"parameters", json::object()},
        {"customLambdaBody", ""},
        {"agentSettings", {{"inheritModel", true}, {"inheritSandbox", true}, {"inheritCodeMode", true}}}
    };
    schema["nodes"].push_back(specialist);
    schema["edges"].push_back(json{
        {"id", "call1"}, {"sourceNodeId", "gen"}, {"sourcePortId", "out"},
        {"targetNodeId", "specialist"}, {"targetPortId", "specialist_in"},
        {"kind", "call"},
        {"callSettings", {{"maxCalls", 2}, {"timeoutSeconds", 30},
                          {"onError", "fail"}, {"description", "use specialist"}}}
    });

    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(g);

    CHECK(src.find("arx_callables") != std::string::npos);
    CHECK(src.find("arx_callers") != std::string::npos);
    CHECK(src.find("taskforge::rt::setCallRegistry(arx_callables, arx_callers)") != std::string::npos);
    CHECK(src.find("\"specialist\"") != std::string::npos);
    CHECK(src.find("\"label\":\"Specialist\"") != std::string::npos);
    CHECK(src.find("\"maxCalls\":2") != std::string::npos);
    CHECK(src.find("task_specialist") == std::string::npos);          // chamavel puro nao vira task
    CHECK(src.find("task_gen.precede(task_specialist)") == std::string::npos);
    CHECK(src.find("task_gen.precede(task_a)") != std::string::npos); // fluxo existente preservado

    // Chamada saindo de um no estatico deve falhar no gerador.
    auto& callEdge = schema["edges"][schema["edges"].size() - 1];
    callEdge["sourceNodeId"] = "a";
    taskforge::core::Graph bad;
    bad.fromJson(schema);
    bool threw = false;
    try {
        (void)TaskflowGenerator::generateSource(bad);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(call_registry_and_tool_definition) {
    using namespace taskforge::rt;
    setCallRegistry(
        R"({"specialist":{"kind":"agent","label":"Specialist","spec":{"provider":"deepseek","model":"x"}}})",
        R"({"caller":[{"target":"specialist","label":"Specialist","kind":"agent","maxCalls":2,"timeoutSeconds":10,"onError":"fail","description":"use specialist"}]})");

    auto targets = callTargetsFor("caller");
    CHECK(targets.size() == 1);
    CHECK(targets[0].id == "specialist");
    CHECK(targets[0].maxCalls == 2);
    CHECK(targets[0].description == "use specialist");
    CHECK(callTargetsFor("unknown").empty());

    auto defs = callToolDefinitions(targets, RuntimeLang::Pt);
    CHECK(defs.size() == 1);
    CHECK(defs[0].name == "call_agent");
    const std::string schema = defs[0].parameters.dump();
    CHECK(schema.find("\"specialist\"") != std::string::npos);

    // Alvo inexistente falha antes de qualquer chamada ao modelo.
    auto res = invokeCall("caller", "run", targets,
                          taskforge::Json{{"target", "missing"}, {"input", "x"}}, RuntimeLang::Pt);
    CHECK(res.isError);

    // Nao interfere em outros testes que usem runAgent.
    setCallRegistry("{}", "{}");
}

TEST(interaction_node_is_authored_and_compiles) {
    using taskforge::compiler::TaskflowGenerator;
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "Interaction"}, {"description", ""}, {"targetStandard", "C++20"},
                      {"version", "1"}, {"projectId", "test"}}},
        {"nodes", json::array({
            json{{"id", "ask"}, {"label", "Pergunta"}, {"type", "interaction"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array({json{{"id", "ask_in"}, {"name", "in"}, {"direction", "input"}, {"type", "flow"}}})},
                 {"outputs", json::array({json{{"id", "ask_out"}, {"name", "out"}, {"direction", "output"}, {"type", "flow"}}})},
                 {"parameters", json::object()},
                 {"customLambdaBody", ""},
                 {"interactionSettings", json{
                     {"mode", "form"}, {"schemaSource", "static"},
                     {"form", json{{"title", "Pergunta"}, {"fields", json::array({
                         json{{"id", "resposta"}, {"type", "text"}, {"label", "Resposta"}}
                     })}}},
                     {"channel", "any"}, {"wait", true}, {"timeoutSeconds", 300},
                     {"onTimeout", "fail"}, {"who", "any"}, {"sensitive", false}
                 }}}
        })},
        {"edges", json::array()}
    };

    taskforge::core::Graph graph;
    graph.fromJson(schema);
    const auto* ask = graph.getNode("ask");
    CHECK(ask != nullptr);
    CHECK(ask->type == taskforge::core::TaskflowNodeType::Interaction);
    CHECK(ask->interactionSettings.contains("form"));

    // O gerador emite a chamada ao runtime de interacao (canal headless na 2B1).
    std::string src = TaskflowGenerator::generateSource(graph);
    CHECK(src.find("taskforge::rt::runInteraction(\"ask\"") != std::string::npos);
}

TEST(interaction_runtime_reads_answers_file) {
    using namespace taskforge::rt;
    const fs::path oldCwd = fs::current_path();
    const fs::path dir = makeTempDir("interaction-run");
    const fs::path answers = dir / "answers.json";
    {
        std::ofstream out(answers);
        out << R"({"ask":{"tipo":"pj","obs":"ok"},"blank":{"tipo":"   "}})";
    }

    const std::string oldEnv = taskforge::platform::getEnv("ARESTRIX_INTERACTION_ANSWERS");
    taskforge::platform::setProcessEnv("ARESTRIX_INTERACTION_ANSWERS", answers.string());
    const std::string oldRunId = taskforge::platform::getEnv("ARESTRIX_RUN_ID");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", "run-test-audit");
    fs::current_path(dir);

    const std::string spec = R"({
        "mode":"form","schemaSource":"static",
        "form":{"title":"Pergunta","fields":[
            {"id":"tipo","type":"choice","label":"Tipo","required":true,"options":[{"value":"pf"},{"value":"pj"}]},
            {"id":"obs","type":"text","label":"Observacao","required":false}
        ]},
        "channel":"any","timeoutSeconds":300,"onTimeout":"fail","sensitive":false
    })";

    std::string result = runInteraction("ask", "Pergunta", spec);
    CHECK(result.find("\"tipo\":\"pj\"") != std::string::npos);
    CHECK(fs::is_regular_file(dir / ".arestrix" / "out" / "ask.txt"));
    CHECK(fs::is_regular_file(dir / ".arestrix" / "out" / "ask.json"));

    std::ifstream txt(dir / ".arestrix" / "out" / "ask.txt");
    std::string content((std::istreambuf_iterator<char>(txt)), std::istreambuf_iterator<char>());
    txt.close();
    CHECK(content.find("Tipo: pj") != std::string::npos);
    CHECK(fs::is_directory(dir / ".arestrix" / "runs" / "run-test-audit" / "interactions"));
    int auditFiles = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dir / ".arestrix" / "runs")) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            ++auditFiles;
            std::ifstream audit(entry.path());
            std::string auditText((std::istreambuf_iterator<char>(audit)), std::istreambuf_iterator<char>());
            CHECK(auditText.find("\"status\": \"ok\"") != std::string::npos);
            CHECK(auditText.find("\"channel\": \"answers\"") != std::string::npos);
            CHECK(auditText.find("tipo") != std::string::npos);
        }
    }
    CHECK(auditFiles >= 1);

    // Sem resposta para o no, o canal falha fechado.
    bool threw = false;
    try {
        (void)runInteraction("ask2", "Sem respostas", spec);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // Campo obrigatório em branco também falha fechado.
    bool blankThrew = false;
    try {
        (void)runInteraction("blank", "Em branco", spec);
    } catch (const std::exception&) {
        blankThrew = true;
    }
    CHECK(blankThrew);

    fs::current_path(oldCwd);
    taskforge::platform::setProcessEnv("ARESTRIX_INTERACTION_ANSWERS", oldEnv);
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", oldRunId);
    fs::remove_all(dir);
}

TEST(interaction_runtime_via_call_agent) {
    using namespace taskforge::rt;
    const fs::path oldCwd = fs::current_path();
    const fs::path dir = makeTempDir("interaction-call");
    const fs::path answers = dir / "answers.json";
    {
        std::ofstream out(answers);
        out << R"({"ask":{"tipo":"pj"}})";
    }

    const std::string oldEnv = taskforge::platform::getEnv("ARESTRIX_INTERACTION_ANSWERS");
    taskforge::platform::setProcessEnv("ARESTRIX_INTERACTION_ANSWERS", answers.string());
    fs::current_path(dir);

    setCallRegistry(
        R"({"ask":{"kind":"interaction","label":"Pergunta","spec":{"schemaSource":"static","form":{"title":"Pergunta","fields":[{"id":"tipo","type":"choice","label":"Tipo","required":true,"options":[{"value":"pf"},{"value":"pj"}]}]},"onTimeout":"fail"}}})",
        R"({"caller":[{"target":"ask","label":"Pergunta","kind":"interaction","maxCalls":1,"onError":"fail","description":"ask"}]})");

    auto targets = callTargetsFor("caller");
    auto res = invokeCall("caller", "run", targets,
                          taskforge::Json{{"target", "ask"}, {"input", "oi"}}, RuntimeLang::Pt);
    CHECK(!res.isError);
    CHECK(res.content.find("\"answers\"") != std::string::npos);
    CHECK(res.content.find("\"tipo\":\"pj\"") != std::string::npos);

    setCallRegistry("{}", "{}");
    fs::current_path(oldCwd);
    taskforge::platform::setProcessEnv("ARESTRIX_INTERACTION_ANSWERS", oldEnv);
    fs::remove_all(dir);
}

TEST(interaction_host_protocol_roundtrip) {
    const fs::path dir = makeTempDir("interaction-host");
    taskforge::platform::ProcessSpec spec;
    spec.argv = {taskforge::platform::executablePath().string(), "--interaction-host-child"};
    spec.workingDir = dir;
    spec.captureStdin = true;
    spec.env["ARESTRIX_INTERACTION_HOST"] = "1";
    spec.env["ARESTRIX_INTERACTION_ANSWERS"] = "";

    std::string error;
    auto proc = taskforge::platform::Process::start(spec, error);
    CHECK(proc != nullptr);
    if (!proc) {
        fs::remove_all(dir);
        return;
    }

    std::string pending;
    bool gotRequest = false;
    bool sentAnswer = false;
    int code = -1;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string chunk;
        auto status = proc->read(chunk, 100);
        if (!chunk.empty()) {
            pending += chunk;
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string prefix = "[ARX_INTERACTION] ";
                if (line.rfind(prefix, 0) == 0) {
                    auto request = taskforge::jsonutil::tryParse(line.substr(prefix.size()));
                    CHECK(request && request->is_object());
                    if (request && request->is_object()) {
                        CHECK(request->value("nodeId", std::string()) == "ask");
                        CHECK(request->value("timeoutSeconds", 0) == 5);
                        CHECK(request->contains("form"));
                        json answer{{"requestId", request->value("requestId", std::string())},
                                    {"answers", json{{"tipo", "pj"}, {"obs", "host"}}}};
                        sentAnswer = proc->writeStdin("[ARX_ANSWER] " + answer.dump() + "\n");
                        gotRequest = true;
                    }
                }
            }
        }
        if (status == taskforge::platform::ReadStatus::Eof) break;
    }

    proc->wait(5000, code);
    CHECK(gotRequest);
    CHECK(sentAnswer);
    CHECK(code == 0);
    CHECK(fs::is_regular_file(dir / ".arestrix" / "out" / "ask.txt"));
    fs::remove_all(dir);
}

TEST(interaction_host_timeout_fails_closed) {
    const fs::path dir = makeTempDir("interaction-host-timeout");
    taskforge::platform::ProcessSpec spec;
    spec.argv = {taskforge::platform::executablePath().string(), "--interaction-host-child-timeout"};
    spec.workingDir = dir;
    spec.captureStdin = true;
    spec.env["ARESTRIX_INTERACTION_HOST"] = "1";
    spec.env["ARESTRIX_INTERACTION_ANSWERS"] = "";

    std::string error;
    auto proc = taskforge::platform::Process::start(spec, error);
    CHECK(proc != nullptr);
    if (!proc) {
        fs::remove_all(dir);
        return;
    }

    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string chunk;
        auto status = proc->read(chunk, 100);
        output += chunk;
        if (status == taskforge::platform::ReadStatus::Eof) break;
    }
    int code = -1;
    proc->wait(5000, code);
    CHECK(code != 0);
    CHECK(output.find("tempo esgotado") != std::string::npos);
    fs::remove_all(dir);
}

TEST(generator_interaction_as_callable) {
    using taskforge::compiler::TaskflowGenerator;
    auto port = [](const char* id, const char* dir, const char* type) {
        return json{{"id", id}, {"name", id}, {"direction", dir}, {"type", type},
                    {"dataType", ""}, {"conditionBranchIndex", -1}};
    };
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "InteractionCall"}, {"description", ""}, {"targetStandard", "C++20"},
                      {"version", "1"}, {"projectId", "test"}}},
        {"nodes", json::array({
            json{{"id", "caller"}, {"label", "Caller"}, {"type", "agent"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array({port("caller_in", "input", "flow")})},
                 {"outputs", json::array({port("caller_out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}},
            json{{"id", "ask"}, {"label", "Pergunta"}, {"type", "interaction"},
                 {"position", {{"x", 300}, {"y", 0}}},
                 {"inputs", json::array({port("ask_in", "input", "flow")})},
                 {"outputs", json::array({port("ask_out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"interactionSettings", json{
                     {"schemaSource", "static"},
                     {"form", json{{"title", "Pergunta"}, {"fields", json::array({
                         json{{"id", "tipo"}, {"type", "choice"}, {"label", "Tipo"}, {"options", json::array({
                             json{{"value", "pf"}}, json{{"value", "pj"}}
                         })}}
                     })}}},
                     {"channel", "any"}, {"onTimeout", "fail"}
                 }}}
        })},
        {"edges", json::array({
            json{{"id", "call_ask"}, {"sourceNodeId", "caller"}, {"sourcePortId", "caller_out"},
                 {"targetNodeId", "ask"}, {"targetPortId", "ask_in"}, {"kind", "call"},
                 {"callSettings", {{"maxCalls", 1}, {"onError", "fail"}, {"description", "ask"}}}}
        })}
    };

    taskforge::core::Graph graph;
    graph.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(graph);
    CHECK(src.find("taskforge::rt::setCallRegistry(arx_callables, arx_callers)") != std::string::npos);
    CHECK(src.find("\"kind\":\"interaction\"") != std::string::npos);
    CHECK(src.find("task_ask") == std::string::npos);   // chamavel puro nao vira task de fluxo
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

TEST(generator_output_vertex_copies_artifact_with_non_empty_body) {
    using taskforge::compiler::TaskflowGenerator;
    auto port = [](const char* id, const char* name, const char* dir, const char* type) {
        return json{{"id", id}, {"name", name}, {"direction", dir}, {"type", type}};
    };
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "output"}, {"description", ""}, {"targetStandard", "C++20"},
                      {"compilerConfig", {{"compiler", "msvc"}, {"cppStandard", "C++20"}, {"buildSystem", "cmake"},
                                          {"taskflowIncludePath", "third_party/taskflow"}, {"outputBinaryName", "o"},
                                          {"enableOpenMP", false}, {"enableCuda", false}, {"extraFlags", json::array()}}},
                      {"version", "1"}, {"projectId", "output"}}},
        {"nodes", json::array({
            json{{"id", "origem"}, {"label", "Origem"}, {"type", "static"}, {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array()}, {"outputs", json::array({port("origem_out", "out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}},
            json{{"id", "entrega"}, {"label", "Entrega"}, {"type", "output"}, {"position", {{"x", 300}, {"y", 0}}},
                 {"inputs", json::array({port("entrega_in", "in", "input", "flow"),
                                        port("entrega_data", "data_in", "input", "data")})},
                 {"outputs", json::array({port("entrega_out", "out", "output", "flow")})},
                 {"parameters", json::object()},
                 // corpo padrao de um vertice criado na interface: nao copia por conta propria
                 {"customLambdaBody", "std::cout << \"corpo do usuario\\n\";"},
                 {"outputSettings", {{"sourcePath", "workspace/a.md"}, {"destinationPath", "exports/a.md"},
                                     {"artifactType", "markdown"}, {"displayName", "A"}}}}
                 })},
        {"edges", json::array({json{{"id", "e1"}, {"sourceNodeId", "origem"}, {"sourcePortId", "origem_out"},
                                    {"targetNodeId", "entrega"}, {"targetPortId", "entrega_in"}}})}};
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(g);

    CHECK(src.find("std::string arx_out_src = \"workspace/a.md\";") != std::string::npos);
    CHECK(src.find("std::string arx_out_dst = \"exports/a.md\";") != std::string::npos);
    CHECK(src.find("std::filesystem::copy_file(arx_out_src, arx_out_dst") != std::string::npos);
    CHECK(src.find("if (!arx_out_parent.empty()) std::filesystem::create_directories(arx_out_parent);") != std::string::npos);
    CHECK(src.find("corpo do usuario") != std::string::npos);            // corpo do usuario preservado
    CHECK(src.find("[OUTPUT_DELIVERY]") != std::string::npos);           // registrado uma vez para a interface
}

TEST(generator_injects_memory_context_for_enabled_agent) {
    using taskforge::compiler::TaskflowGenerator;
    auto port = [](const char* id, const char* name, const char* dir, const char* type) {
        return json{{"id", id}, {"name", name}, {"direction", dir}, {"type", type}};
    };
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "memory"}, {"description", ""}, {"targetStandard", "C++20"},
                      {"compilerConfig", {{"compiler", "msvc"}, {"cppStandard", "C++20"}, {"buildSystem", "cmake"},
                                          {"taskflowIncludePath", "third_party/taskflow"}, {"outputBinaryName", "m"},
                                          {"enableOpenMP", false}, {"enableCuda", false}, {"extraFlags", json::array()}}},
                      {"version", "1"}, {"projectId", "proj-memoria"}}},
        {"nodes", json::array({
            json{{"id", "origem"}, {"label", "Origem"}, {"type", "static"}, {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array()}, {"outputs", json::array({port("origem_out", "out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}},
            json{{"id", "agente"}, {"label", "Agente"}, {"type", "agent"}, {"position", {{"x", 300}, {"y", 0}}},
                 {"inputs", json::array({port("agente_in", "in", "input", "flow")})},
                 {"outputs", json::array({port("agente_out", "out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"agentSettings", {{"inheritMemory", false},
                                    {"memory", {{"enabled", true}, {"persistent", true},
                                                {"readBlackboard", true}, {"writeBlackboard", true},
                                                {"semanticLessons", true}, {"recordLessons", true},
                                                {"allowedKeys", json::array({"resumo"})},
                                                {"domains", json::array({"svg_typography"})}}}}}}
                 })},
        {"edges", json::array({json{{"id", "e1"}, {"sourceNodeId", "origem"}, {"sourcePortId", "origem_out"},
                                    {"targetNodeId", "agente"}, {"targetPortId", "agente_in"}}})}};
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(g);

    CHECK(src.find("#include \"memory/MemoryService.hpp\"") != std::string::npos);
    CHECK(src.find("memory.configure(projectId, arx_mem_persistent)") != std::string::npos);
    CHECK(src.find("memory.buildBrief(projectId, \"pipeline\", std::vector<std::string>{\"svg_typography\"}, "
                   "std::vector<std::string>{\"resumo\"}, 800, true, true, &arx_brief_info)") != std::string::npos);
    CHECK(src.find("ARESTRIX_MEMORY_DISABLE") != std::string::npos);
    CHECK(src.find("recordBriefHits(projectId, \"agente\", \"agente\", arx_brief_info)") != std::string::npos);
    CHECK(src.find("\"memoryEnabled\":true") != std::string::npos);
    CHECK(src.find("\"memory_search\"") != std::string::npos);
    CHECK(src.find("\"memory_record_lesson\"") != std::string::npos);
    CHECK(src.find("runAgent(\"agente\", \"Agente\"") != std::string::npos);
    CHECK(src.find("arx_mem_ctx);") != std::string::npos);
    CHECK(src.find("catch (const std::exception& arx_mem_err)") != std::string::npos);
    CHECK(src.find("recordLesson(projectId, \"svg_typography\", \"agent_failure\"") != std::string::npos);
    CHECK(src.find("setWorking(projectId, \"pipeline\", \"last_stage\", taskforge::Json(\"agente\"))") != std::string::npos);
    CHECK(src.find("memory.clearSession(projectId)") != std::string::npos);

    // A/B configurado no projeto (sem env): desliga brief/ferramentas, mantém metricas.
    json offSchema = schema;
    offSchema["metadata"]["globalDefaults"] = json{{"memory", json{{"measurementMode", "memory_off"},
                                                                   {"recordMetrics", true}}}};
    taskforge::core::Graph offGraph;
    offGraph.fromJson(offSchema);
    std::string offSrc = TaskflowGenerator::generateSource(offGraph);
    CHECK(offSrc.find("\"memoryForceDisabled\":true") != std::string::npos);
    CHECK(offSrc.find("\"memoryEnabled\":false") != std::string::npos);
    CHECK(offSrc.find("\"memory_search\"") == std::string::npos);
    CHECK(offSrc.find("memory.buildBrief(") == std::string::npos);
    CHECK(offSrc.find("|| true || std::getenv") != std::string::npos);
}

TEST(memory_store_working_lessons_temporal_and_usage) {
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryUsageRecord;
    const fs::path dir = makeTempDir("memory-store");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:teste", "project", "", "teste"));

    CHECK(store.setWorking("project:teste", "item-1", "resumo", taskforge::Json("valor")));
    CHECK(store.setWorking("project:teste", "item-1", "ignorado", taskforge::Json("segredo")));
    CHECK(store.getWorking("project:teste", "item-1", "resumo") == taskforge::Json("valor"));
    auto snapshot = store.workingSnapshot("project:teste", "item-1", {"resumo"});
    CHECK(snapshot["resumo"] == taskforge::Json("valor"));
    CHECK(!snapshot.contains("ignorado"));
    auto full = store.workingSnapshot("project:teste", "item-1");
    CHECK(full.contains("ignorado"));

    const std::string lesson1 = store.recordLesson("project:teste", "svg", "auditoria", "REGRA-XYZ", "src");
    const std::string lesson2 = store.recordLesson("project:teste", "svg", "auditoria", "REGRA-XYZ", "src");
    CHECK(!lesson1.empty() && lesson1 == lesson2);
    CHECK(store.getLessons("project:teste", "svg").size() == 1);
    const std::string lessonNew = store.recordLesson("project:teste", "svg", "auditoria", "REGRA-NOVA", "src2");
    CHECK(!lessonNew.empty());
    auto ordered = store.getLessons("project:teste", "svg");
    CHECK(ordered.size() == 2 && ordered[0].id == lessonNew);   // mais recente primeiro
    CHECK(store.formatLessons("project:teste", {"svg"}).find("REGRA-XYZ") != std::string::npos);
    CHECK(store.formatLessons("project:teste", {"outro"}).empty());

    CHECK(store.recordHit("project:teste", "run-1", "node-a", lessonNew, "lesson", true, false));
    CHECK(store.markHitsUsed("project:teste", "run-1", "node-a", {lessonNew}) == 1);
    CHECK(store.finishHits("project:teste", "run-1", "node-a", "success"));
    auto hits = store.hitSummary("project:teste");
    CHECK(hits["injected"] == 1 && hits["used"] == 1 && hits["successHits"] == 1);

    const std::string a = store.ensureNode("project:teste", "node-a", "project", "Alpha");
    const std::string b = store.ensureNode("project:teste", "node-b", "library", "Biblioteca Y");
    const std::string c = store.ensureNode("project:teste", "node-c", "library", "Biblioteca Z");
    CHECK(!a.empty() && !b.empty() && !c.empty());

    const std::string oldEdge = store.recordEdge("project:teste", a, b, "USES", taskforge::Json(),
                                                 "2026-01-01T00:00:00Z", "2026-09-30T23:59:59Z");
    const std::string newEdge = store.recordEdge("project:teste", a, c, "USES", taskforge::Json(),
                                                 "2026-10-01T00:00:00Z", "");
    CHECK(!oldEdge.empty() && !newEdge.empty());

    auto before = store.neighbors("project:teste", a, 1, "2026-06-01T00:00:00Z");
    CHECK(before.size() == 1 && before[0].id == oldEdge);
    auto after = store.neighbors("project:teste", a, 1, "2026-10-15T00:00:00Z");
    CHECK(after.size() == 1 && after[0].id == newEdge);

    auto search = store.searchEdges("project:teste", "Alpha", 10);
    CHECK(!search.empty());
    CHECK(store.invalidateEdge(oldEdge, "2026-09-30T23:59:59Z", "migracao"));
    auto superseded = store.neighbors("project:teste", a, 1, "2026-06-01T00:00:00Z");
    CHECK(superseded.empty());

    MemoryUsageRecord usage;
    usage.scopeId = "project:teste";
    usage.nodeId = "node-a";
    usage.provider = "deepseek";
    usage.model = "deepseek-flash";
    usage.promptTokens = 100;
    usage.completionTokens = 20;
    usage.totalTokens = 120;
    usage.calls = 1;
    usage.memoryContextChars = 50;
    usage.memoryEnabled = 1;
    usage.retries = 2;
    usage.outcome = "success";
    CHECK(store.recordUsage(usage));
    auto summary = store.usageSummary("project:teste");
    CHECK(summary["runs"] == 1);
    CHECK(summary["promptTokens"] == 100);
    CHECK(summary["totalTokens"] == 120);
    CHECK(summary["successRuns"] == 1);
    CHECK(summary["failureRuns"] == 0);
    CHECK(summary["retries"] == 2);

    store.close();
    fs::remove_all(dir);
}

TEST(memory_service_imports_legacy_json) {
    using taskforge::memory::MemoryService;
    const fs::path dir = makeTempDir("memory-legacy");
    const fs::path db = dir / "memory.db";
    const fs::path bb = dir / "arestrix-blackboard-legacy.json";
    const fs::path ls = dir / "arestrix-memory-legacy.json";

    taskforge::Json blackboard = taskforge::Json::object();
    blackboard["item-1"]["resumo"] = "valor-legado";
    std::ofstream(bb) << blackboard.dump(2);

    taskforge::Json lessons = taskforge::Json::object();
    lessons["count"] = 1;
    lessons["lessons"] = taskforge::Json::array();
    lessons["lessons"].push_back(taskforge::Json::object());
    lessons["lessons"][0]["domain"] = "memoria";
    lessons["lessons"][0]["triggerError"] = "legado";
    lessons["lessons"][0]["lessonRule"] = "REGRA-LEGADA";
    lessons["lessons"][0]["sourceItem"] = "legacy";
    std::ofstream(ls) << lessons.dump(2);

    auto& memory = MemoryService::getInstance();
    CHECK(memory.configure("legacy-import-test", true, db.string(), bb.string(), ls.string()));
    auto imported = memory.getLessons("legacy-import-test", "memoria");
    CHECK(imported.size() == 1 && imported[0].lessonRule == "REGRA-LEGADA");
    CHECK(memory.getWorking("legacy-import-test", "item-1", "resumo") == taskforge::Json("valor-legado"));
    CHECK(!memory.formatLessons("legacy-import-test", {"memoria"}).empty());

    memory.closeProject("legacy-import-test");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_service_brief_respects_budget) {
    using taskforge::memory::MemoryService;
    const fs::path dir = makeTempDir("memory-brief");
    const fs::path db = dir / "memory.db";
    auto& memory = MemoryService::getInstance();
    CHECK(memory.configure("brief-test", true, db.string()));
    CHECK(memory.setWorking("brief-test", "pipeline", "resumo", taskforge::Json(std::string(4000, 'x'))));
    CHECK(!memory.recordLesson("brief-test", "memoria", "teste", "uma regra longa para o teste de orcamento").empty());
    taskforge::memory::MemoryBriefInfo info;
    std::string brief = memory.buildBrief("brief-test", "pipeline", {"memoria"}, {"resumo"}, 10, true, true, &info);
    CHECK(!brief.empty());
    CHECK(brief.find("truncado pelo orcamento") != std::string::npos);
    CHECK(brief.size() < 200);
    CHECK(info.truncated);
    CHECK(info.lessonIds.empty());   // nao registra item que nao entrou no prompt
    taskforge::memory::MemoryBriefInfo fullInfo;
    std::string fullBrief = memory.buildBrief("brief-test", "pipeline", {"memoria"}, {"resumo"}, 2000, true, true, &fullInfo);
    CHECK(fullBrief.find("\"resumo\"") != std::string::npos);
    CHECK(fullInfo.lessonIds.size() == 1 && fullInfo.workingKeys.size() == 1);
    CHECK(!fullInfo.truncated);
    memory.closeProject("brief-test");
    std::error_code ec;
    fs::remove_all(dir, ec);
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

TEST(mcp_accepts_http_headers_and_streamable_http_transport) {
    using taskforge::mcp::McpServerConfig;
    using taskforge::mcp::McpTransportType;
    const fs::path dir = makeTempDir("mcp-http-headers");
    setTestEnv("ARX_TEST_KEENABLE_KEY", "segredo-de-teste");
    fs::create_directories(dir / ".workspace" / "mcps");
    {
        std::ofstream f(dir / ".workspace" / "mcps" / "keenable.json", std::ios::binary | std::ios::trunc);
        f << R"({"id":"keenable","name":"Keenable","url":"https://api.keenable.ai/mcp",
                  "headers":{"X-API-Key":"${ARX_TEST_KEENABLE_KEY}"},"isEnabled":true})";
    }

    // Sem "transport", uma URL implica Streamable HTTP; headers aceitam ${VARIAVEL}.
    auto cfg = taskforge::rt::McpToolbox::loadServerConfig(dir, "keenable");
    CHECK(cfg.transport == McpTransportType::Sse);
    CHECK(cfg.headers.size() == 1);
    CHECK(cfg.headers["X-API-Key"] == "segredo-de-teste");

    // O mesmo resolvedor e usado pelo registro global (teste/playground da UI).
    McpServerConfig raw;
    raw.headers["X-API-Key"] = "${ARX_TEST_KEENABLE_KEY}";
    auto expanded = taskforge::mcp::McpClient::expandConfig(raw);
    CHECK(expanded.headers["X-API-Key"] == "segredo-de-teste");

    // Header com quebra de linha e recusado antes de qualquer conexao.
    McpServerConfig bad;
    bad.id = "bad-header";
    bad.transport = McpTransportType::Sse;
    bad.url = "https://example.com/mcp";
    bad.headers["X-Test"] = "ok\r\nInjected: 1";
    taskforge::mcp::McpClient client;
    auto res = client.callTool(bad, "x", {}, 1000);
    CHECK(res.isError && res.errorCode == "invalid_header");

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(mcp_keenable_live_optional) {
    if (!std::getenv("ARX_TEST_KEENABLE")) {
        std::cout << "  (ARX_TEST_KEENABLE ausente: teste de rede ignorado)" << std::endl;
        return;
    }
    taskforge::mcp::McpServerConfig cfg;
    cfg.id = "keenable-live";
    cfg.transport = taskforge::mcp::McpTransportType::Sse;
    cfg.url = "https://api.keenable.ai/mcp";
    taskforge::mcp::McpClient client;
    taskforge::mcp::McpErrorInfo info;
    auto tools = client.listTools(cfg, 20000, &info);
    CHECK(!tools.empty());
    bool hasSearch = false, hasFetch = false;
    for (const auto& t : tools) {
        if (t.name == "search_web_pages") hasSearch = true;
        if (t.name == "fetch_page_content") hasFetch = true;
    }
    CHECK(hasSearch && hasFetch);
    auto res = client.callTool(cfg, "search_web_pages", {{"query", "teorema de Pitagoras"}}, 60000);
    CHECK(res.success && !res.isError);
    CHECK(!res.content.empty());
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

    // Idioma do agente: toolbox em ingles devolve o erro da ferramenta em ingles (fallback en do codigo estavel).
    taskforge::rt::McpToolbox enBox(root, {"eco"}, {}, taskforge::rt::RuntimeLang::En);
    auto died = enBox.call("mcp__eco__die", json::object());
    CHECK(died.isError);
    CHECK(died.content.find("failed to execute the MCP tool") != std::string::npos);
    CHECK(died.content.find("closed before answering") != std::string::npos);

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

// ---------------------------------------------------------------------------
// Assistente do Studio
// ---------------------------------------------------------------------------
namespace {

struct FakeAssistantHost : taskforge::assistant::AssistantHost {
    json project = json{{"projectId", "p1"}, {"name", "Demo"}, {"rootPath", "C:/demo"}};
    fs::path docs;
    int created = 0, saved = 0, runs = 0;
    std::string lastCreateName;

    json currentProject() override { return project; }
    json recentProjects() override { return json::array(); }
    std::optional<json> createProject(const std::string& name, const std::string&, const std::string&, std::string&) override {
        ++created;
        lastCreateName = name;
        project = json{{"projectId", "p2"}, {"name", name}, {"rootPath", "C:/novo/" + name}};
        return taskforge::assistant::GraphDocument::emptySchema(name, "p2");
    }
    std::optional<json> openProject(const std::string& path, std::string& error) override {
        error = "nao existe: " + path;
        return std::nullopt;
    }
    bool savePipeline(const json&, std::string&) override { ++saved; return true; }
    bool runPipeline(const json&, std::string&) override { ++runs; return true; }
    fs::path docsDirectory(taskforge::rt::RuntimeLang) override { return docs; }
};

taskforge::rt::ChatReply scriptedCall(const std::string& id, const std::string& name, json args) {
    taskforge::rt::ChatReply r;
    r.ok = true;
    r.toolCalls.push_back({id, name, std::move(args)});
    return r;
}

taskforge::rt::ChatReply scriptedText(const std::string& text) {
    taskforge::rt::ChatReply r;
    r.ok = true;
    r.text = text;
    return r;
}

} // namespace

TEST(assistant_graph_document_edits_without_losing_fields) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    json schema = as::GraphDocument::emptySchema("g", "p");
    schema["nodes"].push_back(json{{"id", "x"}, {"type", "static"}, {"label", "X"}, {"inputs", json::array()},
                                   {"outputs", json::array()}, {"campoDesconhecido", 42}});
    as::GraphDocument doc(schema);

    auto a = doc.addNode("agent", "Pesquisador", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto b = doc.addNode("static", "Relatorio", 600.0, 100.0, "return;", RuntimeLang::Pt);
    CHECK(a.ok && b.ok);
    CHECK(a.uiOp.value("op", std::string()) == "add_node");
    std::string ida = a.result.value("id", std::string()), idb = b.result.value("id", std::string());
    CHECK(doc.node(ida)->at("outputs").size() == 2);   // out + artifact_out
    CHECK(doc.node(idb)->value("customLambdaBody", std::string()) == "return;");
    CHECK(!doc.addNode("inexistente", "?", std::nullopt, std::nullopt, "", RuntimeLang::Pt).ok);

    auto c = doc.connect(ida, "", idb, "", RuntimeLang::Pt);
    CHECK(c.ok);
    CHECK(c.uiOp["edge"].value("sourcePortId", std::string()) == ida + "_out_0");
    CHECK(!doc.connect(ida, "", idb, "", RuntimeLang::Pt).ok);                     // duplicada
    CHECK(doc.connect(ida, "artifact_out", idb, "in", RuntimeLang::Pt).ok);        // porta pelo nome
    CHECK(!doc.connect(ida, "nao-existe", idb, "", RuntimeLang::En).ok);
    CHECK(!doc.connect(ida, "", ida, "", RuntimeLang::Pt).ok);

    auto u = doc.updateNode(ida, json{{"label", "Analista"}, {"agentSettings", json{{"inheritModel", false}}}}, RuntimeLang::Pt);
    CHECK(u.ok);
    CHECK(doc.node(ida)->at("agentSettings").value("inheritModel", true) == false);
    CHECK(doc.node(ida)->at("agentSettings").value("inheritSandbox", false) == true);   // mesclado, nao substituido
    CHECK(!doc.updateNode(ida, json{{"type", "static"}}, RuntimeLang::Pt).ok);

    // Ciclo sem decisao e erro; com um condition no meio, e um laco valido.
    auto back = doc.connect(idb, "", ida, "", RuntimeLang::Pt);
    CHECK(back.ok);
    CHECK(!doc.validate(RuntimeLang::Pt).value("ok", true));
    CHECK(doc.disconnect(back.result.value("edgeId", std::string()), RuntimeLang::Pt).ok);
    auto cond = doc.addNode("condition", "Revisar", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    std::string idc = cond.result.value("id", std::string());
    CHECK(doc.connect(idb, "", idc, "", RuntimeLang::Pt).ok);
    CHECK(doc.connect(idc, "branch 0", ida, "", RuntimeLang::Pt).ok);
    json v = doc.validate(RuntimeLang::Pt);
    CHECK(v.value("ok", false));
    CHECK(v.at("warnings").size() >= 1);   // branch 1 sem destino e o vertice X isolado

    auto rm = doc.removeNode(idb, RuntimeLang::Pt);
    CHECK(rm.ok && rm.result.at("removedEdges").size() == 3);
    CHECK(doc.node("x")->value("campoDesconhecido", 0) == 42);
    CHECK(doc.schema().at("edges").size() == 1);
}

TEST(assistant_tools_validate_arguments_and_only_read_listed_docs) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    FakeAssistantHost host;
    host.docs = makeTempDir("assistant-docs");
    { std::ofstream(host.docs / "06-interface.md") << "# Interface\n\nO ribbon tem abas."; }
    { std::ofstream(host.docs.parent_path() / "segredo.md") << "# Segredo"; }
    as::GraphDocument doc;

    auto missing = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node", json{{"type", "agent"}});
    CHECK(missing.result.isError);
    CHECK(missing.result.content.find("label") != std::string::npos);
    auto wrongType = as::executeAssistantTool(doc, host, RuntimeLang::En, "move_node", json{{"id", "a"}, {"x", "1"}, {"y", 2}});
    CHECK(wrongType.result.isError);

    auto list = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "studio_docs_list", json::object());
    CHECK(list.result.content.find("06-interface") != std::string::npos);
    CHECK(list.result.content.find("Interface") != std::string::npos);
    auto read = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "studio_docs_read", json{{"id", "06-interface"}});
    CHECK(!read.result.isError && read.result.content.find("ribbon") != std::string::npos);
    auto escape = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "studio_docs_read", json{{"id", "../segredo"}});
    CHECK(escape.result.isError);

    CHECK(as::toolRisk("get_graph") == as::ToolRisk::Read);
    CHECK(as::toolRisk("connect") == as::ToolRisk::Edit);
    CHECK(as::toolRisk("create_project") == as::ToolRisk::Confirm);
    CHECK(as::toolRisk("run_pipeline") == as::ToolRisk::Confirm);
    CHECK(!as::toolRisk("apagar_tudo").has_value());
    CHECK(as::assistantTools(RuntimeLang::En)[0].def.description.find("Lists") != std::string::npos);

    // run_pipeline valida antes: grafo com ciclo sem decisao nao executa.
    auto a = doc.addNode("static", "A", std::nullopt, std::nullopt, "", RuntimeLang::Pt).result.value("id", std::string());
    auto b = doc.addNode("static", "B", std::nullopt, std::nullopt, "", RuntimeLang::Pt).result.value("id", std::string());
    doc.connect(a, "", b, "", RuntimeLang::Pt);
    doc.connect(b, "", a, "", RuntimeLang::Pt);
    CHECK(as::executeAssistantTool(doc, host, RuntimeLang::Pt, "run_pipeline", json::object()).result.isError);
    CHECK(host.runs == 0);
    fs::remove_all(host.docs);
}

TEST(model_client_converts_history_for_openai_and_anthropic) {
    namespace rt = taskforge::rt;
    rt::ChatRequest req;
    req.model = "m";
    req.system = "sys";
    rt::ChatMessage u; u.role = "user"; u.text = "oi";
    rt::ChatMessage a; a.role = "assistant"; a.text = "vou ver"; a.reasoning = "pensando";
    a.toolCalls.push_back({"c1", "get_graph", json::object()});
    a.toolCalls.push_back({"c2", "get_project", json::object()});
    rt::ChatMessage t1; t1.role = "tool"; t1.toolCallId = "c1"; t1.text = "{}";
    rt::ChatMessage t2; t2.role = "tool"; t2.toolCallId = "c2"; t2.text = "erro"; t2.isError = true;
    req.messages = {u, a, t1, t2};
    req.tools.push_back({"get_graph", "d", json{{"type", "object"}}});

    json oa = rt::buildRequestBody(rt::resolveProvider("deepseek"), req, true);
    CHECK(oa["messages"].size() == 5);   // system + user + assistant + 2 tool
    CHECK(oa["messages"][2]["tool_calls"][0]["function"].value("arguments", std::string()) == "{}");
    CHECK(oa["messages"][2].value("reasoning_content", std::string()) == "pensando");
    CHECK(!rt::buildRequestBody(rt::resolveProvider("deepseek"), req, false)["messages"][2].contains("reasoning_content"));
    CHECK(oa["tools"][0]["function"].value("name", std::string()) == "get_graph");

    req.agentId = "agente\r\nx";
    req.runId = "run-1";
    req.memoryScope = "project:demo";
    req.memoryEnabled = true;
    req.memoryContextChars = 640;
    auto gh = rt::gatewayMetadataHeaders(req);
    CHECK(gh["X-Arestrix-Agent"] == "agentex");   // CR/LF removidos
    CHECK(gh["X-Arestrix-Run-Id"] == "run-1");
    CHECK(gh["X-Arestrix-Memory-Scope"] == "project:demo");
    CHECK(gh["X-Arestrix-Memory-Enabled"] == "1");
    CHECK(gh["X-Arestrix-Memory-Context-Chars"] == "640");

    json an = rt::buildRequestBody(rt::resolveProvider("anthropic"), req, true);
    CHECK(an.value("system", std::string()) == "sys");
    CHECK(an["messages"].size() == 3);   // user, assistant, user com os dois tool_result
    CHECK(an["messages"][2]["content"].size() == 2);
    CHECK(an["messages"][2]["content"][1].value("is_error", false));
    CHECK(an["messages"][1]["content"][1].value("type", std::string()) == "tool_use");
    CHECK(an["tools"][0].contains("input_schema"));

    auto r1 = rt::parseResponseBody(rt::resolveProvider("openai"),
        R"({"choices":[{"finish_reason":"tool_calls","message":{"content":null,"tool_calls":[{"id":"x","function":{"name":"connect","arguments":"{\"from\":\"a\"}"}}]}}],"usage":{"prompt_tokens":5,"completion_tokens":2}})");
    CHECK(r1.ok && r1.toolCalls.size() == 1 && r1.toolCalls[0].args.value("from", std::string()) == "a" && r1.inTokens == 5);
    auto r2 = rt::parseResponseBody(rt::resolveProvider("anthropic"),
        R"({"stop_reason":"tool_use","content":[{"type":"text","text":"ok"},{"type":"tool_use","id":"t","name":"get_graph","input":{}}],"usage":{"input_tokens":3,"output_tokens":1}})");
    CHECK(r2.text == "ok" && r2.toolCalls.size() == 1 && r2.outTokens == 1);

    CHECK(!rt::resolveProvider("jev").supportsTools);
    // Servidor local compativel com OpenAI: o suporte a tools depende do modelo, mas o provedor nao e
    // bloqueado de antemao. Se o servidor recusar, a chamada devolve tools_unsupported.
    CHECK(rt::resolveProvider("local").supportsTools);
    CHECK(rt::resolveProvider("anthropic").supportsTools && rt::resolveProvider("deepseek").supportsTools);
    CHECK(rt::normalizeLocalBaseUrl("http://localhost:11434") == "http://localhost:11434/v1");
    CHECK(rt::normalizeLocalBaseUrl("http://localhost:11434/") == "http://localhost:11434/v1");
    CHECK(rt::normalizeLocalBaseUrl(" http://localhost:8080/v1/ ") == "http://localhost:8080/v1");
    CHECK(rt::normalizeLocalBaseUrl("http://localhost:8080/v1/chat/completions") == "http://localhost:8080/v1");
    CHECK(rt::normalizeLocalBaseUrl("http://localhost:8080/custom") == "http://localhost:8080/custom");
    auto roundTrip = rt::ChatMessage::fromJson(a.toJson());
    CHECK(roundTrip.toolCalls.size() == 2 && roundTrip.reasoning == "pensando");
}

TEST(model_client_ai_gateway_and_mistral_provider) {
    namespace rt = taskforge::rt;
    auto setv = [](const char* key, const char* value) {
#ifdef _WIN32
        _putenv_s(key, value);
#else
        if (*value) setenv(key, value, 1); else unsetenv(key);
#endif
    };
    const char* vars[] = {"ARESTRIX_AI_GATEWAY_URL", "ARESTRIX_AI_GATEWAY_KEY",
                          "ARESTRIX_REQUIRE_AI_GATEWAY", "ARESTRIX_AI_GATEWAY_POLICY",
                          "OPENAI_BASE_URL", "LOCAL_LLM_BASE_URL"};
    std::vector<std::pair<std::string, std::string>> saved;
    for (const char* var : vars) {
        const char* current = std::getenv(var);
        saved.push_back({var, current ? current : ""});
        setv(var, "");
    }

    auto mistral = rt::resolveProvider("mistral");
    CHECK(mistral.baseUrl == "https://api.mistral.ai/v1");
    CHECK(mistral.keyEnv == "MISTRAL_API_KEY");

    setv("ARESTRIX_AI_GATEWAY_URL", "https://gw.example.com/");
    setv("ARESTRIX_AI_GATEWAY_KEY", "av_test");
    auto openai = rt::resolveProvider("openai");
    CHECK(openai.gateway);
    CHECK(openai.baseUrl == "https://gw.example.com/openai");
    CHECK(openai.keyEnv == "ARESTRIX_AI_GATEWAY_KEY");
    CHECK(openai.configError.empty());

    auto anthropic = rt::resolveProvider("anthropic");
    CHECK(anthropic.gateway);
    CHECK(anthropic.baseUrl == "https://gw.example.com/anthropic");
    CHECK(anthropic.path == "/v1/messages");

    auto local = rt::resolveProvider("local");
    CHECK(!local.gateway);
    CHECK(local.baseUrl.find("localhost:11434") != std::string::npos);

    setv("ARESTRIX_AI_GATEWAY_URL", "");
    setv("ARESTRIX_REQUIRE_AI_GATEWAY", "1");
    auto required = rt::resolveProvider("deepseek");
    CHECK(required.configError == "gateway_required");
    CHECK(required.baseUrl.empty());
    auto localStillDirect = rt::resolveProvider("local");
    CHECK(localStillDirect.configError.empty());
    CHECK(!localStillDirect.gateway);

    // Politica empresarial: gateway obrigatorio mesmo com require=0 do usuario.
    setv("ARESTRIX_AI_GATEWAY_POLICY", "locked");
    setv("ARESTRIX_REQUIRE_AI_GATEWAY", "0");
    auto lockedMissing = rt::resolveProvider("openai");
    CHECK(lockedMissing.configError == "gateway_required");
    setv("ARESTRIX_AI_GATEWAY_URL", "https://gw.example.com");
    auto locked = rt::resolveProvider("openai");
    CHECK(locked.gateway);
    CHECK(locked.configError.empty());

    for (const auto& [key, value] : saved) setv(key.c_str(), value.c_str());
}

TEST(gateway_store_enforces_budgets_and_records_usage) {
    namespace gw = taskforge::gateway;
    auto dir = makeTempDir("gateway-store");
    gw::GatewayStore store;
    std::string error;
    CHECK(store.open((dir / "gateway.db").string(), error));
    CHECK(store.initSchema(error));

    std::string key;
    std::string keyId;
    CHECK(store.createKey("test", "demo", "frank", 0, 0, key, keyId, error));
    CHECK(!key.empty());
    CHECK(key.rfind("av_", 0) == 0);

    gw::VirtualKey authenticated;
    CHECK(store.authenticate(key, authenticated, error));
    CHECK(authenticated.id == keyId);
    CHECK(authenticated.project == "demo");

    CHECK(store.setBudget("key", keyId, "monthly", 1.0, -1, true, 0.8, error));

    gw::RequestContext context;
    context.requestId = "req_test_1";
    context.keyId = keyId;
    context.project = "demo";
    context.user = "frank";
    context.provider = "openai";
    context.model = "gpt-4o";

    gw::BudgetDecision decision;
    CHECK(store.checkAndReserve(context, 0.6, 1000, decision, error));
    CHECK(decision.allowed);
    CHECK(!decision.budgets.empty());

    gw::BudgetDecision second;
    CHECK(store.checkAndReserve(context, 0.6, 1000, second, error));
    CHECK(!second.allowed);
    CHECK(second.reason == "budget_usd_exceeded");

    gw::TokenUsage usage;
    usage.input = 50;
    usage.output = 50;
    CHECK(store.reconcile(context, usage, 0.2, 0.6, 1000, error));

    gw::BudgetDecision third;
    CHECK(store.checkAndReserve(context, 0.7, 100, third, error));
    CHECK(third.allowed);
    CHECK(store.reconcile(context, gw::TokenUsage{}, 0.0, 0.7, 100, error));

    gw::UsageEvent event;
    event.context = context;
    event.usage = usage;
    event.costUsd = 0.2;
    event.status = "ok";
    event.agent = "agente-teste";
    event.runId = "run-teste";
    event.memoryScope = "project:demo";
    event.memoryEnabled = true;
    event.memoryContextChars = 400;
    event.memoryTokens = 100;
    CHECK(store.recordUsage(event, error));

    json report;
    CHECK(store.usageReport("", keyId, "", report, error));
    CHECK(report["totals"].value("requests", 0LL) == 1);
    CHECK(report["totals"].value("inputTokens", 0LL) == 50);
    CHECK(report["totals"].value("outputTokens", 0LL) == 50);
    CHECK(report["totals"].value("memoryRequests", 0LL) == 1);
    CHECK(report["totals"].value("memoryContextChars", 0LL) == 400);
    CHECK(report["totals"].value("memoryTokens", 0LL) == 100);
    CHECK(report["totals"].value("memoryInputRatio", 0.0) == 2.0);
    CHECK(report["byMemory"].is_array() && report["byMemory"].size() == 1);
    CHECK(report["byMemory"][0].value("memoryEnabled", false));

    json usageForToken;
    CHECK(store.usageForToken(key, usageForToken, error));
    CHECK(usageForToken["key"].value("id", std::string()) == keyId);
    CHECK(usageForToken["budgets"].is_array());
    CHECK(usageForToken["usage"].value("memoryTokens", 0LL) == 100);
    CHECK(usageForToken["byMemory"].is_array() && usageForToken["byMemory"].size() == 1);
    CHECK(usageForToken["byMemory"][0].value("memoryEnabled", false));

    store.close();
    fs::remove_all(dir);
}

TEST(local_runtime_catalog_manifest_and_storage) {
    namespace lr = taskforge::local;
    auto& runtime = lr::LocalRuntime::instance();
    auto catalog = runtime.catalog();
    CHECK(catalog.value("schemaVersion", 0) == 1);
    CHECK(runtime.models().size() >= 3);

    for (const auto& model : runtime.models()) {
        CHECK(!model.id.empty());
        CHECK(model.downloadUrl.rfind("https://", 0) == 0);
        CHECK(model.sha256.size() == 64);
        CHECK(model.sizeBytes > 0);
        CHECK(model.fileName.find('/') == std::string::npos);
        CHECK(model.fileName.find('\\') == std::string::npos);
    }

    auto manifest = runtime.manifest();
    CHECK(manifest["components"].contains("llama.cpp"));
    CHECK(manifest["components"]["llama.cpp"].value("version", std::string()) == "b11669");

    const auto* qwen = runtime.findModel("qwen2.5-7b-instruct-q4_k_m");
    CHECK(qwen != nullptr);
    CHECK(runtime.modelPath(*qwen).filename().string() == "Qwen2.5-7B-Instruct-Q4_K_M.gguf");

    auto state = runtime.state();
    CHECK(state["models"].is_array());
    CHECK(state["models"].size() == runtime.models().size());
    CHECK(state["downloads"].is_array());
    CHECK(state["manifest"]["components"].contains("llama.cpp"));

    std::string downloadError;
    CHECK(runtime.startModelDownload("modelo-inexistente", downloadError).empty());
    CHECK(downloadError == "model_not_found");
    CHECK(runtime.deleteModel("modelo-inexistente").value("ok", true) == false);

    auto dir = makeTempDir("local-runtime");
#ifdef _WIN32
    _putenv_s("ARESTRIX_DATA_DIR", dir.string().c_str());
#else
    setenv("ARESTRIX_DATA_DIR", dir.string().c_str(), 1);
#endif
    CHECK(runtime.dataRoot() == dir);
    CHECK(runtime.modelsDir() == dir / "models");

    auto runtimeState = runtime.runtimeStatus();
    CHECK(runtimeState.value("installed", true) == false);
    CHECK(runtimeState.value("version", std::string()) == "b11669");
    CHECK(!runtimeState.value("binaryId", std::string()).empty());
    const auto hardware = runtimeState["hardware"];
    CHECK(hardware.is_object());
    CHECK(hardware.value("ramTotalBytes", 0LL) > 0);
    CHECK(hardware.value("cpuThreads", 0LL) > 0);
    CHECK(hardware.value("recommendedModelId", std::string()).find("qwen2.5") != std::string::npos);
    CHECK(hardware.value("modelsMax", 0LL) >= 1);
    CHECK(runtime.startServer().value("ok", true) == false);
    CHECK(runtime.startServer().value("error", std::string()) == "runtime_not_installed");
    CHECK(runtime.stopServer().value("ok", false) == true);
    CHECK(runtime.serverRunning() == false);

#ifdef _WIN32
    _putenv_s("ARESTRIX_DATA_DIR", "");
#else
    unsetenv("ARESTRIX_DATA_DIR");
#endif
    fs::remove_all(dir);
}

TEST(assistant_turn_edits_the_graph_asks_before_projects_and_persists_the_session) {
    namespace as = taskforge::assistant;
    namespace rt = taskforge::rt;
    auto root = makeTempDir("assistant-session");
    as::SessionStore store(root);
    FakeAssistantHost host;
    as::GraphDocument doc;
    std::vector<json> events;
    std::vector<json> confirms;
    bool acceptConfirm = false;
    as::TurnIO io;
    io.emit = [&](const json& e) { events.push_back(e); };
    io.confirm = [&](const json& req) { confirms.push_back(req); return acceptConfirm; };

    // Turno 1: cria dois vertices, liga e responde.
    std::vector<rt::ChatReply> script;
    script.push_back(scriptedCall("c1", "add_node", json{{"type", "agent"}, {"label", "Coletor"}}));
    script.push_back(scriptedCall("c2", "add_node", json{{"type", "static"}, {"label", "Saida"}}));
    script.push_back(rt::ChatReply{});   // placeholder: substituido abaixo pelo connect com os ids reais
    script.push_back(scriptedText("Pronto: dois vertices ligados."));
    size_t next = 0;
    std::vector<rt::ChatRequest> requests;
    as::ModelFn model = [&](const rt::ChatRequest& req) {
        requests.push_back(req);
        if (next == 2) {
            auto nodes = doc.summary().at("nodes");
            return scriptedCall("c3", "connect", json{{"from", nodes[0].value("id", std::string())},
                                                     {"to", nodes[1].value("id", std::string())}});
        }
        return script[next];
    };
    auto counted = [&](const rt::ChatRequest& req) { auto r = model(req); ++next; return r; };

    as::Session session = as::SessionStore::create();
    as::TurnOptions opts;
    as::runTurn(session, store, "Monte um pipeline simples", doc, host, as::ModelChoice{"deepseek", "x"}, counted, io, opts);

    CHECK(doc.nodeCount() == 2);
    CHECK(doc.schema().at("edges").size() == 1);
    int ops = 0, texts = 0;
    for (const auto& e : events) {
        if (e.value("type", std::string()) == "op") ++ops;
        if (e.value("type", std::string()) == "text") ++texts;
        CHECK(e.value("sessionId", std::string()) == session.id);
    }
    CHECK(ops == 3 && texts == 1);
    CHECK(events.back().value("type", std::string()) == "done" && events.back().value("ok", false));
    CHECK(requests[0].system.find("Responda em portugues") != std::string::npos);
    CHECK(requests[0].tools.size() == as::assistantTools(rt::RuntimeLang::Pt).size());
    CHECK(requests.back().messages.size() == 7);   // user + 3x(assistant+tool)

    // A sessao fica gravada no projeto e volta igual.
    auto loaded = store.load(session.id);
    CHECK(loaded.has_value());
    CHECK(loaded->title == "Monte um pipeline simples");
    CHECK(loaded->messages.size() == 8);
    CHECK(loaded->transcript.size() == 5);   // user + 3 atividades + resposta
    CHECK(store.list().size() == 1);

    // Turno 2 (en-US): criar projeto pede confirmacao; recusado nao cria; aceito cria e recarrega o canvas.
    events.clear();
    next = 0;
    script = {scriptedCall("p1", "create_project", json{{"name", "Novo"}}), scriptedText("ok"),
              scriptedCall("p2", "create_project", json{{"name", "Novo"}}), scriptedText("done")};
    as::ModelFn plain = [&](const rt::ChatRequest& req) { requests.push_back(req); return script[next++]; };
    opts.locale = "en-US";
    as::runTurn(session, store, "create a project", doc, host, as::ModelChoice{"deepseek", "x"}, plain, io, opts);
    CHECK(confirms.size() == 1 && confirms[0].value("tool", std::string()) == "create_project");
    CHECK(host.created == 0);
    CHECK(requests.back().messages.back().text == "The user declined this action.");
    CHECK(requests.back().system.find("Answer in English") != std::string::npos);

    acceptConfirm = true;
    events.clear();
    as::runTurn(session, store, "yes, create it", doc, host, as::ModelChoice{"deepseek", "x"}, plain, io, opts);
    CHECK(host.created == 1 && host.lastCreateName == "Novo");
    CHECK(doc.nodeCount() == 0);   // o canvas passa a ser o grafo vazio do projeto novo
    bool reload = false;
    for (const auto& e : events) {
        if (e.value("type", std::string()) == "op" && e.at("op").value("op", std::string()) == "load_project") reload = true;
    }
    CHECK(reload);

    // Provedor sem ferramentas e IA nao configurada: erro com codigo, sem chamar o modelo.
    size_t before = requests.size();
    events.clear();
    as::runTurn(session, store, "oi", doc, host, as::ModelChoice{"jev", "j"}, plain, io, opts);
    CHECK(events[0].value("code", std::string()) == "tools_unsupported");
    as::runTurn(session, store, "oi", doc, host, std::nullopt, plain, io, opts);
    CHECK(requests.size() == before);
    CHECK(events[2].value("code", std::string()) == "model_not_configured");

    CHECK(!as::SessionStore::isValidId("../fora"));
    CHECK(!store.load("../fora").has_value());
    CHECK(store.remove(session.id));
    CHECK(store.list().size() == 0);
    fs::remove_all(root);
}

TEST(assistant_talks_to_a_real_http_model_in_both_protocols) {
    namespace as = taskforge::assistant;
    namespace rt = taskforge::rt;
    const char* py = taskforge::platform::commandExists("python") ? "python" : (taskforge::platform::commandExists("python3") ? "python3" : nullptr);
    if (!py) { std::cout << "  (python ausente: teste ignorado)" << std::endl; return; }

    auto root = makeTempDir("assistant-http");
    fs::path log = root / "requests.log";
    int port = 18000 + (std::rand() % 2000);
    taskforge::platform::ProcessSpec spec;
    spec.argv = {py, fs::absolute(fs::path("CppCore") / "tests" / "e2e" / "mock_assistant_llm.py").string(),
                 std::to_string(port), log.string()};
    std::string err;
    auto server = taskforge::platform::Process::start(spec, err);
    CHECK(server != nullptr);
    if (!server) return;
    std::string base = "http://127.0.0.1:" + std::to_string(port);
    for (int i = 0; i < 50; ++i) {
        rt::HttpRequest ping;
        ping.method = "GET";
        ping.url = base + "/";
        ping.timeoutSeconds = 2;
        if (rt::httpSend(ping).status == 200) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    setTestEnv("DEEPSEEK_BASE_URL", base.c_str());
    setTestEnv("DEEPSEEK_API_KEY", "test-key-assistant");
    setTestEnv("ANTHROPIC_BASE_URL", base.c_str());
    setTestEnv("ANTHROPIC_API_KEY", "test-key-assistant");

    auto turn = [&](const std::string& provider, std::vector<json>& events, as::GraphDocument& doc) {
        FakeAssistantHost host;
        as::SessionStore store(root);
        as::Session session = as::SessionStore::create();
        as::TurnIO io;
        io.emit = [&](const json& e) { events.push_back(e); };
        as::runTurn(session, store, "crie um agente", doc, host, as::ModelChoice{provider, "modelo-teste"},
                    [](const rt::ChatRequest& r) { return rt::chatOnce(r); }, io, as::TurnOptions{});
    };

    for (const char* provider : {"deepseek", "anthropic"}) {
        std::vector<json> events;
        as::GraphDocument doc;
        turn(provider, events, doc);
        CHECK(doc.nodeCount() == 1);
        bool text = false;
        for (const auto& e : events) {
            if (e.value("type", std::string()) == "text" && e.value("text", std::string()).find("Criei o vertice node_") == 0) text = true;
        }
        CHECK(text);
        CHECK(!events.empty() && events.back().value("type", std::string()) == "done" && events.back().value("ok", false));
    }
    std::string requests = taskforge::jsonutil::readFile(log);
    CHECK(requests.find("/chat/completions") != std::string::npos);
    CHECK(requests.find("/v1/messages") != std::string::npos);
    CHECK(requests.find("\"tool_result\"") != std::string::npos);   // o resultado voltou no formato Anthropic

    // Chave errada: erro com codigo estavel, sem travar o laco.
    setTestEnv("DEEPSEEK_API_KEY", "chave-errada");
    std::vector<json> events;
    as::GraphDocument doc;
    turn("deepseek", events, doc);
    std::string code;
    for (const auto& e : events) if (e.value("type", std::string()) == "error") code = e.value("code", std::string());
    CHECK(code == "http_error");
    CHECK(doc.nodeCount() == 0);

    for (const char* k : {"DEEPSEEK_BASE_URL", "DEEPSEEK_API_KEY", "ANTHROPIC_BASE_URL", "ANTHROPIC_API_KEY"}) setTestEnv(k, "");
    server->kill();
    fs::remove_all(root);
}

TEST(git_sync_service_and_project_manifest_config) {
    using namespace taskforge::project;

    // 1. Deteccao de servicos comerciais
    CHECK(GitSyncService::isCommercialService("github"));
    CHECK(GitSyncService::isCommercialService("https://github.com/org/repo.git"));
    CHECK(GitSyncService::isCommercialService("gitlab.com"));
    CHECK(GitSyncService::isCommercialService("bitbucket.org"));
    CHECK(!GitSyncService::isCommercialService("gitea.local"));
    CHECK(!GitSyncService::isCommercialService("https://git.empresa.com.br"));

    // 2. Serializacao do Manifesto com Git
    ProjectManifest m;
    m.projectId = "test-git-proj";
    m.name = "Projeto Git Teste";
    m.git.enabled = true;
    m.git.provider = "github";
    m.git.serverUrl = "https://github.com";
    m.git.username = "frankalcantara";
    m.git.repositoryName = "projeto-git-teste";
    m.git.remoteUrl = "https://github.com/frankalcantara/projeto-git-teste.git";
    m.git.isPrivate = true;
    m.git.autoCommitOnRun = true;
    m.git.autoCommitOnShutdown = true;

    taskforge::Json j = m;
    CHECK(j["git"]["enabled"] == true);
    CHECK(j["git"]["isPrivate"] == true);
    CHECK(j["git"]["provider"] == "github");
    CHECK(j["git"]["repositoryName"] == "projeto-git-teste");

    // Desserializacao
    auto m2 = j.get<ProjectManifest>();
    CHECK(m2.git.enabled == true);
    CHECK(m2.git.isPrivate == true);
    CHECK(m2.git.username == "frankalcantara");
    CHECK(m2.git.repositoryName == "projeto-git-teste");
    CHECK(m2.git.remoteUrl == "https://github.com/frankalcantara/projeto-git-teste.git");

    // 3. Verificacao de presenca do Git na maquina
    std::string ver;
    bool installed = GitSyncService::checkGitInstalled(ver);
    CHECK(installed && !ver.empty());

    // 4. Identidade do SO
    std::string uName, uEmail;
    bool hasId = GitSyncService::getSystemIdentity(uName, uEmail);
    CHECK(hasId && !uName.empty());
}

TEST(standalone_runner_service_and_manifest_config) {
    using namespace taskforge::project;
    // 1. Configuracao do Runner e serializacao
    ProjectManifest m;
    m.projectId = "runner-test-id";
    m.name = "Runner Test Project";
    m.runner.enabled = true;
    m.runner.title = "Finance Report Runner";
    m.runner.description = "Automated corporate financial pipeline runner";
    m.runner.version = "1.2.0";
    m.runner.outputDir = "dist/runner";
    m.runner.outputName = "finance_runner.exe";
    m.runner.autoStart = true;
    m.runner.defaultTab = "diagnostics";
    m.runner.allowRestart = true;

    ProjectRunnerParameter param;
    param.id = "month";
    param.label = "Mes de Referencia";
    param.type = "string";
    param.defaultValue = "2026-10";
    param.description = "Mes para geracao do relatorio contabil";
    m.runner.parameters.push_back(param);

    taskforge::Json j = m;
    CHECK(j["runner"]["enabled"] == true);
    CHECK(j["runner"]["title"] == "Finance Report Runner");
    CHECK(j["runner"]["outputName"] == "finance_runner.exe");
    CHECK(j["runner"]["defaultTab"] == "diagnostics");
    CHECK(j["runner"]["parameters"].size() == 1);
    CHECK(j["runner"]["parameters"][0]["id"] == "month");

    // Desserializacao
    auto m2 = j.get<ProjectManifest>();
    CHECK(m2.runner.enabled == true);
    CHECK(m2.runner.title == "Finance Report Runner");
    CHECK(m2.runner.version == "1.2.0");
    CHECK(m2.runner.parameters.size() == 1);
    CHECK(m2.runner.parameters[0].defaultValue == "2026-10");

    // 2. Teste do RunnerService::exportRunner
    auto tempRoot = makeTempDir("runner-export-test");
    taskforge::core::Graph g;
    taskforge::core::NodeData node;
    node.id = "n1";
    node.label = "Tarefa1";
    node.type = taskforge::core::TaskflowNodeType::Static;
    node.customLambdaBody = "int x = 42;";
    g.addNode(node);

    taskforge::core::NodeData ask;
    ask.id = "ask";
    ask.label = "Pergunta";
    ask.type = taskforge::core::TaskflowNodeType::Interaction;
    ask.interactionSettings = {
        {"schemaSource", "static"},
        {"channel", "any"},
        {"timeoutSeconds", 30},
        {"onTimeout", "fail"},
        {"sensitive", true},
        {"form", {{"title", "Pergunta"}, {"fields", taskforge::Json::array({
            taskforge::Json{{"id", "resposta"}, {"type", "text"}, {"label", "Resposta"}, {"required", true}}
        })}}}
    };
    g.addNode(ask);

    auto res = RunnerService::exportRunner(tempRoot, m, g);
    CHECK(res.ok == true);
    CHECK(fs::exists(res.outputDir));
    CHECK(fs::exists(res.manifestPath));

    // Valida conteudo do runner_manifest.json gerado
    auto parsed = taskforge::jsonutil::parseFile(res.manifestPath);
    CHECK(parsed["format"] == "arestrix-standalone-runner");
    CHECK(parsed["title"] == "Finance Report Runner");
    CHECK(parsed["defaultTab"] == "diagnostics");
    CHECK(parsed["graph"]["nodes"].size() == 2);
    CHECK(parsed["interactions"].size() == 1);
    CHECK(parsed["interactions"][0]["id"] == "ask");
    CHECK(parsed["interactions"][0]["channel"] == "any");
    CHECK(parsed["interactions"][0]["sensitive"] == true);
    CHECK(parsed["interactionRequirements"]["count"] == 1);
    CHECK(parsed["interactionRequirements"]["host"] == false);
    CHECK(res.warnings.size() == 1);
    CHECK(res.warnings[0].find("host interativo") != std::string::npos);

    fs::remove_all(tempRoot);
}

TEST(runner_security_audit_blocks_secrets_fail_closed) {
    using namespace taskforge::project;
    std::string err;

    // 1. Textos limpos ou com placeholders de variaveis de ambiente devem passar
    CHECK(RunnerService::auditTextForSecrets("Variavel de ambiente: DEEPSEEK_API_KEY", "test", err) == true);
    CHECK(RunnerService::auditTextForSecrets("Referencia: ${OPENAI_API_KEY}", "test", err) == true);
    CHECK(RunnerService::auditTextForSecrets("Chave de exemplo: your-key-here", "test", err) == true);

    // 2. Chaves reais devem ser bloqueadas (montadas dinamicamente para nao sujar o arquivo de teste)
    std::string fakeSk = std::string("s") + "k-" + "1234567890abcdef1234567890abcdef";
    CHECK(RunnerService::auditTextForSecrets("chave real: " + fakeSk, "test", err) == false);
    CHECK(!err.empty());

    std::string fakeAIza = std::string("AI") + "zaSyD98765432109876543210987654321098";
    CHECK(RunnerService::auditTextForSecrets("google key: " + fakeAIza, "test", err) == false);

    std::string fakePriv = std::string("-----BEGIN ") + "RSA PRIVATE KEY-----";
    CHECK(RunnerService::auditTextForSecrets(fakePriv, "test", err) == false);

    std::string fakeGhp = std::string("gh") + "p_123456789012345678901234567890123456";
    CHECK(RunnerService::auditTextForSecrets("token: " + fakeGhp, "test", err) == false);

    // 3. exportRunner deve abortar se um segredo for injetado no grafo
    auto tempRoot = makeTempDir("runner-sec-test");
    ProjectManifest m;
    m.projectId = "sec-test";
    m.name = "Security Test";
    m.runner.enabled = true;
    m.runner.outputDir = "dist/runner";

    taskforge::core::Graph g;
    taskforge::core::NodeData badNode;
    badNode.id = "bad_node";
    badNode.label = "LeakNode";
    badNode.type = taskforge::core::TaskflowNodeType::Static;
    badNode.customLambdaBody = "std::string key = \"" + fakeSk + "\";";
    g.addNode(badNode);

    auto res = RunnerService::exportRunner(tempRoot, m, g);
    CHECK(res.ok == false);
    CHECK(res.error.find("Segredo detectado") != std::string::npos || res.error.find("fail-closed") != std::string::npos);

    fs::remove_all(tempRoot);
}

TEST(runner_security_cryptographic_signature_fail_closed) {
    using namespace taskforge::project;

    // 1. Validacao de vetores SHA-256 conhecidos (NIST FIPS 180-4)
    CHECK(RunnerSecurity::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(RunnerSecurity::sha256("hello world") == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");

    // 2. Assinatura e verificacao de integridade de pacote
    auto tempRoot = makeTempDir("runner-sig-test");
    auto manifestPath = tempRoot / "runner_manifest.json";
    {
        std::ofstream out(manifestPath);
        out << "{\n  \"name\": \"TestPipeline\",\n  \"version\": \"1.0.0\"\n}\n";
    }

    std::string testSigningSecret = "test-corp-secret-signing-key";
    auto sigRes = RunnerSecurity::signPackage(
        manifestPath,
        testSigningSecret,
        "hmac-sha256",
        "Arestrix Corporate Security",
        "key-corp-01"
    );

    CHECK(sigRes.ok == true);
    CHECK(!sigRes.signature.empty());
    CHECK(!sigRes.packageHash.empty());
    CHECK(fs::exists(tempRoot / "runner_manifest.json.sig"));

    // 3. Verificacao valida deve passar
    std::string vErr;
    bool valid = RunnerSecurity::verifyPackage(manifestPath, testSigningSecret, true, vErr);
    CHECK(valid == true);
    CHECK(vErr.empty());

    // 4. Delegacao do RunnerService::verifyPackage
    bool validViaService = RunnerService::verifyPackage(manifestPath, testSigningSecret, true, vErr);
    CHECK(validViaService == true);

    // 5. Tentativa de adulteracao do pacote (ataque de adulteracao de grafo/manifesto)
    {
        auto jTamper = taskforge::jsonutil::parseFile(manifestPath);
        jTamper["version"] = "9.9.9-malicious";
        std::ofstream out(manifestPath, std::ios::trunc);
        out << jTamper.dump(2);
    }

    // Fail-Closed: a verificacao DEVE falhar imediatamente e bloquear a execucao
    bool tamperedValid = RunnerSecurity::verifyPackage(manifestPath, testSigningSecret, true, vErr);
    CHECK(tamperedValid == false);
    CHECK(!vErr.empty());
    CHECK(vErr.find("Bloqueio de seguranca") != std::string::npos || vErr.find("fail-closed") != std::string::npos);

    // 6. Manifesto sem assinatura com politica obrigatoria (fail-closed)
    auto unsignedManifest = tempRoot / "unsigned_manifest.json";
    {
        std::ofstream out(unsignedManifest);
        out << "{\n  \"name\": \"Unsigned\",\n  \"version\": \"1.0.0\"\n}\n";
    }
    bool unsignedValid = RunnerSecurity::verifyPackage(unsignedManifest, testSigningSecret, true, vErr);
    CHECK(unsignedValid == false);
    CHECK(vErr.find("fail-closed") != std::string::npos);

    fs::remove_all(tempRoot);
}

TEST(runner_corporate_vault_and_memory_purging) {
    using namespace taskforge::project;

    // 1. Limpeza segura de memoria RAM (SecureZeroMemory)
    std::string testSensitiveBuffer = "test_sensitive_memory_wipe_buffer_123456789";
    size_t origSize = testSensitiveBuffer.size();
    CHECK(origSize > 0);
    secureWipeString(testSensitiveBuffer);
    CHECK(testSensitiveBuffer.empty());

    char rawBuffer[64];
    std::memset(rawBuffer, 0xAA, sizeof(rawBuffer));
    secureWipeMemory(rawBuffer, sizeof(rawBuffer));
    bool allZero = true;
    for (size_t i = 0; i < sizeof(rawBuffer); ++i) {
        if (rawBuffer[i] != 0) allZero = false;
    }
    CHECK(allZero == true);

    // 2. Resolucao corporativa em cascata com fallback para Variaveis de Ambiente
#ifdef _WIN32
    _putenv("ARESTRIX_TEST_CORP_TOKEN=CorporateTokenValue123");
#else
    setenv("ARESTRIX_TEST_CORP_TOKEN", "CorporateTokenValue123", 1);
#endif

    RunnerVaultConfig vConfig;
    vConfig.provider = "windows-credential-vault";
    vConfig.fallbackToEnvironment = true;

    std::string resolvedSec;
    std::string rErr;
    bool resOk = CorporateSecretResolver::resolve("ARESTRIX_TEST_CORP_TOKEN", vConfig, resolvedSec, rErr);
    CHECK(resOk == true);
    CHECK(resolvedSec == "CorporateTokenValue123");
    secureWipeString(resolvedSec);

    // Tentativa de segredo inexistente deve falhar limpa sem crash
    std::string notFound;
    bool notFoundOk = CorporateSecretResolver::resolve("NON_EXISTENT_ARESTRIX_SECRET_XYZ", vConfig, notFound, rErr);
    CHECK(notFoundOk == false);
    CHECK(!rErr.empty());

    // 3. Teste do Windows Credential Vault (se disponivel no Windows)
#ifdef _WIN32
    if (WindowsCredentialVault::isAvailable()) {
        std::string winErr;
        std::string testKey = "TestCorpRunnerKey";
        std::string testVal = "SecretDPAPIProtectedValue42";

        bool stored = WindowsCredentialVault::storeSecret(testKey, testVal, winErr);
        if (stored) {
            std::string readVal;
            bool readOk = WindowsCredentialVault::readSecret(testKey, readVal, winErr);
            CHECK(readOk == true);
            CHECK(readVal == testVal);
            secureWipeString(readVal);

            bool deleted = WindowsCredentialVault::deleteSecret(testKey, winErr);
            CHECK(deleted == true);
        }
    }
#endif
}

TEST(runner_enterprise_packaging_wix_msix_and_intune) {
    using namespace taskforge::project;

    // 1. Validacao de GUID deterministico RFC 4122
    std::string guid1 = RunnerPackaging::formatGuid("Arestrix.Finance.Runner");
    std::string guid2 = RunnerPackaging::formatGuid("Arestrix.Finance.Runner");
    CHECK(guid1 == guid2); // Determinismo
    CHECK(guid1.size() == 38);
    CHECK(guid1.front() == '{' && guid1.back() == '}');
    CHECK(guid1[15] == '4'); // UUID v4 version nibble

    std::string guidOther = RunnerPackaging::formatGuid("Arestrix.Other.Runner");
    CHECK(guid1 != guidOther);

    // 2. Geracao de instalador corporativo WiX MSI para Intune e SCCM
    auto tempRoot = makeTempDir("runner-packaging-test");
    ProjectManifest m;
    m.name = "BillingPipeline";
    m.version = "1.3.0";
    m.description = "Corporate automated billing runner";
    m.runner.enabled = true;
    m.runner.title = "Billing Pipeline Runner";
    m.runner.packaging.format = "msi-installer";
    m.runner.packaging.appId = "Arestrix.Billing.Runner";
    m.runner.packaging.publisher = "CN=Corporate TI, O=Enterprise, C=BR";
    m.runner.packaging.silentInstallArgs = "/qn ALLUSERS=1";
    m.runner.packaging.createDesktopShortcut = true;
    m.runner.packaging.createStartMenuShortcut = true;

    // Cria arquivos ficticios para integridade do checksum
    {
        std::ofstream(tempRoot / "billing_runner.exe") << "MZ-BINARY-STUB";
        std::ofstream(tempRoot / "runner_manifest.json") << "{\"format\":\"runner\"}";
        std::ofstream(tempRoot / "run.bat") << "@echo off";
    }

    auto pkgRes = RunnerPackaging::generatePackagingArtifacts(tempRoot, m, "billing_runner.exe");
    CHECK(pkgRes.ok == true);
    CHECK(fs::exists(tempRoot / "installer.wxs"));
    CHECK(fs::exists(tempRoot / "build_msi.bat"));
    CHECK(fs::exists(tempRoot / "build_msi.ps1"));
    CHECK(fs::exists(tempRoot / "intune_detection_rule.ps1"));
    CHECK(fs::exists(tempRoot / "intune_install_command.txt"));
    CHECK(fs::exists(tempRoot / "SHA256SUMS"));

    // Inspeciona conteudo do installer.wxs
    {
        std::ifstream in(tempRoot / "installer.wxs");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string wxsContent = ss.str();
        CHECK(wxsContent.find("<Wix") != std::string::npos);
        CHECK(wxsContent.find("Billing Pipeline Runner") != std::string::npos);
        CHECK(wxsContent.find("Version=\"1.3.0\"") != std::string::npos);
        CHECK(wxsContent.find("Manufacturer=\"CN=Corporate TI, O=Enterprise, C=BR\"") != std::string::npos);
        CHECK(wxsContent.find("InstallScope=\"perMachine\"") != std::string::npos);
    }

    // Inspeciona regra de deteccao Intune
    {
        std::ifstream in(tempRoot / "intune_detection_rule.ps1");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string psContent = ss.str();
        CHECK(psContent.find("$AppName = \"Billing Pipeline Runner\"") != std::string::npos);
        CHECK(psContent.find("exit 0") != std::string::npos);
        CHECK(psContent.find("exit 1") != std::string::npos);
    }

    // Inspeciona SHA256SUMS
    {
        std::ifstream in(tempRoot / "SHA256SUMS");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string sums = ss.str();
        CHECK(!sums.empty());
        CHECK(sums.find("billing_runner.exe") != std::string::npos);
        CHECK(sums.find("runner_manifest.json") != std::string::npos);
    }

    // 3. Geracao de pacote modernizado MSIX (AppxManifest.xml)
    m.runner.packaging.format = "msix-package";
    auto msixRes = RunnerPackaging::generatePackagingArtifacts(tempRoot, m, "billing_runner.exe");
    CHECK(msixRes.ok == true);
    CHECK(fs::exists(tempRoot / "AppxManifest.xml"));
    CHECK(fs::exists(tempRoot / "package_msix.ps1"));

    {
        std::ifstream in(tempRoot / "AppxManifest.xml");
        std::stringstream ss;
        ss << in.rdbuf();
        std::string xmlContent = ss.str();
        CHECK(xmlContent.find("<Package") != std::string::npos);
        CHECK(xmlContent.find("runFullTrust") != std::string::npos);
        CHECK(xmlContent.find("Name=\"Arestrix.Billing.Runner\"") != std::string::npos);
    }

    fs::remove_all(tempRoot);
}

static int runInteractionHostChild(const std::string& mode) {
    const bool timedOutMode = mode == "timeout";
    const std::string spec = std::string(R"({
        "schemaSource": "static",
        "form": {"title": "Host", "fields": [
            {"id": "tipo", "type": "choice", "label": "Tipo", "required": true,
             "options": [{"value": "pf"}, {"value": "pj"}]},
            {"id": "obs", "type": "text", "label": "Observacao", "required": false}
        ]},
        "channel": "studio",
        "timeoutSeconds": )") + (timedOutMode ? "1" : "5") + R"(,
        "onTimeout": "fail",
        "sensitive": false
    })";
    try {
        std::string result = taskforge::rt::runInteraction("ask", "HostTest", spec, "contexto");
        std::cout << "[HELPER_RESULT] " << result << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[HELPER_ERROR] " << ex.what() << "\n";
        return 1;
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--interaction-host-child") {
        return runInteractionHostChild("normal");
    }
    if (argc > 1 && std::string(argv[1]) == "--interaction-host-child-timeout") {
        return runInteractionHostChild("timeout");
    }
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
