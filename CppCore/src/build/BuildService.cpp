#include "build/BuildService.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

#include "compiler/TaskflowGenerator.hpp"
#include "platform/Process.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace build {

namespace fs = std::filesystem;

namespace {

const char* kTelemetryPrefix = "[NF_TELEMETRY:AGENT] ";
const char* kInteractionPrefix = "[ARX_INTERACTION] ";

uint64_t fnv1a(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}

bool writeIfChanged(const fs::path& p, const std::string& content) {
    std::error_code ec;
    if (fs::exists(p, ec)) {
        std::ifstream in(p, std::ios::binary);
        std::string cur((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (cur == content) return false;
    }
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
    return true;
}

#ifdef _WIN32
std::string exeName(const std::string& base) { return base + ".exe"; }
#else
std::string exeName(const std::string& base) { return base; }
#endif

std::string newRunId() {
    static std::atomic<unsigned long long> counter{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "run-" + std::to_string(ms) + "-" + std::to_string(++counter);
}

} // namespace

// ---------------------------------------------------------------------------
Toolchain detectToolchain() {
    Toolchain tc;
#ifdef _WIN32
    const char* roots[] = {"ProgramFiles(x86)", "ProgramFiles"};
    for (const char* envName : roots) {
        std::string base = platform::getEnv(envName);
        if (base.empty()) continue;
        fs::path vswhere = fs::path(base) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        std::error_code ec;
        if (!fs::exists(vswhere, ec)) continue;
        platform::ProcessSpec spec;
        spec.argv = {vswhere.string(), "-latest", "-products", "*", "-requires",
                     "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"};
        auto r = platform::run(spec, 15000);
        std::string vs = r.output;
        while (!vs.empty() && (vs.back() == '\n' || vs.back() == '\r' || vs.back() == ' ')) vs.pop_back();
        if (vs.empty()) continue;
        fs::path vcvars = fs::path(vs) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat";
        if (fs::exists(vcvars, ec)) {
            tc.kind = Toolchain::Kind::Msvc;
            tc.vcvars = vcvars.string();
            tc.description = "MSVC (" + vs + ")";
            return tc;
        }
    }
    if (platform::commandExists("cl")) {
        tc.kind = Toolchain::Kind::Msvc;
        tc.description = "MSVC (cl.exe ja no PATH)";
        return tc;
    }
#else
    if (platform::commandExists("g++")) {
        tc.kind = Toolchain::Kind::Gcc;
        tc.compiler = "g++";
        tc.description = "GCC (g++)";
    } else if (platform::commandExists("clang++")) {
        tc.kind = Toolchain::Kind::Clang;
        tc.compiler = "clang++";
        tc.description = "Clang (clang++)";
    }
#endif
    return tc;
}

RuntimeLayout locateRuntime() {
    RuntimeLayout rt;
    std::vector<fs::path> candidates;
    std::string envHome = platform::getEnv("ARESTRIX_HOME");
    if (!envHome.empty()) candidates.emplace_back(envHome);

    std::vector<fs::path> starts;
    fs::path exe = platform::executablePath();
    if (!exe.empty()) starts.push_back(exe.parent_path());
    std::error_code ec;
    starts.push_back(fs::current_path(ec));
    for (auto s : starts) {
        for (int i = 0; i < 5 && !s.empty(); ++i) {
            candidates.push_back(s);
            s = s.parent_path();
        }
    }

    for (const auto& c : candidates) {
        if (fs::exists(c / "third_party" / "taskflow" / "taskflow" / "taskflow.hpp", ec) &&
            fs::exists(c / "CppCore" / "src" / "rt" / "Runtime.hpp", ec)) {
            rt.home = c;
            break;
        }
    }
    if (rt.home.empty()) {
        rt.error = "Instalacao do Arestrix nao encontrada (defina ARESTRIX_HOME). Execute 'git submodule update --init' "
                   "para obter third_party/taskflow e third_party/simdjson.";
        return rt;
    }

    rt.taskflowInclude = rt.home / "third_party" / "taskflow";
    rt.srcInclude = rt.home / "CppCore" / "src";
    rt.simdjsonInclude = rt.home / "third_party" / "simdjson" / "singleheader";
#ifdef _WIN32
    rt.coreLib = rt.home / "runtime" / "lib" / "arestrix_core.lib";
#else
    rt.coreLib = rt.home / "runtime" / "lib" / "libarestrix_core.a";
#endif
    if (!fs::exists(rt.coreLib, ec)) {
        rt.error = "Biblioteca do runtime nao encontrada: " + rt.coreLib.string() +
                   ". Compile o Arestrix (cmake --build) para gerar arestrix_core.";
        return rt;
    }
    rt.ok = true;
    return rt;
}

// ---------------------------------------------------------------------------
BuildResult buildAndRun(const core::Graph& graph,
                        const fs::path& projectRoot,
                        const BuildEvents& ev,
                        const std::atomic<bool>* cancel) {
    using Clock = std::chrono::steady_clock;
    auto t0 = Clock::now();
    BuildResult result;

    auto log = [&](const std::string& line, bool isErr = false) { if (ev.onLog) ev.onLog(line, isErr); };
    auto phase = [&](const std::string& p, const std::string& m) { if (ev.onPhase) ev.onPhase(p, m); };
    auto finish = [&](bool ok, const std::string& stage, const std::string& error) {
        result.success = ok;
        result.stage = stage;
        result.error = error;
        result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        if (ev.onFinished) ev.onFinished(ok, result.exitCode, result.elapsedMs, error);
        return result;
    };

    std::error_code ec;
    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) {
        return finish(false, "generate", "Diretorio do projeto invalido: " + projectRoot.string());
    }
    if (graph.getNodes().empty()) {
        return finish(false, "generate", "O grafo esta vazio.");
    }

    // 1. Ambiente: compilador + runtime
    Toolchain tc = detectToolchain();
    if (tc.kind == Toolchain::Kind::None) {
#ifdef _WIN32
        return finish(false, "toolchain", "Compilador C++20 nao encontrado. Instale o Visual Studio (ou Build Tools) com a carga 'Desenvolvimento para Desktop com C++'.");
#else
        return finish(false, "toolchain", "Compilador C++20 nao encontrado. Instale g++ (>= 11) ou clang++.");
#endif
    }
    RuntimeLayout rt = locateRuntime();
    if (!rt.ok) return finish(false, "toolchain", rt.error);

    // 2. Gera o codigo
    phase("compiling", "Gerando C++ Taskflow a partir do grafo (" + tc.description + ")...");
    fs::path buildDir = projectRoot / ".arestrix" / "build";
    fs::create_directories(buildDir, ec);
    fs::create_directories(projectRoot / "workspace", ec);

    std::string source = compiler::TaskflowGenerator::generateSource(graph);
    for (const auto& [rel, content] : compiler::TaskflowGenerator::collectSkillAssets(graph)) {
        writeIfChanged(projectRoot / rel, content);
    }
    writeIfChanged(buildDir / "main.cpp", source);

    std::string subflowSource;
    {
        auto subflowSources = compiler::TaskflowGenerator::generateSubflowSources(graph);
        auto it = subflowSources.find("subflows.cpp");
        fs::path subflowPath = buildDir / "subflows.cpp";
        if (it != subflowSources.end()) {
            subflowSource = it->second;
            writeIfChanged(subflowPath, subflowSource);
        } else {
            fs::remove(subflowPath, ec);
        }
    }

    fs::path exePath = buildDir / exeName("pipeline_app");
    fs::path hashFile = buildDir / "main.hash";
    std::string hash = std::to_string(fnv1a(source)) + "|" + std::to_string(fnv1a(subflowSource)) + "|" + rt.coreLib.string() + "|" +
                       std::to_string(fs::exists(rt.coreLib, ec) ? fs::last_write_time(rt.coreLib, ec).time_since_epoch().count() : 0);
    bool upToDate = false;
    if (fs::exists(exePath, ec) && fs::exists(hashFile, ec)) {
        std::ifstream in(hashFile);
        std::string prev((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        upToDate = (prev == hash);
    }

    // 3. Compila
    if (upToDate) {
        log("[BUILD] Codigo inalterado: reaproveitando o executavel compilado.");
    } else {
        fs::remove(hashFile, ec);
        phase("compiling", "Compilando com " + tc.description + " (C++20, Taskflow)...");
        platform::ProcessSpec spec;
        spec.workingDir = buildDir;
        const bool hasSubflows = !subflowSource.empty();

        if (tc.kind == Toolchain::Kind::Msvc) {
            std::ostringstream cmd;
            cmd << "chcp 65001 >nul && set VSLANG=1033 && ";
            if (!tc.vcvars.empty()) cmd << "call \"" << tc.vcvars << "\" >nul && ";
            cmd << "cl /nologo /std:c++20 /EHsc /O2 /MD /utf-8 /permissive- /Zc:__cplusplus /W3 "
                << "/I\"" << rt.taskflowInclude.string() << "\" /I\"" << rt.srcInclude.string() << "\" "
                << "/I\"" << rt.simdjsonInclude.string() << "\" "
                << "main.cpp " << (hasSubflows ? "subflows.cpp " : "")
                << "/Fe:\"" << exePath.filename().string() << "\" "
                << "/link /nologo \"" << rt.coreLib.string() << "\" winhttp.lib advapi32.lib ole32.lib shell32.lib user32.lib";
            spec.shellCommand = cmd.str();
        } else {
            spec.argv = {tc.compiler, "-std=c++20", "-O2", "-pthread",
                         "-I" + rt.taskflowInclude.string(), "-I" + rt.srcInclude.string(),
                         "-I" + rt.simdjsonInclude.string(),
                         "main.cpp"};
            if (hasSubflows) spec.argv.push_back("subflows.cpp");
            spec.argv.push_back(rt.coreLib.string());
            spec.argv.push_back("-o");
            spec.argv.push_back(exePath.filename().string());
        }

        auto cr = platform::run(spec, 15 * 60 * 1000, [&](const std::string& line) {
            if (!line.empty()) log("[BUILD] " + line, line.find("error") != std::string::npos);
        }, cancel);
        if (cr.cancelled) return finish(false, "compile", "Compilacao cancelada.");
        if (!cr.error.empty()) return finish(false, "compile", cr.error);
        if (cr.exitCode != 0 || !fs::exists(exePath, ec)) {
            result.exitCode = cr.exitCode;
            return finish(false, "compile", "A compilacao falhou (codigo " + std::to_string(cr.exitCode) + "). Veja os erros acima.");
        }
        std::ofstream(hashFile, std::ios::trunc) << hash;
        log("[BUILD] Compilacao concluida em " + std::to_string((int)cr.seconds) + " s.");
    }

    // 4. Executa
    phase("running", "Executando o pipeline...");
    platform::ProcessSpec runSpec;
    runSpec.argv = {exePath.string()};
    runSpec.workingDir = projectRoot;
    const std::string runId = newRunId();
    runSpec.env["ARESTRIX_RUN_ID"] = runId;
    runSpec.env["ARESTRIX_PROJECT_ROOT"] = projectRoot.string();

    // Sem host interativo (CLI, Runner headless, testes): caminho antigo, sem stdin.
    if (!ev.onInteraction) {
        runSpec.env["ARESTRIX_INTERACTION_HOST"] = "";
        auto rr = platform::run(runSpec, 0, [&](const std::string& line) {
            if (line.rfind(kTelemetryPrefix, 0) == 0) {
                if (ev.onTelemetry) ev.onTelemetry(line.substr(std::strlen(kTelemetryPrefix)));
            } else if (line.rfind(kInteractionPrefix, 0) == 0) {
                log("[INTERACTION] Pedido de interacao sem host: o pipeline vai falhar fechado.", true);
            } else if (!line.empty()) {
                log(line, false);
            }
        }, cancel);
        result.exitCode = rr.exitCode;
        if (rr.cancelled) return finish(false, "run", "Execucao cancelada pelo usuario.");
        if (!rr.error.empty()) return finish(false, "run", rr.error);
        if (rr.exitCode != 0) return finish(false, "run", "O pipeline terminou com erro (codigo " + std::to_string(rr.exitCode) + ").");
        return finish(true, "", "");
    }

    // Host interativo: o filho usa stdin/stdout delimitados por linha. O callback do
    // Studio bloqueia nesta thread de build e devolve a resposta; o runtime valida.
    runSpec.captureStdin = true;
    runSpec.env["ARESTRIX_INTERACTION_HOST"] = "1";
    std::string startError;
    auto proc = platform::Process::start(runSpec, startError);
    if (!proc) return finish(false, "run", startError);

    std::string pending;
    bool cancelled = false;
    auto handleLine = [&](const std::string& line) {
        if (line.rfind(kTelemetryPrefix, 0) == 0) {
            if (ev.onTelemetry) ev.onTelemetry(line.substr(std::strlen(kTelemetryPrefix)));
            return;
        }
        if (line.rfind(kInteractionPrefix, 0) == 0) {
            auto request = jsonutil::tryParse(line.substr(std::strlen(kInteractionPrefix)));
            if (!request || !request->is_object()) {
                log("[INTERACTION] Pedido de interacao invalido recebido do runtime.", true);
                return;
            }
            const std::string requestId = request->value("requestId", std::string());
            Json reply = Json::object();
            try {
                std::string raw = ev.onInteraction(request->dump(), cancel);
                auto parsed = jsonutil::tryParse(raw);
                if (parsed && parsed->is_object()) reply = *parsed;
                else reply["error"] = "Host devolveu uma resposta invalida.";
            } catch (const std::exception& ex) {
                reply["error"] = std::string("Falha no host de interacao: ") + ex.what();
            }
            if (!reply.contains("requestId")) reply["requestId"] = requestId;
            if (!proc->writeStdin("[ARX_ANSWER] " + reply.dump() + "\n")) {
                log("[INTERACTION] Nao foi possivel responder ao runtime (stdin fechado).", true);
            }
            return;
        }
        if (!line.empty()) log(line, false);
    };
    auto flushLines = [&](bool final) {
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, pos + 1);
            handleLine(line);
        }
        if (final && !pending.empty()) {
            handleLine(pending);
            pending.clear();
        }
    };

    while (true) {
        std::string chunk;
        platform::ReadStatus st = proc->read(chunk, 50);
        if (!chunk.empty()) {
            pending += chunk;
            flushLines(false);
        }
        if (st == platform::ReadStatus::Eof) break;
        if (cancel && cancel->load()) {
            cancelled = true;
            proc->kill();
            break;
        }
    }
    if (cancelled) {
        std::string chunk;
        while (proc->read(chunk, 20) == platform::ReadStatus::Data) {
            pending += chunk;
            chunk.clear();
        }
    }
    flushLines(true);

    int code = -1;
    if (!proc->wait(cancelled ? 3000 : 600000, code)) {
        proc->kill();
        proc->wait(3000, code);
    }
    result.exitCode = code;
    if (cancelled) return finish(false, "run", "Execucao cancelada pelo usuario.");
    if (code != 0) return finish(false, "run", "O pipeline terminou com erro (codigo " + std::to_string(code) + ").");
    return finish(true, "", "");
}

// ---------------------------------------------------------------------------
BuildService& BuildService::instance() {
    static BuildService svc;
    return svc;
}

BuildService::~BuildService() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

bool BuildService::start(const core::Graph& graph, const fs::path& projectRoot, BuildEvents events) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load()) return false;
    if (worker_.joinable()) worker_.join();
    cancel_ = false;
    running_ = true;
    worker_ = std::thread([this, graph, projectRoot, events]() {
        try {
            buildAndRun(graph, projectRoot, events, &cancel_);
        } catch (const std::exception& ex) {
            if (events.onFinished) events.onFinished(false, -1, 0, std::string("Erro interno: ") + ex.what());
        }
        running_ = false;
    });
    return true;
}

void BuildService::stop() {
    cancel_ = true;
}

void BuildService::join() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) worker_.join();
}

} // namespace build
} // namespace taskforge
