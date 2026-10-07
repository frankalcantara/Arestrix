#include "HostProcessDriver.hpp"
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace taskforge {
namespace sandbox {

ExecutionResult HostProcessDriver::execute(const ExecutionParams& params) {
    ExecutionResult result;
    auto startTime = std::chrono::steady_clock::now();

#ifdef _WIN32
    // Monta a linha de comando completa
    std::string cmdLine = params.command;
    for (const auto& arg : params.args) {
        cmdLine += " \"" + arg + "\"";
    }

    // Configuração de Pipes para captura de stdout e stderr
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        result.errorMessage = "Falha ao criar pipes de comunicação de processo";
        result.exitCode = -1;
        return result;
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    const char* workingDir = params.workingDir.empty() ? NULL : params.workingDir.c_str();

    std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back('\0');

    BOOL created = CreateProcessA(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        workingDir,
        &si,
        &pi
    );

    // Fecha a ponta de escrita no processo pai para que a leitura alcance EOF
    CloseHandle(hWritePipe);

    if (!created) {
        CloseHandle(hReadPipe);
        result.errorMessage = "Falha ao iniciar processo local: " + std::to_string(GetLastError());
        result.exitCode = -1;
        return result;
    }

    // Aguarda o término ou estouro do timeout
    DWORD timeoutMs = (params.policy.timeoutSeconds > 0) ? (params.policy.timeoutSeconds * 1000) : INFINITE;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);

    if (waitResult == WAIT_TIMEOUT) {
        result.timedOut = true;
        result.errorMessage = "Processo interrompido por timeout de inatividade (" + std::to_string(params.policy.timeoutSeconds) + "s)";
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    // Lê toda a saída do pipe
    std::string outputBuffer;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        outputBuffer.append(buffer, bytesRead);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    result.exitCode = static_cast<int>(exitCode);
    result.stdoutOutput = outputBuffer;
#else
    // Fallback POSIX básico
    result.stdoutOutput = "[POSIX Host Execution]";
    result.exitCode = 0;
#endif

    auto endTime = std::chrono::steady_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

} // namespace sandbox
} // namespace taskforge
