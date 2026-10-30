// Testes do nucleo C++ do Arestrix (sem framework externo).
// Executar: ctest --test-dir build  (ou ./build/arestrix_tests)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
#include "memory/MemorySanitizer.hpp"
#include "memory/MemoryExtractor.hpp"
#include "memory/EmbeddingProvider.hpp"
#include "memory/MemoryConsolidator.hpp"
#include "memory/MemoryIndexer.hpp"
#include "memory/MemoryInstitutionApi.hpp"
#include "memory/MemoryInstitutionService.hpp"
#include "memory/MemoryPlaybookCompiler.hpp"
#include "memory/MemoryRanker.hpp"
#include "memory/MemoryVectorIndex.hpp"
#include "platform/Process.hpp"
#include "project/OpenableFiles.hpp"
#include "libraries/GitHubSource.hpp"
#include "project/GitSyncService.hpp"
#include "project/MemoryDatabaseGuard.hpp"
#include "sqlite3.h"
#include "project/RunnerService.hpp"
#include "project/CorporateVault.hpp"
#include "project/RunnerSecurity.hpp"
#include "project/RunnerPackaging.hpp"
#include "assistant/AssistantSession.hpp"
#include "assistant/AssistantTools.hpp"
#include "assistant/GraphDocument.hpp"
#include "rt/ModelClient.hpp"
#include "rt/Runtime.hpp"
#include "rt/AsyncCalls.hpp"
#include "rt/CallGraph.hpp"
#include "rt/Interaction.hpp"
#include "rt/Notification.hpp"
#include "local/LocalRuntime.hpp"
#include "gateway/GatewayStore.hpp"
#include "rt/Decision.hpp"
#include "rt/Http.hpp"
#include "rt/NetGuard.hpp"
#include "rt/McpTools.hpp"
#include "rt/McpVertex.hpp"
#include "rt/compress/ContentRouter.hpp"
#include "rt/compress/SmartCrusher.hpp"
#include "rt/compress/LogCompressor.hpp"
#include "rt/compress/CodeCompressor.hpp"
#include "rt/compress/CcrStore.hpp"
#include "rt/compress/ProseTokenizer.hpp"
#include "rt/compress/ProseModel.hpp"
#include "rt/compress/ProseModelInstaller.hpp"
#include "rt/compress/ProseCompressor.hpp"
#include "rt/compress/ProseRecommendation.hpp"
#include "rt/compress/ContextCompressor.hpp"
#include "native/NativeTools.hpp"
#include "rt/Runtime.hpp"
#include "rt/Trace.hpp"
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

// Provider deterministico para testar o caminho hibrido sem rede/modelo real.
class TopicTestEmbeddingProvider : public taskforge::memory::IEmbeddingProvider {
public:
    bool available() const override { return true; }
    std::string modelId() const override { return "test-topic-3"; }
    int dim() const override { return 3; }

    bool embed(const std::vector<std::string>& texts, bool,
               std::vector<taskforge::memory::Embedding>& out, std::string& error) override {
        ++calls;
        out.clear();
        for (const auto& original : texts) {
            std::string text = original;
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            auto has = [&](const char* needle) { return text.find(needle) != std::string::npos; };
            float design = (has("fonte") || has("font") || has("tipografia") ||
                            has("legibilidade") || has("contraste") || has("kerning") ||
                            has("design")) ? 1.0f : 0.0f;
            float failure = (has("erro") || has("falha") || has("rede") || has("error")) ? 1.0f : 0.0f;
            float drawing = (has("svg") || has("desenho") || has("vetor")) ? 1.0f : 0.0f;
            if (design == 0.0f && failure == 0.0f && drawing == 0.0f) design = 0.1f;
            taskforge::memory::Embedding embedding;
            embedding.model = modelId();
            embedding.values = {design, failure, drawing};
            embedding.dim = 3;
            out.push_back(std::move(embedding));
        }
        if (out.size() != texts.size()) {
            error = "quantidade incorreta";
            return false;
        }
        return true;
    }

    int calls = 0;
};

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

TEST(memory_tool_enforces_quota_and_charset) {
    using taskforge::memory::MemoryService;
    const fs::path dir = makeTempDir("memory-tool-quota");
    const fs::path db = dir / "memory.db";
    const std::string pId = "memory-tool-quota";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));

    taskforge::rt::ToolContext ctx;
    ctx.projectId = pId;
    ctx.agentId = "agente";
    ctx.runId = "run-quota";
    ctx.memoryWriteEnabled = true;
    ctx.memoryDomains = {"teste"};
    ctx.memoryMaxLessonsPerRun = 2;
    ctx.memoryMaxLessonChars = 100;
    ctx.allowed = {"memory_propose_lesson"};

    // Dominio nao autorizado continua bloqueado.
    auto domainErr = taskforge::rt::executeTool(ctx, "memory_propose_lesson",
        {{"domain", "outro"}, {"rule", "curta"}, {"trigger", "manual"}});
    CHECK(domainErr.isError);
    CHECK(ctx.memoryLessonsWritten == 0);

    // Regra acima do limite de caracteres.
    ctx.memoryMaxLessonsPerRun = 5;
    auto longErr = taskforge::rt::executeTool(ctx, "memory_propose_lesson",
        {{"domain", "teste"}, {"rule", std::string(130, 'x')}, {"trigger", "manual"}});
    CHECK(longErr.isError);
    CHECK(ctx.memoryLessonsWritten == 0);

    // Duas licoes passam; a terceira e bloqueada pelo limite por execucao.
    ctx.memoryMaxLessonChars = 1000;
    ctx.memoryMaxLessonsPerRun = 2;
    CHECK(!taskforge::rt::executeTool(ctx, "memory_propose_lesson",
        {{"domain", "teste"}, {"rule", "regra-um"}, {"trigger", "manual"}}).isError);
    CHECK(!taskforge::rt::executeTool(ctx, "memory_propose_lesson",
        {{"domain", "teste"}, {"rule", "regra-dois"}, {"trigger", "manual"}}).isError);
    CHECK(ctx.memoryLessonsWritten == 2);
    auto limitErr = taskforge::rt::executeTool(ctx, "memory_propose_lesson",
        {{"domain", "teste"}, {"rule", "regra-tres"}, {"trigger", "manual"}});
    CHECK(limitErr.isError);
    CHECK(limitErr.content.find("Limite de licoes") != std::string::npos);
    CHECK(memory.activeLessonCount(pId) == 0);
    CHECK(memory.listProposals(pId, "pending", 10).size() == 2);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
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
        R"({"caller":[{"target":"specialist","label":"Specialist","kind":"agent","maxCalls":2,"timeoutSeconds":10,"onError":"fail","description":"use specialist","notify":false}]})");

    auto targets = callTargetsFor("caller");
    CHECK(targets.size() == 1);
    CHECK(targets[0].id == "specialist");
    CHECK(targets[0].maxCalls == 2);
    CHECK(targets[0].description == "use specialist");
    CHECK(!targets[0].notify);
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

TEST(interaction_generator_inherits_global_model) {
    using taskforge::compiler::TaskflowGenerator;
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {
            {"graphName", "GeneratedForm"}, {"description", ""}, {"targetStandard", "C++20"},
            {"version", "1"}, {"projectId", "test"},
            {"globalDefaults", {{"model", {{"provider", "deepseek"}, {"model", "deepseek-flash"}}}}}
        }},
        {"nodes", json::array({
            json{{"id", "ask"}, {"label", "Pergunta"}, {"type", "interaction"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array()}, {"outputs", json::array()},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"interactionSettings", json{
                     {"schemaSource", "vertex"},
                     {"form", json{{"title", "Pergunta"}, {"fields", json::array()}}},
                     {"generator", json::object()},
                     {"channel", "any"}, {"onTimeout", "fail"}, {"timeoutSeconds", 300}
                 }}}
        })},
        {"edges", json::array()}
    };
    taskforge::core::Graph graph;
    graph.fromJson(schema);
    const auto* ask = graph.getNode("ask");
    CHECK(ask != nullptr);
    if (!ask) return;
    json spec = TaskflowGenerator::buildInteractionSpec(graph, *ask);
    CHECK(spec["generator"]["provider"] == "deepseek");
    CHECK(spec["generator"]["model"] == "deepseek-flash");
    CHECK(spec["generator"]["fallback"] == "fail");
}

TEST(subflow_callable_emits_native_function) {
    using taskforge::compiler::TaskflowGenerator;
    json childSchema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "Child"}, {"projectId", "test"}, {"targetStandard", "C++20"}}},
        {"nodes", json::array({
            json{{"id", "child_task"}, {"label", "ChildTask"}, {"type", "static"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array()}, {"outputs", json::array()},
                 {"parameters", json::object()}, {"customLambdaBody", "std::cout << \"child\\n\";"}}
        })},
        {"edges", json::array()}
    };
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "Parent"}, {"projectId", "test"}, {"targetStandard", "C++20"}}},
        {"nodes", json::array({
            json{{"id", "caller"}, {"label", "Caller"}, {"type", "agent"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array()},
                 {"outputs", json::array({json{{"id", "caller_out_call"}, {"name", "call"}, {"direction", "output"}, {"type", "call"}}})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}},
            json{{"id", "child"}, {"label", "Child"}, {"type", "subflow"},
                 {"position", {{"x", 300}, {"y", 0}}},
                 {"inputs", json::array()}, {"outputs", json::array()},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"childGraph", childSchema}}
        })},
        {"edges", json::array({
            json{{"id", "call_child"}, {"sourceNodeId", "caller"}, {"sourcePortId", "caller_out_call"},
                 {"targetNodeId", "child"}, {"targetPortId", "child_in"}, {"kind", "call"},
                 {"callSettings", {{"maxCalls", 1}, {"onError", "fail"}}}}
        })}
    };
    taskforge::core::Graph graph;
    graph.fromJson(schema);
    std::string parentSource = TaskflowGenerator::generateSource(graph);
    CHECK(parentSource.find("registerSubflow(\"child\"") != std::string::npos);
    CHECK(parentSource.find("arx_subflow_0_child_run") != std::string::npos);
    auto sources = TaskflowGenerator::generateSubflowSources(graph);
    CHECK(sources.count("subflows.cpp") == 1);
    if (sources.count("subflows.cpp")) {
        CHECK(sources["subflows.cpp"].find("namespace arx_subflow_0_child") != std::string::npos);
        CHECK(sources["subflows.cpp"].find("int arx_main(") != std::string::npos);
        CHECK(sources["subflows.cpp"].find("arx_subflow_0_child_run") != std::string::npos);
        CHECK(sources["subflows.cpp"].find("const std::string& arx_subflow_input") != std::string::npos);
        CHECK(sources["subflows.cpp"].find("&arx_subflow_input") != std::string::npos);
    }
}

TEST(call_vertex_compiles_flow_call) {
    using taskforge::compiler::TaskflowGenerator;
    auto port = [](const std::string& id, const std::string& dir, const std::string& type) {
        return json{{"id", id}, {"name", id}, {"direction", dir}, {"type", type}};
    };
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "CallVertex"}, {"projectId", "test"}, {"targetStandard", "C++20"}}},
        {"nodes", json::array({
            json{{"id", "prep"}, {"label", "Prep"}, {"type", "agent"},
                 {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array({port("prep_in", "input", "flow")})},
                 {"outputs", json::array({port("prep_out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}},
            json{{"id", "invoke"}, {"label", "Invoke"}, {"type", "call"},
                 {"position", {{"x", 300}, {"y", 0}}},
                 {"inputs", json::array({port("invoke_in", "input", "flow")})},
                 {"outputs", json::array({port("invoke_out", "output", "flow"),
                                          port("invoke_call", "output", "call")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"callSettings", {{"input", ""}, {"source", {{"kind", "previous"}}}}}},
            json{{"id", "target"}, {"label", "Target"}, {"type", "agent"},
                 {"position", {{"x", 600}, {"y", 0}}},
                 {"inputs", json::array({port("target_in", "input", "flow")})},
                 {"outputs", json::array({port("target_out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""}}
        })},
        {"edges", json::array({
            json{{"id", "flow_prep_invoke"}, {"sourceNodeId", "prep"}, {"sourcePortId", "prep_out"},
                 {"targetNodeId", "invoke"}, {"targetPortId", "invoke_in"}, {"kind", "flow"}},
            json{{"id", "call_invoke_target"}, {"sourceNodeId", "invoke"}, {"sourcePortId", "invoke_call"},
                 {"targetNodeId", "target"}, {"targetPortId", "target_in"}, {"kind", "call"},
                 {"callSettings", {{"maxCalls", 1}, {"onError", "fail"}}}}
        })}
    };
    taskforge::core::Graph graph;
    graph.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(graph);
    CHECK(src.find("taskforge::rt::runFlowCall(\"invoke\"") != std::string::npos);
    CHECK(src.find("task_invoke") != std::string::npos);
    CHECK(src.find("task_prep.precede(task_invoke)") != std::string::npos);
    CHECK(src.find("task_target") == std::string::npos);
    CHECK(src.find("\"invoke\"") != std::string::npos);
    CHECK(src.find("\"from\":[\"prep\"]") != std::string::npos);

    // Sem a aresta de chamada, o vertice Call e invalido.
    schema["edges"] = json::array({schema["edges"][0]});
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

TEST(trace_records_tasks_and_calls_without_payloads) {
    using namespace taskforge::rt;
    const fs::path oldCwd = fs::current_path();
    const fs::path dir = makeTempDir("trace-run");
    const std::string oldRunId = taskforge::platform::getEnv("ARESTRIX_RUN_ID");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", "run-test-trace");
    fs::current_path(dir);

    traceTaskStart("flow", "Flow");
    traceTaskEnd("flow", "ok");
    traceCallStart("flow/target#1", "flow", "target", "Target", "agent", 1);
    traceTaskStart("target", "Target");
    traceTaskEnd("target", "ok");
    traceCallEnd("flow/target#1", "ok");
    finishTrace(true);

    const fs::path file = dir / ".arestrix" / "runs" / "run-test-trace" / "trace.json";
    CHECK(fs::is_regular_file(file));
    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(content.find("\"runId\":\"run-test-trace\"") != std::string::npos ||
          content.find("\"runId\": \"run-test-trace\"") != std::string::npos);
    CHECK(content.find("\"status\":\"success\"") != std::string::npos ||
          content.find("\"status\": \"success\"") != std::string::npos);
    CHECK(content.find("\"nodeId\":\"flow\"") != std::string::npos);
    CHECK(content.find("\"callId\":\"flow/target#1\"") != std::string::npos);
    CHECK(content.find("\"targetId\":\"target\"") != std::string::npos);
    CHECK(content.find("\"parentCallId\":\"flow/target#1\"") != std::string::npos);

    fs::current_path(oldCwd);
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", oldRunId);
    fs::remove_all(dir);
}

TEST(async_call_returns_pending_then_result_via_call_result) {
    using namespace taskforge::rt;
    const std::string oldNotifications = taskforge::platform::getEnv("ARESTRIX_NOTIFICATIONS");
    taskforge::platform::setProcessEnv("ARESTRIX_NOTIFICATIONS", "0");

    setCallRegistry(
        R"({"slow":{"kind":"subflow","label":"Slow"}})",
        R"({"caller":[{"target":"slow","label":"Slow","kind":"subflow","maxCalls":1,"allowAsync":true}]})");
    registerSubflow("slow", [](const std::string& input) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        return "eco:" + input;
    });

    auto targets = callTargetsFor("caller");
    auto defs = asyncCallToolDefinitions(targets, RuntimeLang::Pt);
    CHECK(defs.size() == 1);
    CHECK(defs[0].name == "call_result");
    auto callDefs = callToolDefinitions(targets, RuntimeLang::Pt);
    CHECK(!callDefs.empty());
    CHECK(callDefs[0].parameters.dump().find("\"async\"") != std::string::npos);

    auto pending = invokeCallAsync("caller", "run", targets,
                                   taskforge::Json{{"target", "slow"}, {"input", "x"}, {"async", true}},
                                   RuntimeLang::Pt);
    CHECK(!pending.isError);
    auto p = taskforge::jsonutil::parse(pending.content);
    const std::string callId = p.value("callId", std::string());
    CHECK(!callId.empty());
    CHECK(p.value("status", std::string()) == "pending");

    auto early = fetchAsyncCallResult("caller", "run", taskforge::Json{{"callId", callId}}, RuntimeLang::Pt);
    CHECK(!early.isError);
    const std::string earlyText = early.content;
    CHECK(earlyText.find("\"status\":\"pending\"") != std::string::npos ||
          earlyText.find("\"status\": \"pending\"") != std::string::npos ||
          earlyText.find("eco:x") != std::string::npos);

    drainAsyncCalls();
    auto done = fetchAsyncCallResult("caller", "run", taskforge::Json{{"callId", callId}}, RuntimeLang::Pt);
    CHECK(!done.isError);
    CHECK(done.content.find("eco:x") != std::string::npos);
    CHECK(done.content.find(callId) != std::string::npos);

    auto wrong = fetchAsyncCallResult("other", "run", taskforge::Json{{"callId", callId}}, RuntimeLang::Pt);
    CHECK(wrong.isError);

    const std::string oldAsync = taskforge::platform::getEnv("ARESTRIX_ASYNC_CALLS");
    taskforge::platform::setProcessEnv("ARESTRIX_ASYNC_CALLS", "0");
    auto disabled = invokeCallAsync("caller", "run", targets,
                                    taskforge::Json{{"target", "slow"}, {"input", "x"}, {"async", true}},
                                    RuntimeLang::Pt);
    CHECK(disabled.isError);
    taskforge::platform::setProcessEnv("ARESTRIX_ASYNC_CALLS", oldAsync);

    setCallRegistry("{}", "{}");
    taskforge::platform::setProcessEnv("ARESTRIX_NOTIFICATIONS", oldNotifications);
}

TEST(notification_channel_writes_jsonl_and_respects_opt_out) {
    using namespace taskforge::rt;
    const fs::path oldCwd = fs::current_path();
    const fs::path dir = makeTempDir("notification-run");
    const std::string oldRunId = taskforge::platform::getEnv("ARESTRIX_RUN_ID");
    const std::string oldEnabled = taskforge::platform::getEnv("ARESTRIX_NOTIFICATIONS");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", "run-test-notify");
    taskforge::platform::setProcessEnv("ARESTRIX_NOTIFICATIONS", "1");
    fs::current_path(dir);

    notify("caller", "call.completed", "success", "Chamada concluída", "caller -> specialist",
           taskforge::Json{{"target", "specialist"}, {"kind", "agent"}});

    const fs::path file = dir / ".arestrix" / "runs" / "run-test-notify" / "notifications.jsonl";
    CHECK(fs::is_regular_file(file));
    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(content.find("\"kind\":\"call.completed\"") != std::string::npos ||
          content.find("\"kind\": \"call.completed\"") != std::string::npos);
    CHECK(content.find("specialist") != std::string::npos);
    CHECK(content.find("\"severity\":\"success\"") != std::string::npos ||
          content.find("\"severity\": \"success\"") != std::string::npos);

    if (fs::is_regular_file(file)) {
        const auto before = fs::file_size(file);
        taskforge::platform::setProcessEnv("ARESTRIX_NOTIFICATIONS", "0");
        notify("caller", "call.failed", "error", "Chamada falhou", "nao deve gravar");
        CHECK(fs::file_size(file) == before);
    } else {
        CHECK(false);
    }

    fs::current_path(oldCwd);
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", oldRunId);
    taskforge::platform::setProcessEnv("ARESTRIX_NOTIFICATIONS", oldEnabled);
    fs::remove_all(dir);
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
                        if (sentAnswer) proc->closeStdin();
                        gotRequest = true;
                    }
                } else if (!line.empty()) {
                    // Diagnóstico: erros/saídas do processo filho chegam pelo mesmo pipe.
                    std::cout << "  [host child] " << line << std::endl;
                }
            }
        }
        if (status == taskforge::platform::ReadStatus::Eof) break;
    }

    proc->wait(5000, code);
    CHECK(gotRequest);
    CHECK(sentAnswer);
    std::cout << "  host child exit code: " << code << std::endl;
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

TEST(generator_critical_sections_use_key_and_concurrency) {
    auto base = tutorialGraph();
    json schema = base.toJson();
    for (auto& node : schema["nodes"]) {
        if (node["id"] == "a") {
            node["type"] = "critical";
            node["criticalSectionId"] = "alpha";
            node["parameters"] = json{{"maxConcurrency", 2}};
        } else if (node["id"] == "b") {
            node["type"] = "critical";
            node["criticalSectionId"] = "beta";
            node["parameters"] = json{{"maxConcurrency", 3}};
        }
    }

    taskforge::core::Graph g;
    g.fromJson(schema);
    const std::string src = taskforge::compiler::TaskflowGenerator::generateSource(g);

    // Uma secao por chave, com a concorrencia vinda de parameters.maxConcurrency.
    CHECK(src.find("tf::Semaphore critical_sec_alpha_0(2);") != std::string::npos);
    CHECK(src.find("tf::Semaphore critical_sec_beta_1(3);") != std::string::npos);
    CHECK(src.find("task_a.acquire(critical_sec_alpha_0);") != std::string::npos);
    CHECK(src.find("task_a.release(critical_sec_alpha_0);") != std::string::npos);
    CHECK(src.find("task_b.acquire(critical_sec_beta_1);") != std::string::npos);
    CHECK(src.find("task_b.release(critical_sec_beta_1);") != std::string::npos);
    // O semaforo global antigo nao existe mais.
    CHECK(src.find("critical_sec(1)") == std::string::npos);
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
                      {"version", "1"}, {"projectId", "proj-memoria"},
                       {"globalDefaults", {{"memory", {{"embeddingEnabled", true},
                                                       {"embeddingModel", "test/multilingual-embed"},
                                                       {"embeddingBaseUrl", "http://127.0.0.1:9999/v1"},
                                                       {"embeddingBatchSize", 8},
                                                       {"consolidationMode", "idle"},
                                                       {"consolidationIdleSeconds", 60},
                                                       {"consolidationDormantDays", 90}}}}}}},
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
                                                {"writeKeys", json::array({"resumo", "last_stage"})},
                                                {"maxLessonsPerRun", 5}, {"maxLessonChars", 1234},
                                                {"domains", json::array({"svg_typography"})}}}}}}
                 })},
        {"edges", json::array({json{{"id", "e1"}, {"sourceNodeId", "origem"}, {"sourcePortId", "origem_out"},
                                    {"targetNodeId", "agente"}, {"targetPortId", "agente_in"}}})}};
    taskforge::core::Graph g;
    g.fromJson(schema);
    std::string src = TaskflowGenerator::generateSource(g);

    CHECK(src.find("#include \"memory/MemoryService.hpp\"") != std::string::npos);
    CHECK(src.find("memory.configure(projectId, arx_mem_persistent, arx_mem_persistent ? arx_mem_db : std::string())") != std::string::npos);
    CHECK(src.find("memory.configureEmbeddings(projectId, arx_embedding_config)") != std::string::npos);
    CHECK(src.find("memory.configureMaintenance(projectId, taskforge::memory::maintenanceConfigFromJson("
                   "taskforge::Json::parse(") != std::string::npos);
    CHECK(src.find("\"consolidationMode\":\"idle\"") != std::string::npos);
    CHECK(src.find("\"consolidationIdleSeconds\":60") != std::string::npos);
    CHECK(src.find("\"consolidationDormantDays\":90") != std::string::npos);
    CHECK(src.find("arx_embedding_config.enabled = true;") != std::string::npos);
    CHECK(src.find("arx_embedding_config.model = \"test/multilingual-embed\";") != std::string::npos);
    CHECK(src.find("arx_embedding_config.baseUrl = \"http://127.0.0.1:9999/v1\";") != std::string::npos);
    CHECK(src.find("arx_embedding_config.batchSize = 8;") != std::string::npos);
    CHECK(src.find("ARESTRIX_PROJECT_ROOT") != std::string::npos);
    CHECK(src.find("ARESTRIX_MEMORY_DB") != std::string::npos);
    CHECK(src.find("ARESTRIX_TELEMETRY_DB") != std::string::npos);
    CHECK(src.find("memory.configureTelemetry(projectId, arx_telemetry_db)") != std::string::npos);
    CHECK(src.find("taskforge::rt::currentItemId(\"pipeline\")") != std::string::npos);
    CHECK(src.find("memory.buildBrief(projectId, arx_item_id, std::vector<std::string>{\"svg_typography\"}, "
                   "std::vector<std::string>{}, 800, true, false, &arx_brief_info, \"agente\")") != std::string::npos);
    CHECK(src.find("ARESTRIX_MEMORY_DISABLE") != std::string::npos);
    CHECK(src.find("recordBriefHits(projectId, taskforge::rt::currentRunId(\"agente\"), \"agente\", arx_brief_info)") != std::string::npos);
    CHECK(src.find("\"memoryEnabled\":true") != std::string::npos);
    CHECK(src.find("\"memoryTaskType\":\"agente\"") != std::string::npos);
    CHECK(src.find("\"memoryMaxLessonsPerRun\":5") != std::string::npos);
    CHECK(src.find("\"memoryMaxLessonChars\":1234") != std::string::npos);
    CHECK(src.find("\"memoryWriteKeys\"") == std::string::npos);
    CHECK(src.find("\"memory_search\"") != std::string::npos);
    CHECK(src.find("\"memory_propose_lesson\"") != std::string::npos);
    CHECK(src.find("runAgent(\"agente\", \"Agente\"") != std::string::npos);
    CHECK(src.find("arx_mem_ctx);") != std::string::npos);
    CHECK(src.find("catch (const std::exception& arx_mem_err)") != std::string::npos);
    CHECK(src.find("proposeLesson(projectId, \"svg_typography\", \"agent_failure\"") != std::string::npos);
    CHECK(src.find("setWorking(projectId, arx_item_id, \"last_stage\"") == std::string::npos);
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
    CHECK(offSrc.find("ARESTRIX_MEMORY_METRICS") == std::string::npos);
    CHECK(offSrc.find("const bool arx_mem_active = false;") != std::string::npos);

    // Blackboard fora do modo simples: nao ha escrita automatica de last_stage.
    json noLastSchema = schema;
    for (auto& node : noLastSchema["nodes"]) {
        if (node.value("id", std::string()) == "agente") {
            node["agentSettings"]["memory"]["writeKeys"] = json::array({"resumo"});
        }
    }
    taskforge::core::Graph noLastGraph;
    noLastGraph.fromJson(noLastSchema);
    std::string noLastSrc = TaskflowGenerator::generateSource(noLastGraph);
    CHECK(noLastSrc.find("setWorking(projectId, arx_item_id, \"last_stage\"") == std::string::npos);
    CHECK(noLastSrc.find("\"memoryWriteKeys\"") == std::string::npos);

    // Novo modelo: modo + tema, sem enabled/persistent/recordLessons por vertice.
    json modeSchema = schema;
    modeSchema["nodes"][1]["agentSettings"]["memory"] = json{
        {"mode", "learn"}, {"theme", "svg_typography"}, {"taskType", "svg_review"},
        {"contextBudgetTokens", 600}, {"maxLessonsPerRun", 5}, {"maxLessonChars", 1234}};
    taskforge::core::Graph modeGraph;
    modeGraph.fromJson(modeSchema);
    std::string modeSrc = TaskflowGenerator::generateSource(modeGraph);
    CHECK(modeSrc.find("\"memoryEnabled\":true") != std::string::npos);
    CHECK(modeSrc.find("\"memoryWriteEnabled\":true") != std::string::npos);
    CHECK(modeSrc.find("\"memoryMode\":\"learn\"") != std::string::npos);
    CHECK(modeSrc.find("\"memoryTaskType\":\"svg_review\"") != std::string::npos);
    CHECK(modeSrc.find("memory.buildBrief(projectId, arx_item_id, std::vector<std::string>{\"svg_typography\"}, "
                       "std::vector<std::string>{}, 600, true, false, &arx_brief_info, \"svg_review\")") != std::string::npos);
    CHECK(modeSrc.find("\"memory_propose_lesson\"") != std::string::npos);
    CHECK(modeSrc.find("const bool arx_mem_persistent = arx_mem_active && (arx_mem_learns || arx_mem_has_db);") != std::string::npos);

    json consultSchema = modeSchema;
    consultSchema["nodes"][1]["agentSettings"]["memory"]["mode"] = "consult";
    taskforge::core::Graph consultGraph;
    consultGraph.fromJson(consultSchema);
    std::string consultSrc = TaskflowGenerator::generateSource(consultGraph);
    CHECK(consultSrc.find("\"memoryEnabled\":true") != std::string::npos);
    CHECK(consultSrc.find("\"memoryWriteEnabled\":false") != std::string::npos);
    CHECK(consultSrc.find("\"memory_propose_lesson\"") == std::string::npos);
    CHECK(consultSrc.find("catch (const std::exception& arx_mem_err)") == std::string::npos);

    json offModeSchema = modeSchema;
    offModeSchema["nodes"][1]["agentSettings"]["memory"]["mode"] = "off";
    taskforge::core::Graph offModeGraph;
    offModeGraph.fromJson(offModeSchema);
    std::string offModeSrc = TaskflowGenerator::generateSource(offModeGraph);
    CHECK(offModeSrc.find("\"memoryEnabled\":false") != std::string::npos);
    CHECK(offModeSrc.find("memory.buildBrief(") == std::string::npos);
    CHECK(offModeSrc.find("\"memory_search\"") == std::string::npos);

    // recordMetrics nao forca mais a persistencia do conhecimento.
    json metricsSchema = offModeSchema;
    metricsSchema["metadata"]["globalDefaults"] = json{{"memory", json{{"recordMetrics", true}}}};
    taskforge::core::Graph metricsGraph;
    metricsGraph.fromJson(metricsSchema);
    std::string metricsSrc = TaskflowGenerator::generateSource(metricsGraph);
    CHECK(metricsSrc.find("const bool arx_mem_active = false;") != std::string::npos);
    CHECK(metricsSrc.find("ARESTRIX_MEMORY_METRICS") == std::string::npos);
}

