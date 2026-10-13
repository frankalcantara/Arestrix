#include "rt/Http.hpp"

#include <algorithm>
#include <cctype>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include "platform/Process.hpp"
#endif

namespace taskforge {
namespace rt {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

} // namespace

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

struct HInternet {
    HINTERNET h = nullptr;
    explicit HInternet(HINTERNET handle = nullptr) : h(handle) {}
    ~HInternet() { if (h) WinHttpCloseHandle(h); }
    HInternet(const HInternet&) = delete;
    HInternet& operator=(const HInternet&) = delete;
};

} // namespace

HttpResponse httpSend(const HttpRequest& req) {
    HttpResponse resp;

    std::wstring wurl = widen(req.url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256], path[4096];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    uc.dwExtraInfoLength = (DWORD)-1;
    wchar_t extra[4096];
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
        resp.error = "URL invalida: " + req.url;
        return resp;
    }
    std::wstring fullPath = std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) +
                            std::wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HInternet session(WinHttpOpen(L"Arestrix-Runtime/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) { resp.error = "WinHttpOpen falhou"; return resp; }

    int t = std::max(5, req.timeoutSeconds) * 1000;
    WinHttpSetTimeouts(session.h, 15000, 15000, 120000, t);

    HInternet conn(WinHttpConnect(session.h, std::wstring(uc.lpszHostName, uc.dwHostNameLength).c_str(), uc.nPort, 0));
    if (!conn.h) { resp.error = "WinHttpConnect falhou (" + std::to_string(GetLastError()) + ")"; return resp; }

    HInternet request(WinHttpOpenRequest(conn.h, widen(req.method).c_str(), fullPath.c_str(), nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request.h) { resp.error = "WinHttpOpenRequest falhou"; return resp; }

    std::wstring headerBlock;
    for (const auto& kv : req.headers) {
        headerBlock += widen(kv.first) + L": " + widen(kv.second) + L"\r\n";
    }

    BOOL sent = WinHttpSendRequest(request.h,
                                   headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                                   (DWORD)-1L,
                                   req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data(),
                                   (DWORD)req.body.size(), (DWORD)req.body.size(), 0);
    if (!sent || !WinHttpReceiveResponse(request.h, nullptr)) {
        resp.error = "Falha de transporte (erro WinHTTP " + std::to_string(GetLastError()) + ")";
        return resp;
    }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    resp.status = (int)status;

    // Cabecalhos de interesse (Retry-After).
    DWORD hsize = 0;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &hsize, WINHTTP_NO_HEADER_INDEX);
    if (hsize > 0) {
        std::wstring raw(hsize / sizeof(wchar_t), L'\0');
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                raw.data(), &hsize, WINHTTP_NO_HEADER_INDEX)) {
            std::string h = narrow(raw);
            size_t pos = 0;
            while (pos < h.size()) {
                size_t eol = h.find("\r\n", pos);
                if (eol == std::string::npos) eol = h.size();
                std::string line = h.substr(pos, eol - pos);
                size_t colon = line.find(':');
                if (colon != std::string::npos && colon > 0) {
                    std::string k = lower(line.substr(0, colon));
                    std::string v = line.substr(colon + 1);
                    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
                    resp.headers[k] = v;
                }
                pos = eol + 2;
            }
        }
    }

    while (true) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(request.h, chunk.data(), avail, &got) || got == 0) break;
        resp.body.append(chunk.data(), got);
    }
    return resp;
}

#else // POSIX: curl CLI

namespace {

std::string cfgQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string tempName(const std::string& tag) {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream ss;
    ss << std::hex << rng();
    auto dir = std::filesystem::temp_directory_path();
    return (dir / ("arestrix-" + tag + "-" + ss.str())).string();
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

HttpResponse httpSend(const HttpRequest& req) {
    HttpResponse resp;
    if (!platform::commandExists("curl")) {
        resp.error = "O comando 'curl' nao foi encontrado no PATH.";
        return resp;
    }

    std::string bodyFile = tempName("body");
    std::string outFile = tempName("out");
    std::string hdrFile = tempName("hdr");
    {
        std::ofstream b(bodyFile, std::ios::binary | std::ios::trunc);
        b << req.body;
    }
    std::filesystem::permissions(bodyFile, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace);

    std::string cfg;
    cfg += "url = " + cfgQuote(req.url) + "\n";
    cfg += "request = " + cfgQuote(req.method) + "\n";
    for (const auto& kv : req.headers) cfg += "header = " + cfgQuote(kv.first + ": " + kv.second) + "\n";
    if (!req.body.empty()) cfg += "data-binary = " + cfgQuote("@" + bodyFile) + "\n";
    cfg += "silent\nshow-error\n";
    cfg += "max-time = " + std::to_string(std::max(5, req.timeoutSeconds)) + "\n";
    cfg += "output = " + cfgQuote(outFile) + "\n";
    cfg += "dump-header = " + cfgQuote(hdrFile) + "\n";
    cfg += "write-out = \"%{http_code}\"\n";

    platform::ProcessSpec spec;
    spec.argv = {"curl", "-K", "-"};
    spec.captureStdin = true;
    std::string err;
    auto proc = platform::Process::start(spec, err);
    if (!proc) {
        resp.error = err;
    } else {
        proc->writeStdin(cfg);
        proc->closeStdin();
        std::string out;
        platform::ReadStatus st;
        while ((st = proc->read(out, 200)) != platform::ReadStatus::Eof) {}
        int code = -1;
        proc->wait(5000, code);
        int http = 0;
        // write-out imprime o codigo HTTP (3 digitos) por ultimo; erros do curl vem antes.
        if (out.size() >= 3) {
            std::string tail = out.substr(out.size() - 3);
            if (std::all_of(tail.begin(), tail.end(), [](unsigned char c) { return std::isdigit(c); })) {
                http = std::atoi(tail.c_str());
            }
        }
        resp.status = http;
        if (code != 0 && http == 0) resp.error = "curl falhou (codigo " + std::to_string(code) + "): " + out;
        resp.body = slurp(outFile);
        std::string hdrs = slurp(hdrFile);
        std::istringstream hs(hdrs);
        std::string line;
        while (std::getline(hs, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t colon = line.find(':');
            if (colon != std::string::npos && colon > 0) {
                std::string v = line.substr(colon + 1);
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                resp.headers[lower(line.substr(0, colon))] = v;
            }
        }
    }
    std::error_code ec;
    std::filesystem::remove(bodyFile, ec);
    std::filesystem::remove(outFile, ec);
    std::filesystem::remove(hdrFile, ec);
    return resp;
}

#endif

} // namespace rt
} // namespace taskforge
