#include "SandboxManager.hpp"
#include <iostream>

namespace taskforge {
namespace sandbox {

SandboxManager::SandboxManager() {
    dockerDriver = std::make_unique<DockerSandboxDriver>();
    microvmDriver = std::make_unique<MicroVMSandboxDriver>();
    hostDriver = std::make_unique<HostProcessDriver>();
}

SandboxManager& SandboxManager::instance() {
    static SandboxManager instance;
    return instance;
}

ISandboxDriver* SandboxManager::getDriver(IsolationType type) {
    switch (type) {
        case IsolationType::Container:
            return dockerDriver->isAvailable() ? static_cast<ISandboxDriver*>(dockerDriver.get()) : nullptr;
        case IsolationType::MicroVM:
            return microvmDriver->isAvailable() ? static_cast<ISandboxDriver*>(microvmDriver.get()) : nullptr;
        case IsolationType::HostDirect:
        default:
            return hostDriver.get();
    }
}

bool SandboxManager::isAvailable(IsolationType type) {
    return getDriver(type) != nullptr;
}

ExecutionResult SandboxManager::execute(const ExecutionParams& params) {
    ISandboxDriver* driver = getDriver(params.policy.isolation);
    if (!driver) {
        if (!params.policy.allowFallbackToHost) {
            ExecutionResult res;
            res.exitCode = -1;
            res.errorMessage = "O isolamento solicitado nao esta disponivel e o fallback para o host nao foi autorizado.";
            return res;
        }
        std::cerr << "[SandboxManager] Aviso: isolamento indisponivel; executando no host (autorizado).\n";
        driver = hostDriver.get();
    }
    return driver->execute(params);
}

} // namespace sandbox
} // namespace taskforge
