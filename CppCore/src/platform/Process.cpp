#include "platform/Process.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
extern char** environ;
#endif

namespace taskforge {
namespace platform {

using Clock = std::chrono::steady_clock;

namespace {
bool looksLikeSecretName(const std::string& n) {
    std::string u;
    for (char ch : n) u.push_back((char)std::toupper((unsigned char)ch));
    auto ends = [&](const char* suf) {
        size_t l = std::strlen(suf);
        return u.size() >= l && u.compare(u.size() - l, l, suf) == 0;
    };
    return ends("_API_KEY") || ends("_TOKEN") || ends("_SECRET") || ends("_PASSWORD") || ends("APIKEY");
}
} // namespace

// ============================================================================
// Windows
// ============================================================================
#ifdef _WIN32
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

// Regras de quoting do CommandLineToArgvW / CRT do MSVC.
std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

class WinProcess : public Process {
public:
    HANDLE hProcess = nullptr;
    HANDLE hThread = nullptr;
    HANDLE hJob = nullptr;
    HANDLE hOutRead = nullptr;
    HANDLE hInWrite = nullptr;

    // Um pipe anonimo do Windows nao pode ser aguardado com tempo limite. Em vez de consultar o pipe em ciclos com pausas, uma
    // thread leitora fica BLOQUEADA em ReadFile (o sistema a acorda quando ha dados) e entrega os bytes por uma variavel de
    // condicao; read() espera nela. Nenhuma pausa e nenhuma consulta repetida em lugar algum.
    std::thread reader;
    std::mutex mu;
    std::condition_variable cv;
    std::string pending;
    bool readerDone = false;

    void startReader() {
        reader = std::thread([this] {
            char buf[8192];
            while (true) {
                DWORD got = 0;
                if (!ReadFile(hOutRead, buf, sizeof(buf), &got, nullptr) || got == 0) break;
                {
                    std::lock_guard<std::mutex> lk(mu);
                    pending.append(buf, got);
                }
                cv.notify_all();
            }
            {
                std::lock_guard<std::mutex> lk(mu);
                readerDone = true;
            }
            cv.notify_all();
        });
    }

    ~WinProcess() override {
        kill();
        closeStdin();
        if (reader.joinable()) {
            CancelIoEx(hOutRead, nullptr);   // solta a leitura bloqueada mesmo que um neto ainda segure o pipe
            reader.join();
        }
        if (hOutRead) CloseHandle(hOutRead);
        if (hThread) CloseHandle(hThread);
        if (hProcess) CloseHandle(hProcess);
        if (hJob) CloseHandle(hJob);   // KILL_ON_JOB_CLOSE derruba qualquer descendente
    }

    bool writeStdin(const std::string& data) override {
        if (!hInWrite) return false;
        size_t off = 0;
        while (off < data.size()) {
            DWORD w = 0;
            if (!WriteFile(hInWrite, data.data() + off, (DWORD)(data.size() - off), &w, nullptr)) return false;
            off += w;
        }
        return true;
    }

    void closeStdin() override {
        if (hInWrite) { CloseHandle(hInWrite); hInWrite = nullptr; }
    }

    ReadStatus read(std::string& out, int timeoutMs) override {
        std::unique_lock<std::mutex> lk(mu);
        cv.wait_for(lk, std::chrono::milliseconds(std::max(0, timeoutMs)), [&] { return !pending.empty() || readerDone; });
        if (!pending.empty()) {
            out.append(pending);
            pending.clear();
            return ReadStatus::Data;
        }
        return readerDone ? ReadStatus::Eof : ReadStatus::Timeout;
    }

    bool wait(int timeoutMs, int& exitCode) override {
        DWORD r = WaitForSingleObject(hProcess, timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs);
        if (r != WAIT_OBJECT_0) return false;
        DWORD code = 1;
        GetExitCodeProcess(hProcess, &code);
        exitCode = (int)code;
        return true;
    }

    bool running() override {
        return WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT;
    }