TEST(generator_emits_context_compression_section) {
    using taskforge::compiler::TaskflowGenerator;
    auto port = [](const char* id, const char* name, const char* dir, const char* type) {
        return json{{"id", id}, {"name", name}, {"direction", dir}, {"type", type}};
    };
    const json compression = json{{"enabled", true}, {"minBlockTokens", 4096}};
    const json agentSettings = json{{"inheritCompression", false}, {"compression", compression}};
    json schema = {
        {"version", "1.0.0"},
        {"metadata", {{"graphName", "compression"}, {"description", ""}, {"targetStandard", "C++20"},
                      {"compilerConfig", {{"compiler", "msvc"}, {"cppStandard", "C++20"}, {"buildSystem", "cmake"},
                                          {"taskflowIncludePath", "third_party/taskflow"}, {"outputBinaryName", "c"},
                                          {"enableOpenMP", false}, {"enableCuda", false}, {"extraFlags", json::array()}}},
                      {"version", "1"}, {"projectId", "proj-compress"}, {"globalDefaults", json::object()}}},
        {"nodes", json::array({
            json{{"id", "agente"}, {"label", "Agente"}, {"type", "agent"}, {"position", {{"x", 0}, {"y", 0}}},
                 {"inputs", json::array({port("agente_in", "in", "input", "flow")})},
                 {"outputs", json::array({port("agente_out", "out", "output", "flow")})},
                 {"parameters", json::object()}, {"customLambdaBody", ""},
                 {"agentSettings", agentSettings}}
                 })},
        {"edges", json::array()}};
    taskforge::core::Graph graphWithCompression;
    graphWithCompression.fromJson(schema);
    const std::string src = TaskflowGenerator::generateSource(graphWithCompression);
    CHECK(src.find("\"compression\":{\"enabled\":true") != std::string::npos);
    CHECK(src.find("\"minBlockTokens\":4096") != std::string::npos);

    // Sem a secao, o runtime recebe um objeto vazio e mantem a compactacao desligada.
    schema["nodes"][0]["agentSettings"] = json::object();
    taskforge::core::Graph graphPlain;
    graphPlain.fromJson(schema);
    const std::string plainSrc = TaskflowGenerator::generateSource(graphPlain);
    CHECK(plainSrc.find("\"compression\":{}") != std::string::npos);
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

TEST(memory_store_enforces_project_lesson_cap) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-lesson-cap");
    MemoryStore store((dir / "memory.db").string(), 2);
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:cap", "project", "", "cap"));

    CHECK(!store.recordLesson("project:cap", "cap", "manual", "REGRA-CAP-1", "seed").empty());
    CHECK(!store.recordLesson("project:cap", "cap", "manual", "REGRA-CAP-2", "seed").empty());
    CHECK(store.recordLesson("project:cap", "cap", "manual", "REGRA-CAP-3", "seed").empty());
    CHECK(store.activeLessonCount("project:cap") == 2);

    // Restauracao/migracao pode reconstruir um projeto acima do teto de agentes.
    CHECK(!store.recordLesson("project:cap", "cap", "manual", "REGRA-CAP-RESTORE", "seed",
                              "snapshot_restore").empty());
    CHECK(store.activeLessonCount("project:cap") == 3);

    store.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_sanitizer_redacts_known_secrets) {
    using taskforge::memory::sanitizeJsonInPlace;
    using taskforge::memory::sanitizeText;

    const std::string secret = "sk-proj-" + std::string(40, 'a');
    const std::string awsKey = "AKIA" + std::string("ABCDEFGHIJKLMNOP");
    auto result = sanitizeText("chave " + secret + " e Bearer abcdefgh12345678 e AIza" + std::string(35, 'b') +
                               " e " + awsKey + " e ghp_" + std::string(30, 'c'));
    CHECK(result.redacted);
    CHECK(result.text.find("sk-proj-") == std::string::npos);
    CHECK(result.text.find("Bearer abc") == std::string::npos);
    CHECK(result.text.find("AIza") == std::string::npos);
    CHECK(result.text.find("AKIA") == std::string::npos);
    CHECK(result.text.find("ghp_") == std::string::npos);
    CHECK(result.text.find("[REDACTED]") != std::string::npos);

    auto assignment = sanitizeText("api_key=" + secret + " token: " + secret + " ?token=" + secret);
    CHECK(assignment.text.find(secret) == std::string::npos);
    CHECK(assignment.text.find("api_key=[REDACTED]") != std::string::npos);
    CHECK(assignment.text.find("?token=[REDACTED]") != std::string::npos);

    const std::string privateKeyText = "-----BEGIN " + std::string("RSA PRIVATE KEY") +
        "-----\nMIIEow\n-----END " + std::string("RSA PRIVATE KEY") + "-----";
    auto privateKey = sanitizeText(privateKeyText);
    CHECK(privateKey.text.find("MIIEow") == std::string::npos);
    CHECK(privateKey.text.find("[REDACTED PRIVATE KEY]") != std::string::npos);

    auto normal = sanitizeText("texto normal, sem segredo");
    CHECK(!normal.redacted);
    CHECK(normal.text == "texto normal, sem segredo");

    auto truncated = sanitizeText(std::string(50, 'x'), 10);
    CHECK(truncated.truncated);
    CHECK(truncated.text.size() == 10);

    taskforge::Json nested = taskforge::Json::object();
    nested["error"] = "Bearer " + std::string(30, 'z');
    nested["items"] = taskforge::Json::array({taskforge::Json("chave " + secret)});
    CHECK(sanitizeJsonInPlace(nested) >= 2);
    CHECK(nested.dump().find(secret) == std::string::npos);
    CHECK(nested.dump().find("Bearer " + std::string(30, 'z')) == std::string::npos);
}

