#include "DockerSandboxDriver.hpp"
#include "platform/Process.hpp"

#include <filesystem>

namespace taskforge {
namespace sandbox {

namespace fs = std::filesystem;

DockerSandboxDriver::DockerSandboxDriver(const std::string& image) {
    defaultImage = image;
    if (defaultImage.empty()) defaultImage = platform::getEnv("ARESTRIX_SANDBOX_IMAGE");
    if (defaultImage.empty()) defaultImage = "debian:stable-slim";
}

bool DockerSandboxDriver::isAvailable() const {
    std::lock_guard<std::mutex> lock(probeMutex);
    auto now = std::chrono::steady_clock::now();
    if (lastProbe.time_since_epoch().count() != 0 && now - lastProbe < std::chrono::seconds(30)) {
        return lastProbeResult;
    }
    bool ok = false;
    if (platform::commandExists("docker")) {
        platform::ProcessSpec spec;
        spec.argv = {"docker", "version", "--format", "{{.Server.Version}}"};
        auto r = platform::run(spec, 8000);
        ok = r.ok();                    // exit != 0 quando o daemon nao responde
    }
    lastProbe = now;
    lastProbeResult = ok;
    return ok;
}

std::vector<std::string> DockerSandboxDriver::buildDockerArgs(const ExecutionParams& params) const {
    std::vector<std::string> a = {"docker", "run", "--rm", "--init",
                                  "--cap-drop=ALL", "--security-opt=no-new-privileges",
                                  "--pids-limit=256", "--read-only", "--tmpfs", "/tmp"};

    if (params.policy.maxMemoryMB > 0) {
        a.push_back("--memory=" + std::to_string(params.policy.maxMemoryMB) + "m");
    }

    // O agente conversa com o LLM a partir do processo hospedeiro, nunca de dentro do
    // container. Portanto "egress_llm_only" tambem nao precisa de rede no container;
    // so "full" libera rede.
    if (params.policy.network != NetworkAccess::Full) {
        a.push_back("--network=none");
    }

    std::error_code ec;
    fs::path base = params.workingDir.empty() ? fs::current_path() : fs::path(params.workingDir);
    base = fs::weakly_canonical(base, ec);

    auto mount = [&](const std::string& rel, const char* mode) {
        if (rel.empty()) return;
        fs::path host = fs::weakly_canonical(base / rel, ec);
        // Nunca monta nada fora do workspace.
        auto rb = host.lexically_relative(base);
        if (rb.empty() || *rb.begin() == "..") return;
        a.push_back("-v");
        a.push_back(host.string() + ":/workspace/" + rb.generic_string() + ":" + mode);
    };

    if (params.policy.readOnlyMounts.empty() && params.policy.readWriteMounts.empty()) {
        a.push_back("-v");
        a.push_back(base.string() + ":/workspace:rw");
    } else {
        for (const auto& ro : params.policy.readOnlyMounts) mount(ro, "ro");
        for (const auto& rw : params.policy.readWriteMounts) mount(rw, "rw");
    }
    a.push_back("-w");
    a.push_back("/workspace");

    for (const auto& env : params.envVars) {
        a.push_back("-e");
        a.push_back(env.first + "=" + env.second);
    }

    a.push_back(defaultImage);
    if (params.useShell) {
        a.push_back("sh");
        a.push_back("-c");
        a.push_back(params.command);
    } else {
        a.push_back(params.command);
        for (const auto& arg : params.args) a.push_back(arg);
    }
    return a;
}

ExecutionResult DockerSandboxDriver::execute(const ExecutionParams& params) {
    if (!isAvailable()) {
        ExecutionResult res;
        res.exitCode = -1;
        res.errorMessage = "Docker nao esta disponivel (CLI ausente ou daemon parado).";
        return res;
    }

    platform::ProcessSpec spec;
    spec.argv = buildDockerArgs(params);
    spec.workingDir = params.workingDir;

    int timeoutMs = params.policy.timeoutSeconds > 0 ? params.policy.timeoutSeconds * 1000 : 0;
    auto r = platform::run(spec, timeoutMs, params.onLine);

    ExecutionResult res;
    res.exitCode = r.exitCode;
    res.stdoutOutput = r.output;
    res.timedOut = r.timedOut;
    res.durationSeconds = r.seconds;
    res.errorMessage = r.error;
    if (r.timedOut) res.errorMessage = "Container interrompido por timeout";
    return res;
}

} // namespace sandbox
} // namespace taskforge
