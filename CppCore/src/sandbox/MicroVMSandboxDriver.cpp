#include "MicroVMSandboxDriver.hpp"
#include "platform/Process.hpp"

namespace taskforge {
namespace sandbox {

bool MicroVMSandboxDriver::isAvailable() const {
#ifdef _WIN32
    if (!platform::commandExists("wsl")) return false;
    platform::ProcessSpec spec;
    spec.argv = {"wsl", "--status"};
    return platform::run(spec, 8000).ok();
#else
    return false;
#endif
}

ExecutionResult MicroVMSandboxDriver::execute(const ExecutionParams& params) {
    ExecutionResult res;
    if (!isAvailable()) {
        res.exitCode = -1;
        res.errorMessage = "MicroVM / WSL2 nao esta disponivel neste sistema.";
        return res;
    }
    if (params.policy.network == NetworkAccess::Offline) {
        res.exitCode = -1;
        res.errorMessage = "WSL2 nao consegue garantir rede offline por processo; use Docker para esta politica.";
        return res;
    }

    ExecutionParams wsl = params;
    wsl.useShell = false;
    wsl.args.clear();
    wsl.command = "wsl";
    if (!params.workingDir.empty()) {
        wsl.args = {"--cd", params.workingDir};
    }
    wsl.args.push_back("--exec");
    if (params.useShell) {
        wsl.args.push_back("sh");
        wsl.args.push_back("-c");
        wsl.args.push_back(params.command);
    } else {
        wsl.args.push_back(params.command);
        for (const auto& a : params.args) wsl.args.push_back(a);
    }
    wsl.workingDir.clear();
    return processExecutor.execute(wsl);
}

} // namespace sandbox
} // namespace taskforge