TEST(memory_store_sanitizes_secrets) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-sanitize");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:sanitize", "project", "", "sanitize"));

    const std::string secret = "sk-proj-" + std::string(40, 'a');
    CHECK(!store.recordLesson("project:sanitize", "seguranca", "Bearer " + secret,
                              "Nunca gravar " + secret, "seed").empty());
    auto lessons = store.getLessons("project:sanitize", "seguranca");
    CHECK(lessons.size() == 1);
    CHECK(lessons[0].lessonRule.find(secret) == std::string::npos);
    CHECK(lessons[0].lessonRule.find("[REDACTED]") != std::string::npos);
    CHECK(lessons[0].triggerError.find(secret) == std::string::npos);

    CHECK(store.setWorking("project:sanitize", "pipeline", "token", taskforge::Json("valor " + secret)));
    auto working = store.getWorking("project:sanitize", "pipeline", "token");
    CHECK(working.is_string());
    CHECK(working.str().find(secret) == std::string::npos);
    CHECK(working.str().find("[REDACTED]") != std::string::npos);

    const std::string a = store.ensureNode("project:sanitize", "node-sanitize-a", "project", "Alpha");
    const std::string b = store.ensureNode("project:sanitize", "node-sanitize-b", "library",
                                           "Biblioteca " + secret);
    CHECK(!a.empty() && !b.empty());
    taskforge::Json value = taskforge::Json("Bearer " + secret);
    taskforge::Json meta = taskforge::Json::object();
    meta["note"] = "api_key=" + secret;
    CHECK(!store.recordEdge("project:sanitize", a, b, "USES", value, "2020-01-01T00:00:00Z", "",
                            0.9, "active", "test", "", meta).empty());
    auto edges = store.neighbors("project:sanitize", a, 1);
    CHECK(edges.size() == 1);
    CHECK(edges[0].valueJson.find(secret) == std::string::npos);
    CHECK(edges[0].metadata.find(secret) == std::string::npos);

    store.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_store_purge_removes_all_cognitive_and_telemetry_data) {
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryUsageRecord;
    const fs::path dir = makeTempDir("memory-purge");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:purge", "project", "", "purge"));

    const std::string lesson = store.recordLesson("project:purge", "purge", "manual", "REGRA-PURGE", "seed");
    CHECK(!lesson.empty());
    CHECK(store.setWorking("project:purge", "pipeline", "resumo", taskforge::Json("valor")));
    const std::string a = store.ensureNode("project:purge", "node-purge-a", "project", "Alpha");
    const std::string b = store.ensureNode("project:purge", "node-purge-b", "library", "Beta");
    CHECK(!a.empty() && !b.empty());
    CHECK(!store.recordEdge("project:purge", a, b, "USES", taskforge::Json("x"),
                            "2020-01-01T00:00:00Z", "", 0.8, "active", "test").empty());
    CHECK(!store.recordEpisode("project:purge", "run-purge", "node", "pipeline", "test", "episodio").empty());
    CHECK(store.recordHit("project:purge", "run-purge", "node", lesson, "lesson", true, false));
    MemoryUsageRecord usage;
    usage.scopeId = "project:purge";
    usage.runId = "run-purge";
    usage.nodeId = "node";
    usage.provider = "test";
    usage.model = "test";
    usage.totalTokens = 10;
    usage.outcome = "success";
    CHECK(store.recordUsage(usage));

    CHECK(store.purge("project:purge", taskforge::memory::MemoryPurgeOptions{false, true}));
    CHECK(store.activeLessonCount("project:purge") == 0);
    CHECK(store.getWorking("project:purge", "pipeline", "resumo").is_null());
    CHECK(store.usageSummary("project:purge")["runs"] == 0);
    CHECK(store.hitSummary("project:purge")["hits"] == 0);
    CHECK(store.neighbors("project:purge", a, 1).empty());

    // O escopo continua utilizavel depois do expurgo.
    CHECK(!store.recordLesson("project:purge", "purge", "manual", "REGRA-POS-PURGE", "seed").empty());

    store.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_store_prune_removes_only_old_telemetry) {
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryUsageRecord;
    const fs::path dir = makeTempDir("memory-prune");
    const fs::path db = dir / "memory.db";
    const std::string scope = "project:prune";

    {
        MemoryStore store(db.string());
        std::string error;
        CHECK(store.open(error));
        CHECK(store.ensureScope(scope, "project", "", "prune"));
        CHECK(!store.recordEpisode(scope, "run-old", "node", "pipeline", "test", "episodio antigo").empty());
        CHECK(store.recordHit(scope, "run-old", "node", "mem-old", "lesson", true, false));
        MemoryUsageRecord usage;
        usage.scopeId = scope;
        usage.runId = "run-old";
        usage.nodeId = "node";
        usage.provider = "test";
        usage.model = "test";
        usage.totalTokens = 10;
        usage.outcome = "success";
        CHECK(store.recordUsage(usage));
        store.close();
    }

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(db.string().c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(raw,
        "UPDATE memory_episode SET created_at='2000-01-01T00:00:00Z' WHERE scope_id='project:prune';"
        "UPDATE memory_hit SET created_at='2000-01-01T00:00:00Z' WHERE scope_id='project:prune';"
        "UPDATE memory_usage SET created_at='2000-01-01T00:00:00Z' WHERE scope_id='project:prune';",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    {
        MemoryStore store(db.string());
        std::string error;
        CHECK(store.open(error));
        auto pruned = store.prune(scope, 30);
        CHECK(pruned.episodes == 1);
        CHECK(pruned.hits == 1);
        CHECK(pruned.usage == 1);
        CHECK(store.usageSummary(scope)["runs"] == 0);
        CHECK(store.hitSummary(scope)["hits"] == 0);

        CHECK(!store.recordEpisode(scope, "run-new", "node", "pipeline", "test", "episodio novo").empty());
        auto again = store.prune(scope, 30);
        CHECK(again.episodes == 0 && again.hits == 0 && again.usage == 0);
        store.close();
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_service_clear_session_resets_ephemeral_store) {
    using taskforge::memory::MemoryService;
    auto& memory = MemoryService::getInstance();
    memory.closeProject("ephemeral-reset");
    CHECK(memory.configure("ephemeral-reset", false, ""));
    CHECK(!memory.isPersistent("ephemeral-reset"));
    CHECK(memory.setWorking("ephemeral-reset", "pipeline", "resumo", taskforge::Json("valor")));
    CHECK(memory.getWorking("ephemeral-reset", "pipeline", "resumo") == taskforge::Json("valor"));
    CHECK(memory.clearSession("ephemeral-reset"));
    CHECK(memory.getWorking("ephemeral-reset", "pipeline", "resumo").is_null());
    memory.closeProject("ephemeral-reset");
}

TEST(runtime_item_id_and_input_hash_are_canonical) {
    using taskforge::rt::canonicalInputHash;
    using taskforge::rt::currentItemId;

    const std::string oldItem = taskforge::platform::getEnv("ARESTRIX_ITEM_ID");
    taskforge::platform::setProcessEnv("ARESTRIX_ITEM_ID", "item-42");
    CHECK(currentItemId("pipeline") == "item-42");
    taskforge::platform::setProcessEnv("ARESTRIX_ITEM_ID", "");
    CHECK(currentItemId("pipeline") == "pipeline");
    taskforge::platform::setProcessEnv("ARESTRIX_ITEM_ID", oldItem);

    const std::string a = canonicalInputHash("node", "item", "input");
    CHECK(a.size() == 64);
    CHECK(a == canonicalInputHash("node", "item", "input"));
    CHECK(a != canonicalInputHash("node", "item-2", "input"));
    CHECK(a != canonicalInputHash("node", "item", "input-2"));
    CHECK(a != canonicalInputHash("node-2", "item", "input"));
}

TEST(memory_service_non_persistent_never_touches_project_database) {
    using taskforge::memory::MemoryService;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path dir = fs::temp_directory_path() / ("arx_test_memory_a0_" + stamp);
    fs::path memoryDir = dir / ".arestrix" / "memory";
    fs::create_directories(memoryDir);
    const fs::path dbPath = memoryDir / "memory.db";
    const std::string pId = "a0-guard-" + stamp;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);

    // 1) Cria conhecimento persistente.
    CHECK(memory.configure(pId, true, dbPath.string()));
    CHECK(memory.recordLesson(pId, "svg_typography", "sombra em texto",
                              "nao usar sombra em texto") != std::string());
    CHECK(memory.getLessons(pId).size() == 1);
    memory.closeProject(pId);
    CHECK(fs::exists(dbPath));
    const auto mtimeBefore = fs::last_write_time(dbPath);

    // 2) Simula o main gerado: regime nao persistente recebendo o caminho do
    // arquivo real. O servico deve usar :memory: e nao tocar no arquivo.
    CHECK(memory.configure(pId, false, dbPath.string()));
    CHECK(!memory.isPersistent(pId));
    CHECK(memory.databasePath(pId) == ":memory:");
    CHECK(memory.getLessons(pId).empty());
    CHECK(memory.clearSession(pId));
    memory.closeProject(pId);

    // 3) O banco real permanece intacto e ainda tem a licao.
    CHECK(fs::exists(dbPath));
    CHECK(fs::last_write_time(dbPath) == mtimeBefore);
    CHECK(memory.configure(pId, true, dbPath.string()));
    CHECK(memory.getLessons(pId).size() == 1);
    memory.closeProject(pId);

    // 4) Um caminho inexistente nao pode ser criado por um regime efemero.
    const fs::path missing = memoryDir / "nao-criar.db";
    const std::string pId2 = "a0-missing-" + stamp;
    memory.closeProject(pId2);
    CHECK(!fs::exists(missing));
    CHECK(memory.configure(pId2, false, missing.string()));
    CHECK(memory.databasePath(pId2) == ":memory:");
    CHECK(!fs::exists(missing));
    memory.closeProject(pId2);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_freeze_knowledge_creates_consistent_copy) {
    using taskforge::memory::MemoryService;
    using taskforge::memory::MemoryStore;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path dir = fs::temp_directory_path() / ("arx_test_memory_freeze_" + stamp);
    const fs::path dbPath = dir / ".arestrix" / "memory" / "memory.db";
    const fs::path frozenPath = dir / ".arestrix" / "telemetry" / "ab" / "frozen.db";
    const std::string pId = "freeze-" + stamp;
    const std::string scope = "project:" + pId;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, dbPath.string()));
    CHECK(!memory.recordLesson(pId, "svg", "manual", "regra congelada").empty());

    std::string fingerprint;
    std::string error;
    CHECK(memory.freezeKnowledge(pId, frozenPath.string(), fingerprint, error));
    CHECK(error.empty());
    CHECK(fingerprint.size() == 64);
    CHECK(fs::exists(frozenPath));

    MemoryStore frozen(frozenPath.string());
    std::string openError;
    CHECK(frozen.open(openError));
    CHECK(frozen.getLessons(scope).size() == 1);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_telemetry_is_separate_from_knowledge) {
    using taskforge::memory::MemoryService;
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryUsageRecord;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root = fs::temp_directory_path() / ("arx_test_memory_telemetry_" + stamp);
    const fs::path knowledgePath = root / ".arestrix" / "memory" / "memory.db";
    const fs::path telemetryPath = root / ".arestrix" / "telemetry" / "telemetry.db";
    const std::string pId = "telemetry-separate-" + stamp;
    const std::string scope = "project:" + pId;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, knowledgePath.string()));
    CHECK(memory.configureTelemetry(pId, telemetryPath.string()));
    const std::string lessonId = memory.recordLesson(pId, "svg_typography", "sombra",
                                                     "nao usar sombra em texto");
    CHECK(!lessonId.empty());

    MemoryUsageRecord usage;
    usage.scopeId = pId;
    usage.runId = "run-telemetry";
    usage.nodeId = "node-telemetry";
    usage.provider = "test";
    usage.model = "test";
    usage.totalTokens = 42;
    usage.outcome = "success";
    CHECK(memory.recordUsage(usage));
    const std::string episodeId = memory.recordEpisode(pId, "run-telemetry", "node-telemetry",
                                                       "pipeline", "agent_success", "ok");
    CHECK(!episodeId.empty());
    taskforge::memory::MemoryBriefInfo brief;
    brief.lessonIds = {lessonId};
    CHECK(memory.recordBriefHits(pId, "run-telemetry", "node-telemetry", brief));

    CHECK(memory.telemetryDatabasePath(pId) == telemetryPath.string());
    CHECK(memory.usageSummary(pId)["runs"] == 1);
    CHECK(memory.hitSummary(pId)["hits"] == 1);
    CHECK(!memory.getEpisode(pId, episodeId).empty());
    memory.closeProject(pId);

    {
        MemoryStore knowledge(knowledgePath.string());
        std::string error;
        CHECK(knowledge.open(error));
        CHECK(knowledge.usageSummary(scope)["runs"] == 0);
        CHECK(knowledge.hitSummary(scope)["hits"] == 0);
    }
    {
        MemoryStore telemetry(telemetryPath.string());
        std::string error;
        CHECK(telemetry.open(error));
        CHECK(telemetry.usageSummary(scope)["runs"] == 1);
        CHECK(telemetry.hitSummary(scope)["hits"] == 1);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(memory_telemetry_hits_reinforce_knowledge) {
    using taskforge::memory::MemoryService;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root = fs::temp_directory_path() / ("arx_test_memory_reinforce_" + stamp);
    const fs::path knowledgePath = root / ".arestrix" / "memory" / "memory.db";
    const fs::path telemetryPath = root / ".arestrix" / "telemetry" / "telemetry.db";
    const std::string pId = "telemetry-reinforce-" + stamp;
    const std::string scope = "project:" + pId;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, knowledgePath.string()));
    CHECK(memory.configureTelemetry(pId, telemetryPath.string()));
    const std::string lessonId = memory.recordLesson(pId, "svg_typography", "sombra",
                                                     "nao usar sombra em texto");
    CHECK(!lessonId.empty());

    taskforge::memory::MemoryBriefInfo brief;
    brief.lessonIds = {lessonId};
    CHECK(memory.recordBriefHits(pId, "run-reinforce", "node-reinforce", brief));
    CHECK(memory.markHitsUsed(pId, "run-reinforce", "node-reinforce", {lessonId}) == 1);
    CHECK(memory.finishHits(pId, "run-reinforce", "node-reinforce", "success"));

    taskforge::memory::MemoryEdgeRecord edge;
    CHECK(memory.store(pId)->getEdge(scope, lessonId, edge));
    CHECK(edge.successCount == 1);
    CHECK(edge.failureCount == 0);
    CHECK(edge.confidence > 0.9);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(memory_telemetry_records_without_knowledge_store) {
    using taskforge::memory::MemoryService;
    using taskforge::memory::MemoryUsageRecord;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root = fs::temp_directory_path() / ("arx_test_memory_telemetry_only_" + stamp);
    const fs::path knowledgePath = root / ".arestrix" / "memory" / "memory.db";
    const fs::path telemetryPath = root / ".arestrix" / "telemetry" / "telemetry.db";
    const std::string pId = "telemetry-only-" + stamp;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configureTelemetry(pId, telemetryPath.string()));
    CHECK(!fs::exists(knowledgePath));

    MemoryUsageRecord usage;
    usage.scopeId = pId;
    usage.runId = "run-only";
    usage.nodeId = "node-only";
    usage.provider = "test";
    usage.model = "test";
    usage.totalTokens = 7;
    usage.outcome = "success";
    CHECK(memory.recordUsage(usage));
    CHECK(!memory.recordEpisode(pId, "run-only", "node-only", "item-1",
                                "agent_success", "ok").empty());

    CHECK(!fs::exists(knowledgePath));
    CHECK(memory.usageSummary(pId)["runs"] == 1);
    CHECK(fs::exists(telemetryPath));

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(memory_validation_links_episode_and_is_idempotent) {
    using taskforge::memory::MemoryService;
    using taskforge::memory::MemoryValidationRecord;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root = fs::temp_directory_path() / ("arx_test_memory_validation_" + stamp);
    const fs::path knowledgePath = root / ".arestrix" / "memory" / "memory.db";
    const fs::path telemetryPath = root / ".arestrix" / "telemetry" / "telemetry.db";
    const std::string pId = "validation-" + stamp;

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, knowledgePath.string()));
    CHECK(memory.configureTelemetry(pId, telemetryPath.string()));
    const std::string episodeId = memory.recordEpisode(pId, "run-validation", "node-validation",
                                                       "pipeline", "agent_success", "ok");
    CHECK(!episodeId.empty());

    MemoryValidationRecord validation;
    validation.episodeId = episodeId;
    validation.runId = "run-validation";
    validation.nodeId = "node-validation";
    validation.validatorId = "validator.svg";
    validation.validatorVersion = "1";
    validation.ok = true;
    validation.score = 0.75;
    validation.reasonsJson = "[\"viewBox ok\"]";
    validation.artifactHash = "abc123";
    const std::string validationId = memory.recordValidation(pId, validation);
    CHECK(!validationId.empty());

    auto validations = memory.listValidations(pId, episodeId);
    CHECK(validations.size() == 1);
    CHECK(validations[0].id == validationId);
    CHECK(validations[0].ok);
    CHECK(validations[0].score == 0.75);
    CHECK(validations[0].validatorId == "validator.svg");

    // Mesmo validador e versao: upsert, sem duplicar.
    validation.ok = false;
    validation.score = 0.25;
    validation.reasonsJson = "[\"viewBox invalido\"]";
    CHECK(memory.recordValidation(pId, validation) == validationId);
    validations = memory.listValidations(pId, episodeId);
    CHECK(validations.size() == 1);
    CHECK(!validations[0].ok);
    CHECK(validations[0].score == 0.25);

    // Outro validador: uma segunda linha.
    validation.validatorId = "validator.audit";
    validation.ok = true;
    CHECK(!memory.recordValidation(pId, validation).empty());
    CHECK(memory.listValidations(pId, episodeId).size() == 2);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(memory_database_guard_prunes_telemetry_on_shutdown_using_manifest) {
    using taskforge::project::MemoryDatabaseGuard;
    using taskforge::memory::MemoryService;
    using taskforge::memory::MemoryUsageRecord;
    namespace fs = std::filesystem;

    const auto stamp = std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    fs::path root = fs::temp_directory_path() / ("arx_test_mem_retention_" + stamp);
    fs::create_directories(root / ".arestrix" / "memory");
    fs::create_directories(root / "storage");
    const std::string pId = "retention-shutdown-test";
    const fs::path dbPath = root / ".arestrix" / "memory" / "memory.db";
    const fs::path telemetryPath = root / ".arestrix" / "telemetry" / "telemetry.db";

    {
        std::ofstream manifest(root / "arestrix.project.json");
        manifest << taskforge::Json{{"projectId", pId},
                                    {"globalDefaults", taskforge::Json{{"memory", taskforge::Json{{"retentionDays", 1}}}}}}
                        .dump(2);
    }

    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, dbPath.string()));
    CHECK(memory.configureTelemetry(pId, telemetryPath.string()));
    CHECK(!memory.recordEpisode(pId, "run-old", "node", "pipeline", "test", "episodio antigo").empty());
    taskforge::memory::MemoryBriefInfo brief;
    brief.lessonIds = {"mem-old"};
    CHECK(memory.recordBriefHits(pId, "run-old", "node", brief));
    MemoryUsageRecord usage;
    usage.scopeId = pId;
    usage.runId = "run-old";
    usage.nodeId = "node";
    usage.provider = "test";
    usage.model = "test";
    usage.totalTokens = 10;
    usage.outcome = "success";
    CHECK(memory.recordUsage(usage));
    memory.closeProject(pId);

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(telemetryPath.string().c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(raw,
        "UPDATE memory_episode SET created_at='2000-01-01T00:00:00Z';"
        "UPDATE memory_hit SET created_at='2000-01-01T00:00:00Z';"
        "UPDATE memory_usage SET created_at='2000-01-01T00:00:00Z';",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    std::string error;
    CHECK(MemoryDatabaseGuard::applyRetentionOnShutdown(root, pId, error));
    CHECK(error.empty());
    CHECK(memory.usageSummary(pId)["runs"] == 0);
    CHECK(memory.hitSummary(pId)["hits"] == 0);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(root, ec);
}

TEST(runtime_current_run_id_prefers_environment) {
    const std::string old = taskforge::platform::getEnv("ARESTRIX_RUN_ID");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", "run-pipeline-xyz");
    CHECK(taskforge::rt::currentRunId("fallback-node") == "run-pipeline-xyz");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", "");
    CHECK(taskforge::rt::currentRunId("fallback-node") == "fallback-node");
    taskforge::platform::setProcessEnv("ARESTRIX_RUN_ID", old);
}

TEST(memory_hit_unique_key_upserts_instead_of_duplicating) {
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryHitRef;
    MemoryStore store(":memory:");
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:hit-unique", "project", "", "hit-unique"));

    CHECK(store.recordHit("project:hit-unique", "run-1", "node-a", "mem-1", "lesson", true, false));
    CHECK(store.recordHit("project:hit-unique", "run-1", "node-a", "mem-1", "lesson", false, true));
    auto summary = store.hitSummary("project:hit-unique");
    CHECK(summary["hits"] == 1);
    CHECK(summary["injected"] == 1);
    CHECK(summary["used"] == 1);

    CHECK(store.recordConsultedHits("project:hit-unique", "run-2", "node-b",
                                    {MemoryHitRef{"mem-2", "fact"}}) == 1);
    CHECK(store.recordConsultedHits("project:hit-unique", "run-2", "node-b",
                                    {MemoryHitRef{"mem-2", "fact"}}) == 1);
    summary = store.hitSummary("project:hit-unique");
    CHECK(summary["hits"] == 2);
    CHECK(summary["consulted"] == 1);

    store.close();
}

TEST(memory_hit_unique_migration_deduplicates_existing_rows) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-hit-migration");
    const fs::path db = dir / "memory.db";
    {
        MemoryStore store(db.string());
        std::string error;
        CHECK(store.open(error));
        CHECK(store.ensureScope("project:hit-migration", "project", "", "hit-migration"));
        store.close();
    }

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(db.string().c_str(), &raw, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(raw,
        "DROP INDEX IF EXISTS idx_memory_hit_unique;"
        "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at) "
        "VALUES('h1','project:hit-migration','r','n','m','lesson',1,0,'','2026-01-01T00:00:00Z');"
        "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at) "
        "VALUES('h2','project:hit-migration','r','n','m','lesson',0,1,'success','2026-01-02T00:00:00Z');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    {
        MemoryStore store(db.string());
        std::string error;
        CHECK(store.open(error));
        auto summary = store.hitSummary("project:hit-migration");
        CHECK(summary["hits"] == 1);
        CHECK(summary["used"] == 1);
        CHECK(summary["injected"] == 1);
        CHECK(store.recordHit("project:hit-migration", "r", "n", "m", "lesson", true, false));
        summary = store.hitSummary("project:hit-migration");
        CHECK(summary["hits"] == 1);
        store.close();
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_schema_migrates_previous_version_database) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-old-schema");
    const fs::path db = dir / "memory.db";
    const std::string scope = "project:old-schema";

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(db.string().c_str(), &raw, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    CHECK(sqlite3_exec(raw,
        "CREATE TABLE memory_usage ("
        " id TEXT PRIMARY KEY, scope_id TEXT NOT NULL, run_id TEXT, node_id TEXT, provider TEXT, model TEXT,"
        " prompt_tokens INTEGER NOT NULL DEFAULT 0, completion_tokens INTEGER NOT NULL DEFAULT 0,"
        " total_tokens INTEGER NOT NULL DEFAULT 0, calls INTEGER NOT NULL DEFAULT 0,"
        " latency_ms INTEGER NOT NULL DEFAULT 0, memory_context_chars INTEGER NOT NULL DEFAULT 0,"
        " memory_enabled INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL);"
        "CREATE TABLE memory_hit ("
        " id TEXT PRIMARY KEY, scope_id TEXT NOT NULL, run_id TEXT, node_id TEXT, memory_id TEXT NOT NULL,"
        " kind TEXT NOT NULL, injected INTEGER NOT NULL DEFAULT 0, used INTEGER NOT NULL DEFAULT 0,"
        " outcome TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL);"
        "CREATE TABLE memory_episode ("
        " id TEXT PRIMARY KEY, scope_id TEXT NOT NULL, run_id TEXT NOT NULL, node_id TEXT, item_id TEXT,"
        " kind TEXT NOT NULL, content TEXT NOT NULL, sanitized INTEGER NOT NULL DEFAULT 1,"
        " created_at TEXT NOT NULL, metadata TEXT NOT NULL DEFAULT '{}');"
        "INSERT INTO memory_usage(id,scope_id,run_id,node_id,provider,model,total_tokens,created_at)"
        " VALUES('u1','project:old-schema','r','n','p','m',10,'2026-01-01T00:00:00Z');"
        "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at)"
        " VALUES('h1','project:old-schema','r','n','mem','lesson',1,0,'','2026-01-01T00:00:00Z');"
        "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at)"
        " VALUES('h2','project:old-schema','r','n','mem','lesson',0,1,'success','2026-01-02T00:00:00Z');"
        "INSERT INTO memory_episode(id,scope_id,run_id,node_id,item_id,kind,content,created_at,metadata)"
        " VALUES('old-ep','project:old-schema','r','n','pipeline','agent_success','ok','2026-01-01T00:00:00Z','{}');",
        nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);

    {
        MemoryStore store(db.string());
        std::string error;
        CHECK(store.open(error));
        auto usage = store.usageSummary(scope);
        CHECK(usage["runs"] == 1);
        CHECK(usage["totalTokens"] == 10);
        CHECK(usage["retries"] == 0);
        auto hits = store.hitSummary(scope);
        CHECK(hits["hits"] == 1);
        CHECK(hits["injected"] == 1);
        CHECK(hits["used"] == 1);
        CHECK(store.recordHit(scope, "r", "n", "mem", "lesson", true, false));
        CHECK(store.hitSummary(scope)["hits"] == 1);
        auto oldEpisode = store.getEpisode(scope, "old-ep");
        CHECK(oldEpisode.value("id", std::string()) == "old-ep");
        CHECK(oldEpisode.value("taskType", std::string()).empty());
        CHECK(oldEpisode["toolCalls"].is_array() && oldEpisode["toolCalls"].empty());
        store.close();
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_l2_schema_and_enriched_episode) {
    using taskforge::memory::MemoryEpisodeRecord;
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-l2-episode");
    const fs::path dbPath = dir / "memory.db";
    const std::string scope = "project:l2-episode";

    {
        MemoryStore store(dbPath.string());
        std::string error;
        CHECK(store.open(error));
        CHECK(store.ensureScope(scope, "project", "", "l2-episode"));

        MemoryEpisodeRecord episode;
        episode.scopeId = scope;
        episode.runId = "run-l2";
        episode.nodeId = "agent-l2";
        episode.itemId = "pipeline";
        episode.kind = "agent_success";
        episode.content = "agent completed";
        episode.taskType = "svg_typography";
        episode.outcome = "success";
        episode.errorClass = "";
        episode.inputHash = "hash-123";
        episode.toolCalls = taskforge::Json::array({
            taskforge::Json{{"name", "read"}, {"ok", true}},
            taskforge::Json{{"name", "write"}, {"ok", true}}});
        episode.filesTouched = taskforge::Json::array({taskforge::Json("workspace/scene.svg")});
        episode.artifacts = taskforge::Json::array({taskforge::Json("exports/scene.svg")});
        episode.evidence = taskforge::Json::array({taskforge::Json{{"kind", "artifact"}, {"ok", true}}});
        episode.successSignals = taskforge::Json{{"outputNonEmpty", true}, {"success", true}};
        episode.metadata = taskforge::Json{{"domain", "svg"}, {"note", "api_key=sk-proj-" + std::string(40, 'x')}};

        const std::string id = store.recordEpisode(episode);
        CHECK(!id.empty());
        auto saved = store.getEpisode(scope, id);
        CHECK(saved.value("id", std::string()) == id);
        CHECK(saved.value("taskType", std::string()) == "svg_typography");
        CHECK(saved.value("outcome", std::string()) == "success");
        CHECK(saved["toolCalls"].size() == 2);
        CHECK(saved["filesTouched"].size() == 1);
        CHECK(saved["artifacts"].size() == 1);
        CHECK(saved["evidence"].size() == 1);
        CHECK(saved["successSignals"].value("outputNonEmpty", false));
        CHECK(saved["redactions"].get<long long>() > 0);
        CHECK(saved.dump().find("sk-proj-") == std::string::npos);
        store.close();
    }

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(dbPath.string().c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    const char* tables[] = {"memory_proposal", "memory_embedding", "memory_playbook", "memory_alias"};
    for (const char* table : tables) {
        sqlite3_stmt* st = nullptr;
        CHECK(sqlite3_prepare_v2(raw,
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;", -1, &st, nullptr) == SQLITE_OK);
        sqlite3_bind_text(st, 1, table, -1, SQLITE_TRANSIENT);
        CHECK(sqlite3_step(st) == SQLITE_ROW);
        sqlite3_finalize(st);
    }
    {
        sqlite3_stmt* st = nullptr;
        CHECK(sqlite3_prepare_v2(raw,
            "SELECT COUNT(*) FROM pragma_table_info('memory_embedding') WHERE name='domain';",
            -1, &st, nullptr) == SQLITE_OK);
        CHECK(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) == 1);
        sqlite3_finalize(st);
    }
    sqlite3_close(raw);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_extractor_generates_proposals_and_promotes) {
    using taskforge::memory::MemoryEpisodeRecord;
    using taskforge::memory::MemoryExtractor;
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-extractor");
    const fs::path dbPath = dir / "memory.db";
    const std::string scope = "project:extractor";

    {
        MemoryStore store(dbPath.string());
        std::string error;
        CHECK(store.open(error));
        CHECK(store.ensureScope(scope, "project", "", "extractor"));

        MemoryEpisodeRecord success;
        success.scopeId = scope;
        success.runId = "run-extract";
        success.nodeId = "agent-extract";
        success.kind = "agent_success";
        success.content = "agent completed";
        success.taskType = "svg_typography";
        success.outcome = "success";
        success.toolCalls = taskforge::Json::array({
            taskforge::Json{{"name", "read"}, {"ok", true}},
            taskforge::Json{{"name", "write"}, {"ok", true}}});
        success.filesTouched = taskforge::Json::array({taskforge::Json("workspace/scene.svg")});
        success.artifacts = taskforge::Json::array({taskforge::Json("exports/scene.svg")});
        success.successSignals = taskforge::Json{{"outputNonEmpty", true}, {"success", true}};
        const std::string successId = store.recordEpisode(success);
        CHECK(!successId.empty());
        CHECK(MemoryExtractor::extractAndStore(store, scope, successId) > 0);
        auto pending = store.listProposals(scope, "pending", 100);
        CHECK(!pending.empty());
        const size_t pendingCount = pending.size();
        CHECK(MemoryExtractor::extractAndStore(store, scope, successId) > 0);
        CHECK(store.listProposals(scope, "pending", 100).size() == pendingCount);

        std::string factId;
        std::string playbookId;
        std::string entityId;
        for (const auto& proposal : pending) {
            if (proposal.kind == "fact" && factId.empty()) {
                const auto payload = taskforge::jsonutil::parse(proposal.payloadJson);
                if (payload.value("predicate", std::string()) == "USES_TOOL" &&
                    payload.value("object", std::string()) == "read") {
                    factId = proposal.id;
                }
            }
            if (proposal.kind == "playbook" && playbookId.empty()) playbookId = proposal.id;
            if (proposal.kind == "entity" && entityId.empty()) entityId = proposal.id;
        }
        CHECK(!factId.empty() && !playbookId.empty() && !entityId.empty());

        std::string promoted;
        std::string promoteError;
        CHECK(store.promoteProposal(scope, factId, "test", promoteError, &promoted));
        CHECK(!promoted.empty());
        const std::string factPromoted = promoted;
        CHECK(store.searchContext(scope, "svg_typography", 10).find("USES_TOOL") != std::string::npos);
        CHECK(store.promoteProposal(scope, playbookId, "test", promoteError, &promoted));
        CHECK(!promoted.empty());
        CHECK(store.promoteProposal(scope, entityId, "test", promoteError, &promoted));
        CHECK(!promoted.empty());

        std::string rejectError;
        std::string summaryId;
        for (const auto& proposal : pending) {
            if (proposal.kind == "summary") { summaryId = proposal.id; break; }
        }
        CHECK(!summaryId.empty());
        CHECK(store.rejectProposal(scope, summaryId, "test", "nao relevante", rejectError));
        CHECK(store.searchContext(scope, "svg_typography success", 10).empty());

        // Regerar e aceitar o mesmo fato nao duplica a aresta.
        taskforge::Json duplicateFact = taskforge::Json{
            {"subject", "svg_typography"}, {"predicate", "USES_TOOL"}, {"object", "read"}};
        taskforge::Json duplicateEvidence = taskforge::Json::array(
            {taskforge::Json{{"kind", "test"}, {"episodeId", successId}}});
        const std::string duplicateId = store.recordProposal(
            scope, "fact", duplicateFact, successId, duplicateEvidence, 0.9);
        CHECK(!duplicateId.empty());
        std::string duplicatePromoted;
        CHECK(store.promoteProposal(scope, duplicateId, "test", promoteError, &duplicatePromoted));
        CHECK(duplicatePromoted == factPromoted);

        MemoryEpisodeRecord failure;
        failure.scopeId = scope;
        failure.runId = "run-fail";
        failure.nodeId = "agent-fail";
        failure.kind = "agent_failure";
        failure.content = "agent failed";
        failure.taskType = "svg_typography";
        failure.outcome = "failure";
        failure.errorClass = "validation_error";
        const std::string failureId = store.recordEpisode(failure);
        CHECK(!failureId.empty());
        CHECK(MemoryExtractor::extractAndStore(store, scope, failureId) == 1);
        auto failureProposals = store.listProposals(scope, "pending", 100);
        std::string lessonId;
        for (const auto& proposal : failureProposals) {
            if (proposal.kind == "lesson") { lessonId = proposal.id; break; }
        }
        CHECK(!lessonId.empty());
        CHECK(store.promoteProposal(scope, lessonId, "test", promoteError, &promoted));
        CHECK(store.getLessons(scope, "svg_typography").size() >= 1);

        // Fato sem evidencia nao pode ser promovido.
        taskforge::Json noEvidence = taskforge::Json{{"subject", "a"}, {"predicate", "USES_TOOL"}, {"object", "b"}};
        const std::string unsafe = store.recordProposal(scope, "fact", noEvidence, "", taskforge::Json::array(), 0.5);
        CHECK(!unsafe.empty());
        std::string unsafeError;
        CHECK(!store.promoteProposal(scope, unsafe, "test", unsafeError));
        CHECK(unsafeError.find("evidencia") != std::string::npos);

        store.close();
    }

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(dbPath.string().c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    CHECK(sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM memory_playbook WHERE scope_id=?;",
                             -1, &st, nullptr) == SQLITE_OK);
    sqlite3_bind_text(st, 1, scope.c_str(), -1, SQLITE_TRANSIENT);
    CHECK(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int64(st, 0) >= 1);
    sqlite3_finalize(st);
    st = nullptr;
    CHECK(sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM memory_audit WHERE action='promote_proposal';",
                             -1, &st, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int64(st, 0) >= 4);
    sqlite3_finalize(st);
    sqlite3_close(raw);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_embedding_defaults_use_embeddinggemma2) {
    using taskforge::memory::defaultEmbeddingConfig;
    using taskforge::memory::embeddingConfigFromJson;
    const auto config = defaultEmbeddingConfig();
    CHECK(config.model == "embeddinggemma-2");
    CHECK(config.dim == 768);
    CHECK(config.maxChars == 8000);
    CHECK(config.batchSize == 8);
    CHECK(!config.enabled);

    const auto parsed = embeddingConfigFromJson(taskforge::Json{
        {"embeddingModel", "custom-embed"},
        {"embeddingDimensions", 512},
        {"embeddingEnabled", true}});
    CHECK(parsed.model == "custom-embed");
    CHECK(parsed.dim == 512);
    CHECK(parsed.enabled);
}

TEST(memory_vector_index_serialization_and_rrf) {
    using taskforge::memory::MemoryRanker;
    using taskforge::memory::MemorySearchCandidate;
    using taskforge::memory::MemoryVectorHit;
    using taskforge::memory::MemoryVectorIndex;

    const auto bytes = MemoryVectorIndex::serialize({1.0f, -2.5f, 0.0f});
    const auto values = MemoryVectorIndex::deserialize(bytes, 3);
    CHECK(values.size() == 3);
    CHECK(std::abs(values[0] - 1.0f) < 1e-6f);
    CHECK(std::abs(values[1] + 2.5f) < 1e-6f);
    CHECK(std::abs(values[2]) < 1e-6f);
    CHECK(MemoryVectorIndex::cosine({1, 0}, {1, 0}) > 0.999);
    CHECK(std::abs(MemoryVectorIndex::cosine({1, 0}, {0, 1})) < 1e-6);

    MemorySearchCandidate lexical;
    lexical.id = "mem-1";
    lexical.kind = "fact";
    lexical.text = "fontes --USES_TOOL--> contraste";
    lexical.confidence = 0.9;
    MemoryVectorHit vector;
    vector.objectType = "edge";
    vector.objectId = "mem-1";
    vector.text = lexical.text;
    vector.score = 0.95;
    const auto fused = MemoryRanker::fuse({lexical}, {vector}, 5);
    CHECK(fused.size() == 1);
    CHECK(fused[0].sources == "lexical+vector");
    CHECK(fused[0].score > 0.0);
}

TEST(memory_service_hybrid_search_and_reindex) {
    using taskforge::memory::EmbeddingConfig;
    using taskforge::memory::MemoryService;
    using taskforge::memory::defaultEmbeddingConfig;

    const fs::path dir = makeTempDir("memory-hybrid");
    const fs::path db = dir / "memory.db";
    const std::string pId = "memory-hybrid-test";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));

    EmbeddingConfig config = defaultEmbeddingConfig();
    config.enabled = true;
    config.provider = "local";
    config.model = "test-topic-3";
    config.dim = 3;
    config.baseUrl = "http://127.0.0.1:1/v1";
    config.batchSize = 4;
    CHECK(memory.configureEmbeddings(pId, config));
    auto provider = std::make_shared<TopicTestEmbeddingProvider>();
    memory.setEmbeddingProvider(pId, provider);

    const std::string designLesson = memory.recordLesson(
        pId, "design", "manual", "fontes precisam de contraste minimo", "seed");
    const std::string networkLesson = memory.recordLesson(
        pId, "rede", "manual", "erro de rede deve ser repetido", "seed");
    CHECK(!designLesson.empty() && !networkLesson.empty());

    // C4: a busca nao indexa; a indexacao e explicita.
    std::string indexError;
    auto initialIndex = memory.reindexEmbeddings(pId, true, indexError);
    CHECK(indexError.empty());
    CHECK(initialIndex["ok"] == taskforge::Json(true));

    // "legibilidade" nao existe no texto lexical da licao; so o vetor liga os dois.
    std::vector<taskforge::memory::MemoryHitRef> hits;
    std::string result = memory.searchContext(pId, "legibilidade", 5, {}, &hits);
    CHECK(result.find("fontes precisam de contraste minimo") != std::string::npos);
    CHECK(result.find("[LICAO]") != std::string::npos);
    CHECK(!hits.empty());

    // Filtro de dominio vale tambem para candidatos vetoriais.
    result = memory.searchContext(pId, "legibilidade", 5, {"rede"});
    CHECK(result.find("fontes precisam de contraste minimo") == std::string::npos);

    // Reabre o projeto para provar que o indice persiste no SQLite.
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));
    memory.configureEmbeddings(pId, config);
    memory.setEmbeddingProvider(pId, provider);

    // Sem embeddings, a busca lexical continua funcionando e nao chama o provider.
    const int callsBeforeDisabled = provider->calls;
    EmbeddingConfig disabled = config;
    disabled.enabled = false;
    memory.configureEmbeddings(pId, disabled);
    memory.setEmbeddingProvider(pId, provider);
    result = memory.searchContext(pId, "fontes", 5, {});
    CHECK(result.find("fontes precisam de contraste minimo") != std::string::npos);
    CHECK(provider->calls == callsBeforeDisabled);

    // Reindexacao completa e idempotente.
    memory.configureEmbeddings(pId, config);
    memory.setEmbeddingProvider(pId, provider);
    auto first = memory.reindexEmbeddings(pId, true, indexError);
    CHECK(indexError.empty());
    CHECK(first["ok"] == taskforge::Json(true));
    CHECK(first["indexed"].get<long long>() >= 2);
    auto second = memory.reindexEmbeddings(pId, false, indexError);
    CHECK(indexError.empty());
    CHECK(second["indexed"].get<long long>() == 0);
    CHECK(second["skipped"].get<long long>() >= 2);

    auto status = memory.embeddingStatus(pId);
    CHECK(status["active"] == taskforge::Json(true));
    CHECK(status["indexed"].get<long long>() >= 2);

    // Um fato/aresta entra no bloco de fatos relevantes do brief.
    auto* store = memory.store(pId);
    const std::string scope = "project:" + pId;
    const std::string rootNode = store->ensureNode(scope, "node-hybrid-root-2", "project", "Projeto");
    const std::string fontNode = store->ensureNode(scope, "node-hybrid-font", "entity", "Fontes");
    CHECK(!rootNode.empty() && !fontNode.empty());
    taskforge::Json edgeMetadata = taskforge::Json{{"domain", "design"}};
    CHECK(!store->recordEdge(scope, rootNode, fontNode, "USES", taskforge::Json(),
                             "2020-01-01T00:00:00Z", "", 0.9, "active", "test", "", edgeMetadata).empty());

    // C4: indexa o novo fato antes do brief.
    CHECK(memory.reindexEmbeddings(pId, true, indexError)["ok"] == taskforge::Json(true));
    CHECK(indexError.empty());

    taskforge::memory::MemoryBriefInfo info;
    const std::string brief = memory.buildBrief(pId, "pipeline", {"design"}, {}, 2000, true, false, &info);
    CHECK(brief.find("[FATO]") != std::string::npos);
    CHECK(brief.find("FATOS RELEVANTES") != std::string::npos);
    CHECK(!info.factIds.empty());

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_hybrid_latency_benchmark) {
    using taskforge::memory::EmbeddingConfig;
    using taskforge::memory::MemoryService;
    using taskforge::memory::defaultEmbeddingConfig;

    const fs::path dir = makeTempDir("memory-hybrid-bench");
    const fs::path db = dir / "memory.db";
    const std::string pId = "memory-hybrid-bench";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));

    EmbeddingConfig config = defaultEmbeddingConfig();
    config.enabled = true;
    config.provider = "local";
    config.model = "test-topic-3";
    config.dim = 3;
    config.baseUrl = "http://127.0.0.1:1/v1";
    config.batchSize = 16;
    memory.configureEmbeddings(pId, config);
    auto provider = std::make_shared<TopicTestEmbeddingProvider>();
    memory.setEmbeddingProvider(pId, provider);

    for (int i = 0; i < 120; ++i) {
        const std::string domain = (i % 2 == 0) ? "design" : "rede";
        const std::string rule = (i % 2 == 0)
            ? "fontes e contraste para legibilidade " + std::to_string(i)
            : "erro de rede deve ser repetido " + std::to_string(i);
        CHECK(!memory.recordLesson(pId, domain, "bench", rule, "bench").empty());
    }

    auto p95 = [](std::vector<double> samples) {
        if (samples.empty()) return 0.0;
        std::sort(samples.begin(), samples.end());
        size_t index = static_cast<size_t>(samples.size() * 95 / 100);
        if (index >= samples.size()) index = samples.size() - 1;
        return samples[index];
    };
    auto measure = [&](const std::string& query) {
        std::vector<double> samples;
        for (int i = 0; i < 60; ++i) {
            const auto start = std::chrono::steady_clock::now();
            (void)memory.searchContext(pId, query, 5, {});
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count();
            samples.push_back(static_cast<double>(elapsed) / 1000.0);
        }
        return p95(std::move(samples));
    };

    EmbeddingConfig disabled = config;
    disabled.enabled = false;
    memory.configureEmbeddings(pId, disabled);
    memory.setEmbeddingProvider(pId, provider);
    const double lexicalP95 = measure("fontes");

    memory.configureEmbeddings(pId, config);
    memory.setEmbeddingProvider(pId, provider);
    std::string indexError;
    auto indexed = memory.reindexEmbeddings(pId, true, indexError);
    CHECK(indexError.empty());
    CHECK(indexed["indexed"].get<long long>() >= 120);
    const double hybridP95 = measure("legibilidade");
    std::cout << "  [benchmark] lexical p95=" << lexicalP95
              << "ms hybrid p95=" << hybridP95 << "ms (120 licoes)\n";
    CHECK(lexicalP95 < 100.0);
    CHECK(hybridP95 < 100.0);

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_consolidator_dedup_conflicts_decay_and_generalize) {
    using taskforge::memory::ConsolidationOptions;
    using taskforge::memory::ConsolidationReport;
    using taskforge::memory::MemoryConsolidator;
    using taskforge::memory::MemoryStore;

    const fs::path dir = makeTempDir("memory-consolidator");
    const fs::path dbPath = dir / "memory.db";
    const std::string scope = "project:consolidator";
    MemoryStore store(dbPath.string());
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope(scope, "project", "", "consolidator"));

    // Aliases: mesmo nome normalizado resolve para o mesmo node.
    const std::string library = store.resolveNode(scope, "Biblioteca", "entity");
    CHECK(!library.empty());
    CHECK(store.resolveNode(scope, "biblioteca", "entity") == library);
    CHECK(store.nodeByAlias(scope, "BIBLIOTECA") == library);

    const std::string designA = store.recordLesson(scope, "design", "manual",
        "Use contraste mínimo em textos pequenos", "seed");
    const std::string designB = store.recordLesson(scope, "design", "manual",
        "Use contraste minimo em textos pequenos", "seed");
    CHECK(!designA.empty() && !designB.empty());

    const std::string root = store.ensureNode(scope, "node-c-root", "project", "Projeto");
    const std::string fontsA = store.ensureNode(scope, "node-c-font-a", "entity", "Fontes");
    const std::string fontsB = store.ensureNode(scope, "node-c-font-b", "entity", "fontes");
    CHECK(!root.empty() && !fontsA.empty() && !fontsB.empty());
    taskforge::Json designMeta = taskforge::Json{{"domain", "design"}};
    CHECK(!store.recordEdge(scope, root, fontsA, "USES", taskforge::Json(), "", "",
                            0.8, "active", "test", "", designMeta).empty());
    CHECK(!store.recordEdge(scope, root, fontsB, "USES", taskforge::Json(), "", "",
                            0.6, "active", "test", "", designMeta).empty());

    const std::string root2 = store.ensureNode(scope, "node-c-root2", "project", "Projeto 2");
    const std::string arial = store.ensureNode(scope, "node-c-arial", "entity", "Arial");
    const std::string helvetica = store.ensureNode(scope, "node-c-helvetica", "entity", "Helvetica");
    CHECK(!store.recordEdge(scope, root2, arial, "USES_FONT", taskforge::Json(), "", "",
                            0.9, "active", "test", "", designMeta).empty());
    CHECK(!store.recordEdge(scope, root2, helvetica, "USES_FONT", taskforge::Json(), "", "",
                            0.7, "active", "test", "", designMeta).empty());

    const std::string oldNode = store.ensureNode(scope, "node-c-old", "entity", "Fato Expirado");
    const std::string expiredEdge = store.recordEdge(scope, root, oldNode, "OLD_FACT",
        taskforge::Json(), "2020-01-01T00:00:00Z", "2020-01-02T00:00:00Z",
        0.5, "active", "test", "", designMeta);
    CHECK(!expiredEdge.empty());

    const std::string rareNode = store.ensureNode(scope, "node-c-rare", "entity", "Recurso Raro");
    const std::string rareEdge = store.recordEdge(scope, root, rareNode, "RARE",
        taskforge::Json(), "", "", 0.8, "active", "test", "", designMeta);
    CHECK(!rareEdge.empty());
    taskforge::memory::MemoryEdgeRecord rareRecord;
    CHECK(store.getEdge(scope, rareEdge, rareRecord));
    CHECK(store.updateEdgeState(scope, rareEdge, "active", rareRecord.supersededBy,
                                rareRecord.confidence, taskforge::Json::object(), "",
                                "", "2020-01-01T00:00:00Z", error));

    CHECK(!store.recordLesson(scope, "seguranca", "manual",
        "Sempre validar a entrada", "seed").empty());
    CHECK(!store.recordLesson(scope, "seguranca", "manual",
        "Nunca validar a entrada", "seed").empty());

    for (int i = 0; i < 3; ++i) {
        CHECK(!store.recordLesson(scope, "svg", "manual",
            "Regra distinta de svg numero " + std::to_string(i), "seed").empty());
    }

    ConsolidationOptions options;
    options.dormantAfterDays = 30;
    options.generalizeMinSources = 3;
    auto report = MemoryConsolidator::run(store, scope, options, error);
    CHECK(error.empty());
    CHECK(report.ok());
    CHECK(report.duplicates >= 2);
    CHECK(report.contradictions >= 2);
    CHECK(report.expired >= 1);
    CHECK(report.dormant >= 1);
    CHECK(report.generalizations >= 3);

    CHECK(store.getLessons(scope, "design").size() == 1);
    CHECK(store.getLessons(scope, "seguranca").size() == 1);
    const auto edges = store.listEdges(scope, "", 20000);
    size_t supersedes = 0;
    size_t contradicts = 0;
    size_t generalizes = 0;
    size_t candidates = 0;
    for (const auto& edge : edges) {
        if (edge.relation == "SUPERSEDES") ++supersedes;
        if (edge.relation == "CONTRADICTS") ++contradicts;
        if (edge.relation == "GENERALIZES") ++generalizes;
        if (edge.status == "candidate" && edge.relation == "USES_FONT") ++candidates;
    }
    CHECK(supersedes >= 1);
    CHECK(contradicts >= 4);
    CHECK(generalizes >= 3);
    CHECK(candidates == 1);

    taskforge::memory::MemoryEdgeRecord expiredRecord;
    CHECK(store.getEdge(scope, expiredEdge, expiredRecord));
    CHECK(expiredRecord.status == "superseded");
    CHECK(expiredRecord.metadata.find("expired") != std::string::npos);

    taskforge::memory::MemoryEdgeRecord rareDormant;
    CHECK(store.getEdge(scope, rareEdge, rareDormant));
    CHECK(!rareDormant.dormantAt.empty());
    CHECK(store.searchContext(scope, "Recurso Raro", 10).empty());

    auto summary = store.consolidationSummary(scope);
    CHECK(summary["candidate"].get<long long>() >= 1);
    CHECK(summary["superseded"].get<long long>() >= 2);
    CHECK(summary["dormant"].get<long long>() >= 1);
    CHECK(summary["conflicts"].get<long long>() >= 4);
    CHECK(summary["aliases"].get<long long>() >= 1);

    // Segunda passagem e idempotente.
    auto report2 = MemoryConsolidator::run(store, scope, options, error);
    CHECK(error.empty());
    CHECK(report2.ok());
    CHECK(report2.duplicates == 0);
    CHECK(report2.contradictions == 0);
    CHECK(report2.expired == 0);
    CHECK(report2.dormant == 0);
    CHECK(report2.generalizations == 0);

    store.close();

    sqlite3* raw = nullptr;
    CHECK(sqlite3_open_v2(dbPath.string().c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* st = nullptr;
    CHECK(sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM memory_audit WHERE action IN "
                                  "('dedup_supersede','detect_contradiction','decay_dormant','generalize_domain');",
                             -1, &st, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(st) == SQLITE_ROW && sqlite3_column_int(st, 0) >= 4);
    sqlite3_finalize(st);
    sqlite3_close(raw);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_hits_reinforce_and_clear_dormant) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-hits-reinforce");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    const std::string scope = "project:hits-reinforce";
    CHECK(store.ensureScope(scope, "project", "", "hits"));
    const std::string root = store.ensureNode(scope, "node-r-root", "project", "Projeto");
    const std::string leaf = store.ensureNode(scope, "node-r-leaf", "entity", "Folha");
    const std::string edgeId = store.recordEdge(scope, root, leaf, "USES", taskforge::Json(),
                                                "", "", 0.7, "active", "test");
    CHECK(!edgeId.empty());
    taskforge::memory::MemoryEdgeRecord record;
    CHECK(store.getEdge(scope, edgeId, record));
    CHECK(store.updateEdgeState(scope, edgeId, "active", "", record.confidence,
                                taskforge::Json::object(), "", "2020-01-01T00:00:00Z",
                                "", error));

    CHECK(store.recordHit(scope, "run-r1", "agent-r", edgeId, "fact", true, false));
    CHECK(store.markHitsUsed(scope, "run-r1", "agent-r", {edgeId}) == 1);
    CHECK(store.finishHits(scope, "run-r1", "agent-r", "success"));
    CHECK(store.getEdge(scope, edgeId, record));
    CHECK(record.successCount == 1);
    CHECK(!record.lastUsed.empty());
    CHECK(record.dormantAt.empty());

    CHECK(store.recordHit(scope, "run-r2", "agent-r", edgeId, "fact", true, false));
    CHECK(store.markHitsUsed(scope, "run-r2", "agent-r", {edgeId}) == 1);
    CHECK(store.finishHits(scope, "run-r2", "agent-r", "failure"));
    CHECK(store.getEdge(scope, edgeId, record));
    CHECK(record.failureCount == 1);
    CHECK(record.confidence < 0.72);

    store.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_snapshot_preserves_decay_fields) {
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-snapshot-decay");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    const std::string scope = "project:snapshot-decay";
    CHECK(store.ensureScope(scope, "project", "", "snapshot"));
    const std::string root = store.ensureNode(scope, "node-s-root", "project", "Projeto");
    const std::string leaf = store.ensureNode(scope, "node-s-leaf", "entity", "Folha");
    const std::string edgeId = store.recordEdge(scope, root, leaf, "USES", taskforge::Json(),
                                                "", "", 0.7, "active", "test");
    CHECK(!edgeId.empty());
    CHECK(store.updateEdgeState(scope, edgeId, "active", "", 0.6, taskforge::Json::object(),
                                "", "2020-02-02T00:00:00Z", "2020-01-01T00:00:00Z", error));
    CHECK(store.addAlias(scope, "folha", leaf, "test"));
    const std::string playbookId = store.recordPlaybook(
        scope, "snapshot_task", "Snapshot playbook",
        taskforge::Json::array({taskforge::Json{{"tool", "read"}},
                                taskforge::Json{{"tool", "write"}}}),
        taskforge::Json::object(), 0.7, "active", "ep-snapshot",
        taskforge::Json::array({taskforge::Json{{"kind", "episode"}, {"id", "ep-snapshot"}}}),
        2, 1);
    CHECK(!playbookId.empty());
    auto snapshot = store.toCognitiveJson(scope);
    CHECK(snapshot["edges"].is_array());
    CHECK(snapshot["aliases"].is_array());

    CHECK(store.purge(scope, taskforge::memory::MemoryPurgeOptions{false, false}));
    CHECK(store.importCognitiveJson(scope, snapshot, error));
    taskforge::memory::MemoryEdgeRecord restored;
    CHECK(store.getEdge(scope, edgeId, restored));
    CHECK(restored.dormantAt == "2020-02-02T00:00:00Z");
    CHECK(restored.lastUsed == "2020-01-01T00:00:00Z");
    CHECK(store.nodeByAlias(scope, "folha") == leaf);
    taskforge::memory::MemoryPlaybookRecord restoredPlaybook;
    CHECK(store.getPlaybook(scope, playbookId, restoredPlaybook));
    CHECK(restoredPlaybook.successCount == 2);
    CHECK(restoredPlaybook.failureCount == 1);
    CHECK(restoredPlaybook.status == "active");
    CHECK(restoredPlaybook.stepsJson.find("read") != std::string::npos);
    store.close();
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_playbook_compiler_and_brief) {
    using taskforge::memory::MemoryEpisodeRecord;
    using taskforge::memory::MemoryService;
    const fs::path dir = makeTempDir("memory-playbook");
    const fs::path db = dir / "memory.db";
    const std::string pId = "memory-playbook-test";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));

    auto makeEpisode = [&](const std::string& id, const std::string& outcome,
                           const std::string& taskType) {
        MemoryEpisodeRecord episode;
        episode.scopeId = "project:" + pId;
        episode.runId = "run-" + id;
        episode.nodeId = "agent-" + id;
        episode.kind = outcome == "success" ? "agent_success" : "agent_failure";
        episode.content = "episode " + id;
        episode.taskType = taskType;
        episode.outcome = outcome;
        episode.toolCalls = taskforge::Json::array({
            taskforge::Json{{"name", "read"}, {"ok", true}},
            taskforge::Json{{"name", "write"}, {"ok", true}}});
        episode.evidence = taskforge::Json::array(
            {taskforge::Json{{"kind", "artifact"}, {"id", id}}});
        return memory.recordEpisode(pId, episode);
    };

    const std::string success1 = makeEpisode("p1", "success", "svg_playbook");
    const std::string success2 = makeEpisode("p2", "success", "svg_playbook");
    const std::string failure1 = makeEpisode("p3", "failure", "svg_playbook");
    const std::string failureOnly = makeEpisode("p4", "failure", "sem_sucesso");
    CHECK(!success1.empty() && !success2.empty() && !failure1.empty() && !failureOnly.empty());

    CHECK(memory.extractProposals(pId, success1) >= 1);
    auto playbooks = memory.listPlaybooks(pId, "svg_playbook", "", 10);
    CHECK(playbooks.size() == 1);
    CHECK(playbooks[0].value("status", std::string()) == "candidate");
    CHECK(playbooks[0].value("successCount", 0LL) == 1);

    CHECK(memory.extractProposals(pId, success2) >= 1);
    playbooks = memory.listPlaybooks(pId, "svg_playbook", "", 10);
    CHECK(playbooks.size() == 1);
    CHECK(playbooks[0].value("status", std::string()) == "candidate");
    CHECK(playbooks[0].value("successCount", 0LL) == 2);
    const double confidenceBefore = playbooks[0].value("confidence", 0.0);

    CHECK(memory.extractProposals(pId, failure1) >= 1);
    playbooks = memory.listPlaybooks(pId, "svg_playbook", "", 10);
    CHECK(playbooks.size() == 1);
    CHECK(playbooks[0].value("failureCount", 0LL) == 1);
    CHECK(playbooks[0].value("confidence", 0.0) < confidenceBefore);

    CHECK(memory.extractProposals(pId, failureOnly) >= 1);
    CHECK(memory.listPlaybooks(pId, "sem_sucesso", "", 10).empty());

    taskforge::memory::MemoryBriefInfo info;
    const std::string brief = memory.buildBrief(pId, "pipeline", {"svg_playbook"}, {},
                                                2000, true, false, &info);
    CHECK(brief.find("ARESTRIX MEMORIA: PLAYBOOKS") == std::string::npos);
    CHECK(brief.find("[PLAYBOOK] svg_playbook") == std::string::npos);
    CHECK(info.playbookIds.empty());

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_workspace_promotion_and_isolation) {
    using taskforge::memory::MemoryService;
    const fs::path dir = makeTempDir("memory-workspace");
    const fs::path projectDb = dir / "projeto" / ".arestrix" / "memory" / "memory.db";
    const fs::path workspaceDb = dir / "projeto" / ".workspace" / "memory" / "memory.db";
    const fs::path siblingDb = dir / "irmao" / ".workspace" / "memory" / "memory.db";
    const std::string pA = "workspace-project-a";
    const std::string pB = "workspace-project-b";
    const std::string wsA = "workspace-a";
    const std::string wsB = "workspace-b";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pA);
    memory.closeProject(pB);
    CHECK(memory.configure(pA, true, projectDb.string()));
    CHECK(memory.configureProjectWorkspace(pA, wsA, workspaceDb.string()));
    CHECK(memory.configure(pB, true, (dir / "irmao" / ".arestrix" / "memory" / "memory.db").string()));
    CHECK(memory.configureProjectWorkspace(pB, wsB, siblingDb.string()));

    const std::string privateLesson = memory.recordLesson(pA, "privado", "manual",
                                                          "REGRA-PRIVADA-PROJETO", "seed");
    CHECK(!privateLesson.empty());
    auto* workspaceA = memory.workspaceStore(wsA);
    CHECK(workspaceA != nullptr);
    CHECK(!workspaceA->recordLesson("workspace:" + wsA, "compartilhado", "manual",
                                    "REGRA-WORKSPACE", "seed").empty());
    auto* workspaceB = memory.workspaceStore(wsB);
    CHECK(workspaceB != nullptr);

    // Projeto le o workspace; workspace nao le o projeto privado.
    auto result = memory.searchContext(pA, "REGRA-WORKSPACE", 10, {});
    CHECK(result.find("REGRA-WORKSPACE") != std::string::npos);
    CHECK(result.find("ARESTRIX WORKSPACE MEMORY") != std::string::npos);
    CHECK(workspaceA->searchContext("workspace:" + wsA, "REGRA-PRIVADA-PROJETO", 10, {}).empty());

    // Promocao exige evidencia.
    std::string error;
    CHECK(!memory.promoteToWorkspace(pA, "lesson", privateLesson,
                                     taskforge::Json::array(), "test", error));
    CHECK(error.find("evidencia") != std::string::npos);

    taskforge::Json evidence = taskforge::Json::array(
        {taskforge::Json{{"kind", "manual"}, {"note", "revisado"}}});
    CHECK(memory.promoteToWorkspace(pA, "lesson", privateLesson, evidence, "test", error));
    CHECK(workspaceA->getLessons("workspace:" + wsA, "privado").size() == 1);
    result = memory.searchContext(pA, "REGRA-PRIVADA-PROJETO", 10, {});
    CHECK(result.find("REGRA-PRIVADA-PROJETO") != std::string::npos);

    // Promocao de fato.
    auto* projectStore = memory.store(pA);
    const std::string scopeA = "project:" + pA;
    const std::string rootNode = projectStore->ensureNode(scopeA, "node-w-root", "project", "Projeto");
    const std::string leafNode = projectStore->ensureNode(scopeA, "node-w-leaf", "entity", "Folha");
    CHECK(!rootNode.empty() && !leafNode.empty());
    const std::string factId = projectStore->recordEdge(
        scopeA, rootNode, leafNode, "USES", taskforge::Json(), "", "", 0.8, "active", "test");
    CHECK(!factId.empty());
    CHECK(memory.promoteToWorkspace(pA, "fact", factId, evidence, "test", error));
    bool foundFact = false;
    for (const auto& edge : workspaceA->listEdges("workspace:" + wsA, "active", 100)) {
        if (edge.relation == "USES") foundFact = true;
    }
    CHECK(foundFact);

    // Irmão não enxerga o promovido.
    CHECK(workspaceB->searchContext("workspace:" + wsB, "REGRA-PRIVADA-PROJETO", 10, {}).empty());
    CHECK(memory.workspaceStatus(pA).value("configured", false));
    CHECK(memory.workspaceStatus(pA).value("lessons", 0LL) >= 2);

    memory.closeProject(pA);
    memory.closeProject(pB);
    memory.closeWorkspace(wsA);
    memory.closeWorkspace(wsB);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_institution_auth_policy_and_isolation) {
    using taskforge::memory::MemoryInstitutionService;
    using taskforge::memory::MemoryRole;
    using taskforge::memory::MemoryStore;
    const fs::path dir = makeTempDir("memory-institution");
    const fs::path db = dir / "institution.db";
    MemoryInstitutionService service;
    std::string error;
    CHECK(service.configure("acme", db.string(), error));
    CHECK(service.addToken("viewer-token", "ana", MemoryRole::Viewer));
    CHECK(service.addToken("editor-token", "bruno", MemoryRole::Editor));
    CHECK(service.addToken("admin-token", "carla", "admin"));

    CHECK(service.writeLesson("viewer-token", "geral", "manual", "REGRA-INSTITUCIONAL", error).empty());
    CHECK(error.find("permissao") != std::string::npos);
    const std::string lessonId = service.writeLesson("editor-token", "geral", "manual",
                                                     "REGRA-INSTITUCIONAL", error);
    CHECK(!lessonId.empty());
    const std::string found = service.readContext("viewer-token", "REGRA-INSTITUCIONAL", 10, {}, error);
    CHECK(found.find("REGRA-INSTITUCIONAL") != std::string::npos);
    CHECK(service.readContext("token-invalido", "REGRA-INSTITUCIONAL", 10, {}, error).empty());
    CHECK(error.find("autenticado") != std::string::npos);
    auto status = service.status("admin-token", error);
    CHECK(status.value("ok", false));
    CHECK(status.value("role", std::string()) == "admin");
    CHECK(status.value("lessons", 0LL) >= 1);

    CHECK(service.setPolicy("admin-token", "retention_days", taskforge::Json(30), true, error));
    CHECK(!service.setPolicy("editor-token", "retention_days", taskforge::Json(10), false, error));
    CHECK(error.find("permissao") != std::string::npos);

    // Invariante de escopo: filho nao sobrescreve politica imutavel do ancestral.
    const fs::path policyDb = dir / "policy.db";
    MemoryStore store(policyDb.string());
    CHECK(store.open(error));
    CHECK(store.ensureScope("institution:acme", "institution", "", "acme"));
    CHECK(store.ensureScope("workspace:child", "workspace", "institution:acme", "child"));
    CHECK(store.setPolicy("institution:acme", "retention_days", taskforge::Json(30), true,
                          "admin", error));
    CHECK(!store.setPolicy("workspace:child", "retention_days", taskforge::Json(10), false,
                           "child", error));
    CHECK(error.find("imutavel") != std::string::npos);
    CHECK(store.setPolicy("workspace:child", "allow_external_models", taskforge::Json(false),
                          false, "child", error));
    auto effective = store.effectivePolicy("workspace:child", "retention_days");
    CHECK(effective.value("scopeId", std::string()) == "institution:acme");
    CHECK(effective["value"].get<int>() == 30);
    store.close();

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_institution_http_contract_roles_and_health) {
    using taskforge::memory::handleInstitutionRest;
    using taskforge::memory::MemoryInstitutionService;
    const fs::path dir = makeTempDir("memory-institution-http");
    const fs::path db = dir / "institution.db";
    MemoryInstitutionService service;
    std::string error;
    CHECK(service.configure("acme", db.string(), error));
    CHECK(service.addToken("viewer-token", "ana", "viewer"));
    CHECK(service.addToken("editor-token", "bruno", "editor"));
    CHECK(service.addToken("admin-token", "carla", "admin"));

    auto health = handleInstitutionRest(service, "GET", "/health", "", "");
    CHECK(health.status == 200);
    auto healthJson = taskforge::jsonutil::parse(health.body);
    CHECK(healthJson.value("service", std::string()) == "arestrix-memory");
    CHECK(healthJson.value("authRequired", false));

    auto unauthorized = handleInstitutionRest(service, "GET", "/v1/status", "", "");
    CHECK(unauthorized.status == 401);
    CHECK(unauthorized.body.find("autenticado") != std::string::npos);

    auto viewerStatus = handleInstitutionRest(service, "GET", "/v1/status", "viewer-token", "");
    CHECK(viewerStatus.status == 200);
    auto viewerJson = taskforge::jsonutil::parse(viewerStatus.body);
    CHECK(viewerJson.value("role", std::string()) == "viewer");
    CHECK(viewerJson.value("institutionId", std::string()) == "acme");

    auto viewerWrite = handleInstitutionRest(
        service, "POST", "/v1/lessons", "viewer-token",
        R"({"domain":"geral","triggerError":"manual","lessonRule":"REGRA-HTTP"})");
    CHECK(viewerWrite.status == 403);

    auto editorWrite = handleInstitutionRest(
        service, "POST", "/v1/lessons", "editor-token",
        R"({"domain":"geral","triggerError":"manual","lessonRule":"REGRA-HTTP"})");
    CHECK(editorWrite.status == 200);
    auto editorJson = taskforge::jsonutil::parse(editorWrite.body);
    CHECK(editorJson.value("ok", false));
    CHECK(!editorJson.value("id", std::string()).empty());

    auto context = handleInstitutionRest(
        service, "POST", "/v1/context", "viewer-token",
        R"({"query":"REGRA-HTTP","limit":5})");
    CHECK(context.status == 200);
    CHECK(context.body.find("REGRA-HTTP") != std::string::npos);

    auto editorPolicy = handleInstitutionRest(
        service, "POST", "/v1/policies", "editor-token",
        R"({"key":"retention_days","value":30,"immutable":true})");
    CHECK(editorPolicy.status == 403);

    auto adminPolicy = handleInstitutionRest(
        service, "POST", "/v1/policies", "admin-token",
        R"({"key":"retention_days","value":30,"immutable":true})");
    CHECK(adminPolicy.status == 200);

    auto badBody = handleInstitutionRest(service, "POST", "/v1/lessons", "editor-token", "{}");
    CHECK(badBody.status == 400);

    auto badLimit = handleInstitutionRest(
        service, "POST", "/v1/context", "viewer-token",
        R"({"query":"REGRA-HTTP","limit":"dez"})");
    CHECK(badLimit.status == 400);

    auto missingRoute = handleInstitutionRest(service, "GET", "/v1/nope", "viewer-token", "");
    CHECK(missingRoute.status == 404);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_institution_mcp_tools_and_auth) {
    using taskforge::memory::handleInstitutionMcp;
    using taskforge::memory::MemoryInstitutionService;
    const fs::path dir = makeTempDir("memory-institution-mcp");
    const fs::path db = dir / "institution.db";
    MemoryInstitutionService service;
    std::string error;
    CHECK(service.configure("acme", db.string(), error));
    CHECK(service.addToken("viewer-token", "ana", "viewer"));
    CHECK(service.addToken("editor-token", "bruno", "editor"));

    auto unauthorizedInit = handleInstitutionMcp(
        service, "", taskforge::Json{{"jsonrpc", "2.0"}, {"id", 0}, {"method", "initialize"}});
    CHECK(unauthorizedInit.hasResponse);
    CHECK(unauthorizedInit.httpStatus == 401);

    auto initialize = handleInstitutionMcp(
        service, "viewer-token",
        taskforge::Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
    CHECK(initialize.hasResponse);
    CHECK(initialize.response["result"]["serverInfo"]["name"].str() == "arestrix-memory");

    auto tools = handleInstitutionMcp(
        service, "viewer-token",
        taskforge::Json{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}});
    CHECK(tools.hasResponse);
    const std::string toolsDump = tools.response["result"]["tools"].dump();
    CHECK(toolsDump.find("memory_institution_context") != std::string::npos);
    CHECK(toolsDump.find("memory_institution_write_lesson") != std::string::npos);
    CHECK(toolsDump.find("memory_institution_set_policy") != std::string::npos);

    auto viewerWrite = handleInstitutionMcp(
        service, "viewer-token",
        taskforge::Json{{"jsonrpc", "2.0"},
                        {"id", 3},
                        {"method", "tools/call"},
                        {"params", taskforge::Json{{"name", "memory_institution_write_lesson"},
                                                   {"arguments", taskforge::Json{{"domain", "geral"},
                                                                                 {"triggerError", "manual"},
                                                                                 {"lessonRule", "REGRA-MCP"}}}}}});
    CHECK(viewerWrite.hasResponse);
    CHECK(viewerWrite.response["result"]["isError"].get<bool>());

    auto editorWrite = handleInstitutionMcp(
        service, "editor-token",
        taskforge::Json{{"jsonrpc", "2.0"},
                        {"id", 4},
                        {"method", "tools/call"},
                        {"params", taskforge::Json{{"name", "memory_institution_write_lesson"},
                                                   {"arguments", taskforge::Json{{"domain", "geral"},
                                                                                 {"triggerError", "manual"},
                                                                                 {"lessonRule", "REGRA-MCP"}}}}}});
    CHECK(editorWrite.hasResponse);
    CHECK(!editorWrite.response["result"]["isError"].get<bool>());

    auto context = handleInstitutionMcp(
        service, "viewer-token",
        taskforge::Json{{"jsonrpc", "2.0"},
                        {"id", 5},
                        {"method", "tools/call"},
                        {"params", taskforge::Json{{"name", "memory_institution_context"},
                                                   {"arguments", taskforge::Json{{"query", "REGRA-MCP"}}}}}});
    CHECK(context.hasResponse);
    CHECK(!context.response["result"]["isError"].get<bool>());
    CHECK(context.response["result"]["content"][0]["text"].str().find("REGRA-MCP") != std::string::npos);

    auto unauthorized = handleInstitutionMcp(
        service, "bad-token",
        taskforge::Json{{"jsonrpc", "2.0"},
                        {"id", 6},
                        {"method", "tools/call"},
                        {"params", taskforge::Json{{"name", "memory_institution_status"},
                                                   {"arguments", taskforge::Json::object()}}}});
    CHECK(unauthorized.hasResponse);
    CHECK(unauthorized.httpStatus == 401);
    CHECK(unauthorized.response["error"]["code"].get<int>() == -32001);

    auto notification = handleInstitutionMcp(
        service, "viewer-token",
        taskforge::Json{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
    CHECK(!notification.hasResponse);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_institution_token_loader) {
    using taskforge::memory::loadInstitutionTokens;
    using taskforge::memory::MemoryInstitutionService;
    const fs::path dir = makeTempDir("memory-institution-tokens");
    const fs::path db = dir / "institution.db";
    const fs::path tokens = dir / "tokens.txt";
    std::ofstream(tokens) << "# token principal role\n"
                          << "file-token ana viewer\n"
                          << "editor-token bruno editor\n";
    MemoryInstitutionService service;
    std::string error;
    CHECK(service.configure("acme", db.string(), error));
    CHECK(loadInstitutionTokens(service, tokens.string(), "", error));

    taskforge::memory::MemoryPrincipal principal;
    CHECK(service.authenticate("file-token", principal));
    CHECK(principal.id == "ana");
    CHECK(principal.role == taskforge::memory::MemoryRole::Viewer);
    CHECK(service.authenticate("editor-token", principal));
    CHECK(principal.role == taskforge::memory::MemoryRole::Editor);
    CHECK(!service.authenticate("missing", principal));

    MemoryInstitutionService jsonService;
    CHECK(jsonService.configure("acme", (dir / "json.db").string(), error));
    CHECK(loadInstitutionTokens(
        jsonService, "",
        R"([{"token":"admin-token","principal":"carla","role":"admin"}])", error));
    CHECK(jsonService.authenticate("admin-token", principal));
    CHECK(principal.role == taskforge::memory::MemoryRole::Admin);

    CHECK(!loadInstitutionTokens(jsonService, "", R"({"token":"x"})", error));
    CHECK(error.find("array") != std::string::npos);

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
    taskforge::memory::MemoryBriefInfo zeroInfo;
    std::string zeroBrief = memory.buildBrief("brief-test", "pipeline", {"memoria"}, {"resumo"}, 0, true, true, &zeroInfo);
    CHECK(zeroBrief.empty());
    CHECK(zeroInfo.chars == 0 && zeroInfo.estimatedTokens == 0 && !zeroInfo.truncated);
    CHECK(zeroInfo.lessonIds.empty() && zeroInfo.workingKeys.empty());
    memory.closeProject("brief-test");
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_maintenance_config_and_background_run) {
    using taskforge::memory::MemoryEpisodeRecord;
    using taskforge::memory::MemoryMaintenanceConfig;
    using taskforge::memory::MemoryService;
    using taskforge::memory::maintenanceConfigFromJson;

    taskforge::Json raw = taskforge::Json::object();
    raw["consolidationMode"] = "idle";
    raw["consolidationIdleSeconds"] = 15;
    raw["consolidationEpisodeThreshold"] = 7;
    raw["consolidationDormantDays"] = 42;
    raw["deduplicate"] = false;
    const MemoryMaintenanceConfig parsed = maintenanceConfigFromJson(raw);
    CHECK(parsed.enabled);
    CHECK(parsed.mode == "idle");
    CHECK(parsed.idleSeconds == 15);
    CHECK(parsed.episodeThreshold == 7);
    CHECK(parsed.options.dormantAfterDays == 42);
    CHECK(!parsed.options.deduplicate);

    taskforge::Json legacy = taskforge::Json::object();
    legacy["consolidationEnabled"] = true;
    CHECK(maintenanceConfigFromJson(legacy).mode == "idle");
    taskforge::Json invalid = taskforge::Json::object();
    invalid["consolidationMode"] = "always";
    const MemoryMaintenanceConfig invalidParsed = maintenanceConfigFromJson(invalid);
    CHECK(!invalidParsed.enabled && invalidParsed.mode == "off");

    const fs::path dir = makeTempDir("memory-maintenance");
    const fs::path db = dir / "memory.db";
    const std::string pId = "memory-maintenance-test";
    auto& memory = MemoryService::getInstance();
    memory.closeProject(pId);
    CHECK(memory.configure(pId, true, db.string()));

    MemoryMaintenanceConfig config;
    config.enabled = true;
    config.mode = "after_episodes";
    config.episodeThreshold = 1;
    config.options.dormantAfterDays = 30;
    CHECK(memory.configureMaintenance(pId, config));
    auto status = memory.maintenanceStatus(pId);
    CHECK(status.value("enabled", false));
    CHECK(status.value("mode", std::string()) == "after_episodes");
    CHECK(status.value("runs", 0LL) == 0);

    MemoryEpisodeRecord episode;
    episode.scopeId = "project:" + pId;
    episode.runId = "run-maintenance";
    episode.nodeId = "agent-maintenance";
    episode.kind = "agent_success";
    episode.content = "episodio de manutencao";
    episode.outcome = "success";
    CHECK(!memory.recordEpisode(pId, episode).empty());

    bool ran = false;
    for (int i = 0; i < 200; ++i) {
        status = memory.maintenanceStatus(pId);
        if (status.value("runs", 0LL) >= 1) {
            ran = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ran);
    status = memory.maintenanceStatus(pId);
    CHECK(status.value("runs", 0LL) >= 1);
    CHECK(status.value("lastReason", std::string()) == "after_episodes");
    CHECK(status.contains("lastReport"));
    CHECK(status["lastReport"].is_object());

    // A rodada manual tambem atualiza o mesmo estado observavel.
    std::string error;
    const taskforge::Json report = memory.runMaintenanceOnce(pId, "manual-test", error);
    CHECK(error.empty());
    CHECK(report.is_object());
    status = memory.maintenanceStatus(pId);
    CHECK(status.value("runs", 0LL) >= 2);
    CHECK(status.value("lastReason", std::string()) == "manual-test");

    memory.closeProject(pId);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(memory_search_respects_domains_limit_and_records_consulted_hits) {
    using taskforge::memory::MemoryStore;
    using taskforge::memory::MemoryHitRef;
    const fs::path dir = makeTempDir("memory-search");
    MemoryStore store((dir / "memory.db").string());
    std::string error;
    CHECK(store.open(error));
    CHECK(store.ensureScope("project:busca", "project", "", "busca"));

    const std::string alpha = store.recordLesson("project:busca", "alpha", "manual", "REGRA-ALPHA", "seed");
    const std::string beta = store.recordLesson("project:busca", "beta", "manual", "REGRA-BETA", "seed");
    CHECK(!alpha.empty() && !beta.empty());

    const std::string root = store.ensureNode("project:busca", "node-search-root", "project", "Projeto Busca");
    const std::string a = store.ensureNode("project:busca", "node-search-a", "library", "Biblioteca Alfa");
    const std::string b = store.ensureNode("project:busca", "node-search-b", "library", "Biblioteca Beta");
    const std::string expired = store.ensureNode("project:busca", "node-search-exp", "library", "Biblioteca Expirada");
    CHECK(!root.empty() && !a.empty() && !b.empty() && !expired.empty());

    taskforge::Json metaA = taskforge::Json::object();
    metaA["domain"] = "alpha";
    taskforge::Json metaB = taskforge::Json::object();
    metaB["domain"] = "beta";
    CHECK(!store.recordEdge("project:busca", root, a, "USES", taskforge::Json(),
                            "2020-01-01T00:00:00Z", "", 0.8, "active", "test", "", metaA).empty());
    CHECK(!store.recordEdge("project:busca", root, b, "USES", taskforge::Json(),
                            "2020-01-01T00:00:00Z", "", 0.8, "active", "test", "", metaB).empty());
    CHECK(!store.recordEdge("project:busca", root, expired, "USES", taskforge::Json(),
                            "2020-01-01T00:00:00Z", "2020-01-02T00:00:00Z", 0.8, "active", "test", "", metaA).empty());

    // 1. Dominio restrito: so a licao de alpha aparece.
    std::vector<MemoryHitRef> hits;
    std::string result = store.searchContext("project:busca", "REGRA", 10, {"alpha"}, &hits);
    CHECK(result.find("REGRA-ALPHA") != std::string::npos);
    CHECK(result.find("REGRA-BETA") == std::string::npos);
    CHECK(hits.size() == 1 && hits[0].kind == "lesson");

    // 2. Limite total: uma licao, mesmo com duas correspondencias.
    hits.clear();
    result = store.searchContext("project:busca", "REGRA", 1, {}, &hits);
    const size_t firstLesson = result.find("[LICAO]");
    CHECK(firstLesson != std::string::npos);
    CHECK(result.find("[LICAO]", firstLesson + 1) == std::string::npos);
    CHECK(hits.size() == 1);

    // 3. Fato com metadata.domain autorizado; fato de outro dominio e fato expirado ficam de fora.
    hits.clear();
    result = store.searchContext("project:busca", "Biblioteca", 10, {"alpha"}, &hits);
    CHECK(result.find("Biblioteca Alfa") != std::string::npos);
    CHECK(result.find("Biblioteca Beta") == std::string::npos);
    CHECK(result.find("Biblioteca Expirada") == std::string::npos);
    CHECK(hits.size() == 1 && hits[0].kind == "fact");

    // 4. Agente sem restricao continua vendo os dois fatos validos.
    result = store.searchContext("project:busca", "Biblioteca", 10, {});
    CHECK(result.find("Biblioteca Alfa") != std::string::npos);
    CHECK(result.find("Biblioteca Beta") != std::string::npos);
    CHECK(result.find("Biblioteca Expirada") == std::string::npos);

    // 5. Memoria descoberta por busca e nao injetada vira hit consultado.
    CHECK(store.recordConsultedHits("project:busca", "run-search", "node-x",
                                    {MemoryHitRef{alpha, "lesson"}}) == 1);
    auto summary = store.hitSummary("project:busca");
    CHECK(summary["injected"] == 0);
    CHECK(summary["consulted"] == 1);
    CHECK(summary["usedInjected"] == 0);

    // 6. Memoria injetada e depois consultada atualiza a mesma linha, sem duplicar.
    CHECK(store.recordHit("project:busca", "run-2", "node-y", alpha, "lesson", true, false));
    CHECK(store.recordConsultedHits("project:busca", "run-2", "node-y",
                                    {MemoryHitRef{alpha, "lesson"}}) == 1);
    summary = store.hitSummary("project:busca");
    CHECK(summary["injected"] == 1);
    CHECK(summary["usedInjected"] == 1);
    CHECK(summary["consulted"] == 1);
    CHECK(summary["used"] == 2);

    store.close();
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
    for (const char* want : {"arquivos", "http", "dados", "tempo", "git", "sqlite"})
        CHECK(std::find(ids.begin(), ids.end(), std::string("arestrix.") + want) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), std::string("arestrix.memoria")) == ids.end());
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

TEST(native_time) {
    auto root = makeTempDir("native-time");

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
    int created = 0, saved = 0, projectUpdates = 0;
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
    bool updateProject(const json& patch, json& updated, std::string& error) override {
        if (!patch.is_object()) { error = "patch invalido"; return false; }
        for (const char* key : {"name", "version", "description", "outputDir"}) {
            if (patch.contains(key)) {
                if (!patch.at(key).is_string()) { error = "campo deve ser texto"; return false; }
                project[key] = patch.at(key);
            }
        }
        ++projectUpdates;
        updated = project;
        return true;
    }
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
    CHECK(doc.node(ida)->at("outputs").size() == 3);   // out + artifact_out + call
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

TEST(assistant_supports_call_and_interaction_vertices) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    as::GraphDocument doc;

    auto agent = doc.addNode("agent", "Pesquisador", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto interaction = doc.addNode("interaction", "Aprovar", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto call = doc.addNode("call", "Chamar revisor", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    CHECK(agent.ok && interaction.ok && call.ok);
    std::string agentId = agent.result.value("id", std::string());
    std::string interactionId = interaction.result.value("id", std::string());
    std::string callId = call.result.value("id", std::string());

    // O agente expoe a porta call, como no Graph.buildNode do Studio.
    bool agentCallPort = false;
    auto agentNode = doc.node(agentId);
    CHECK(agentNode.has_value());
    for (const auto& p : agentNode->at("outputs")) {
        if (p.value("name", std::string()) == "call" && p.value("type", std::string()) == "call") agentCallPort = true;
    }
    CHECK(agentCallPort);
    CHECK(doc.node(callId)->at("callSettings").value("input", std::string()) == "");

    // Porta call cria aresta de chamada com os padroes de ICallSettings.
    auto callEdge = doc.connect(agentId, "call", interactionId, "in", RuntimeLang::Pt);
    CHECK(callEdge.ok);
    CHECK(callEdge.result.value("kind", std::string()) == "call");
    CHECK(doc.schema().at("edges").size() == 1);
    CHECK(doc.schema().at("edges")[0].value("kind", std::string()) == "call");
    CHECK(doc.schema().at("edges")[0].at("callSettings").value("maxCalls", 0) == 1);
    CHECK(doc.summary().at("edges")[0].value("kind", std::string()) == "call");

    // O tipo da aresta e coerente com a porta.
    CHECK(!doc.connect(callId, "call", agentId, "in", RuntimeLang::En, "flow").ok);
    CHECK(!doc.connect(interactionId, "", callId, "in", RuntimeLang::En, "call").ok);

    // Vertice Call sem alvo e erro; com um alvo e maxCalls proprio, valida.
    CHECK(!doc.validate(RuntimeLang::Pt).value("ok", true));
    auto c = doc.connect(callId, "call", agentId, "in", RuntimeLang::Pt, "",
                         json{{"maxCalls", 3}, {"description", "revisar"}});
    CHECK(c.ok);
    CHECK(doc.schema().at("edges")[1].at("callSettings").value("maxCalls", 0) == 3);
    CHECK(doc.schema().at("edges")[1].at("callSettings").value("description", std::string()) == "revisar");
    CHECK(doc.validate(RuntimeLang::Pt).value("ok", false));

    // Settings novos editaveis e mesclados, sem perder o restante do objeto.
    auto ui = doc.updateNode(interactionId, json{{"interactionSettings", json{{"channel", "studio"}}}}, RuntimeLang::Pt);
    CHECK(ui.ok);
    CHECK(doc.node(interactionId)->at("interactionSettings").value("channel", std::string()) == "studio");
    CHECK(doc.node(interactionId)->at("interactionSettings").at("form").is_object());
    auto uc = doc.updateNode(callId, json{{"callSettings", json{{"input", "contexto"}}}}, RuntimeLang::Pt);
    CHECK(uc.ok);
    CHECK(doc.node(callId)->at("callSettings").value("input", std::string()) == "contexto");
    CHECK(doc.node(callId)->at("callSettings").at("source").is_object());

    // Alvo de chamada nao pode participar do fluxo.
    CHECK(doc.connect(callId, "", interactionId, "in", RuntimeLang::Pt).ok);
    CHECK(!doc.validate(RuntimeLang::Pt).value("ok", true));
}

TEST(assistant_warns_on_write_conflict_between_parallel_paths) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    as::GraphDocument doc;

    auto a1 = doc.addNode("static", "Coletar", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto oa = doc.addNode("output", "Entrega A", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto b1 = doc.addNode("static", "Baixar", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto ob = doc.addNode("output", "Entrega B", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    CHECK(a1.ok && oa.ok && b1.ok && ob.ok);

    const std::string a1id = a1.result.value("id", std::string());
    const std::string oaid = oa.result.value("id", std::string());
    const std::string b1id = b1.result.value("id", std::string());
    const std::string obid = ob.result.value("id", std::string());

    // Mesmo arquivo escrito de formas diferentes: a comparacao normaliza.
    CHECK(doc.updateNode(oaid, json{{"outputSettings", json{{"destinationPath", "exports\\relatorio.md"}}}},
                         RuntimeLang::Pt).ok);
    CHECK(doc.updateNode(obid, json{{"outputSettings", json{{"destinationPath", "./exports/relatorio.md"}}}},
                         RuntimeLang::Pt).ok);
    CHECK(doc.connect(a1id, "", oaid, "", RuntimeLang::Pt).ok);
    CHECK(doc.connect(b1id, "", obid, "", RuntimeLang::Pt).ok);

    auto hasConflict = [](const json& v) {
        const json& warnings = v.at("warnings");
        for (const auto& w : warnings) {
            if (w.is_string() && w.get<std::string>().find("podem gravar") != std::string::npos) return true;
        }
        return false;
    };

    json v = doc.validate(RuntimeLang::Pt);
    CHECK(v.value("ok", false));   // aviso nunca bloqueia a execucao
    CHECK(hasConflict(v));

    // Com caminho de fluxo entre os dois, um executa depois do outro: deixa de ser corrida.
    CHECK(doc.connect(oaid, "", obid, "", RuntimeLang::Pt).ok);
    CHECK(!hasConflict(doc.validate(RuntimeLang::Pt)));

    // Caminhos diferentes: nada a avisar.
    CHECK(doc.updateNode(obid, json{{"outputSettings", json{{"destinationPath", "exports/outro.md"}}}},
                         RuntimeLang::Pt).ok);
    CHECK(!hasConflict(doc.validate(RuntimeLang::Pt)));
}

TEST(assistant_lists_exports_and_workspace_and_confines_artifact_reads) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    FakeAssistantHost host;
    fs::path root = makeTempDir("assistant-artifacts");
    host.project["rootPath"] = root.string();
    fs::create_directories(root / "exports");
    fs::create_directories(root / "workspace");
    { std::ofstream(root / "exports" / "relatorio.svg") << "<svg/>"; }
    { std::ofstream(root / "workspace" / "resposta.md") << "# Resposta"; }
    { std::ofstream(root.parent_path() / "segredo-artefato.txt") << "segredo"; }

    as::GraphDocument doc;
    auto out = doc.addNode("output", "Entrega", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    CHECK(out.ok);
    std::string outId = out.result.value("id", std::string());
    CHECK(doc.updateNode(outId, json{{"outputSettings", json{{"sourcePath", "workspace/resposta.md"}}}}, RuntimeLang::Pt).ok);

    auto list = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_artifacts", json::object());
    CHECK(!list.result.isError);
    CHECK(list.result.content.find("workspace/resposta.md") != std::string::npos);
    CHECK(list.result.content.find("exports/relatorio.svg") != std::string::npos);

    auto read = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_artifact", json{{"path", "workspace/resposta.md"}});
    CHECK(!read.result.isError);
    CHECK(read.result.content.find("# Resposta") != std::string::npos);
    auto escape = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_artifact",
                                           json{{"path", "../segredo-artefato.txt"}});
    CHECK(escape.result.isError);

    fs::remove_all(root);
}

TEST(assistant_reads_skills_mcp_and_updates_project_metadata) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    FakeAssistantHost host;
    fs::path root = makeTempDir("assistant-assets");
    host.project["rootPath"] = root.string();
    fs::create_directories(root / ".workspace" / "skills" / "revisor");
    json skill = json{{"id", "revisor"}, {"name", "Revisor"}, {"description", "Revisa textos"},
                      {"category", "qa"}, {"license", "MIT"}, {"repoName", "workspace"},
                      {"markdownContent", "# Revisor\n\nRevise o texto."},
                      {"systemPrompt", "Revise."}, {"tools", json::array()}};
    { std::ofstream(root / ".workspace" / "skills" / "revisor" / "skill.json") << skill.dump(2); }
    { std::ofstream(root / ".workspace" / "skills" / "revisor" / "SKILL.md") << "# Revisor\n\nRevise o texto."; }

    as::GraphDocument doc;
    auto agent = doc.addNode("agent", "Escritor", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    std::string agentId = agent.result.value("id", std::string());

    auto list = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_skills", json::object());
    CHECK(!list.result.isError);
    CHECK(list.result.content.find("\"revisor\"") != std::string::npos);
    CHECK(list.result.content.find("\"Revisor\"") != std::string::npos);

    auto read = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_skill", json{{"id", "revisor"}});
    CHECK(!read.result.isError);
    CHECK(read.result.content.find("Revise o texto.") != std::string::npos);
    CHECK(as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_skill", json{{"id", "../fora"}}).result.isError);

    auto attached = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_skill",
                                             json{{"node_id", agentId}, {"skill_id", "revisor"}});
    CHECK(!attached.result.isError);
    CHECK(attached.uiOps.size() == 1);
    auto agentNode = doc.node(agentId);
    CHECK(agentNode->at("agentSettings").at("skills").size() == 1);
    CHECK(agentNode->at("agentSettings").value("activeSkillId", std::string()) == "revisor");
    CHECK(as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_skill",
                                   json{{"node_id", agentId}, {"skill_id", "revisor"}}).uiOps.size() == 1);
    CHECK(doc.node(agentId)->at("agentSettings").at("skills").size() == 1);   // atualiza, nao duplica

    auto servers = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_mcp_servers", json::object());
    CHECK(!servers.result.isError);
    CHECK(servers.result.content.find("arestrix.dados") != std::string::npos);
    auto tools = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_mcp_tools", json{{"server", "arestrix.dados"}});
    CHECK(!tools.result.isError);
    CHECK(tools.result.content.find("json_query") != std::string::npos);
    CHECK(as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_mcp_tools", json{{"server", "nao.existe"}}).result.isError);

    auto updated = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "update_project",
                                            json{{"patch", json{{"name", "Novo Nome"}, {"version", "2.0.0"},
                                                                {"description", "Nova descricao"},
                                                                {"outputDir", "Resultados"}}}});
    CHECK(!updated.result.isError);
    CHECK(host.projectUpdates == 1);
    CHECK(updated.result.content.find("\"Novo Nome\"") != std::string::npos);
    CHECK(updated.result.content.find("\"Resultados\"") != std::string::npos);
    CHECK(doc.schema().at("metadata").value("graphName", std::string()) == "Novo Nome");
    CHECK(doc.schema().at("metadata").value("description", std::string()) == "Nova descricao");
    CHECK(updated.uiOps.size() == 1 && updated.uiOps[0].value("op", std::string()) == "update_project");
    CHECK(updated.uiOps[0].at("patch").value("version", std::string()) == "2.0.0");
    CHECK(updated.uiOps[0].at("patch").value("outputDir", std::string()) == "Resultados");
    CHECK(as::toolRisk("update_project") == as::ToolRisk::Confirm);
    CHECK(as::executeAssistantTool(doc, host, RuntimeLang::Pt, "update_project",
                                   json{{"patch", json{{"name", 42}}}}).result.isError);

    fs::remove_all(root);
}

TEST(assistant_attaches_only_audited_mcp_servers) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    FakeAssistantHost host;
    fs::path root = makeTempDir("assistant-mcp");
    host.project["rootPath"] = root.string();
    fs::create_directories(root / ".workspace" / "mcps");
    fs::create_directories(root / ".workspace" / "governance");

    auto writeServer = [&](const std::string& id, bool enabled) {
        json cfg = json{{"id", id}, {"name", id}, {"transport", "stdio"}, {"command", "node"},
                        {"args", json::array({"server.js"})}, {"isEnabled", enabled},
                        {"cachedTools", json::array({json{{"name", "echo"}, {"description", "Echo"},
                                                          {"inputSchema", json{{"type", "object"}}}}})}};
        std::ofstream(root / ".workspace" / "mcps" / (id + ".json")) << cfg.dump(2);
    };
    writeServer("eco", true);
    writeServer("pendente", true);
    writeServer("rogue", true);

    auto ledgerEntry = [](const std::string& id, const std::string& verdict, const std::string& hash) {
        return json{{"hash", hash}, {"serverId", id}, {"name", id}, {"transport", "stdio"}, {"verdict", verdict},
                    {"riskScore", verdict == "REJECT" ? 95 : 0}, {"auditedAt", "2026-10-27T00:00:00Z"},
                    {"auditedBy", "test"}, {"findingsCount", 0}, {"findings", json::array()},
                    {"warnings", json::array()}, {"checklist", json::array()}, {"toolCount", 1},
                    {"toolNames", json::array({"echo"})}, {"provenance", "external-custom"}};
    };
    json ledger = json{{"version", "1.0.0"}, {"updatedAt", "2026-10-27T00:00:00Z"},
                       {"servers", json{{"hash-eco", ledgerEntry("eco", "APPROVE", "hash-eco")},
                                        {"hash-rogue", ledgerEntry("rogue", "REJECT", "hash-rogue")}}}};
    { std::ofstream(root / ".workspace" / "governance" / "mcp-ledger.json") << ledger.dump(2); }

    as::GraphDocument doc;
    auto agent = doc.addNode("agent", "Pesquisador", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto mcp = doc.addNode("mcp", "Consultar", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    std::string agentId = agent.result.value("id", std::string());
    std::string mcpId = mcp.result.value("id", std::string());

    // Vertice mcp: server + tool auditados, argumentos e origem.
    auto mcpOk = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                          json{{"node_id", mcpId}, {"server", "eco"}, {"tool", "echo"},
                                               {"arguments", json{{"text", "oi"}}}, {"source", json{{"kind", "none"}}}});
    CHECK(!mcpOk.result.isError);
    CHECK(doc.node(mcpId)->at("mcpSettings").value("server", std::string()) == "eco");
    CHECK(doc.node(mcpId)->at("mcpSettings").value("tool", std::string()) == "echo");
    CHECK(doc.node(mcpId)->at("mcpSettings").at("arguments").value("text", std::string()) == "oi");
    CHECK(mcpOk.uiOps.size() == 1);

    // Agente: servidor entra em codeMode e a ferramenta restrita em allowedMcpTools.
    auto agentOk = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                            json{{"node_id", agentId}, {"server", "eco"}, {"tool", "echo"}});
    CHECK(!agentOk.result.isError);
    auto agentNode = doc.node(agentId);
    CHECK(agentNode->at("agentSettings").value("inheritCodeMode", true) == false);
    CHECK(agentNode->at("agentSettings").at("codeMode").at("mcpServers")[0].get<std::string>() == "eco");
    CHECK(agentNode->at("agentSettings").at("codeMode").at("allowedMcpTools")[0].get<std::string>() == "eco/echo");

    // Pendente / rejeitado / ferramenta inexistente sao recusados no proprio fluxo.
    auto pending = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                            json{{"node_id", agentId}, {"server", "pendente"}});
    CHECK(pending.result.isError);
    CHECK(pending.result.content.find("pendente de auditoria") != std::string::npos);
    auto rejected = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                             json{{"node_id", agentId}, {"server", "rogue"}});
    CHECK(rejected.result.isError);
    CHECK(rejected.result.content.find("REJEITADO") != std::string::npos);
    auto missingTool = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                                json{{"node_id", mcpId}, {"server", "eco"}, {"tool", "nao_existe"}});
    CHECK(missingTool.result.isError);
    auto mcpNoTool = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                              json{{"node_id", mcpId}, {"server", "eco"}});
    CHECK(mcpNoTool.result.isError);

    // Servidor nativo nao depende do ledger.
    auto native = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "attach_mcp",
                                           json{{"node_id", agentId}, {"server", "arestrix.dados"}, {"tool", "json_query"}});
    CHECK(!native.result.isError);

    // validate_graph passa a aplicar a governanca MCP.
    CHECK(doc.validate(RuntimeLang::Pt).value("ok", false));   // topologia pura continua valida
    auto valid = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "validate_graph", json::object());
    CHECK(valid.result.content.find("\"ok\":true") != std::string::npos);

    CHECK(doc.updateNode(mcpId, json{{"mcpSettings", json{{"server", "pendente"}}}}, RuntimeLang::Pt).ok);
    auto invalid = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "validate_graph", json::object());
    CHECK(invalid.result.content.find("\"ok\":false") != std::string::npos);
    CHECK(invalid.result.content.find("pendente de auditoria") != std::string::npos);

    fs::remove_all(root);
}

