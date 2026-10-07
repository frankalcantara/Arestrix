#pragma once

#include "ISandboxDriver.hpp"

namespace taskforge {
namespace sandbox {

class HostProcessDriver : public ISandboxDriver {
public:
    HostProcessDriver() = default;
    ~HostProcessDriver() override = default;

    std::string getName() const override { return "HostProcessDriver"; }
    IsolationType getType() const override { return IsolationType::HostDirect; }
    bool isAvailable() const override { return true; }

    ExecutionResult execute(const ExecutionParams& params) override;
};

} // namespace sandbox
} // namespace taskforge
