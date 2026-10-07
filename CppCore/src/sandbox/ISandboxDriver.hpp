#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>

namespace taskforge {
namespace sandbox {

enum class IsolationType {
    Container,      // Docker / Podman container efêmero
    MicroVM,        // Firecracker / WSL2 isolado
    HostDirect      // Execução direta fora da VM (modo leve/desenvolvimento)
};

enum class NetworkAccess {
    Offline,        // Bloqueio completo de tráfego de rede
    EgressLlmOnly,  // Apenas conexões de saída para APIs de LLM
    Full            // Acesso total irrestrito à rede
};

struct SandboxPolicy {
    IsolationType isolation = IsolationType::Container;
    NetworkAccess network = NetworkAccess::EgressLlmOnly;
    int maxMemoryMB = 512;
    int timeoutSeconds = 300;
    std::vector<std::string> readOnlyMounts;
    std::vector<std::string> readWriteMounts;
    std::vector<std::string> allowedTools;
};

struct ExecutionResult {
    int exitCode = -1;
    std::string stdoutOutput;
    std::string stderrOutput;
    bool timedOut = false;
    double durationSeconds = 0.0;
    std::string errorMessage;

    bool isSuccess() const {
        return exitCode == 0 && !timedOut;
    }
};

struct ExecutionParams {
    std::string agentId;
    std::string command;
    std::vector<std::string> args;
    std::string workingDir;
    SandboxPolicy policy;
    std::map<std::string, std::string> envVars;
};

class ISandboxDriver {
public:
    virtual ~ISandboxDriver() = default;

    virtual std::string getName() const = 0;
    virtual IsolationType getType() const = 0;
    virtual bool isAvailable() const = 0;
    virtual ExecutionResult execute(const ExecutionParams& params) = 0;
};

} // namespace sandbox
} // namespace taskforge