TEST(assistant_edits_nested_child_graphs) {
    namespace as = taskforge::assistant;
    using taskforge::rt::RuntimeLang;
    FakeAssistantHost host;
    as::GraphDocument doc;
    auto sub = doc.addNode("subflow", "Sub", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    CHECK(sub.ok);
    std::string subId = sub.result.value("id", std::string());

    auto addA = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                         json{{"type", "static"}, {"label", "Dentro A"}, {"parent_path", json::array({subId})}});
    CHECK(!addA.result.isError);
    CHECK(addA.uiOps.size() == 1 && addA.uiOps[0].value("op", std::string()) == "set_child_graph");
    CHECK(addA.uiOps[0].at("path")[0].get<std::string>() == subId);
    auto subNode = doc.node(subId);
    CHECK(subNode->at("childGraph").at("nodes").size() == 1);
    CHECK(subNode->at("childGraph").at("nodes")[0].value("label", std::string()) == "Dentro A");
    std::string childA = subNode->at("childGraph").at("nodes")[0].value("id", std::string());

    auto addB = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                         json{{"type", "static"}, {"label", "Dentro B"}, {"parent_path", json::array({subId})}});
    CHECK(!addB.result.isError);
    std::string childB = doc.node(subId)->at("childGraph").at("nodes")[1].value("id", std::string());
    auto connectChild = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "connect",
                                                  json{{"from", childA}, {"to", childB}, {"parent_path", json::array({subId})}});
    CHECK(!connectChild.result.isError);
    CHECK(doc.node(subId)->at("childGraph").at("edges").size() == 1);

    auto getChild = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_graph",
                                             json{{"parent_path", json::array({subId})}});
    CHECK(!getChild.result.isError);
    CHECK(getChild.result.content.find("Dentro A") != std::string::npos);
    CHECK(getChild.result.content.find("Dentro B") != std::string::npos);

    auto updateChild = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "update_node",
                                                json{{"id", childA}, {"patch", json{{"label", "Alterado"}}},
                                                     {"parent_path", json::array({subId})}});
    CHECK(!updateChild.result.isError);
    CHECK(doc.node(subId)->at("childGraph").at("nodes")[0].value("label", std::string()) == "Alterado");

    // Subflow dentro de subflow: caminho com dois niveis.
    auto inner = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                          json{{"type", "subflow"}, {"label", "Inner"}, {"parent_path", json::array({subId})}});
    CHECK(!inner.result.isError);
    std::string innerId;
    auto afterInner = doc.node(subId);
    for (const auto& n : afterInner->at("childGraph").at("nodes")) {
        if (n.value("label", std::string()) == "Inner") innerId = n.value("id", std::string());
    }
    CHECK(!innerId.empty());
    auto deep = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                         json{{"type", "static"}, {"label", "Profundo"},
                                              {"parent_path", json::array({subId, innerId})}});
    CHECK(!deep.result.isError);
    auto deepGet = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "get_graph",
                                            json{{"parent_path", json::array({subId, innerId})}});
    CHECK(!deepGet.result.isError);
    CHECK(deepGet.result.content.find("Profundo") != std::string::npos);

    // Caminho invalido (vertice que nao e subflow/module).
    auto rootStatic = doc.addNode("static", "Raiz", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto badPath = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                            json{{"type", "static"}, {"label", "X"},
                                                 {"parent_path", json::array({rootStatic.result.value("id", std::string())})}});
    CHECK(badPath.result.isError);

    // Grafo filho com ciclo sem decisao: validate_graph no escopo detecta.
    auto c1 = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                       json{{"type", "static"}, {"label", "C1"}, {"parent_path", json::array({subId})}});
    auto c2 = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "add_node",
                                       json{{"type", "static"}, {"label", "C2"}, {"parent_path", json::array({subId})}});
    CHECK(!c1.result.isError && !c2.result.isError);
    std::string c1Id, c2Id;
    auto afterC = doc.node(subId);
    for (const auto& n : afterC->at("childGraph").at("nodes")) {
        if (n.value("label", std::string()) == "C1") c1Id = n.value("id", std::string());
        if (n.value("label", std::string()) == "C2") c2Id = n.value("id", std::string());
    }
    CHECK(!c1Id.empty() && !c2Id.empty());
    CHECK(!as::executeAssistantTool(doc, host, RuntimeLang::Pt, "connect",
                                    json{{"from", c1Id}, {"to", c2Id}, {"parent_path", json::array({subId})}}).result.isError);
    CHECK(!as::executeAssistantTool(doc, host, RuntimeLang::Pt, "connect",
                                    json{{"from", c2Id}, {"to", c1Id}, {"parent_path", json::array({subId})}}).result.isError);
    auto invalidChild = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "validate_graph",
                                                  json{{"parent_path", json::array({subId})}});
    CHECK(invalidChild.result.content.find("\"ok\":false") != std::string::npos);
    CHECK(invalidChild.result.content.find("Ciclo") != std::string::npos);

    // clear_graph no escopo esvazia apenas o subgrafo interno.
    auto clearInner = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "clear_graph",
                                               json{{"parent_path", json::array({subId, innerId})}});
    CHECK(!clearInner.result.isError);
    auto afterClear = doc.node(subId);
    CHECK(afterClear->at("childGraph").at("nodes").size() >= 1);   // o subgrafo pai continua
    for (const auto& n : afterClear->at("childGraph").at("nodes")) {
        if (n.value("id", std::string()) == innerId) CHECK(n.at("childGraph").at("nodes").size() == 0);
    }
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
    auto types = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "list_node_types", json::object());
    CHECK(types.result.content.find("\"call\"") != std::string::npos);

    // run_pipeline valida antes: grafo com ciclo sem decisao nao executa.
    auto a = doc.addNode("static", "A", std::nullopt, std::nullopt, "", RuntimeLang::Pt).result.value("id", std::string());
    auto b = doc.addNode("static", "B", std::nullopt, std::nullopt, "", RuntimeLang::Pt).result.value("id", std::string());
    doc.connect(a, "", b, "", RuntimeLang::Pt);
    doc.connect(b, "", a, "", RuntimeLang::Pt);
    auto invalidRun = as::executeAssistantTool(doc, host, RuntimeLang::Pt, "run_pipeline", json::object());
    CHECK(invalidRun.result.isError);
    CHECK(invalidRun.uiOps.empty());

    // Grafo valido: a execucao e delegada a interface, que aplica o GraphValidator/ledger e o PipelineRunner.
    as::GraphDocument runDoc;
    runDoc.addNode("static", "Unico", std::nullopt, std::nullopt, "", RuntimeLang::Pt);
    auto run = as::executeAssistantTool(runDoc, host, RuntimeLang::Pt, "run_pipeline", json::object());
    CHECK(!run.result.isError);
    CHECK(run.uiOps.size() == 1 && run.uiOps[0].value("op", std::string()) == "run_pipeline");

    // Ferramenta connect propaga kind=call e call_settings.
    as::GraphDocument callDoc;
    auto ta = as::executeAssistantTool(callDoc, host, RuntimeLang::Pt, "add_node", json{{"type", "agent"}, {"label", "A"}});
    auto ti = as::executeAssistantTool(callDoc, host, RuntimeLang::Pt, "add_node", json{{"type", "interaction"}, {"label", "I"}});
    CHECK(!ta.result.isError && !ti.result.isError);
    auto nodes = callDoc.summary().at("nodes");
    auto tc = as::executeAssistantTool(callDoc, host, RuntimeLang::Pt, "connect",
                                       json{{"from", nodes[0].value("id", std::string())},
                                            {"kind", "call"},
                                            {"to", nodes[1].value("id", std::string())},
                                            {"call_settings", json{{"maxCalls", 2}}}});
    CHECK(!tc.result.isError);
    CHECK(callDoc.schema().at("edges")[0].value("kind", std::string()) == "call");
    CHECK(callDoc.schema().at("edges")[0].at("callSettings").value("maxCalls", 0) == 2);
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
    ProjectGitConfig defaultGit;
    CHECK(defaultGit.enabled == false); // Opcional por padrao

    ProjectManifest m;
    m.projectId = "test-git-proj";
    m.name = "Projeto Git Teste";
    m.outputDir = "Saidas";
    m.git.enabled = true;
    m.git.provider = "github";
    m.git.serverUrl = "https://github.com";
    m.git.username = "frankalcantara";
    m.git.repositoryName = "projeto-git-teste";
    m.git.remoteUrl = "https://github.com/frankalcantara/projeto-git-teste.git";
    m.git.isPrivate = true;
    m.git.autoCommitOnRun = true;
    m.git.autoCommitOnShutdown = true;
    m.git.timedPushEnabled = true;
    m.git.timedPushIntervalMinutes = 10;

    taskforge::Json j = m;
    CHECK(j["git"]["enabled"] == true);
    CHECK(j["outputDir"] == "Saidas");
    CHECK(!j["git"].contains("backupMode"));
    CHECK(j["git"]["isPrivate"] == true);
    CHECK(j["git"]["provider"] == "github");
    CHECK(j["git"]["repositoryName"] == "projeto-git-teste");

    // Desserializacao
    auto m2 = j.get<ProjectManifest>();
    CHECK(m2.git.enabled == true);
    CHECK(m2.outputDir == "Saidas");
    CHECK(m2.git.isPrivate == true);
    CHECK(m2.git.username == "frankalcantara");
    CHECK(m2.git.repositoryName == "projeto-git-teste");
    CHECK(m2.git.remoteUrl == "https://github.com/frankalcantara/projeto-git-teste.git");
    CHECK(m2.git.timedPushEnabled == true);
    CHECK(m2.git.timedPushIntervalMinutes == 10);

    // 3. Verificacao de presenca do Git na maquina
    std::string ver;
    bool installed = GitSyncService::checkGitInstalled(ver);
    CHECK(installed && !ver.empty());

    // 4. Identidade do SO (opcional): o servico aplica fallback local no repositorio
    std::string uName, uEmail;
    bool hasId = GitSyncService::getSystemIdentity(uName, uEmail);
    if (hasId) {
        CHECK(!uName.empty());
        CHECK(!uEmail.empty());
    }

    // 5. Teste de commitLocal em repositorio temporario
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / ("arx_test_git_commit_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(tempDir);
    std::string initErr;
    bool initOk = GitSyncService::initProjectRepo(tempDir, "", initErr);
    CHECK(initOk);

    std::ofstream f(tempDir / "README.md");
    f << "# Test Repo\n";
    f.close();

    std::string commitOut, commitErr;
    bool cOk = GitSyncService::commitLocal(tempDir, "Initial local commit", commitOut, commitErr);
    CHECK(cOk);

    auto st = GitSyncService::getStatus(tempDir);
    CHECK(st.isRepo == true);
    CHECK(st.hasUncommittedChanges == false);

    // 6. Teste de bloqueio de segredos (Secret Scanning)
    std::ofstream secFile(tempDir / "secrets.env");
    std::string dummySecret = std::string("s") + "k-proj-abcde1234567890abcdef1234567890abcdef";
    secFile << "OPENAI_KEY=" << dummySecret << "\n";
    secFile.close();
    bool secCommitOk = GitSyncService::commitLocal(tempDir, "Commit with secret", commitOut, commitErr);
    CHECK(!secCommitOk);
    CHECK(commitErr.find("Secret scan guard") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
}

TEST(ensure_safe_gitignore_excludes_databases_and_dumps) {
    using namespace taskforge::project;
    namespace fs = std::filesystem;

    fs::path tempRoot = fs::temp_directory_path() /
        ("arx_test_safe_gitignore_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(tempRoot / "storage");

    GitSyncService::ensureSafeGitignore(tempRoot);
    fs::path gitignorePath = tempRoot / ".gitignore";
    CHECK(fs::exists(gitignorePath));

    std::ifstream ig(gitignorePath);
    std::string igContent((std::istreambuf_iterator<char>(ig)), std::istreambuf_iterator<char>());
    ig.close();

    // Bancos binarios, dumps SQL e snapshots nunca entram no Git.
    CHECK(igContent.find("storage/*.sqlite") != std::string::npos);
    CHECK(igContent.find("storage/*.db") != std::string::npos);
    CHECK(igContent.find("storage/*.sql") != std::string::npos);
    CHECK(igContent.find("*.sqlite") != std::string::npos);
    CHECK(igContent.find("*.sql") != std::string::npos);
    CHECK(igContent.find("*.dump.json") != std::string::npos);
    CHECK(igContent.find("*.db.corrupt-*") != std::string::npos);
    CHECK(igContent.find(".arestrix/") != std::string::npos);
    CHECK(igContent.find("output/") != std::string::npos);

    std::error_code ec;
    fs::remove_all(tempRoot, ec);
}

TEST(memory_database_guard_quarantines_corrupt_database) {
    using taskforge::project::MemoryDatabaseGuard;
    namespace fs = std::filesystem;

    fs::path tempRoot = fs::temp_directory_path() /
        ("arx_test_mem_guard_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::path memoryDir = tempRoot / ".arestrix" / "memory";
    fs::create_directories(memoryDir);
    const fs::path dbPath = memoryDir / "memory.db";
    {
        std::ofstream corrupt(dbPath, std::ios::binary | std::ios::trunc);
        corrupt << "isto nao e um banco sqlite";
    }

    std::string error;
    CHECK(MemoryDatabaseGuard::quarantineIfCorrupt(tempRoot, error));
    CHECK(error.empty());
    CHECK(!fs::exists(dbPath));

    bool quarantined = false;
    for (const auto& entry : fs::directory_iterator(memoryDir)) {
        if (entry.path().filename().string().find(".corrupt-") != std::string::npos) {
            quarantined = true;
            break;
        }
    }
    CHECK(quarantined);

    // Raiz sem banco: nao cria nada e nao falha.
    CHECK(MemoryDatabaseGuard::quarantineIfCorrupt(tempRoot, error));

    std::error_code ec;
    fs::remove_all(tempRoot, ec);
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

TEST(github_source_validation) {
    using namespace taskforge::libraries;

    CHECK(isValidRepo("organizacao/projeto"));
    CHECK(!isValidRepo("organizacao"));
    CHECK(!isValidRepo("../projeto"));
    CHECK(!isValidRepo("organizacao/projeto/extra"));

    CHECK(isValidRef("main"));
    CHECK(isValidRef("feature/nova-biblioteca"));
    CHECK(!isValidRef("../main"));
    CHECK(!isValidRef("/main"));

    CHECK(isValidAssetPath("skills/exemplo/SKILL.md"));
    CHECK(isValidAssetPath("mcps/servidor.json"));
    CHECK(!isValidAssetPath("../SKILL.md"));
    CHECK(!isValidAssetPath("/etc/passwd"));
    CHECK(!isValidAssetPath("skills\\exemplo\\SKILL.md"));

    CHECK(isValidCommit("abcdef1"));
    CHECK(isValidCommit(std::string(40, 'a')));
    CHECK(!isValidCommit("XYZ"));
    CHECK(!isValidCommit("abc"));

    CHECK(classifyAssetKind("skills/exemplo/SKILL.md") == "skill");
    CHECK(classifyAssetKind("mcps/servidor.json") == "mcp");
    CHECK(classifyAssetKind("README.md").empty());
    CHECK(classifyAssetKind("skills/exemplo/skill.json").empty());

    CHECK(deriveAssetName("skills/exemplo/SKILL.md", "organizacao/projeto") == "exemplo");
    CHECK(deriveAssetName("mcps/servidor.json", "organizacao/projeto") == "servidor");
}

TEST(github_source_rejects_invalid_requests) {
    using namespace taskforge::libraries;
    using taskforge::Json;

    const Json invalidRepo = taskforge::jsonutil::parse(
        fetchLibrarySourceJson(R"({"mode":"manifest","repo":"../projeto"})"));
    CHECK(!invalidRepo.value("ok", true));
    CHECK(invalidRepo.value("errorCode", "") == "github_invalid_request");

    const Json invalidPath = taskforge::jsonutil::parse(
        fetchLibrarySourceJson(R"({"mode":"asset","repo":"organizacao/projeto","path":"../SKILL.md"})"));
    CHECK(!invalidPath.value("ok", true));
    CHECK(invalidPath.value("errorCode", "") == "github_invalid_request");

    const Json invalidMode = taskforge::jsonutil::parse(
        fetchLibrarySourceJson(R"({"mode":"unknown","repo":"organizacao/projeto"})"));
    CHECK(!invalidMode.value("ok", true));
    CHECK(invalidMode.value("errorCode", "") == "github_invalid_request");
}

TEST(github_source_live_manifest) {
    using taskforge::Json;
    if (!std::getenv("ARESTRIX_GITHUB_LIVE")) {
        std::cout << "  [skip] ARESTRIX_GITHUB_LIVE nao definido\n";
        return;
    }
    const std::string payload = taskforge::libraries::fetchLibrarySourceJson(
        R"({"mode":"manifest","repo":"octocat/Hello-World","ref":"master"})");
    const Json result = taskforge::jsonutil::parse(payload);
    CHECK(result.value("ok", false));
    CHECK(result.value("commit", "").size() >= 7);
}

TEST(github_source_mock_manifest_and_asset) {
    using taskforge::Json;
    if (!std::getenv("ARESTRIX_TEST_MODE")
        || !std::getenv("ARESTRIX_TEST_GITHUB_API_BASE")
        || !std::getenv("ARESTRIX_TEST_GITHUB_RAW_BASE")) {
        std::cout << "  [skip] ARESTRIX_TEST_MODE/ARESTRIX_TEST_GITHUB_* nao definidos\n";
        return;
    }

    const Json manifest = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(
            R"({"mode":"manifest","repo":"e2e/library","ref":"main"})"));
    CHECK(manifest.value("ok", false));
    CHECK(manifest.value("license", "") == "MIT");
    const std::string commit = manifest.value("commit", "");
    CHECK(commit.size() >= 7);

    int assets = 0;
    bool hasSafe = false;
    bool hasHostile = false;
    bool hasMcp = false;
    for (const auto& asset : manifest["assets"]) {
        ++assets;
        const std::string path = asset.value("path", "");
        if (path == "skills/safe/SKILL.md") hasSafe = true;
        if (path == "skills/hostile/SKILL.md") hasHostile = true;
        if (path == "mcps/db.json") hasMcp = true;
    }
    CHECK(assets == 4);
    CHECK(hasSafe);
    CHECK(hasHostile);
    CHECK(hasMcp);

    const std::string safeRequest =
        R"({"mode":"asset","repo":"e2e/library","ref":"main","path":"skills/safe/SKILL.md","commit":")"
        + commit + R"("})";
    const Json safe = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(safeRequest));
    CHECK(safe.value("ok", false));
    CHECK(safe.value("content", "").find("Skill Segura E2E") != std::string::npos);

    const Json missing = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(
            R"({"mode":"manifest","repo":"missing/repo","ref":"main"})"));
    CHECK(!missing.value("ok", true));
    CHECK(missing.value("errorCode", "") == "github_not_found");

    const Json rate = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(
            R"({"mode":"manifest","repo":"rate/limit","ref":"main"})"));
    CHECK(!rate.value("ok", true));
    CHECK(rate.value("errorCode", "") == "github_rate_limited");

    const Json blocked = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(
            R"({"mode":"manifest","repo":"blocked/license","ref":"main"})"));
    CHECK(blocked.value("ok", false));
    CHECK(blocked.value("license", "") == "GPL-3.0");

    const std::string largeRequest =
        R"({"mode":"asset","repo":"e2e/library","ref":"main","path":"skills/large/SKILL.md","commit":")"
        + commit + R"("})";
    const Json large = taskforge::jsonutil::parse(
        taskforge::libraries::fetchLibrarySourceJson(largeRequest));
    CHECK(!large.value("ok", true));
    CHECK(large.value("errorCode", "") == "github_too_large");
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

