#pragma once

#include <string>
#include <vector>
#include <map>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace project {

struct ProjectPipelineRef {
    std::string id;
    std::string name;
    std::string file; // caminho relativo a raiz do projeto (ex: "pipelines/main.json")
    bool isPrimary = false;
    std::string description;
};

inline void to_json(taskforge::Json& j, const ProjectPipelineRef& p) {
    j = taskforge::Json{
        {"id", p.id},
        {"name", p.name},
        {"file", p.file},
        {"isPrimary", p.isPrimary},
        {"description", p.description}
    };
}

inline void from_json(const taskforge::Json& j, ProjectPipelineRef& p) {
    p.id = j.value("id", "");
    p.name = j.value("name", "");
    p.file = j.value("file", "");
    p.isPrimary = j.value("isPrimary", false);
    p.description = j.value("description", "");
}

struct ProjectDatabaseConfig {
    std::string type = "sqlite"; // "sqlite" ou "postgres"
    std::string filePath = "storage/database.sqlite";
    std::string url;
};

inline void to_json(taskforge::Json& j, const ProjectDatabaseConfig& d) {
    j = taskforge::Json{
        {"type", d.type},
        {"filePath", d.filePath},
        {"url", d.url}
    };
}

inline void from_json(const taskforge::Json& j, ProjectDatabaseConfig& d) {
    d.type = j.value("type", "sqlite");
    d.filePath = j.value("filePath", "storage/database.sqlite");
    d.url = j.value("url", "");
}

struct ProjectGitConfig {
    bool enabled = false; // Opcional: modo 100% local por padrao
    std::string provider = "github"; // "github", "gitlab", "gitea", "custom"
    std::string serverUrl = "https://github.com";
    std::string username = ""; // Apenas identificador (sem tokens ou senhas)
    std::string repositoryName = ""; // Nome do repositorio remoto (privado)
    std::string remoteUrl = ""; // URL remota Git (ex: https://github.com/user/repo.git)
    bool isPrivate = true;
    bool autoCommitOnRun = true;
    bool autoCommitOnShutdown = true;
    bool timedPushEnabled = true;
    int timedPushIntervalMinutes = 10;
    std::string lastSyncAt = "";
    std::string lastCommitHash = "";
};

inline void to_json(taskforge::Json& j, const ProjectGitConfig& g) {
    j = taskforge::Json{
        {"enabled", g.enabled},
        {"provider", g.provider},
        {"serverUrl", g.serverUrl},
        {"username", g.username},
        {"repositoryName", g.repositoryName},
        {"remoteUrl", g.remoteUrl},
        {"isPrivate", g.isPrivate},
        {"autoCommitOnRun", g.autoCommitOnRun},
        {"autoCommitOnShutdown", g.autoCommitOnShutdown},
        {"timedPushEnabled", g.timedPushEnabled},
        {"timedPushIntervalMinutes", g.timedPushIntervalMinutes},
        {"lastSyncAt", g.lastSyncAt},
        {"lastCommitHash", g.lastCommitHash}
    };
}

inline void from_json(const taskforge::Json& j, ProjectGitConfig& g) {
    g.enabled = j.value("enabled", false);
    g.provider = j.value("provider", "github");
    g.serverUrl = j.value("serverUrl", "https://github.com");
    g.username = j.value("username", "");
    g.repositoryName = j.value("repositoryName", "");
    g.remoteUrl = j.value("remoteUrl", "");
    g.isPrivate = j.value("isPrivate", true);
    g.autoCommitOnRun = j.value("autoCommitOnRun", true);
    g.autoCommitOnShutdown = j.value("autoCommitOnShutdown", true);
    g.timedPushEnabled = j.value("timedPushEnabled", true);
    g.timedPushIntervalMinutes = j.value("timedPushIntervalMinutes", 10);
    g.lastSyncAt = j.value("lastSyncAt", "");
    g.lastCommitHash = j.value("lastCommitHash", "");
}

struct ProjectRunnerParameter {
    std::string id;
    std::string label;
    std::string type = "string"; // "string", "number", "boolean", "file"
    std::string defaultValue;
    std::string description;
};

inline void to_json(taskforge::Json& j, const ProjectRunnerParameter& p) {
    j = taskforge::Json{
        {"id", p.id},
        {"label", p.label},
        {"type", p.type},
        {"defaultValue", p.defaultValue},
        {"description", p.description}
    };
}

inline void from_json(const taskforge::Json& j, ProjectRunnerParameter& p) {
    p.id = j.value("id", "");
    p.label = j.value("label", "");
    p.type = j.value("type", "string");
    p.defaultValue = j.value("defaultValue", "");
    p.description = j.value("description", "");
}

// Fase 2: Governanca e Assinatura Criptografica de Pacotes
struct RunnerSignatureConfig {
    bool enabled = false;
    std::string algorithm = "ed25519"; // "ed25519", "sha256-rsa", "hmac-sha256"
    std::string keyId;
    std::string publicKey;
    bool requiredForExecution = false; // Se true, o runner recusa executar pacotes sem assinatura valida (fail-closed)
    std::string signature;
    std::string signedAt;
    std::string issuer;
};

