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
            if (dockerDriver->isAvailable()) {
                return dockerDriver.get();
            }
            std::cout << "[SandboxManager] Aviso: Docker solicitado mas não detectado. Usando HostProcessDriver com isolamento de diretório.\n";
            return hostDriver.get();

        case IsolationType::MicroVM:
            if (microvmDriver->isAvailable()) {
                return microvmDriver.get();
            }
            std::cout << "[SandboxManager] Aviso: MicroVM solicitada mas não disponível. Usando HostProcessDriver.\n";
            return hostDriver.get();

        case IsolationType::HostDirect:
        default:
            return hostDriver.get();
    }
}

ExecutionResult SandboxManager::execute(const ExecutionParams& params) {
    ISandboxDriver* driver = getDriver(params.policy.isolation);
    std::cout << "[SandboxManager] Disparando agente '" << params.agentId 
              << "' via driver: " << driver->getName() << "\n";
    return driver->execute(params);
}

} // namespace sandbox
} // namespace taskforge