// ---------------------------------------------------------------------------
// Compactacao de contexto (Etapa 1)
// ---------------------------------------------------------------------------
namespace cc = taskforge::rt::compress;

static std::string bigJson(int items) {
    taskforge::Json root = taskforge::Json::object();
    taskforge::Json results = taskforge::Json::array();
    for (int i = 0; i < items; ++i) {
        results.push_back(taskforge::Json{{"id", i},
                                          {"path", "docs/file-" + std::to_string(i) + ".md"},
                                          {"score", i * 1.5},
                                          {"tags", taskforge::Json::array({"alpha", "beta"})}});
    }
    root["results"] = results;
    root["total"] = items;
    return root.dump();
}

static std::string repeatedLog(int repeats) {
    std::string text;
    for (int i = 0; i < repeats; ++i) {
        text += "2026-10-29T10:00:" + std::to_string(i % 60) + "Z INFO GET /health 200 3ms\n";
        text += "2026-10-29T10:00:" + std::to_string(i % 60) + "Z INFO GET /ready 200 2ms\n";
    }
    text += "2026-10-29T10:01:00Z FATAL disk full no volume C:\\dados\n";
    text += "2026-10-29T10:01:01Z ERROR request 9f3a2b1c failed\n";
    return text;
}

TEST(context_router_classifies_content) {
    CHECK(cc::classifyContent(bigJson(40)).kind == cc::ContentKind::Json);
    CHECK(cc::classifyContent(repeatedLog(20)).kind == cc::ContentKind::Log);
    CHECK(cc::classifyContent(
              "#include <string>\n#include <vector>\n\n// Utilidades de texto do modulo de relatorios, com "
              "comentarios longos para passar do limiar minimo do classificador.\nnamespace x {\nclass A {\npublic:\n"
              "    int f() { return 1; }\n    int g() { return 2; }\n    int h() { return 3; }\n};\n}\n// fim do arquivo\n")
              .kind == cc::ContentKind::Code);
    CHECK(cc::classifyContent(
              "Este e um texto corrido sobre arquitetura de software, sem marcadores de codigo, sem "
              "timestamps e sem estrutura JSON. Ele existe apenas para representar prosa longa o "
              "suficiente para o classificador nao cair no caso pequeno. As frases seguem em periodos "
              "comuns, com virgulas e pontos finais, e nenhuma linha termina em ponto e virgula ou chave. "
              "O objetivo e exercitar a decisao pelo caminho padrao.")
              .kind == cc::ContentKind::Prose);
    CHECK(cc::classifyContent("curto").kind == cc::ContentKind::Skip);
}