inline void to_json(taskforge::Json& j, const RunnerSignatureConfig& s) {
    j = taskforge::Json{
        {"enabled", s.enabled},
        {"algorithm", s.algorithm},
        {"keyId", s.keyId},
        {"publicKey", s.publicKey},
        {"requiredForExecution", s.requiredForExecution},
        {"signature", s.signature},
        {"signedAt", s.signedAt},
        {"issuer", s.issuer}
    };
}

inline void from_json(const taskforge::Json& j, RunnerSignatureConfig& s) {
    s.enabled = j.value("enabled", false);
    s.algorithm = j.value("algorithm", "ed25519");
    s.keyId = j.value("keyId", "");
    s.publicKey = j.value("publicKey", "");
    s.requiredForExecution = j.value("requiredForExecution", false);
    s.signature = j.value("signature", "");
    s.signedAt = j.value("signedAt", "");
    s.issuer = j.value("issuer", "");
}

// Fase 2: Integracao com Cofres Corporativos (Windows Credential Vault e Azure Key Vault)
struct RunnerVaultConfig {
    std::string provider = "windows-credential-vault"; // "windows-credential-vault", "azure-key-vault", "hybrid", "none"
    std::string azureKeyVaultUrl;
    std::string azureAuthMethod = "managed-identity"; // "managed-identity", "service-principal", "azure-cli"
    std::string azureTenantId;
    std::string azureClientId;
    std::string secretPrefix = "Arestrix_";
    bool fallbackToEnvironment = true;
    bool autoPurgeMemory = true;
};

inline void to_json(taskforge::Json& j, const RunnerVaultConfig& v) {
    j = taskforge::Json{
        {"provider", v.provider},
        {"azureKeyVaultUrl", v.azureKeyVaultUrl},
        {"azureAuthMethod", v.azureAuthMethod},
        {"azureTenantId", v.azureTenantId},
        {"azureClientId", v.azureClientId},
        {"secretPrefix", v.secretPrefix},
        {"fallbackToEnvironment", v.fallbackToEnvironment},
        {"autoPurgeMemory", v.autoPurgeMemory}
    };
}

inline void from_json(const taskforge::Json& j, RunnerVaultConfig& v) {
    v.provider = j.value("provider", "windows-credential-vault");
    v.azureKeyVaultUrl = j.value("azureKeyVaultUrl", "");
    v.azureAuthMethod = j.value("azureAuthMethod", "managed-identity");
    v.azureTenantId = j.value("azureTenantId", "");
    v.azureClientId = j.value("azureClientId", "");
    v.secretPrefix = j.value("secretPrefix", "Arestrix_");
    v.fallbackToEnvironment = j.value("fallbackToEnvironment", true);
    v.autoPurgeMemory = j.value("autoPurgeMemory", true);
}

// Fase 2: Empacotamento Corporativo Homologavel (MSI WiX / MSIX AppxManifest para Intune e SCCM)
struct RunnerPackagingConfig {
    std::string format = "standalone-folder"; // "standalone-folder", "msi-installer", "msix-package"
    std::string appId = "Arestrix.Company.PipelineRunner";
    std::string publisher = "CN=Corporate, O=Enterprise, C=BR";
    std::string upgradeCode; // GUID para silent upgrades
    std::string silentInstallArgs = "/qn ALLUSERS=1";
    bool createDesktopShortcut = true;
    bool createStartMenuShortcut = true;
};

inline void to_json(taskforge::Json& j, const RunnerPackagingConfig& p) {
    j = taskforge::Json{
        {"format", p.format},
        {"appId", p.appId},
        {"publisher", p.publisher},
        {"upgradeCode", p.upgradeCode},
        {"silentInstallArgs", p.silentInstallArgs},
        {"createDesktopShortcut", p.createDesktopShortcut},
        {"createStartMenuShortcut", p.createStartMenuShortcut}
    };
}

inline void from_json(const taskforge::Json& j, RunnerPackagingConfig& p) {
    p.format = j.value("format", "standalone-folder");
    p.appId = j.value("appId", "Arestrix.Company.PipelineRunner");
    p.publisher = j.value("publisher", "CN=Corporate, O=Enterprise, C=BR");
    p.upgradeCode = j.value("upgradeCode", "");
    p.silentInstallArgs = j.value("silentInstallArgs", "/qn ALLUSERS=1");
    p.createDesktopShortcut = j.value("createDesktopShortcut", true);
    p.createStartMenuShortcut = j.value("createStartMenuShortcut", true);
}

