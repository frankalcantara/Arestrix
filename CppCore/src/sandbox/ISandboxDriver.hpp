#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <functional>

namespace taskforge {
namespace sandbox {

enum class IsolationType {
    Container,      // Docker / Podman container efemero
    MicroVM,        // WSL2 (VM com kernel proprio)
    HostDirect      // Execucao direta no host (sem fronteira de seguranca)
};

enum class NetworkAccess {
    Offline,        // Bloqueio completo de trafego de rede
    EgressLlmOnly,  // Apenas o hospedeiro fala com APIs de LLM; a ferramenta nao tem rede
    Full            // Acesso total irrestrito a rede
};

struct SandboxPolicy {
    IsolationType isolation = IsolationType::Container;
    NetworkAccess network = NetworkAccess::EgressLlmOnly;
    int maxMemoryMB = 512;
    int timeoutSeconds = 300;
    std::vector<std::string> readOnlyMounts;
    std::vector<std::string> readWriteMounts;
    std::vector<std::string> allowedTools;
    // Se o nivel de isolamento pedido nao estiver disponivel, NAO cai para o host
    // silenciosamente. So executa no host se isto for explicitamente true.
    bool allowFallbackToHost = false;
};

struct ExecutionResult {
    int exitCode = -1;
    std::string stdoutOutput;   // stdout + stderr intercalados
    std::string stderrOutput;
    bool timedOut = false;
    double durationSeconds = 0.0;
    std::string errorMessage;

    bool isSuccess() const {
        return exitCode == 0 && !timedOut && errorMessage.empty();
    }
};

struct ExecutionParams {
    std::string agentId;
    std::string command;
    std::vector<std::string> args;
    std::string workingDir;
    SandboxPolicy policy;
    std::map<std::string, std::string> envVars;
    // true: `command` e uma linha de shell escrita pelo usuario/agente (cmd /c ou sh -c).
    // false: `command` + `args` formam um argv executado diretamente, sem shell.
    bool useShell = false;
    // Recebe cada linha de saida em streaming (opcional).
    std::function<void(const std::string&)> onLine;
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
