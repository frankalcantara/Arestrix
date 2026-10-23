#include "platform/StudioConsole.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <streambuf>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#endif

namespace taskforge {
namespace platform {

namespace fs = std::filesystem;

#ifndef ARESTRIX_STUDIO_VERSION
#define ARESTRIX_STUDIO_VERSION "dev"
#endif

namespace {

#ifdef _WIN32

std::mutex g_logMutex;
std::ofstream* g_log = nullptr;

// Escreve cada caractere nos dois destinos (console e arquivo), serializando cout/cerr
// para que linhas de threads diferentes nao se misturem no mesmo arquivo.
class TeeBuf : public std::streambuf {
public:
    TeeBuf(std::streambuf* console, std::streambuf* file) : console_(console), file_(file) {}

protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        std::lock_guard<std::mutex> lock(g_logMutex);
        const int a = console_ ? console_->sputc(static_cast<char>(c)) : c;
        const int b = file_ ? file_->sputc(static_cast<char>(c)) : c;
        if (a == traits_type::eof() || b == traits_type::eof()) return traits_type::eof();
        return c;
    }

    std::streamsize xsputn(const char* s, std::streamsize n) override {
        std::lock_guard<std::mutex> lock(g_logMutex);
        const std::streamsize a = console_ ? console_->sputn(s, n) : n;
        const std::streamsize b = file_ ? file_->sputn(s, n) : n;
        return std::min(a, b);
    }

    int sync() override {
        std::lock_guard<std::mutex> lock(g_logMutex);
        const int a = console_ ? console_->pubsync() : 0;
        const int b = file_ ? file_->pubsync() : 0;
        return (a == 0 && b == 0) ? 0 : -1;
    }

private:
    std::streambuf* console_;
    std::streambuf* file_;
};

fs::path envPath(const wchar_t* name) {
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::wstring value(needed, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), needed);
    if (copied == 0 || copied >= needed) return {};
    value.resize(copied);
    return fs::path(value);
}

std::string localTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void rotateLog(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    const auto size = fs::file_size(path, ec);
    if (ec || size <= 5u * 1024u * 1024u) return;
    fs::path old = path;
    old += ".1";
    fs::remove(old, ec);
    fs::rename(path, old, ec);
}

#endif // _WIN32

} // namespace

fs::path studioLogPath() {
#ifdef _WIN32
    fs::path base = envPath(L"LOCALAPPDATA");
    if (base.empty()) base = envPath(L"TEMP");
    if (base.empty()) {
        std::error_code ec;
        base = fs::current_path(ec);
        if (ec) base = fs::path(".");
    }
    return base / "Arestrix" / "logs" / "studio.log";
#else
    const char* state = std::getenv("XDG_STATE_HOME");
    if (state && *state) return fs::path(state) / "arestrix" / "studio.log";
    const char* home = std::getenv("HOME");
    if (home && *home) return fs::path(home) / ".local" / "state" / "arestrix" / "studio.log";
    std::error_code ec;
    fs::path base = fs::current_path(ec);
    if (ec) base = fs::path(".");
    return base / "studio.log";
#endif
}

void setupStudioConsole() {
#ifdef _WIN32
    static bool done = false;
    if (done) return;
    done = true;

    // Se fomos iniciados por um terminal (inclusive CppCore/dev.py), o console e
    // compartilhado com o shell e deve continuar visivel. Se o console pertence somente
    // ao Studio (duplo clique/atalho), ocultamos a janela.
    HWND console = GetConsoleWindow();
    bool shared = false;
    if (console) {
        DWORD pids[2] = {};
        const DWORD count = GetConsoleProcessList(pids, 2);
        shared = count > 1;
        if (!shared) ShowWindow(console, SW_HIDE);
    } else if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        // Processo sem console, mas o pai tem um: usa o console do pai.
        shared = true;
        FILE* file = nullptr;
        freopen_s(&file, "CONOUT$", "w", stdout);
        freopen_s(&file, "CONOUT$", "w", stderr);
        freopen_s(&file, "CONIN$", "r", stdin);
        std::cout.clear();
        std::cerr.clear();
        console = GetConsoleWindow();
    }
    const bool hasConsole = console != nullptr;

    const fs::path path = studioLogPath();
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    rotateLog(path);
    g_log = new std::ofstream(path, std::ios::app | std::ios::binary);
    if (!g_log->is_open()) {
        delete g_log;
        g_log = nullptr;
    }

    const std::string header = "\n=== Arestrix Studio " ARESTRIX_STUDIO_VERSION " - " + localTimestamp() + " ===\n";
    if (g_log) {
        *g_log << header;
        *g_log << "[C++] Console: " << (shared ? "terminal" : (hasConsole ? "hidden" : "none")) << "\n";
        g_log->flush();
    }
    if (hasConsole) {
        std::cout << header;
        std::cout.flush();
    }

    // Espelha cout/cerr no console (se houver) e no arquivo. Os buffers alocados aqui
    // vivem ate o fim do processo; os streams padrao podem ser finalizados depois deles.
    if (g_log) {
        std::cout.rdbuf(new TeeBuf(hasConsole ? std::cout.rdbuf() : nullptr, g_log->rdbuf()));
        std::cerr.rdbuf(new TeeBuf(hasConsole ? std::cerr.rdbuf() : nullptr, g_log->rdbuf()));
    }

    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (hasConsole) {
        std::cout << "[C++] Log: " << path.string() << std::endl;
    } else if (!g_log) {
        // Sem console e sem arquivo: nao ha para onde reportar. O Studio segue com a UI.
    }
#else
    // No Linux o Studio continua sendo executado a partir do terminal.
#endif
}

} // namespace platform
} // namespace taskforge
