#pragma once

#include <string>
#include "ISandboxDriver.hpp"
#include "HostProcessDriver.hpp"

namespace taskforge {
namespace sandbox {

class MicroVMSandboxDriver : public ISandboxDriver {
private:
    HostProcessDriver processExecutor;

public:
    MicroVMSandboxDriver() = default;
    ~MicroVMSandboxDriver() override = default;

    std::string getName() const override { return "MicroVMSandboxDriver"; }
    IsolationType getType() const override { return IsolationType::MicroVM; }
    bool isAvailable() const override;

    ExecutionResult execute(const ExecutionParams& params) override;
};

} // namespace sandbox
} // namespace taskforge