TEST(smart_crusher_samples_long_arrays_and_keeps_keys) {
    cc::JsonOptions options;
    options.maxArrayItems = 5;

    const std::string input = bigJson(100);
    cc::CompressResult result = cc::crushJson(input, options);
    CHECK(result.changed);
    CHECK(result.bytesAfter < result.bytesBefore);
    CHECK(result.text.find("\"id\"") != std::string::npos);
    CHECK(result.text.find("itens omitidos") != std::string::npos);

    auto parsed = taskforge::jsonutil::tryParse(result.text);
    CHECK(parsed.has_value());
    CHECK(parsed->at("results").size() == 6);          // 5 itens + marcador
    CHECK(parsed->at("results").at(static_cast<size_t>(0)).at("id").get<int>() == 0);
    CHECK(parsed->at("total").get<int>() == 100);

    // JSON pequeno ou invalido nao muda.
    CHECK(!cc::crushJson("{\"a\":1}", options).changed);
    CHECK(!cc::crushJson("{oops", options).changed);
}

TEST(log_compressor_collapses_repeats_and_keeps_critical) {
    cc::LogOptions options;
    const std::string input = repeatedLog(40);
    cc::CompressResult result = cc::compressLog(input, options);
    CHECK(result.changed);
    CHECK(result.bytesAfter < result.bytesBefore);
    CHECK(result.text.find("FATAL") != std::string::npos);
    CHECK(result.text.find("ERROR") != std::string::npos);
    CHECK(result.text.find("omitida") != std::string::npos);
    // Nao pode reordenar: o FATAL continua depois dos blocos de repeticao.
    CHECK(result.text.find("FATAL") > result.text.find("INFO"));
}

