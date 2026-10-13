#pragma once

#include "ISandboxDriver.hpp"
#include "HostProcessDriver.hpp"
#include <chrono>
#include <mutex>

namespace taskforge {
namespace sandbox {

class DockerSandboxDriver : public ISandboxDriver {
private:
    std::string defaultImage;
    HostProcessDriver processExecutor;
    mutable std::mutex probeMutex;
    mutable std::chrono::steady_clock::time_point lastProbe{};
    mutable bool lastProbeResult = false;

public:
    explicit DockerSandboxDriver(const std::string& image = "");
    ~DockerSandboxDriver() override = default;

    std::string getName() const override { return "DockerSandboxDriver"; }
    IsolationType getType() const override { return IsolationType::Container; }
    // true somente se o daemon responde (nao basta o CLI existir). Cache de 30 s.
    bool isAvailable() const override;

    ExecutionResult execute(const ExecutionParams& params) override;

    // Monta o argv do `docker run` (sem shell, sem concatenacao de strings).
    std::vector<std::string> buildDockerArgs(const ExecutionParams& params) const;
};

} // namespace sandbox
} // namespace taskforge