    void kill() override {
        if (hJob) TerminateJobObject(hJob, 1);
        else if (hProcess) TerminateProcess(hProcess, 1);
    }
};

} // namespace

std::unique_ptr<Process> Process::start(const ProcessSpec& spec, std::string& error) {
    std::wstring cmdLine;
    if (!spec.shellCommand.empty()) {
        cmdLine = L"cmd.exe /d /s /c \"" + widen(spec.shellCommand) + L"\"";
    } else if (!spec.argv.empty()) {
        for (size_t i = 0; i < spec.argv.size(); ++i) {
            if (i) cmdLine += L' ';
            cmdLine += quoteArg(widen(spec.argv[i]));
        }
    } else {
        error = "Comando vazio";
        return nullptr;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE outR = nullptr, outW = nullptr, inR = nullptr, inW = nullptr;
    if (!CreatePipe(&outR, &outW, &sa, 0)) { error = "CreatePipe(stdout) falhou"; return nullptr; }
    SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulIn = INVALID_HANDLE_VALUE;
    if (spec.captureStdin) {
        if (!CreatePipe(&inR, &inW, &sa, 0)) { CloseHandle(outR); CloseHandle(outW); error = "CreatePipe(stdin) falhou"; return nullptr; }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
    } else {
        nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outW;
    si.hStdError = outW;
    si.hStdInput = spec.captureStdin ? inR : nulIn;

    // Bloco de ambiente: ambiente atual + overrides.
    std::wstring envBlock;
    bool needEnvBlock = !spec.env.empty() || spec.scrubSecrets;
    if (needEnvBlock) {
        std::map<std::wstring, std::wstring> merged;
        LPWCH cur = GetEnvironmentStringsW();
        for (LPWCH p = cur; p && *p; p += wcslen(p) + 1) {
            std::wstring kv(p);
            size_t eq = kv.find(L'=', 1);
            if (eq == std::wstring::npos) continue;
            std::wstring k = kv.substr(0, eq);
            std::wstring origName = k;
            std::transform(k.begin(), k.end(), k.begin(), ::towupper);
            if (spec.scrubSecrets && looksLikeSecretName(narrow(origName))) continue;
            merged[k] = kv;
        }
        if (cur) FreeEnvironmentStringsW(cur);
        for (const auto& [k, v] : spec.env) {
            std::wstring wk = widen(k);
            std::wstring key = wk;
            std::transform(key.begin(), key.end(), key.begin(), ::towupper);
            merged[key] = wk + L"=" + widen(v);
        }
        for (const auto& [k, kv] : merged) { envBlock += kv; envBlock.push_back(L'\0'); }
        envBlock.push_back(L'\0');
    }

    std::wstring wdir = spec.workingDir.empty() ? std::wstring() : spec.workingDir.wstring();

    auto proc = std::make_unique<WinProcess>();

    // Job Object: encerra a arvore inteira e aplica limite de memoria.
    proc->hJob = CreateJobObjectW(nullptr, nullptr);
    if (proc->hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (spec.memoryLimitMB > 0) {
            li.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            li.ProcessMemoryLimit = (SIZE_T)spec.memoryLimitMB * 1024 * 1024;
        }
        SetInformationJobObject(proc->hJob, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }

    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, flags,
                             needEnvBlock ? (LPVOID)envBlock.data() : nullptr,
                             wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        // O processo atual pode estar num job que nao permite breakaway.
        flags &= ~CREATE_BREAKAWAY_FROM_JOB;
        ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE, flags,
                            needEnvBlock ? (LPVOID)envBlock.data() : nullptr,
                            wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    }

    CloseHandle(outW);
    if (inR) CloseHandle(inR);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);

    if (!ok) {
        error = "Falha ao iniciar processo (erro Win32 " + std::to_string(GetLastError()) + "): " + narrow(cmdLine);
        CloseHandle(outR);
        if (inW) CloseHandle(inW);
        return nullptr;
    }

    if (proc->hJob) AssignProcessToJobObject(proc->hJob, pi.hProcess);
    ResumeThread(pi.hThread);

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->hOutRead = outR;
    proc->hInWrite = inW;
    proc->startReader();
    return proc;
}

std::filesystem::path executablePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return n ? std::filesystem::path(std::wstring(buf, n)) : std::filesystem::path();
}

bool commandExists(const std::string& program) {
    wchar_t buf[MAX_PATH * 4];
    std::wstring w = widen(program);
    const wchar_t* exts[] = {L".exe", L".cmd", L".bat", L".com"};
    for (const wchar_t* ext : exts) {
        if (SearchPathW(nullptr, w.c_str(), ext, (DWORD)(sizeof(buf) / sizeof(buf[0])), buf, nullptr) > 0) return true;
    }
    return false;
}

std::string getEnv(const std::string& name) {
    std::wstring w = widen(name);
    DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
    if (n == 0) return std::string();
    std::wstring v(n, L'\0');
    DWORD got = GetEnvironmentVariableW(w.c_str(), v.data(), n);
    v.resize(got);
    return narrow(v);
}

bool setUserEnv(const std::string& name, const std::string& value) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
    }
    if (value.empty() || value.size() > 8192 || value.find('\0') != std::string::npos) return false;

    std::wstring wn = widen(name), wv = widen(value);
    SetEnvironmentVariableW(wn.c_str(), wv.c_str());

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LONG r = RegSetValueExW(key, wn.c_str(), 0, REG_SZ, (const BYTE*)wv.c_str(), (DWORD)((wv.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    if (r == ERROR_SUCCESS) {
        DWORD_PTR res = 0;
        SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, &res);
    }
    return r == ERROR_SUCCESS;
}

namespace {

// IFileOpenDialog em primeiro plano. `extraOptions` decide entre pasta (FOS_PICKFOLDERS) e arquivo.
std::string runOpenDialog(FILEOPENDIALOGOPTIONS extraOptions,
                          const std::string& title,
                          const std::vector<FileFilter>& filters,
                          const std::string& initialDir) {
    std::string selected;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&dlg));
    if (SUCCEEDED(hr) && dlg) {
        FILEOPENDIALOGOPTIONS opt;
        if (SUCCEEDED(dlg->GetOptions(&opt))) dlg->SetOptions(opt | extraOptions | FOS_FORCEFILESYSTEM);
        std::wstring wt = widen(title);
        dlg->SetTitle(wt.c_str());

        // COMDLG_FILTERSPEC so guarda ponteiros: as strings precisam viver ate o Show().
        std::vector<std::wstring> names, patterns;
        for (const auto& f : filters) {
            names.push_back(widen(f.name));
            patterns.push_back(widen(f.pattern));
        }
        std::vector<COMDLG_FILTERSPEC> specs;
        for (size_t i = 0; i < names.size(); ++i) specs.push_back({names[i].c_str(), patterns[i].c_str()});
        if (!specs.empty()) dlg->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());

        if (!initialDir.empty()) {
            std::wstring wdir = widen(initialDir);
            std::replace(wdir.begin(), wdir.end(), L'/', L'\\');
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(wdir.c_str(), nullptr, IID_PPV_ARGS(&folder))) && folder) {
                dlg->SetFolder(folder);
                folder->Release();
            }
        }
        HWND owner = GetForegroundWindow();
        if (!owner) owner = GetActiveWindow();
        if (SUCCEEDED(dlg->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    selected = narrow(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return selected;
}

} // namespace

std::string pickFolderDialog(const std::string& title) {
    return runOpenDialog(FOS_PICKFOLDERS, title, {}, "");
}

std::string pickFileDialog(const std::string& title,
                           const std::vector<FileFilter>& filters,
                           const std::string& initialDir) {
    return runOpenDialog(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST, title, filters, initialDir);
}

std::vector<std::string> listRoots() {
    std::vector<std::string> roots;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) roots.push_back(std::string(1, (char)('A' + i)) + ":\\");
    }
    if (roots.empty()) roots.push_back("C:\\");
    return roots;
}

