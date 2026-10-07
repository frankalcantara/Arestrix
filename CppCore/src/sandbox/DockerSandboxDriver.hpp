#pragma once

#include "ISandboxDriver.hpp"
#include "HostProcessDriver.hpp"

namespace taskforge {
namespace sandbox {

class DockerSandboxDriver : public ISandboxDriver {
private:
    std::string defaultImage = "taskforge-agent-sandbox:latest";
    HostProcessDriver processExecutor;

public:
    explicit DockerSandboxDriver(const std::string& image = "taskforge-agent-sandbox:latest");
    ~DockerSandboxDriver() override = default;

    std::string getName() const override { return "DockerSandboxDriver"; }
    IsolationType getType() const override { return IsolationType::Container; }
    bool isAvailable() const override;

    ExecutionResult execute(const ExecutionParams& params) override;

    // Construtor do comando seguro docker
    std::string buildDockerCommand(const ExecutionParams& params) const;
};

} // namespace sandbox
} // namespace taskforge
