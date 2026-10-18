#include "project/RunnerSecurity.hpp"
#include "util/Json.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace taskforge {
namespace project {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Implementação Portátil e Segura de SHA-256 (NIST FIPS 180-4)
// ---------------------------------------------------------------------------

namespace {

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
};

void sha256Init(Sha256Ctx& ctx) {
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
    ctx.count = 0;
}

void sha256Transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t W[64];
    for (int i = 0; i < 16; ++i) {
        W[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t T1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        uint32_t T2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256Update(Sha256Ctx& ctx, const uint8_t* data, size_t len) {
    size_t index = static_cast<size_t>((ctx.count >> 3) & 0x3f);
    ctx.count += (static_cast<uint64_t>(len) << 3);
    size_t partLen = 64 - index;
    size_t i = 0;

    if (len >= partLen) {
        std::memcpy(&ctx.buffer[index], data, partLen);
        sha256Transform(ctx.state, ctx.buffer);
        for (i = partLen; i + 63 < len; i += 64) {
            sha256Transform(ctx.state, &data[i]);
        }
        index = 0;
    }
    std::memcpy(&ctx.buffer[index], &data[i], len - i);
}

void sha256Final(Sha256Ctx& ctx, uint8_t digest[32]) {
    uint8_t finalCount[8];
    for (int i = 0; i < 8; ++i) {
        finalCount[i] = static_cast<uint8_t>((ctx.count >> ((7 - i) * 8)) & 0xff);
    }
    uint8_t pad = 0x80;
    sha256Update(ctx, &pad, 1);
    uint8_t zero = 0x00;
    while ((ctx.count & 0x1f8) != 0x1c0) {
        sha256Update(ctx, &zero, 1);
    }
    sha256Update(ctx, finalCount, 8);
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xff);
        digest[i * 4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xff);
        digest[i * 4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xff);
        digest[i * 4 + 3] = static_cast<uint8_t>(ctx.state[i] & 0xff);
    }
}

static std::string hexEncode(const uint8_t* data, size_t len) {
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        ss << std::setw(2) << static_cast<int>(data[i]);
    }
    return ss.str();
}

static std::string hmacSha256(const std::string& key, const std::string& data) {
    uint8_t k[64] = {0};
    if (key.size() > 64) {
        Sha256Ctx kctx;
        sha256Init(kctx);
        sha256Update(kctx, reinterpret_cast<const uint8_t*>(key.data()), key.size());
        sha256Final(kctx, k);
    } else {
        std::memcpy(k, key.data(), key.size());
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }

    uint8_t innerHash[32];
    Sha256Ctx innerCtx;
    sha256Init(innerCtx);
    sha256Update(innerCtx, ipad, 64);
    sha256Update(innerCtx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    sha256Final(innerCtx, innerHash);

    uint8_t outerHash[32];
    Sha256Ctx outerCtx;
    sha256Init(outerCtx);
    sha256Update(outerCtx, opad, 64);
    sha256Update(outerCtx, innerHash, 32);
    sha256Final(outerCtx, outerHash);

    return hexEncode(outerHash, 32);
}

} // namespace

std::string RunnerSecurity::sha256(const std::string& data) {
    Sha256Ctx ctx;
    sha256Init(ctx);
    sha256Update(ctx, reinterpret_cast<const uint8_t*>(data.data()), data.size());
    uint8_t digest[32];
    sha256Final(ctx, digest);
    return hexEncode(digest, 32);
}

std::string RunnerSecurity::sha256File(const fs::path& filePath, std::string& outError) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in.is_open()) {
        outError = "Nao foi possivel abrir o arquivo: " + filePath.string();
        return "";
    }
    Sha256Ctx ctx;
    sha256Init(ctx);
    char buf[4096];
    while (in.read(buf, sizeof(buf))) {
        sha256Update(ctx, reinterpret_cast<const uint8_t*>(buf), in.gcount());
    }
    if (in.gcount() > 0) {
        sha256Update(ctx, reinterpret_cast<const uint8_t*>(buf), in.gcount());
    }
    uint8_t digest[32];
    sha256Final(ctx, digest);
    return hexEncode(digest, 32);
}

// ---------------------------------------------------------------------------
// Assinatura e Verificação de Pacotes
// ---------------------------------------------------------------------------

