#include "rt/NetGuard.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace taskforge {
namespace rt {
namespace net {

namespace {

bool publicV4(const uint8_t a[4]) {
    const unsigned b0 = a[0], b1 = a[1], b2 = a[2];
    if (b0 == 0 || b0 == 10 || b0 == 127) return false;                 // "esta rede", privado, loopback
    if (b0 == 100 && b1 >= 64 && b1 <= 127) return false;              // CGNAT
    if (b0 == 169 && b1 == 254) return false;                          // link-local (inclui metadados de nuvem)
    if (b0 == 172 && b1 >= 16 && b1 <= 31) return false;               // privado
    if (b0 == 192 && b1 == 0 && (b2 == 0 || b2 == 2)) return false;    // IETF e documentacao
    if (b0 == 192 && b1 == 168) return false;                           // privado
    if (b0 == 198 && (b1 == 18 || b1 == 19)) return false;             // testes de desempenho
    if (b0 == 198 && b1 == 51 && b2 == 100) return false;              // documentacao
    if (b0 == 203 && b1 == 0 && b2 == 113) return false;               // documentacao
    if (b0 >= 224) return false;                                        // multicast, reservado, broadcast
    return true;
}

bool publicV6(const uint8_t b[16]) {
    static const uint8_t zero10[10] = {0};
    if (std::memcmp(b, zero10, 10) == 0) {
        if (b[10] == 0xff && b[11] == 0xff) return publicV4(b + 12);    // ::ffff:a.b.c.d (IPv4 mapeado)
        return false;                                                   // ::, ::1 e ::a.b.c.d
    }
    if (b[0] == 0x00 && b[1] == 0x64 && b[2] == 0xff && b[3] == 0x9b) { // 64:ff9b::/96 (NAT64) embute um IPv4
        static const uint8_t zero8[8] = {0};
        if (std::memcmp(b + 4, zero8, 8) == 0) return publicV4(b + 12);
        return false;                                                   // 64:ff9b:1::/48 (uso local)
    }
    if ((b[0] & 0xe0) != 0x20) return false;                            // so 2000::/3 e unicast global
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x0d && b[3] == 0xb8) return false;   // 2001:db8::/32 documentacao
    if (b[0] == 0x20 && b[1] == 0x01 && b[2] == 0x00 && b[3] == 0x00) return false;   // 2001::/32 Teredo (embute IPv4)
    if (b[0] == 0x20 && b[1] == 0x02) return publicV4(b + 2);                          // 2002::/16 6to4 embute um IPv4
    return true;
}

std::mutex g_resolverMutex;
Resolver g_testResolver;

#ifdef _WIN32
void ensureWinsock() {
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    (void)started;
}
#endif

} // namespace

bool isPublicAddress(const std::string& ip) {
#ifdef _WIN32
    ensureWinsock();
#endif
    uint8_t buf[16];
    if (inet_pton(AF_INET, ip.c_str(), buf) == 1) return publicV4(buf);
    if (inet_pton(AF_INET6, ip.c_str(), buf) == 1) return publicV6(buf);
    return false;
}

void setResolverForTest(Resolver resolver) {
    std::lock_guard<std::mutex> lk(g_resolverMutex);
    g_testResolver = std::move(resolver);
}

Resolution resolveHost(const std::string& host) {
    {
        std::lock_guard<std::mutex> lk(g_resolverMutex);
        if (g_testResolver) return g_testResolver(host);
    }
#ifdef _WIN32
    ensureWinsock();
#endif
    Resolution out;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &list);
    if (rc != 0 || !list) {
        out.error = "o nome '" + host + "' nao foi resolvido";
        return out;
    }
    for (addrinfo* p = list; p; p = p->ai_next) {
        char text[INET6_ADDRSTRLEN] = {0};
        if (p->ai_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr, text, sizeof text);
        } else if (p->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(p->ai_addr)->sin6_addr, text, sizeof text);
        } else {
            continue;
        }
        const std::string s = text;
        if (!s.empty() && std::find(out.addresses.begin(), out.addresses.end(), s) == out.addresses.end()) out.addresses.push_back(s);
    }
    freeaddrinfo(list);
    if (out.addresses.empty()) out.error = "o nome '" + host + "' nao devolveu nenhum endereco";
    return out;
}

std::string hostOfUrl(const std::string& url) {
    size_t start = url.find("://");
    start = start == std::string::npos ? 0 : start + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    const size_t at = authority.rfind('@');
    if (at != std::string::npos) authority.erase(0, at + 1);
    if (!authority.empty() && authority.front() == '[') {
        const size_t close = authority.find(']');
        authority = close == std::string::npos ? std::string() : authority.substr(1, close - 1);
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) authority.erase(colon);
    }
    for (auto& c : authority) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return authority;
}

std::string checkHostIsPublic(const std::string& host, std::vector<std::string>* addresses) {
    if (host.empty()) return "host vazio";
#ifdef _WIN32
    ensureWinsock();
#endif
    uint8_t buf[16];
    if (inet_pton(AF_INET, host.c_str(), buf) == 1 || inet_pton(AF_INET6, host.c_str(), buf) == 1) {
        if (!isPublicAddress(host)) return "o endereco " + host + " nao e publico";
        if (addresses) *addresses = {host};
        return "";
    }
    Resolution r = resolveHost(host);
    if (!r.error.empty()) return r.error;
    for (const auto& ip : r.addresses) {
        if (!isPublicAddress(ip))
            return "o nome '" + host + "' aponta para um endereco interno ou reservado (" + ip + "); recusado para evitar acesso a rede interna";
    }
    if (addresses) *addresses = r.addresses;
    return "";
}

} // namespace net
} // namespace rt
} // namespace taskforge
