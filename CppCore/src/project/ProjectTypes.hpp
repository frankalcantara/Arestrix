#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace taskforge {
namespace project {

struct ProjectPipelineRef {
    std::string id;
    std::string name;
    std::string file; // caminho relativo a raiz do projeto (ex: "pipelines/main.json")
    bool isPrimary = false;
    std::string description;
};

inline void to_json(nlohmann::json& j, const ProjectPipelineRef& p) {
    j = nlohmann::json{
        {"id", p.id},
        {"name", p.name},
        {"file", p.file},
        {"isPrimary", p.isPrimary},
        {"description", p.description}
    };
}

inline void from_json(const nlohmann::json& j, ProjectPipelineRef& p) {
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

inline void to_json(nlohmann::json& j, const ProjectDatabaseConfig& d) {
    j = nlohmann::json{
        {"type", d.type},
        {"filePath", d.filePath},
        {"url", d.url}
    };
}

inline void from_json(const nlohmann::json& j, ProjectDatabaseConfig& d) {
    d.type = j.value("type", "sqlite");
    d.filePath = j.value("filePath", "storage/database.sqlite");
    d.url = j.value("url", "");
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
    nlohmann::json globalDefaults = nlohmann::json::object();
    ProjectDatabaseConfig database;
    std::map<std::string, std::string> environment;
};

inline void to_json(nlohmann::json& j, const ProjectManifest& m) {
    j = nlohmann::json{
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
        {"environment", m.environment}
    };
}

inline void from_json(const nlohmann::json& j, ProjectManifest& m) {
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
}

struct RecentProjectItem {
    std::string projectId;
    std::string name;
    std::string rootPath;
    std::string manifestPath;
    std::string lastOpened;
};

inline void to_json(nlohmann::json& j, const RecentProjectItem& r) {
    j = nlohmann::json{
        {"projectId", r.projectId},
        {"name", r.name},
        {"rootPath", r.rootPath},
        {"manifestPath", r.manifestPath},
        {"lastOpened", r.lastOpened}
    };
}

inline void from_json(const nlohmann::json& j, RecentProjectItem& r) {
    r.projectId = j.value("projectId", "");
    r.name = j.value("name", "");
    r.rootPath = j.value("rootPath", "");
    r.manifestPath = j.value("manifestPath", "");
    r.lastOpened = j.value("lastOpened", "");
}

} // namespace project
} // namespace taskforge
