#pragma once

#include "ISandboxDriver.hpp"
#include "DockerSandboxDriver.hpp"
#include "MicroVMSandboxDriver.hpp"
#include "HostProcessDriver.hpp"
#include <memory>

namespace taskforge {
namespace sandbox {

class SandboxManager {
private:
    std::unique_ptr<DockerSandboxDriver> dockerDriver;
    std::unique_ptr<MicroVMSandboxDriver> microvmDriver;
    std::unique_ptr<HostProcessDriver> hostDriver;

    SandboxManager();

public:
    static SandboxManager& instance();

    // Devolve o driver do tipo pedido, ou nullptr se ele nao estiver disponivel.
    // Nao ha fallback silencioso para outro nivel de isolamento.
    ISandboxDriver* getDriver(IsolationType type);

    bool isAvailable(IsolationType type);

    // Executa respeitando policy.isolation. Se o driver pedido nao estiver disponivel,
    // falha com mensagem clara, a menos que policy.allowFallbackToHost seja true.
    ExecutionResult execute(const ExecutionParams& params);
};

} // namespace sandbox
} // namespace taskforge