bool openInVsCode(const std::filesystem::path& path) {
    std::wstring arg = L"\"" + path.wstring() + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", L"code", arg.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

// ============================================================================
// POSIX
// ============================================================================
#else
namespace {

class PosixProcess : public Process {
public:
    pid_t pid = -1;
    int outFd = -1;
    int inFd = -1;
    bool eof = false;

    // O fim do processo e aguardado por uma thread bloqueada em waitpid (sem consulta periodica); wait() espera na condicao.
    std::thread waiter;
    std::mutex mu;
    std::condition_variable cv;
    bool reaped = false;
    int status = 0;

    void startWaiter() {
        waiter = std::thread([this] {
            int st = 0;
            pid_t r;
            do { r = ::waitpid(pid, &st, 0); } while (r < 0 && errno == EINTR);
            {
                std::lock_guard<std::mutex> lk(mu);
                reaped = true;
                status = (r == pid) ? st : 0;
            }
            cv.notify_all();
        });
    }

    ~PosixProcess() override {
        kill();
        closeStdin();
        if (outFd >= 0) ::close(outFd);
        if (waiter.joinable()) waiter.join();
    }

    bool writeStdin(const std::string& data) override {
        if (inFd < 0) return false;
        size_t off = 0;
        while (off < data.size()) {
            ssize_t w = ::write(inFd, data.data() + off, data.size() - off);
            if (w < 0) { if (errno == EINTR) continue; return false; }
            off += (size_t)w;
        }
        return true;
    }

    void closeStdin() override {
        if (inFd >= 0) { ::close(inFd); inFd = -1; }
    }

    ReadStatus read(std::string& out, int timeoutMs) override {
        if (eof) return ReadStatus::Eof;
        struct pollfd pfd;
        pfd.fd = outFd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int r = ::poll(&pfd, 1, std::max(0, timeoutMs));
        if (r == 0) return ReadStatus::Timeout;
        if (r < 0) {
            if (errno == EINTR) return ReadStatus::Timeout;
            eof = true;
            return ReadStatus::Eof;
        }
        char buf[8192];
        ssize_t n = ::read(outFd, buf, sizeof(buf));
        if (n > 0) { out.append(buf, (size_t)n); return ReadStatus::Data; }
        eof = true;
        return ReadStatus::Eof;
    }

    bool wait(int timeoutMs, int& exitCode) override {
        std::unique_lock<std::mutex> lk(mu);
        if (timeoutMs < 0) cv.wait(lk, [&] { return reaped; });
        else if (!cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return reaped; })) return false;
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
        return true;
    }

    bool running() override {
        std::lock_guard<std::mutex> lk(mu);
        return !reaped;
    }

    void kill() override {
        bool done;
        { std::lock_guard<std::mutex> lk(mu); done = reaped; }
        if (pid > 0 && !done) ::killpg(pid, SIGKILL);
    }
};

} // namespace

