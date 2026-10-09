#pragma once

#include "ProjectTypes.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>

namespace taskforge {
namespace project {

class ProjectManager {
public:
    static ProjectManager& getInstance();

    // Ciclo de vida de projetos
    bool createProject(ProjectManifest manifest);
    bool loadProject(const std::string& pathOrDir);
    bool saveProject(const ProjectManifest& manifest);
    ProjectManifest getCurrentProject() const;
    bool hasActiveProject() const;

    // Histórico de projetos recentes
    std::vector<RecentProjectItem> listRecentProjects() const;
    bool addRecentProject(const RecentProjectItem& item);
    bool removeRecentProject(const std::string& projectId);

    // Utilitários de caminhos
    std::string getActiveProjectRoot() const;
    std::string resolveProjectPath(const std::string& relativePath) const;

private:
    ProjectManager();
    ~ProjectManager() = default;

    ProjectManager(const ProjectManager&) = delete;
    ProjectManager& operator=(const ProjectManager&) = delete;

    void loadRecentProjectsFromDisk();
    void saveRecentProjectsToDisk() const;
    void ensureDefaultProject();

    mutable std::mutex mutex_;
    ProjectManifest currentProject_;
    std::vector<RecentProjectItem> recentProjects_;
    std::string recentProjectsFilePath_ = "arestrix-recent-projects.json";
};

} // namespace project
} // namespace taskforge
