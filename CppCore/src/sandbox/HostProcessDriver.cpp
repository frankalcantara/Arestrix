#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <vector>
#include <string>
#include "HostProcessDriver.hpp"
#include <iostream>
#include <sstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
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

    // Bloco de ambiente: herda o ambiente atual e sobrepõe params.envVars (nomes sem distinção de maiúsculas, como no Windows)
    std::vector<char> envBlock;
    if (!params.envVars.empty()) {
        std::map<std::string, std::pair<std::string, std::string>> merged; // chave em minúsculas -> {nome, valor}
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        };
        if (LPCH env = GetEnvironmentStringsA()) {
            for (const char* p = env; *p; p += std::strlen(p) + 1) {
                const char* eq = std::strchr(p + 1, '='); // p+1: preserva entradas como "=C:=C:\\"
                if (!eq) continue;
                std::string name(p, eq), value(eq + 1);
                merged[lower(name)] = {name, value};
            }
            FreeEnvironmentStringsA(env);
        }
        for (const auto& kv : params.envVars) {
            merged[lower(kv.first)] = {kv.first, kv.second};
        }
        for (const auto& entry : merged) {
            std::string line = entry.second.first + "=" + entry.second.second;
            envBlock.insert(envBlock.end(), line.begin(), line.end());
            envBlock.push_back('\0');
        }
        envBlock.push_back('\0');
    }

    BOOL created = CreateProcessA(
        NULL,
        cmdBuffer.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        envBlock.empty() ? NULL : envBlock.data(),
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

    // Lê a saída enquanto o processo executa (evita que o filho bloqueie com o pipe cheio)
    // até o término ou estouro do timeout. PeekNamedPipe nunca bloqueia, mesmo que um
    // processo neto mantenha a ponta de escrita aberta após o término do filho.
    std::string outputBuffer;
    char buffer[4096];
    auto drainPipe = [&]() {
        DWORD avail = 0;
        while (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            DWORD bytesRead = 0;
            DWORD toRead = (avail < sizeof(buffer)) ? avail : (DWORD)sizeof(buffer);
            if (!ReadFile(hReadPipe, buffer, toRead, &bytesRead, NULL) || bytesRead == 0) break;
            outputBuffer.append(buffer, bytesRead);
        }
    };

    const bool hasTimeout = params.policy.timeoutSeconds > 0;
    const auto deadline = startTime + std::chrono::seconds(hasTimeout ? params.policy.timeoutSeconds : 0);
    while (true) {
        drainPipe();
        if (WaitForSingleObject(pi.hProcess, 20) != WAIT_TIMEOUT) break;
        if (hasTimeout && std::chrono::steady_clock::now() >= deadline) {
            result.timedOut = true;
            result.errorMessage = "Processo interrompido por timeout de inatividade (" + std::to_string(params.policy.timeoutSeconds) + "s)";
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            break;
        }
    }
    drainPipe();

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    result.exitCode = static_cast<int>(exitCode);
    result.stdoutOutput = outputBuffer;
#else
    // POSIX: executa "sh -c '<command> "$@"' sh <args...>" (equivalente à linha de comando montada no Windows),
    // com stdout+stderr em um único pipe, variáveis de ambiente da política e watchdog de timeout.
    int pipeFds[2];
    if (pipe(pipeFds) != 0) {
        result.errorMessage = "Falha ao criar pipes de comunicação de processo";
        result.exitCode = -1;
        return result;
    }

    std::vector<std::string> argvStorage = {"/bin/sh", "-c", params.command + " \"$@\"", "sh"};
    for (const auto& arg : params.args) argvStorage.push_back(arg);
    std::vector<char*> argv;
    for (auto& s : argvStorage) argv.push_back(s.data());
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        result.errorMessage = "Falha ao iniciar processo local: " + std::string(strerror(errno));
        result.exitCode = -1;
        return result;
    }

    if (pid == 0) {
        // Processo filho: novo grupo para que o timeout encerre também os netos
        setsid();
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        if (!params.workingDir.empty() && chdir(params.workingDir.c_str()) != 0) {
            _exit(126);
        }
        for (const auto& kv : params.envVars) {
            setenv(kv.first.c_str(), kv.second.c_str(), 1);
        }
        execv("/bin/sh", argv.data());
        _exit(127);
    }

    close(pipeFds[1]);
    fcntl(pipeFds[0], F_SETFL, fcntl(pipeFds[0], F_GETFL) | O_NONBLOCK);

    const bool hasTimeout = params.policy.timeoutSeconds > 0;
    const auto deadline = startTime + std::chrono::seconds(hasTimeout ? params.policy.timeoutSeconds : 0);

    // Lê a saída durante a execução (evita bloqueio do filho com pipe cheio) até EOF ou timeout
    std::string outputBuffer;
    char buffer[4096];
    bool eof = false;
    while (!eof) {
        int waitMs = 200;
        if (hasTimeout) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) {
                result.timedOut = true;
                result.errorMessage = "Processo interrompido por timeout de inatividade (" + std::to_string(params.policy.timeoutSeconds) + "s)";
                kill(-pid, SIGKILL);
                break;
            }
            if (remaining < waitMs) waitMs = static_cast<int>(remaining);
        }
        struct pollfd pfd = {pipeFds[0], POLLIN, 0};
        int pr = poll(&pfd, 1, waitMs);
        if (pr < 0 && errno != EINTR) break;
        if (pr > 0) {
            ssize_t n;
            while ((n = read(pipeFds[0], buffer, sizeof(buffer))) > 0) {
                outputBuffer.append(buffer, static_cast<size_t>(n));
            }
            if (n == 0) eof = true;
        }
    }
    close(pipeFds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exitCode = 128 + WTERMSIG(status);
    } else {
        result.exitCode = 1;
    }
    result.stdoutOutput = outputBuffer;
#endif

    auto endTime = std::chrono::steady_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

} // namespace sandbox
} // namespace taskforge
