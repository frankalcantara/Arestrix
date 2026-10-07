#pragma once

#include "ISandboxDriver.hpp"
#include "DockerSandboxDriver.hpp"
#include "MicroVMSandboxDriver.hpp"
#include "HostProcessDriver.hpp"
#include <memory>
#include <iostream>

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

    ISandboxDriver* getDriver(IsolationType type);

    ExecutionResult execute(const ExecutionParams& params);
};

} // namespace sandbox
} // namespace taskforge
