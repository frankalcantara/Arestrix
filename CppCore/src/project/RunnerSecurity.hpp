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
     * Grava os metadados de assinatura no runner_manifest.json e cria o arquivo runner_manifest.json.sig.
     */
    static PackageSignatureResult signPackage(
        const std::filesystem::path& manifestPath,
        const std::string& privateKeyOrSecret,
        const std::string& algorithm = "hmac-sha256",
        const std::string& issuer = "Arestrix Enterprise Security",
        const std::string& keyId = "corp-signing-key-1"
    );

    /**
     * Verifica a assinatura criptográfica de um pacote de runner de forma estrita (fail-closed).
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