std::unique_ptr<Process> Process::start(const ProcessSpec& spec, std::string& error) {
    std::vector<std::string> argv = spec.argv;
    if (!spec.shellCommand.empty()) argv = {"/bin/sh", "-c", spec.shellCommand};
    if (argv.empty()) { error = "Comando vazio"; return nullptr; }

    int outPipe[2], inPipe[2] = {-1, -1}, errPipe[2];
    if (::pipe(outPipe) != 0) { error = "pipe() falhou"; return nullptr; }
    if (spec.captureStdin && ::pipe(inPipe) != 0) { error = "pipe() falhou"; return nullptr; }
    if (::pipe(errPipe) != 0) { error = "pipe() falhou"; return nullptr; }
    ::fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

    std::vector<char*> cargv;
    for (auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = ::fork();
    if (pid < 0) { error = "fork() falhou"; return nullptr; }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::dup2(outPipe[1], 1);
        ::dup2(outPipe[1], 2);
        if (spec.captureStdin) {
            ::dup2(inPipe[0], 0);
        } else {
            int nul = ::open("/dev/null", O_RDONLY);
            if (nul >= 0) ::dup2(nul, 0);
        }
        ::close(outPipe[0]); ::close(outPipe[1]);
        if (inPipe[0] >= 0) { ::close(inPipe[0]); ::close(inPipe[1]); }
        ::close(errPipe[0]);
        if (spec.scrubSecrets) {
            std::vector<std::string> doomed;
            for (char** e = environ; e && *e; ++e) {
                std::string kv(*e);
                std::string name = kv.substr(0, kv.find('='));
                if (looksLikeSecretName(name)) doomed.push_back(name);
            }
            for (const auto& n : doomed) ::unsetenv(n.c_str());
        }
        for (const auto& kv : spec.env) ::setenv(kv.first.c_str(), kv.second.c_str(), 1);
        if (!spec.workingDir.empty() && ::chdir(spec.workingDir.c_str()) != 0) {
            int e = errno;
            if (::write(errPipe[1], &e, sizeof(e)) < 0) {}
            _exit(127);
        }
        if (spec.memoryLimitMB > 0) {
            struct rlimit rl;
            rl.rlim_cur = rl.rlim_max = (rlim_t)spec.memoryLimitMB * 1024 * 1024;
            ::setrlimit(RLIMIT_AS, &rl);
        }
        ::execvp(cargv[0], cargv.data());
        int e = errno;
        if (::write(errPipe[1], &e, sizeof(e)) < 0) {}
        _exit(127);
    }

    ::setpgid(pid, pid);
    ::close(outPipe[1]);
    ::close(errPipe[1]);
    if (inPipe[0] >= 0) ::close(inPipe[0]);

    int childErr = 0;
    ssize_t n = ::read(errPipe[0], &childErr, sizeof(childErr));
    ::close(errPipe[0]);
    if (n == (ssize_t)sizeof(childErr)) {
        ::waitpid(pid, nullptr, 0);
        ::close(outPipe[0]);
        if (inPipe[1] >= 0) ::close(inPipe[1]);
        error = "Falha ao iniciar '" + argv[0] + "': " + std::strerror(childErr);
        return nullptr;
    }

    auto proc = std::make_unique<PosixProcess>();
    proc->pid = pid;
    proc->outFd = outPipe[0];
    proc->inFd = inPipe[1];
    proc->startWaiter();
    return proc;
}

