#include "MicroVMSandboxDriver.hpp"
#include <sstream>

namespace taskforge {
namespace sandbox {

bool MicroVMSandboxDriver::isAvailable() const {
#ifdef _WIN32
    // No Windows, verifica se o subsistema WSL2 está disponível
    ExecutionParams checkParams;
    checkParams.command = "wsl";
    checkParams.args = {"--status"};
    checkParams.policy.timeoutSeconds = 5;

    HostProcessDriver checker;
    auto res = checker.execute(checkParams);
    return res.exitCode == 0;
#else
    return false;
#endif
}

ExecutionResult MicroVMSandboxDriver::execute(const ExecutionParams& params) {
    if (!isAvailable()) {
        ExecutionResult fallbackRes;
        fallbackRes.exitCode = -1;
        fallbackRes.errorMessage = "MicroVM / WSL2 não está disponível no sistema.";
        return fallbackRes;
    }

    std::ostringstream ss;
    ss << "wsl --exec " << params.command;
    for (const auto& arg : params.args) {
        ss << " \"" << arg << "\"";
    }

    ExecutionParams wslParams;
    wslParams.command = ss.str();
    wslParams.workingDir = params.workingDir;
    wslParams.policy = params.policy;

    return processExecutor.execute(wslParams);
}

} // namespace sandbox
} // namespace taskforge
