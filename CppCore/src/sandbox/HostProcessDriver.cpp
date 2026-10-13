#include "HostProcessDriver.hpp"
#include "platform/Process.hpp"

namespace taskforge {
namespace sandbox {

// Isolamento oferecido: processo filho com Job Object (Windows) / process group (POSIX),
// limite de memoria, timeout com encerramento da arvore e diretorio de trabalho fixo.
// NAO e uma fronteira de seguranca: o processo roda com as permissoes do usuario.
ExecutionResult HostProcessDriver::execute(const ExecutionParams& params) {
    ExecutionResult result;

    platform::ProcessSpec spec;
    if (params.useShell) {
        spec.shellCommand = params.command;
    } else {
        spec.argv.push_back(params.command);
        for (const auto& a : params.args) spec.argv.push_back(a);
    }
    spec.workingDir = params.workingDir;
    spec.env = params.envVars;
    spec.memoryLimitMB = params.policy.maxMemoryMB;
    spec.scrubSecrets = true;   // codigo de agente nunca herda chaves de API do hospedeiro

    int timeoutMs = params.policy.timeoutSeconds > 0 ? params.policy.timeoutSeconds * 1000 : 0;
    auto r = platform::run(spec, timeoutMs, params.onLine);

    result.exitCode = r.exitCode;
    result.stdoutOutput = r.output;
    result.timedOut = r.timedOut;
    result.durationSeconds = r.seconds;
    result.errorMessage = r.error;
    if (r.timedOut) {
        result.errorMessage = "Processo interrompido por timeout (" + std::to_string(params.policy.timeoutSeconds) + "s)";
    }
    return result;
}

} // namespace sandbox
} // namespace taskforge