std::filesystem::path executablePath() {
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : p;
}

bool commandExists(const std::string& program) {
    if (program.find('/') != std::string::npos) return ::access(program.c_str(), X_OK) == 0;
    const char* path = std::getenv("PATH");
    if (!path) return false;
    std::string p(path);
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find(':', start);
        if (end == std::string::npos) end = p.size();
        std::string dir = p.substr(start, end - start);
        if (!dir.empty() && ::access((dir + "/" + program).c_str(), X_OK) == 0) return true;
        start = end + 1;
    }
    return false;
}

std::string getEnv(const std::string& name) {
    const char* v = std::getenv(name.c_str());
    return v ? std::string(v) : std::string();
}

bool setUserEnv(const std::string& name, const std::string& value) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
    }
    if (value.empty() || value.size() > 8192 || value.find('\0') != std::string::npos) return false;
    ::setenv(name.c_str(), value.c_str(), 1);

    const char* home = std::getenv("HOME");
    if (!home) return true;
    std::filesystem::path dir = std::filesystem::path(home) / ".config" / "arestrix";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path file = dir / "env";

    // Reescreve preservando as demais chaves; valor entre aspas simples (escapadas).
    std::vector<std::string> lines;
    {
        std::ifstream in(file);
        std::string l;
        while (std::getline(in, l)) {
            if (l.rfind("export " + name + "=", 0) != 0) lines.push_back(l);
        }
    }
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    lines.push_back("export " + name + "=" + quoted);
    {
        std::ofstream out(file, std::ios::trunc);
        for (auto& l : lines) out << l << "\n";
    }
    ::chmod(file.c_str(), 0600);
    return true;
}