struct ProjectRunnerConfig {
    bool enabled = false;
    std::string title;
    std::string description;
    std::string version = "1.0.0";
    std::string outputDir = "dist/runner";
    std::string outputName;
    bool autoStart = false;
    std::string defaultTab = "progress"; // "progress", "diagnostics", "operations"
    bool allowRestart = true;
    std::vector<ProjectRunnerParameter> parameters;
    RunnerSignatureConfig signature;
    RunnerVaultConfig vault;
    RunnerPackagingConfig packaging;
};

inline void to_json(taskforge::Json& j, const ProjectRunnerConfig& r) {
    j = taskforge::Json{
        {"enabled", r.enabled},
        {"title", r.title},
        {"description", r.description},
        {"version", r.version},
        {"outputDir", r.outputDir},
        {"outputName", r.outputName},
        {"autoStart", r.autoStart},
        {"defaultTab", r.defaultTab},
        {"allowRestart", r.allowRestart},
        {"parameters", r.parameters},
        {"signature", r.signature},
        {"vault", r.vault},
        {"packaging", r.packaging}
    };
}

inline void from_json(const taskforge::Json& j, ProjectRunnerConfig& r) {
    r.enabled = j.value("enabled", false);
    r.title = j.value("title", "");
    r.description = j.value("description", "");
    r.version = j.value("version", "1.0.0");
    r.outputDir = j.value("outputDir", "dist/runner");
    r.outputName = j.value("outputName", "");
    r.autoStart = j.value("autoStart", false);
    r.defaultTab = j.value("defaultTab", "progress");
    r.allowRestart = j.value("allowRestart", true);
    if (j.contains("parameters") && j["parameters"].is_array()) {
        r.parameters = j["parameters"].get<std::vector<ProjectRunnerParameter>>();
    }
    if (j.contains("signature") && j["signature"].is_object()) {
        r.signature = j["signature"].get<RunnerSignatureConfig>();
    }
    if (j.contains("vault") && j["vault"].is_object()) {
        r.vault = j["vault"].get<RunnerVaultConfig>();
    }
    if (j.contains("packaging") && j["packaging"].is_object()) {
        r.packaging = j["packaging"].get<RunnerPackagingConfig>();
    }
}

struct ProjectManifest {
    std::string projectId;
    std::string name;
    std::string version = "1.0.0";
    std::string description;
    std::string createdAt;
    std::string updatedAt;
    std::string rootPath;
    std::vector<ProjectPipelineRef> pipelines;
    taskforge::Json globalDefaults = taskforge::Json::object();
    ProjectDatabaseConfig database;
    std::map<std::string, std::string> environment;
    ProjectGitConfig git;
    ProjectRunnerConfig runner;
};

inline void to_json(taskforge::Json& j, const ProjectManifest& m) {
    j = taskforge::Json{
        {"projectId", m.projectId},
        {"name", m.name},
        {"version", m.version},
        {"description", m.description},
        {"createdAt", m.createdAt},
        {"updatedAt", m.updatedAt},
        {"rootPath", m.rootPath},
        {"pipelines", m.pipelines},
        {"globalDefaults", m.globalDefaults},
        {"database", m.database},
        {"environment", m.environment},
        {"git", m.git},
        {"runner", m.runner}
    };
}

inline void from_json(const taskforge::Json& j, ProjectManifest& m) {
    m.projectId = j.value("projectId", "");
    m.name = j.value("name", "");
    m.version = j.value("version", "1.0.0");
    m.description = j.value("description", "");
    m.createdAt = j.value("createdAt", "");
    m.updatedAt = j.value("updatedAt", "");
    m.rootPath = j.value("rootPath", "");
    if (j.contains("pipelines")) {
        m.pipelines = j["pipelines"].get<std::vector<ProjectPipelineRef>>();
    }
    if (j.contains("globalDefaults")) {
        m.globalDefaults = j["globalDefaults"];
    }
    if (j.contains("database")) {
        m.database = j["database"].get<ProjectDatabaseConfig>();
    }
    if (j.contains("environment")) {
        m.environment = j["environment"].get<std::map<std::string, std::string>>();
    }
    if (j.contains("git")) {
        m.git = j["git"].get<ProjectGitConfig>();
    }
    if (j.contains("runner")) {
        m.runner = j["runner"].get<ProjectRunnerConfig>();
    }
}

struct RecentProjectItem {
    std::string projectId;
    std::string name;
    std::string rootPath;
    std::string manifestPath;
    std::string lastOpened;
};

inline void to_json(taskforge::Json& j, const RecentProjectItem& r) {
    j = taskforge::Json{
        {"projectId", r.projectId},
        {"name", r.name},
        {"rootPath", r.rootPath},
        {"manifestPath", r.manifestPath},
        {"lastOpened", r.lastOpened}
    };
}

inline void from_json(const taskforge::Json& j, RecentProjectItem& r) {
    r.projectId = j.value("projectId", "");
    r.name = j.value("name", "");
    r.rootPath = j.value("rootPath", "");
    r.manifestPath = j.value("manifestPath", "");
    r.lastOpened = j.value("lastOpened", "");
}

} // namespace project
} // namespace taskforge
