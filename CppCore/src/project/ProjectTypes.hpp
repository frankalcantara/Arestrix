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
    bool enabled = true;
    std::string provider = "github"; // "github", "gitlab", "gitea", "custom"
    std::string serverUrl = "https://github.com";
    std::string username = ""; // Apenas identificador (sem tokens ou senhas)
    std::string repositoryName = ""; // Nome do repositorio remoto (privado)
    std::string remoteUrl = ""; // URL remota Git (ex: https://github.com/user/repo.git)
    bool isPrivate = true;
    bool autoCommitOnRun = true;
    bool autoCommitOnShutdown = true;
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
        {"lastSyncAt", g.lastSyncAt},
        {"lastCommitHash", g.lastCommitHash}
    };
}

inline void from_json(const taskforge::Json& j, ProjectGitConfig& g) {
    g.enabled = j.value("enabled", true);
    g.provider = j.value("provider", "github");
    g.serverUrl = j.value("serverUrl", "https://github.com");
    g.username = j.value("username", "");
    g.repositoryName = j.value("repositoryName", "");
    g.remoteUrl = j.value("remoteUrl", "");
    g.isPrivate = j.value("isPrivate", true);
    g.autoCommitOnRun = j.value("autoCommitOnRun", true);
    g.autoCommitOnShutdown = j.value("autoCommitOnShutdown", true);
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
        {"parameters", r.parameters}
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