std::string pickFolderDialog(const std::string& title) {
    std::string out;
    ProcessSpec s;
    if (commandExists("zenity")) {
        s.argv = {"zenity", "--file-selection", "--directory", "--title=" + title};
    } else if (commandExists("kdialog")) {
        s.argv = {"kdialog", "--getexistingdirectory", ".", "--title", title};
    } else {
        return out;
    }
    auto r = run(s, 0);
    if (r.exitCode == 0) out = r.output;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::string pickFileDialog(const std::string& title,
                           const std::vector<FileFilter>& filters,
                           const std::string& initialDir) {
    std::string out;
    ProcessSpec s;
    if (commandExists("zenity")) {
        s.argv = {"zenity", "--file-selection", "--title=" + title};
        if (!initialDir.empty()) s.argv.push_back("--filename=" + initialDir + "/");
        for (const auto& f : filters) {
            std::string globs = f.pattern;
            std::replace(globs.begin(), globs.end(), ';', ' ');
            s.argv.push_back("--file-filter=" + f.name + " | " + globs);
        }
    } else if (commandExists("kdialog")) {
        std::string spec;
        for (const auto& f : filters) {
            std::string globs = f.pattern;
            std::replace(globs.begin(), globs.end(), ';', ' ');
            if (!spec.empty()) spec += "\n";
            spec += globs + "|" + f.name;
        }
        s.argv = {"kdialog", "--getopenfilename", initialDir.empty() ? "." : initialDir, spec, "--title", title};
    } else {
        return out;
    }
    auto r = run(s, 0);
    if (r.exitCode == 0) out = r.output;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

std::vector<std::string> listRoots() {
    std::vector<std::string> roots{"/"};
    if (const char* home = std::getenv("HOME")) roots.push_back(home);
    return roots;
}

bool openInVsCode(const std::filesystem::path& path) {
    ProcessSpec s;
    s.argv = {"code", path.string()};
    std::string err;
    auto p = Process::start(s, err);
    return p != nullptr;
}
#endif

// ============================================================================
// Comum
// ============================================================================
RunResult run(const ProcessSpec& spec,
              int timeoutMs,
              const std::function<void(const std::string&)>& onLine,
              const std::atomic<bool>* cancel,
              size_t maxOutputBytes) {
    RunResult result;
    auto t0 = Clock::now();
    auto proc = Process::start(spec, result.error);
    if (!proc) return result;

    std::string pending;
    auto flushLines = [&](bool final) {
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (onLine) onLine(line);
            pending.erase(0, pos + 1);
        }
        if (final && !pending.empty()) {
            if (onLine) onLine(pending);
            pending.clear();
        }
    };

    auto deadline = t0 + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
    bool stoppedEarly = false;
    while (true) {
        std::string chunk;
        ReadStatus st = proc->read(chunk, 50);
        if (!chunk.empty()) {
            if (result.output.size() < maxOutputBytes) result.output += chunk;
            pending += chunk;
            flushLines(false);
        }
        if (st == ReadStatus::Eof) break;
        if (cancel && cancel->load()) { result.cancelled = true; proc->kill(); stoppedEarly = true; break; }
        if (timeoutMs > 0 && Clock::now() >= deadline) { result.timedOut = true; proc->kill(); stoppedEarly = true; break; }
    }

    // Drena o que sobrou no pipe depois de um kill.
    if (stoppedEarly) {
        std::string chunk;
        while (proc->read(chunk, 20) == ReadStatus::Data) {
            if (result.output.size() < maxOutputBytes) result.output += chunk;
            pending += chunk;
            chunk.clear();
        }
    }
    flushLines(true);

    int code = -1;
    if (!proc->wait(stoppedEarly ? 3000 : 600000, code)) { proc->kill(); proc->wait(3000, code); }
    result.exitCode = code;
    result.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return result;
}

} // namespace platform
} // namespace taskforge
