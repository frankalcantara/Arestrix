#pragma once

// Compila o grafo em um executavel nativo (C++20 + Taskflow + runtime Arestrix) e o executa,
// transmitindo logs e telemetria em tempo real.

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "core/Graph.hpp"

namespace taskforge {
namespace build {

struct Toolchain {
    enum class Kind { None, Msvc, Gcc, Clang };
    Kind kind = Kind::None;
    std::string vcvars;        // MSVC: caminho do vcvars64.bat
    std::string compiler;      // GCC/Clang: executavel
    std::string description;
};

// Detecta o compilador C++20 do sistema (MSVC via vswhere no Windows; g++/clang++ no Linux).
Toolchain detectToolchain();

struct RuntimeLayout {
    bool ok = false;
    std::string error;
    std::filesystem::path home;            // raiz de uma instalacao do Arestrix
    std::filesystem::path coreLib;         // arestrix_core.lib / libarestrix_core.a
    std::filesystem::path taskflowInclude; // third_party/taskflow
    std::filesystem::path srcInclude;      // CppCore/src
    std::filesystem::path simdjsonInclude; // third_party/simdjson/singleheader
};

// Localiza a instalacao (ARESTRIX_HOME, ou subindo a partir do executavel / cwd).
RuntimeLayout locateRuntime();

struct BuildEvents {
    std::function<void(const std::string& phase, const std::string& message)> onPhase;
    std::function<void(const std::string& line, bool isError)> onLog;
    std::function<void(const std::string& telemetryJson)> onTelemetry;
    std::function<void(bool success, int exitCode, long long elapsedMs, const std::string& error)> onFinished;
};

struct BuildResult {
    bool success = false;
    int exitCode = -1;
    long long elapsedMs = 0;
    std::string error;
    std::string stage;   // "toolchain" | "generate" | "compile" | "run" | ""
};

// Executa o fluxo completo (gerar -> compilar -> rodar) de forma bloqueante.
BuildResult buildAndRun(const core::Graph& graph,
                        const std::filesystem::path& projectRoot,
                        const BuildEvents& events,
                        const std::atomic<bool>* cancel = nullptr);

// Versao assincrona usada pela interface (uma execucao por vez).
class BuildService {
public:
    static BuildService& instance();

    bool start(const core::Graph& graph, const std::filesystem::path& projectRoot, BuildEvents events);
    void stop();
    bool isRunning() const { return running_.load(); }
    void join();

private:
    BuildService() = default;
    ~BuildService();

    std::atomic<bool> running_{false};
    std::atomic<bool> cancel_{false};
    std::thread worker_;
    std::mutex mutex_;
};

} // namespace build
} // namespace taskforge
