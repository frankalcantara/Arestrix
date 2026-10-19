// Download nativo de modelos. Windows usa WinHTTP; POSIX usa o curl ja exigido pelo runtime.
// O arquivo e gravado em um .part e retomado com HTTP Range. A verificacao SHA-256 fica no
// chamador (LocalRuntime), que so promove o .part a arquivo final depois de conferir o hash.

#include "local/LocalRuntime.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "platform/Process.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace taskforge {
namespace local {

namespace fs = std::filesystem;

namespace {

#ifdef _WIN32

struct WinHandle {
    HINTERNET value = nullptr;
    ~WinHandle() { if (value) WinHttpCloseHandle(value); }
    WinHandle() = default;
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
    explicit operator bool() const { return value != nullptr; }
};

std::wstring widen(const std::string& value) {
    if (value.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

std::string transportError(const char* operation) {
    return std::string(operation) + " falhou (WinHTTP " + std::to_string(GetLastError()) + ")";
}

#endif

} // namespace

bool downloadToFile(const std::string& url, const fs::path& target,
                    const std::atomic<bool>& cancel, const DownloadProgress& progress,
                    long long expectedBytes, std::string& error) {
    error.clear();
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
        error = "URL de download invalida.";
        return false;
    }

    std::error_code ec;
    uintmax_t existing = fs::is_regular_file(target, ec) ? fs::file_size(target, ec) : 0;
    if (ec) existing = 0;

#ifdef _WIN32

    const std::wstring wideUrl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[8192]{};
    wchar_t extra[8192]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0, &parts)) {
        error = "URL de download invalida.";
        return false;
    }
    const std::wstring fullPath =
        std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
        std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    WinHandle session;
    session.value = WinHttpOpen(L"Arestrix-Model-Download/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = transportError("WinHttpOpen"); return false; }
    WinHttpSetTimeouts(session.value, 30000, 30000, 60000, 60000);

    WinHandle connection;
    connection.value = WinHttpConnect(session.value,
                                      std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(),
                                      parts.nPort, 0);
    if (!connection) { error = transportError("WinHttpConnect"); return false; }

    WinHandle request;
    request.value = WinHttpOpenRequest(connection.value, L"GET", fullPath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) { error = transportError("WinHttpOpenRequest"); return false; }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    std::wstring headers = L"User-Agent: Arestrix/1.0\r\nAccept: */*\r\n";
    if (existing > 0) headers += L"Range: bytes=" + std::to_wstring(existing) + L"-\r\n";

    if (!WinHttpSendRequest(request.value, headers.c_str(), static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = transportError("WinHttpSendRequest");
        return false;
    }
    if (!WinHttpReceiveResponse(request.value, nullptr)) {
        error = transportError("WinHttpReceiveResponse");
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        error = transportError("WinHttpQueryHeaders(status)");
        return false;
    }

    if (status == 416 && existing > 0) {
        if (progress) progress(static_cast<long long>(existing), static_cast<long long>(existing));
        return true;   // o chamador confere o hash e o tamanho
    }
    if (status != 200 && status != 206) {
        error = "Servidor respondeu HTTP " + std::to_string(status) + ".";
        if (status == 401 || status == 403) {
            error += " O download exige autenticacao ou aceite de licenca na origem.";
        }
        return false;
    }

    const bool append = status == 206 && existing > 0;
    if (status == 200) existing = 0;

    unsigned long long contentLength = 0;
    {
        wchar_t lengthText[64]{};
        DWORD lengthSize = sizeof(lengthText);
        if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH,
                                WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &lengthSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            try { contentLength = std::stoull(std::wstring(lengthText)); } catch (...) { contentLength = 0; }
        }
    }

    long long total = static_cast<long long>(contentLength);
    if (append) total += static_cast<long long>(existing);
    if (total <= 0) total = expectedBytes;
    long long received = append ? static_cast<long long>(existing) : 0;
    if (progress) progress(received, total);

    std::ofstream out(target, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out.is_open()) {
        error = "Nao foi possivel abrir o arquivo de destino.";
        return false;
    }

    std::vector<char> buffer(64 * 1024);
    for (;;) {
        if (cancel.load()) {
            out.flush();
            error = "cancelled";
            return false;
        }

        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            error = transportError("WinHttpQueryDataAvailable");
            return false;
        }
        if (available == 0) break;

        DWORD remaining = available;
        while (remaining > 0) {
            const DWORD chunk = std::min<DWORD>(remaining, static_cast<DWORD>(buffer.size()));
            DWORD read = 0;
            if (!WinHttpReadData(request.value, buffer.data(), chunk, &read)) {
                error = transportError("WinHttpReadData");
                return false;
            }
            if (read == 0) break;
            out.write(buffer.data(), static_cast<std::streamsize>(read));
            if (!out) {
                error = "Falha ao gravar o arquivo de destino.";
                return false;
            }
            received += static_cast<long long>(read);
            if (total > 0 && received > total) total = received;
            if (progress) progress(received, total);
            remaining -= read;
        }
    }

    out.flush();
    if (!out.good()) {
        error = "Falha ao finalizar o arquivo de destino.";
        return false;
    }
    if (progress) progress(received, total > 0 ? total : received);
    return true;

#else

    // POSIX: curl com retomada. O progresso e obtido observando o tamanho do .part.
    auto runCurl = [&](bool resume, int& exitCode, std::string& output) -> bool {
        platform::ProcessSpec spec;
        spec.argv = {"curl", "-L", "--fail", "--silent", "--show-error", "-A", "Arestrix/1.0"};
        if (resume) {
            spec.argv.push_back("-C");
            spec.argv.push_back("-");
        }
        spec.argv.push_back("-o");
        spec.argv.push_back(target.string());
        spec.argv.push_back(url);

        std::string startError;
        auto process = platform::Process::start(spec, startError);
        if (!process) {
            output = startError;
            return false;
        }

        std::string line;
        while (process->running()) {
            process->read(line, 100);
            if (output.size() < 8192) output += line;
            line.clear();
            if (cancel.load()) {
                process->kill();
                return false;
            }
            const uintmax_t size = fs::is_regular_file(target, ec) ? fs::file_size(target, ec) : 0;
            if (!ec && progress) {
                progress(static_cast<long long>(size),
                         expectedBytes > 0 ? expectedBytes : static_cast<long long>(size));
            }
        }
        if (!process->wait(2000, exitCode)) {
            process->kill();
            output += "\ncurl nao terminou no prazo.";
            return false;
        }
        process->read(line, 0);
        if (output.size() < 8192) output += line;
        return exitCode == 0;
    };

    int exitCode = -1;
    std::string output;
    bool ok = runCurl(existing > 0, exitCode, output);
    if (!ok && !cancel.load() && existing > 0) {
        // Servidor sem suporte a Range ou arquivo parcial invalido: recomeca uma vez.
        fs::remove(target, ec);
        output.clear();
        ok = runCurl(false, exitCode, output);
    }
    if (cancel.load()) {
        error = "cancelled";
        return false;
    }
    if (!ok) {
        error = "curl falhou (codigo " + std::to_string(exitCode) + "): " + output;
        return false;
    }
    const uintmax_t size = fs::is_regular_file(target, ec) ? fs::file_size(target, ec) : 0;
    if (progress) {
        progress(static_cast<long long>(size),
                 expectedBytes > 0 ? expectedBytes : static_cast<long long>(size));
    }
    return true;

#endif
}

} // namespace local
} // namespace taskforge
