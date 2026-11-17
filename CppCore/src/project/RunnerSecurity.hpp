#pragma once

#include "project/ProjectTypes.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace taskforge {
namespace project {

struct PackageSignatureResult {
    bool ok = false;
    std::string error;
    std::string algorithm;
    std::string publicKey;
    std::string signature;
    std::string packageHash;
    std::string signedAt;
    std::string issuer;
};

class RunnerSecurity {
public:
    /**
     * Calcula o hash SHA-256 criptográfico canônico de uma string ou arquivo.
     */
    static std::string sha256(const std::string& data);
    static std::string sha256File(const std::filesystem::path& filePath, std::string& outError);

    /**
     * Gera assinatura criptográfica para o pacote de distribuição do runner.
     * Suporta Ed25519 (recomendado) e HMAC-SHA256 (legado).
     * Grava os metadados de assinatura no runner_manifest.json e cria o arquivo runner_manifest.json.sig.
     *
     * Para Ed25519, privateKeyOrSecret deve ser uma seed hexadecimal de 32 bytes
     * (64 caracteres). A chave privada nunca é gravada no manifesto.
     */
    static PackageSignatureResult signPackage(
        const std::filesystem::path& manifestPath,
        const std::string& privateKeyOrSecret,
        const std::string& algorithm = "ed25519",
        const std::string& issuer = "Arestrix Enterprise Security",
        const std::string& keyId = "corp-signing-key-1",
        bool requiredForExecution = false
    );

    /**
     * Deriva a chave pública Ed25519 (hex) a partir de uma seed hexadecimal de 32 bytes.
     */
    static std::string ed25519PublicKey(const std::string& seedHex, std::string& outError);

    /**
     * Gera uma seed Ed25519 hexadecimal de 32 bytes usando o CSPRNG do sistema.
     */
    static std::string generateEd25519Seed(std::string& outError);

    /**
     * Verifica a assinatura criptográfica de um pacote de runner de forma estrita (fail-closed).
     * Para Ed25519, publicKeyOrSecret deve ser a chave pública hexadecimal de 32 bytes.
     * Se requiredForExecution for true e a assinatura for nula ou inválida, retorna false e impede o runner de iniciar.
     */
    static bool verifyPackage(
        const std::filesystem::path& manifestPath,
        const std::string& publicKeyOrSecret,
        bool requiredForExecution,
        std::string& outError
    );
};

} // namespace project
} // namespace taskforge
