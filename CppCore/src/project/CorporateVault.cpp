#include "project/CorporateVault.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace taskforge {
namespace project {

void secureWipeMemory(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#ifdef _WIN32
    SecureZeroMemory(ptr, size);
#else
    volatile char* p = reinterpret_cast<volatile char*>(ptr);
    while (size--) {
        *p++ = 0;
    }
#endif
}

void secureWipeString(std::string& s) {
    if (!s.empty()) {
        secureWipeMemory(&s[0], s.size());
        s.clear();
    }
}

#ifdef _WIN32
static std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring out(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], size);
    return out;
}
#endif

// ---------------------------------------------------------------------------
// Windows Credential Vault (DPAPI / Win32 Credential Manager)
// ---------------------------------------------------------------------------

bool WindowsCredentialVault::isAvailable() {
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool WindowsCredentialVault::storeSecret(
    const std::string& targetName,
    const std::string& secretValue,
    std::string& outError
) {
#ifdef _WIN32
    std::string fullTarget = "Arestrix/" + targetName;
    std::wstring wTarget = utf8ToWide(fullTarget);

    CREDENTIALW cred = {0};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(wTarget.c_str());
    cred.CredentialBlobSize = (DWORD)secretValue.size();
    cred.CredentialBlob = (LPBYTE)secretValue.data();
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE; // ou CRED_PERSIST_ENTERPRISE se em dominio

    std::wstring wComment = L"Segredo Seguro Corporativo - Arestrix Standalone Runner";
    cred.Comment = const_cast<LPWSTR>(wComment.c_str());

    if (!CredWriteW(&cred, 0)) {
        DWORD err = GetLastError();
        outError = "Falha ao gravar no Cofre do Windows (CredWrite error: " + std::to_string(err) + ")";
        return false;
    }
    return true;
#else
    outError = "Cofre de Credenciais do Windows disponivel apenas no ambiente Windows.";
    return false;
#endif
}

bool WindowsCredentialVault::readSecret(
    const std::string& targetName,
    std::string& outSecret,
    std::string& outError
) {
#ifdef _WIN32
    std::string fullTarget = "Arestrix/" + targetName;
    std::wstring wTarget = utf8ToWide(fullTarget);

    PCREDENTIALW pCred = nullptr;
    if (!CredReadW(wTarget.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
        // Tenta tambem sem o prefixo Arestrix/ caso tenha sido cadastrado com nome simples
        std::wstring wDirect = utf8ToWide(targetName);
        if (!CredReadW(wDirect.c_str(), CRED_TYPE_GENERIC, 0, &pCred)) {
            DWORD err = GetLastError();
            outError = "Credencial nao encontrada no Cofre do Windows (" + targetName + ", code: " + std::to_string(err) + ")";
            return false;
        }
    }

    if (pCred && pCred->CredentialBlob && pCred->CredentialBlobSize > 0) {
        outSecret.assign(reinterpret_cast<const char*>(pCred->CredentialBlob), pCred->CredentialBlobSize);
        // Limpa a memoria do blob antes de liberar
        SecureZeroMemory(pCred->CredentialBlob, pCred->CredentialBlobSize);
        CredFree(pCred);
        return true;
    }

    if (pCred) CredFree(pCred);
    outError = "Blob de credencial vazio no Cofre do Windows (" + targetName + ")";
    return false;
#else
    outError = "Cofre de Credenciais do Windows disponivel apenas no ambiente Windows.";
    return false;
#endif
}

bool WindowsCredentialVault::deleteSecret(
    const std::string& targetName,
    std::string& outError
) {
#ifdef _WIN32
    std::string fullTarget = "Arestrix/" + targetName;
    std::wstring wTarget = utf8ToWide(fullTarget);

    if (!CredDeleteW(wTarget.c_str(), CRED_TYPE_GENERIC, 0)) {
        DWORD err = GetLastError();
        outError = "Falha ao remover credencial do Cofre do Windows (code: " + std::to_string(err) + ")";
        return false;
    }
    return true;
#else
    outError = "Cofre de Credenciais do Windows disponivel apenas no ambiente Windows.";
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Azure Key Vault Provider (Managed Identity & Service Principal)
// ---------------------------------------------------------------------------

bool AzureKeyVaultProvider::fetchSecret(
    const std::string& vaultUrl,
    const std::string& secretName,
    const std::string& authMethod,
    const std::string& tenantId,
    const std::string& clientId,
    std::string& outSecret,
    std::string& outError
) {
    if (vaultUrl.empty()) {
        outError = "URL do Azure Key Vault nao informada.";
        return false;
    }
    if (secretName.empty()) {
        outError = "Nome do segredo no Azure Key Vault nao informado.";
        return false;
    }

    std::string token;

    // 1. Obter Bearer Token do Azure Entra ID
    if (authMethod == "managed-identity" || authMethod.empty()) {
        // IMDS (Instance Metadata Service) do Azure
        std::string imdsUrl = "http://169.254.169.254/metadata/identity/oauth2/token?api-version=2018-02-01&resource=https://vault.azure.net";
        if (!clientId.empty()) {
            imdsUrl += "&client_id=" + clientId;
        }

        rt::HttpRequest imdsReq;
        imdsReq.method = "GET";
        imdsReq.url = imdsUrl;
        imdsReq.headers["Metadata"] = "true";
        imdsReq.timeoutSeconds = 5;

        auto imdsRes = rt::httpSend(imdsReq);
        if (imdsRes.status == 200 && !imdsRes.body.empty()) {
            try {
                auto j = taskforge::jsonutil::parse(imdsRes.body);
                token = j.value("access_token", "");
            } catch (...) {}
        }
    } else if (authMethod == "service-principal") {
        const char* secretEnv = std::getenv("AZURE_CLIENT_SECRET");
        if (secretEnv && *secretEnv && !tenantId.empty() && !clientId.empty()) {
            std::string oauthEndpoint = "https://login.microsoftonline.com/" + tenantId + "/oauth2/v2.0/token";
            rt::HttpRequest tokReq;
            tokReq.method = "POST";
            tokReq.url = oauthEndpoint;
            tokReq.headers["Content-Type"] = "application/x-www-form-urlencoded";
            tokReq.body = "grant_type=client_credentials&client_id=" + clientId +
                          "&client_secret=" + std::string(secretEnv) +
                          "&scope=https://vault.azure.net/.default";
            tokReq.timeoutSeconds = 10;

            auto tokRes = rt::httpSend(tokReq);
            if (tokRes.status == 200 && !tokRes.body.empty()) {
                try {
                    auto j = taskforge::jsonutil::parse(tokRes.body);
                    token = j.value("access_token", "");
                } catch (...) {}
            }
        }
    }

    // Fallback: se houver variável AZURE_KEY_VAULT_TOKEN no ambiente local de TI
    if (token.empty()) {
        const char* localTok = std::getenv("AZURE_KEY_VAULT_TOKEN");
        if (localTok && *localTok) {
            token = localTok;
        }
    }

    if (token.empty()) {
        outError = "Nao foi possivel obter token de autenticacao para o Azure Key Vault (Managed Identity ou Service Principal).";
        return false;
    }

    // 2. Consulta a API REST do Azure Key Vault
    std::string base = vaultUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();

    std::string secretUrl = base + "/secrets/" + secretName + "?api-version=7.4";

    rt::HttpRequest secReq;
    secReq.method = "GET";
    secReq.url = secretUrl;
    secReq.headers["Authorization"] = "Bearer " + token;
    secReq.headers["Content-Type"] = "application/json";
    secReq.timeoutSeconds = 15;

    auto secRes = rt::httpSend(secReq);

    // Limpa o token da memória imediatamente
    secureWipeString(token);

    if (secRes.status != 200 || secRes.body.empty()) {
        outError = "Falha ao obter segredo do Azure Key Vault HTTP " + std::to_string(secRes.status) + ": " + secRes.error;
        return false;
    }

    try {
        auto j = taskforge::jsonutil::parse(secRes.body);
        outSecret = j.value("value", "");
        if (outSecret.empty()) {
            outError = "Campo 'value' vazio no retorno do Azure Key Vault para '" + secretName + "'";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        outError = std::string("Erro no parsing da resposta do Azure Key Vault: ") + ex.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Corporate Secret Resolver (Resolução Unificada em Cascata)
// ---------------------------------------------------------------------------

bool CorporateSecretResolver::resolve(
    const std::string& secretKey,
    const RunnerVaultConfig& config,
    std::string& outSecret,
    std::string& outError
) {
    if (secretKey.empty()) {
        outError = "Chave de segredo vazia.";
        return false;
    }

    std::string lookupKey = secretKey;
    // Se configurado prefixo corporativo e a chave nao o tiver, anexa para consulta no cofre
    if (!config.secretPrefix.empty() && lookupKey.rfind(config.secretPrefix, 0) != 0) {
        // Tenta com o prefixo primeiro
    }

    // 1. Azure Key Vault
    if (config.provider == "azure-key-vault" || config.provider == "hybrid") {
        if (!config.azureKeyVaultUrl.empty()) {
            std::string akvErr;
            std::string akvName = config.secretPrefix + secretKey;
            // Azure Key Vault aceita apenas alfanumerico e hifens (converte '_' em '-')
            for (char& c : akvName) {
                if (c == '_') c = '-';
            }
            if (AzureKeyVaultProvider::fetchSecret(
                config.azureKeyVaultUrl,
                akvName,
                config.azureAuthMethod,
                config.azureTenantId,
                config.azureClientId,
                outSecret,
                akvErr
            )) {
                return true;
            }
        }
    }

    // 2. Windows Credential Vault (DPAPI)
    if (config.provider == "windows-credential-vault" || config.provider == "hybrid") {
        if (WindowsCredentialVault::isAvailable()) {
            std::string winErr;
            // Tenta com prefixo corporativo
            std::string target = config.secretPrefix + secretKey;
            if (WindowsCredentialVault::readSecret(target, outSecret, winErr)) {
                return true;
            }
            // Tenta direto com a chave original
            if (WindowsCredentialVault::readSecret(secretKey, outSecret, winErr)) {
                return true;
            }
        }
    }

    // 3. Fallback para Variaveis de Ambiente do Sistema Operacional
    if (config.fallbackToEnvironment) {
        const char* val = std::getenv(secretKey.c_str());
        if (val && *val) {
            outSecret = val;
            return true;
        }
    }

    outError = "Segredo '" + secretKey + "' nao pode ser resolvido no provedor corporativo (" + config.provider + ")";
    return false;
}

} // namespace project
} // namespace taskforge