PackageSignatureResult RunnerSecurity::signPackage(
    const fs::path& manifestPath,
    const std::string& privateKeyOrSecret,
    const std::string& algorithm,
    const std::string& issuer,
    const std::string& keyId
) {
    PackageSignatureResult res;
    res.algorithm = algorithm.empty() ? "hmac-sha256" : algorithm;
    res.issuer = issuer.empty() ? "Arestrix Enterprise Security" : issuer;

    if (privateKeyOrSecret.empty()) {
        res.ok = false;
        res.error = "Chave de assinatura privada ou segredo corporativo ausente.";
        return res;
    }

    if (!fs::exists(manifestPath)) {
        res.ok = false;
        res.error = "Arquivo de manifesto nao encontrado: " + manifestPath.string();
        return res;
    }

    std::string content;
    {
        std::ifstream in(manifestPath);
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    }

    taskforge::Json j;
    try {
        j = taskforge::jsonutil::parse(content);
    } catch (const std::exception& e) {
        res.ok = false;
        res.error = std::string("Falha ao analisar JSON do manifesto: ") + e.what();
        return res;
    }

    // Remove qualquer assinatura pre-existente para calcular o hash canonico do pacote
    if (j.contains("signature")) {
        j.erase("signature");
    }

    std::string canonicalJson = j.dump();
    std::string packageHash = sha256(canonicalJson);
    res.packageHash = packageHash;

    // Gera a assinatura usando HMAC-SHA256 ou simulação criptográfica
    std::string sigStr = hmacSha256(privateKeyOrSecret, packageHash);
    res.signature = sigStr;

    // Timestamp atual
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ssTime;
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &in_time_t);
#else
    gmtime_r(&in_time_t, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    res.signedAt = std::string(buf);

    res.publicKey = sha256(privateKeyOrSecret + "_public");

    // Injeta metadados de assinatura no runner_manifest.json
    j["signature"] = {
        {"enabled", true},
        {"algorithm", res.algorithm},
        {"keyId", keyId.empty() ? "corp-signing-key-1" : keyId},
        {"publicKey", res.publicKey},
        {"requiredForExecution", true},
        {"signature", res.signature},
        {"signedAt", res.signedAt},
        {"issuer", res.issuer},
        {"packageHash", res.packageHash}
    };

    {
        std::ofstream out(manifestPath, std::ios::trunc);
        out << j.dump(2);
    }

    // Grava também o arquivo de assinatura desanexado (.sig)
    fs::path sigPath = manifestPath.parent_path() / (manifestPath.filename().string() + ".sig");
    {
        std::ofstream sigOut(sigPath, std::ios::trunc);
        sigOut << res.signature << "\n";
    }

    res.ok = true;
    return res;
}

bool RunnerSecurity::verifyPackage(
    const fs::path& manifestPath,
    const std::string& publicKeyOrSecret,
    bool requiredForExecution,
    std::string& outError
) {
    if (!fs::exists(manifestPath)) {
        outError = "Manifesto do runner inexistente em: " + manifestPath.string();
        return false;
    }

    std::string content;
    {
        std::ifstream in(manifestPath);
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    }

    taskforge::Json j;
    try {
        j = taskforge::jsonutil::parse(content);
    } catch (const std::exception& e) {
        outError = std::string("Bloqueio de seguranca (fail-closed): manifesto corrompido ou invalido: ") + e.what();
        return false;
    }

    if (!j.contains("signature") || !j["signature"].is_object()) {
        if (requiredForExecution) {
            outError = "Bloqueio de seguranca (fail-closed): pacote nao assinado digitalmente pela TI corporativa.";
            return false;
        }
        return true; // Politica permissiva
    }

    auto sigObj = j["signature"];
    std::string expectedSig = sigObj.value("signature", "");
    std::string algo = sigObj.value("algorithm", "hmac-sha256");
    bool required = sigObj.value("requiredForExecution", false) || requiredForExecution;

    if (expectedSig.empty()) {
        if (required) {
            outError = "Bloqueio de seguranca (fail-closed): campo de assinatura vazio no manifesto.";
            return false;
        }
        return true;
    }

    // Remove signature para recriar o hash canônico
    j.erase("signature");
    std::string canonicalJson = j.dump();
    std::string computedHash = sha256(canonicalJson);

    // Valida com o segredo corporativo ou chave pública
    if (!publicKeyOrSecret.empty()) {
        std::string computedSig = hmacSha256(publicKeyOrSecret, computedHash);
        if (computedSig != expectedSig) {
            outError = "Bloqueio de seguranca (fail-closed): assinatura criptografica invalida ou pacote adulterado.";
            return false;
        }
    } else {
        // Se a chave não foi fornecida mas havia hash anotado no manifesto
        if (sigObj.contains("packageHash")) {
            std::string origHash = sigObj["packageHash"].get<std::string>();
            if (origHash != computedHash) {
                outError = "Bloqueio de seguranca (fail-closed): integridade violada! Hash do grafo difere da assinatura corporativa.";
                return false;
            }
        }
    }

    return true;
}

} // namespace project
} // namespace taskforge
