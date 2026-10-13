#pragma once

#include "ISandboxDriver.hpp"
#include "HostProcessDriver.hpp"

namespace taskforge {
namespace sandbox {

// Executa dentro de uma distribuicao WSL2 (Windows). A VM isola o kernel, mas o WSL2
// compartilha o sistema de arquivos do hospedeiro (/mnt/*) e nao oferece controle de
// rede por processo; por isso politicas "offline" sao recusadas em vez de fingidas.
class MicroVMSandboxDriver : public ISandboxDriver {
private:
    HostProcessDriver processExecutor;

public:
    MicroVMSandboxDriver() = default;
    ~MicroVMSandboxDriver() override = default;

    std::string getName() const override { return "MicroVMSandboxDriver(WSL2)"; }
    IsolationType getType() const override { return IsolationType::MicroVM; }
    bool isAvailable() const override;

    ExecutionResult execute(const ExecutionParams& params) override;
};

} // namespace sandbox
} // namespace taskforge
