#pragma once

#include "project/ProjectTypes.hpp"
#include <string>
#include <optional>

namespace taskforge {
namespace project {

/**
 * Utilitario para limpeza estrita de memoria RAM contendo segredos ou credenciais.
 * Evita que chaves de API permaneçam em dumps de memoria ou memoria residual.
 */
void secureWipeMemory(void* ptr, size_t size);
void secureWipeString(std::string& s);

/**
 * Integracao nativa com o Cofre de Credenciais do Windows (Windows Credential Manager / DPAPI).
 * Armazena e recupera segredos de forma protegida pelo subsistema de seguranca do Windows.
 */
class WindowsCredentialVault {
public:
    static bool isAvailable();

    static bool storeSecret(
        const std::string& targetName,
        const std::string& secretValue,
        std::string& outError
    );

    static bool readSecret(
        const std::string& targetName,
        std::string& outSecret,
        std::string& outError
    );

    static bool deleteSecret(
        const std::string& targetName,
        std::string& outError
    );
};

/**
 * Integracao corporativa com o Azure Key Vault via REST API e Managed Identity / Entra ID.
 */
class AzureKeyVaultProvider {
public:
    static bool fetchSecret(
        const std::string& vaultUrl,
        const std::string& secretName,
        const std::string& authMethod,
        const std::string& tenantId,
        const std::string& clientId,
        std::string& outSecret,
        std::string& outError
    );
};

/**
 * Resolvedor unificado de segredos corporativos em tempo de execucao.
 * Implementa a cascata de seguranca:
 * 1. Azure Key Vault (se configurado)
 * 2. Windows Credential Vault (DPAPI)
 * 3. Variaveis de ambiente protegidas do SO (se fallback permitido)
 */
class CorporateSecretResolver {
public:
    static bool resolve(
        const std::string& secretKey,
        const RunnerVaultConfig& config,
        std::string& outSecret,
        std::string& outError
    );
};

} // namespace project
} // namespace taskforge
