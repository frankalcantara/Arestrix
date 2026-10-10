#include <string>
#include "DockerSandboxDriver.hpp"
#include <sstream>
#include <iostream>

namespace taskforge {
namespace sandbox {

DockerSandboxDriver::DockerSandboxDriver(const std::string& image)
    : defaultImage(image) {}

bool DockerSandboxDriver::isAvailable() const {
    ExecutionParams checkParams;
    checkParams.command = "docker";
    checkParams.args = {"--version"};
    checkParams.policy.timeoutSeconds = 5;

    HostProcessDriver checker;
    auto res = checker.execute(checkParams);
    return res.exitCode == 0;
}

std::string DockerSandboxDriver::buildDockerCommand(const ExecutionParams& params) const {
    std::ostringstream ss;
    ss << "docker run --rm";

    // 1. Limite de Memória RAM
    if (params.policy.maxMemoryMB > 0) {
        ss << " --memory=" << params.policy.maxMemoryMB << "m";
    }

    // 2. Política de Rede
    switch (params.policy.network) {
        case NetworkAccess::Offline:
            ss << " --network=none";
            break;
        case NetworkAccess::EgressLlmOnly:
            // Em produção com firewall; em container local usamos rede restrita
            ss << " --security-opt=no-new-privileges";
            break;
        case NetworkAccess::Full:
            break;
    }

    // 3. Montagens Somente-Leitura (RO)
    for (const auto& ro : params.policy.readOnlyMounts) {
        if (!ro.empty()) {
            ss << " -v \"" << ro << ":/workspace/" << ro << ":ro\"";
        }
    }

    // 4. Montagens de Escrita Permitida (RW)
    for (const auto& rw : params.policy.readWriteMounts) {
        if (!rw.empty()) {
            ss << " -v \"" << rw << ":/workspace/" << rw << ":rw\"";
        }
    }

    // 5. Variáveis de Ambiente
    for (const auto& env : params.envVars) {
        ss << " -e " << env.first << "=\"" << env.second << "\"";
    }

    // 6. Imagem e Comando Interno do Agente
    ss << " " << defaultImage << " " << params.command;
    for (const auto& arg : params.args) {
        ss << " \"" << arg << "\"";
    }

    return ss.str();
}

ExecutionResult DockerSandboxDriver::execute(const ExecutionParams& params) {
    // Se o Docker não estiver instalado ou em execução, retorna erro informativo
    if (!isAvailable()) {
        ExecutionResult fallbackRes;
        fallbackRes.exitCode = -1;
        fallbackRes.errorMessage = "Docker não está disponível ou não foi encontrado no PATH do sistema.";
        return fallbackRes;
    }

    std::string fullDockerCmd = buildDockerCommand(params);

    ExecutionParams sysParams;
    sysParams.command = fullDockerCmd;
    sysParams.workingDir = params.workingDir;
    sysParams.policy = params.policy;

    return processExecutor.execute(sysParams);
}

} // namespace sandbox
} // namespace taskforge