TEST(ccr_store_roundtrip_and_rejects_invalid_tokens) {
    fs::path root = makeTempDir("ccr");
    cc::CcrStore store(root, "proj", "run-1");
    const std::string original = bigJson(3);
    const std::string token = store.store(original);
    CHECK(token.size() == 16);
    auto back = store.retrieve(token);
    CHECK(back.has_value());
    CHECK(*back == original);

    CHECK(!store.retrieve("../etc/passwd").has_value());
    CHECK(!store.retrieve("zzzz").has_value());
    CHECK(!store.retrieve("0000000000000000").has_value());
    fs::remove_all(root);
}

TEST(ccr_store_prunes_across_runs_by_size_and_age) {
    fs::path root = makeTempDir("ccr-prune");

    cc::CcrLimits limits;
    limits.maxAge = std::chrono::hours(24 * 7);
    limits.maxBytes = 500;

    std::string oldToken;
    {
        cc::CcrStore run1(root, "proj", "run-1", limits);
        oldToken = run1.store(std::string(300, 'a'));
        CHECK(oldToken.size() == 16);
        // Envelhece o arquivo para a ordem da poda ser deterministica.
        std::error_code ec;
        fs::last_write_time(run1.directory() / (oldToken + ".txt"),
                            fs::file_time_type::clock::now() - std::chrono::hours(1), ec);
        CHECK(!ec);
    }

    cc::CcrStore run2(root, "proj", "run-2", limits);
    const std::string newToken = run2.store(std::string(300, 'b'));
    CHECK(newToken.size() == 16);

    // Projeto passou de 500 bytes: a execucao mais antiga foi removida.
    cc::CcrStore check(root, "proj", "run-2", limits);
    CHECK(!check.retrieve(oldToken).has_value());
    CHECK(check.retrieve(newToken).has_value());

    // Idade: um arquivo vencido some no proximo store, mesmo de outra execucao.
    cc::CcrLimits shortLived;
    shortLived.maxAge = std::chrono::hours(1);
    shortLived.maxBytes = 1ull * 1024 * 1024;
    cc::CcrStore aged(root, "proj2", "run-1", shortLived);
    const std::string agedToken = aged.store(std::string(50, 'c'));
    std::error_code ec;
    fs::last_write_time(aged.directory() / (agedToken + ".txt"),
                        fs::file_time_type::clock::now() - std::chrono::hours(2), ec);
    CHECK(!ec);
    cc::CcrStore aged2(root, "proj2", "run-2", shortLived);
    (void)aged2.store(std::string(50, 'd'));
    CHECK(!aged2.retrieve(agedToken).has_value());

    fs::remove_all(root);
}

static std::string sampleCppFile() {
    return "#include <string>\n"
           "#include <vector>\n"
           "\n"
           "// Classe de exemplo usada nos testes de compactacao de codigo.\n"
           "namespace demo {\n"
           "class Accumulator {\n"
           "public:\n"
           "    int value() const;\n"
           "    void big(int limit) {\n"
           "        for (int i = 0; i < limit; ++i) {\n"
           "            value_ += i;\n"
           "            if (value_ > 3) {\n"
           "                value_ -= 1;\n"
           "                total_ += value_;\n"
           "            }\n"
           "            value_ *= 2;\n"
           "        }\n"
           "    }\n"
           "private:\n"
           "    int value_ = 0;\n"
           "    int total_ = 0;\n"
           "};\n"
           "}\n";
}

TEST(code_compressor_keeps_signatures_and_collapses_bodies) {
    const std::string input = sampleCppFile();
    cc::CompressResult result = cc::compressCode(input, cc::CodeOptions{});

    CHECK(result.changed);
    CHECK(result.bytesAfter < result.bytesBefore);
    CHECK(result.text.find("#include <string>") != std::string::npos);
    CHECK(result.text.find("class Accumulator") != std::string::npos);
    CHECK(result.text.find("void big(int limit) {") != std::string::npos);
    CHECK(result.text.find("int value_ = 0;") != std::string::npos);
    CHECK(result.text.find("linha(s) omitida(s)") != std::string::npos);
    // Corpo do metodo nao entra na visao comprimida.
    CHECK(result.text.find("value_ += i;") == std::string::npos);
    CHECK(result.text.find("total_ += value_;") == std::string::npos);
}

TEST(code_compressor_handles_python_indentation) {
    std::string input;
    for (int i = 0; i < 12; ++i) {
        input += "def funcao_" + std::to_string(i) + "(valor):\n";
        input += "    total = valor + " + std::to_string(i) + "\n";
        input += "    if total > 10:\n";
        input += "        total = total - 1\n";
        input += "    return total\n";
        input += "\n";
    }
    cc::CompressResult result = cc::compressCode(input, cc::CodeOptions{});
    CHECK(result.changed);
    CHECK(result.text.find("def funcao_0(valor):") != std::string::npos);
    CHECK(result.text.find("return total") == std::string::npos);
    CHECK(result.text.find("linha(s) omitida(s)") != std::string::npos);
}

static std::string extractToken(const std::string& text) {
    const size_t at = text.find("[CCR:");
    if (at == std::string::npos) return {};
    const size_t end = text.find(']', at);
    if (end == std::string::npos) return {};
    return text.substr(at + 5, end - at - 5);
}

TEST(context_compressor_compresses_only_tool_messages_with_ccr) {
    fs::path root = makeTempDir("ctxcompress");
    cc::CompressionSettings settings;
    settings.enabled = true;
    settings.minBlockTokens = 64;
    settings.minRunTokens = 128;
    settings.maxArrayItems = 5;

    cc::CcrStore store(root, "proj", "run-1");
    cc::ContextCompressor compressor(settings, &store, taskforge::rt::RuntimeLang::Pt);

    taskforge::rt::ChatMessage toolBig;
    toolBig.role = "tool";
    toolBig.text = bigJson(100);
    toolBig.toolCallId = "call-1";

    taskforge::rt::ChatMessage toolSmall;
    toolSmall.role = "tool";
    toolSmall.text = "{\"ok\":true}";
    toolSmall.toolCallId = "call-2";

    taskforge::rt::ChatMessage finalMessage;
    finalMessage.role = "assistant";
    finalMessage.text = "concluido";

    std::vector<taskforge::rt::ChatMessage> messages{toolBig, toolSmall, finalMessage};
    const std::string original = toolBig.text;
    const cc::CompressionStats stats = compressor.apply(messages);

    CHECK(stats.blocks == 1);
    CHECK(stats.tokensAfter < stats.tokensBefore);
    CHECK(stats.kinds == "json");
    CHECK(messages[0].text.find("[CCR:") != std::string::npos);
    CHECK(messages[1].text == "{\"ok\":true}");
    CHECK(messages[2].text == "concluido");

    const std::string token = extractToken(messages[0].text);
    CHECK(token.size() == 16);
    auto back = store.retrieve(token);
    CHECK(back.has_value());
    CHECK(*back == original);

    // Reaplicar nao recomprime o que ja esta marcado.
    const cc::CompressionStats again = compressor.apply(messages);
    CHECK(again.blocks == 0);

    fs::remove_all(root);
}

static taskforge::Json tinyWordPiece() {
    taskforge::Json vocab = taskforge::Json::object();
    vocab["[PAD]"] = 0;
    vocab["[UNK]"] = 1;
    vocab["[CLS]"] = 2;
    vocab["[SEP]"] = 3;
    vocab["the"] = 4;
    vocab["quick"] = 5;
    vocab["brown"] = 6;
    vocab["fox"] = 7;
    vocab["jumps"] = 8;
    vocab["over"] = 9;
    vocab["lazy"] = 10;
    vocab["dog"] = 11;
    vocab["."] = 12;
    vocab["run"] = 13;
    vocab["##s"] = 14;
    vocab["cafe"] = 15;
    vocab["http"] = 16;
    vocab["example"] = 17;
    vocab["report"] = 18;

    taskforge::Json model = taskforge::Json::object();
    model["type"] = "WordPiece";
    model["unk_token"] = "[UNK]";
    model["continuing_subword_prefix"] = "##";
    model["max_input_chars_per_word"] = 100;
    model["vocab"] = vocab;

    taskforge::Json normalizer = taskforge::Json::object();
    normalizer["type"] = "BertNormalizer";
    normalizer["clean_text"] = true;
    normalizer["lowercase"] = true;
    normalizer["strip_accents"] = true;

    taskforge::Json preTokenizer = taskforge::Json::object();
    preTokenizer["type"] = "BertPreTokenizer";

    taskforge::Json root = taskforge::Json::object();
    root["model"] = model;
    root["normalizer"] = normalizer;
    root["pre_tokenizer"] = preTokenizer;
    return root;
}

static taskforge::Json tinyByteLevelBpe() {
    const std::string g = "\xC4\xA0";   // Ġ (ByteLevel do espaco)
    taskforge::Json vocab = taskforge::Json::object();
    vocab["[UNK]"] = 0;
    vocab["[CLS]"] = 1;
    vocab["[SEP]"] = 2;
    vocab["t"] = 3;
    vocab["h"] = 4;
    vocab["e"] = 5;
    vocab["th"] = 6;
    vocab["the"] = 7;
    vocab[g] = 8;
    vocab[g + "the"] = 10;
    vocab["."] = 11;

    taskforge::Json merges = taskforge::Json::array();
    merges.push_back(taskforge::Json::array({"t", "h"}));
    merges.push_back(taskforge::Json::array({"th", "e"}));
    merges.push_back(taskforge::Json::array({g, "the"}));

    taskforge::Json model = taskforge::Json::object();
    model["type"] = "BPE";
    model["vocab"] = vocab;
    model["merges"] = merges;

    taskforge::Json preTokenizer = taskforge::Json::object();
    preTokenizer["type"] = "ByteLevel";
    preTokenizer["add_prefix_space"] = false;
    preTokenizer["use_regex"] = true;

    taskforge::Json added = taskforge::Json::array();
    added.push_back(taskforge::Json{{"id", 1}, {"content", "[CLS]"}, {"special", true}});
    added.push_back(taskforge::Json{{"id", 2}, {"content", "[SEP]"}, {"special", true}});
    added.push_back(taskforge::Json{{"id", 0}, {"content", "[UNK]"}, {"special", true}});
    added.push_back(taskforge::Json{{"id", 14}, {"content", "[IP]"}, {"special", false}});

    taskforge::Json root = taskforge::Json::object();
    root["model"] = model;
    root["pre_tokenizer"] = preTokenizer;
    root["added_tokens"] = added;
    return root;
}

TEST(prose_tokenizer_bpe_bytelevel_offsets) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyByteLevelBpe());
    CHECK(tokenizer.has_value());
    CHECK(tokenizer->isByteLevelBpe());
    CHECK(tokenizer->clsId() == 1);
    CHECK(tokenizer->sepId() == 2);
    CHECK(tokenizer->unkId() == 0);

    const auto simple = tokenizer->tokenize("the", 0);
    CHECK(simple.size() == 1);
    CHECK(simple[0].id == 7);

    const std::string spacedText = " the.";
    const auto spaced = tokenizer->tokenize(spacedText, 0);
    CHECK(spaced.size() == 2);
    CHECK(spaced[0].id == 10);   // " the"
    CHECK(spaced[1].id == 11);   // "."
    CHECK(spacedText.substr(spaced[0].start, spaced[0].end - spaced[0].start) == " the");
    CHECK(spacedText.substr(spaced[1].start, spaced[1].end - spaced[1].start) == ".");

    const auto withAdded = tokenizer->tokenize("the[IP]", 0);
    CHECK(withAdded.size() == 2);
    CHECK(withAdded[0].id == 7);
    CHECK(withAdded[1].id == 14);
}

TEST(prose_tokenizer_wordpiece_offsets) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyWordPiece());
    CHECK(tokenizer.has_value());

    const std::string text = "The quick brown fox jumps over the lazy dog.";
    const auto tokens = tokenizer->tokenize(text, 0);
    CHECK(tokens.size() == 10);
    CHECK(tokens.front().id == 4);   // lowercased "the"
    CHECK(text.substr(tokens.front().start, tokens.front().end - tokens.front().start) == "The");
    CHECK(tokens.back().id == 12);   // "."
    CHECK(text.substr(tokens.back().start, tokens.back().end - tokens.back().start) == ".");

    const auto runs = tokenizer->tokenize("runs", 0);
    CHECK(runs.size() == 2);
    CHECK(runs[0].id == 13);
    CHECK(runs[1].id == 14);

    const std::string cafeText = "café";
    const auto cafe = tokenizer->tokenize(cafeText, 0);
    CHECK(cafe.size() == 1);
    CHECK(cafe[0].id == 15);
    CHECK(cafeText.substr(cafe[0].start, cafe[0].end - cafe[0].start) == "café");
}

TEST(prose_tokenizer_rejects_other_models) {
    taskforge::Json other = taskforge::Json::object();
    taskforge::Json model = taskforge::Json::object();
    model["type"] = "BPE";
    other["model"] = model;
    CHECK(!cc::ProseTokenizer::fromJson(other).has_value());
}

class EveryFifthProseModel : public cc::ProseModel {
public:
    bool available() const override { return true; }
    std::vector<float> keepProbabilities(const std::vector<int>& ids) override {
        std::vector<float> out(ids.size(), 0.1f);
        for (size_t i = 0; i < out.size(); i += 5) out[i] = 0.9f;
        return out;
    }
};

class WindowRecordingProseModel : public cc::ProseModel {
public:
    bool available() const override { return true; }
    std::vector<float> keepProbabilities(const std::vector<int>& ids) override {
        windows.push_back(ids.size());
        return std::vector<float>(ids.size(), 0.1f);
    }
    std::vector<size_t> windows;
};

class UnavailableProseModel : public cc::ProseModel {
public:
    bool available() const override { return false; }
    std::vector<float> keepProbabilities(const std::vector<int>&) override { return {}; }
};

TEST(prose_compressor_keeps_urls_and_punctuation) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyWordPiece());
    CHECK(tokenizer.has_value());

    std::string text;
    for (int i = 0; i < 6; ++i) text += "the quick brown fox jumps over the lazy dog. ";
    text += "See https://example.com/report.pdf and the quick brown fox.";

    EveryFifthProseModel model;
    const cc::CompressResult result = cc::compressProse(text, *tokenizer, model, cc::ProseOptions{});
    CHECK(result.changed);
    CHECK(result.bytesAfter < result.bytesBefore);
    CHECK(result.text.find("https://example.com/report.pdf") != std::string::npos);
    CHECK(result.text.find('.') != std::string::npos);
}

TEST(prose_compressor_is_noop_without_model) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyWordPiece());
    CHECK(tokenizer.has_value());
    const std::string text = "the quick brown fox jumps over the lazy dog. the quick brown fox jumps over the lazy dog.";
    UnavailableProseModel model;
    const cc::CompressResult result = cc::compressProse(text, *tokenizer, model, cc::ProseOptions{});
    CHECK(!result.changed);
    CHECK(result.text == text);
}

TEST(prose_compressor_chunks_long_text_into_windows) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyWordPiece());
    CHECK(tokenizer.has_value());
    std::string text;
    for (int i = 0; i < 30; ++i) text += "the quick brown fox jumps over the lazy dog. ";
    const size_t total = tokenizer->tokenize(text, 0).size();
    CHECK(total > 64);

    WindowRecordingProseModel model;
    cc::ProseOptions options;
    options.maxTokens = 100000;
    options.windowTokens = 32;
    const cc::CompressResult result = cc::compressProse(text, *tokenizer, model, options);
    CHECK(model.windows.size() >= 2);
    size_t covered = 0;
    for (size_t size : model.windows) {
        CHECK(size <= 32);
        covered += size;
    }
    CHECK(covered == total);
    CHECK(result.changed);
}

TEST(prose_compressor_skips_blocks_above_max_tokens_without_truncating) {
    auto tokenizer = cc::ProseTokenizer::fromJson(tinyWordPiece());
    CHECK(tokenizer.has_value());
    std::string text;
    for (int i = 0; i < 30; ++i) text += "the quick brown fox jumps over the lazy dog. ";
    const size_t total = tokenizer->tokenize(text, 0).size();
    CHECK(total > 32);

    WindowRecordingProseModel model;
    cc::ProseOptions options;
    options.maxTokens = total - 1;
    const cc::CompressResult result = cc::compressProse(text, *tokenizer, model, options);
    CHECK(!result.changed);
    CHECK(result.text == text);
    CHECK(model.windows.empty());
}

TEST(prose_model_manifest_pins_urls_and_hashes) {
    const auto manifest = cc::proseModelManifest();
    CHECK(manifest.value("id", std::string()) == "kompress-v2-base");
    CHECK(manifest.value("revision", std::string()).size() == 40);

    const auto& files = manifest["files"];
    CHECK(files.size() == 4);
    bool sawFp32 = false;
    bool sawInt8 = false;
    bool sawTokenizer = false;
    bool sawConfig = false;
    for (const auto& file : files) {
        const std::string name = file.value("name", std::string());
        const std::string url = file.value("url", std::string());
        const std::string hash = file.value("sha256", std::string());
        const long long size = file.value("size", 0LL);
        CHECK(url.rfind("https://huggingface.co/chopratejas/kompress-v2-base/resolve/", 0) == 0);
        CHECK(hash.size() == 64);
        CHECK(size > 0);
        if (name == "kompress-fp32.onnx") {
            sawFp32 = true;
            CHECK(size == 600561247);
            CHECK(file.value("variant", std::string()) == "fp32");
        } else if (name == "kompress-int8-wo.onnx") {
            sawInt8 = true;
            CHECK(size == 274049435);
            CHECK(file.value("variant", std::string()) == "int8");
        } else if (name == "tokenizer.json") {
            sawTokenizer = true;
            CHECK(size == 3583228);
        } else if (name == "config.json") {
            sawConfig = true;
            CHECK(size == 432);
        }
    }
    CHECK(sawFp32);
    CHECK(sawInt8);
    CHECK(sawTokenizer);
    CHECK(sawConfig);

    CHECK(cc::proseModelDownloadStatus("pd-missing").value("ok", true) == false);
}

TEST(prose_model_installer_rejects_unknown_variant) {
    std::string error;
    const std::string job = cc::startProseModelDownload("../etc", error);
    CHECK(job.empty());
    CHECK(error == "invalid_variant");
    CHECK(cc::cancelProseModelDownload("pd-missing") == false);
}

TEST(prose_recommendation_picks_variant_by_machine_capacity) {
    cc::MachineProfile strong{8, 32 * 1024, 16 * 1024, 200 * 1024};
    CHECK(cc::recommendProseVariant(strong).variant == "fp32");

    cc::MachineProfile tightRam{4, 8 * 1024, 1024, 50 * 1024};
    CHECK(cc::recommendProseVariant(tightRam).variant == "int8");

    // fp32 nao cabe no disco, int8 cabe.
    cc::MachineProfile tightDisk{8, 16 * 1024, 4 * 1024, 800};
    CHECK(cc::recommendProseVariant(tightDisk).variant == "int8");

    cc::MachineProfile tiny{4, 4 * 1024, 1024, 400};
    auto none = cc::recommendProseVariant(tiny);
    CHECK(none.variant == "none");
    CHECK(none.reason == "disk");

    cc::MachineProfile singleCore{1, 32 * 1024, 16 * 1024, 200 * 1024};
    auto cpu = cc::recommendProseVariant(singleCore);
    CHECK(cpu.variant == "none");
    CHECK(cpu.reason == "cpu");

    // GPU dedicada + DirectML: fp32 na GPU vence o CPU.
    cc::MachineProfile gpu = strong;
    gpu.dmlAvailable = true;
    gpu.gpus = {{0, "Intel UHD", 1024, "intel"}, {1, "GeForce GTX 1650", 4096, "nvidia"}};
    gpu.dmlDevice = 1;
    CHECK(cc::recommendProseVariant(gpu).variant == "gpu");

    // GPU presente, mas build sem DirectML: continua no CPU.
    gpu.dmlAvailable = false;
    CHECK(cc::recommendProseVariant(gpu).variant == "fp32");

    // So iGPU: nao vale a pena; CPU fp32.
    cc::MachineProfile igpu = strong;
    igpu.dmlAvailable = true;
    igpu.gpus = {{0, "Intel UHD", 1024, "intel"}};
    igpu.dmlDevice = -1;
    CHECK(cc::recommendProseVariant(igpu).variant == "fp32");

    // Sonda real: nunca devolve perfil vazio (ambiente de teste tem CPU e RAM).
    cc::MachineProfile detected = cc::detectMachineProfile(fs::temp_directory_path());
    CHECK(detected.cpuThreads >= 1);
    CHECK(detected.ramTotalMB > 0);
}

TEST(prose_recommendation_picks_discrete_gpu_for_directml) {
    std::vector<cc::GpuInfo> hybrid{{0, "Intel UHD", 1024, "intel"},
                                    {1, "GeForce GTX 1650", 4096, "nvidia"},
                                    {2, "Microsoft Basic Render Driver", 0, "microsoft"}};
    CHECK(cc::bestDmlDevice(hybrid) == 1);

    std::vector<cc::GpuInfo> igpuOnly{{0, "Intel UHD", 1024, "intel"}};
    CHECK(cc::bestDmlDevice(igpuOnly) == -1);

    std::vector<cc::GpuInfo> strongGpu{{0, "GeForce RTX 4090", 24576, "nvidia"},
                                       {1, "Intel UHD", 1024, "intel"}};
    CHECK(cc::bestDmlDevice(strongGpu) == 0);

    std::vector<cc::GpuInfo> software{{0, "Microsoft Basic Render Driver", 0, "microsoft"}};
    CHECK(cc::bestDmlDevice(software) == -1);
}

TEST(context_compressor_skips_code_of_files_being_edited) {
    fs::path root = makeTempDir("ctxcode");
    cc::CompressionSettings settings;
    settings.enabled = true;
    settings.minBlockTokens = 64;
    settings.minRunTokens = 64;
    settings.code = true;

    cc::CcrStore store(root, "proj", "run-1");
    cc::ContextCompressor compressor(settings, &store, taskforge::rt::RuntimeLang::Pt);

    taskforge::rt::ChatMessage readCall;
    readCall.role = "assistant";
    readCall.toolCalls.push_back(taskforge::rt::ChatToolCall{
        "r1", "read", taskforge::Json{{"path", "src/app.cpp"}}});

    taskforge::rt::ChatMessage readResult;
    readResult.role = "tool";
    readResult.toolCallId = "r1";
    readResult.text = sampleCppFile();

    taskforge::rt::ChatMessage editCall;
    editCall.role = "assistant";
    editCall.toolCalls.push_back(taskforge::rt::ChatToolCall{
        "w1", "edit", taskforge::Json{{"path", "src\\app.cpp"}}});   // normalizacao de separador

    taskforge::rt::ChatMessage editResult;
    editResult.role = "tool";
    editResult.toolCallId = "w1";
    editResult.text = "ok";

    taskforge::rt::ChatMessage finalMessage;
    finalMessage.role = "assistant";
    finalMessage.text = "feito";

    std::vector<taskforge::rt::ChatMessage> withEdit{readCall, readResult, editCall, editResult, finalMessage};
    const std::string original = readResult.text;
    CHECK(compressor.apply(withEdit).blocks == 0);
    CHECK(withEdit[1].text == original);

    // Sem a edicao, o mesmo arquivo e comprimido normalmente.
    std::vector<taskforge::rt::ChatMessage> withoutEdit{readCall, readResult, finalMessage};
    const cc::CompressionStats applied = compressor.apply(withoutEdit);
    CHECK(applied.blocks == 1);
    CHECK(withoutEdit[1].text.find("[CCR:") != std::string::npos);
    CHECK(withoutEdit[1].text.find("value_ += i;") == std::string::npos);

    fs::remove_all(root);
}

TEST(context_compressor_is_off_by_default_and_never_touches_last_message) {
    fs::path root = makeTempDir("ctxoff");
    cc::CcrStore store(root, "proj", "run-1");

    taskforge::rt::ChatMessage tool;
    tool.role = "tool";
    tool.text = bigJson(100);

    std::vector<taskforge::rt::ChatMessage> messages{tool};
    // Mensagem unica e a ultima: nunca e tocada.
    cc::CompressionSettings enabled;
    enabled.enabled = true;
    enabled.minBlockTokens = 64;
    enabled.minRunTokens = 128;
    cc::ContextCompressor on(enabled, &store, taskforge::rt::RuntimeLang::Pt);
    CHECK(on.apply(messages).blocks == 0);
    CHECK(messages[0].text.find("[CCR:") == std::string::npos);

    // Desligado: nada muda, mesmo com run grande.
    taskforge::rt::ChatMessage first;
    first.role = "tool";
    first.text = bigJson(100);
    taskforge::rt::ChatMessage last;
    last.role = "assistant";
    last.text = "fim";
    std::vector<taskforge::rt::ChatMessage> pair{first, last};
    cc::ContextCompressor off(cc::CompressionSettings{}, &store, taskforge::rt::RuntimeLang::Pt);
    CHECK(off.apply(pair).blocks == 0);
    CHECK(pair[0].text == first.text);

    fs::remove_all(root);
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--interaction-host-child") {
        return runInteractionHostChild("normal");
    }
    if (argc > 1 && std::string(argv[1]) == "--interaction-host-child-timeout") {
        return runInteractionHostChild("timeout");
    }
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--filter=", 0) == 0) {
            filter = arg.substr(9);
        }
    }
    std::srand((unsigned)std::time(nullptr));
    for (auto& t : tests()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) {
            continue;
        }
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
