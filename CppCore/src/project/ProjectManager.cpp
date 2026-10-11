#include "ProjectManager.hpp"
#include "../memory/AgentBlackboard.hpp"
#include "../memory/SemanticMemoryEngine.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace taskforge {
namespace project {

namespace {

std::string getCurrentIsoTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

} // namespace

ProjectManager& ProjectManager::getInstance() {
    static ProjectManager instance;
    return instance;
}

ProjectManager::ProjectManager() {
    loadRecentProjectsFromDisk();
    ensureDefaultProject();
}

void ProjectManager::ensureDefaultProject() {
    if (currentProject_.projectId.empty()) {
        currentProject_.projectId = "default-workspace";
        currentProject_.name = "Workspace Padrão";
        currentProject_.version = "1.0.0";
        currentProject_.description = "Workspace nativo inicial do Arestrix Studio";
        currentProject_.createdAt = getCurrentIsoTime();
        currentProject_.updatedAt = currentProject_.createdAt;
        currentProject_.rootPath = std::filesystem::current_path().string();

        ProjectPipelineRef mainPipe;
        mainPipe.id = "main";
        mainPipe.name = "Pipeline Principal";
        mainPipe.file = "pipelines/main.json";
        mainPipe.isPrimary = true;
        currentProject_.pipelines.push_back(mainPipe);
    }
}

bool ProjectManager::createProject(ProjectManifest manifest) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (manifest.projectId.empty()) {
        manifest.projectId = "projeto_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }

    if (manifest.rootPath.empty()) {
        std::filesystem::path defaultDir = std::filesystem::current_path() / "projects" / manifest.projectId;
        manifest.rootPath = defaultDir.string();
    }

    std::filesystem::path root(manifest.rootPath);
    try {
        std::filesystem::create_directories(root / "pipelines");
        std::filesystem::create_directories(root / "storage");
        std::filesystem::create_directories(root / "output");
    } catch (const std::exception& e) {
        std::cerr << "[ProjectManager] Falha ao criar pastas do projeto: " << e.what() << "\n";
        return false;
    }

    std::string nowStr = getCurrentIsoTime();
    if (manifest.createdAt.empty()) manifest.createdAt = nowStr;
    manifest.updatedAt = nowStr;

    if (manifest.pipelines.empty()) {
        ProjectPipelineRef mainPipe;
        mainPipe.id = "main";
        mainPipe.name = "Pipeline Principal";
        mainPipe.file = "pipelines/main.json";
        mainPipe.isPrimary = true;
        mainPipe.description = "Pipeline central do projeto";
        manifest.pipelines.push_back(mainPipe);

        // Cria o arquivo inicial de pipeline vazio se nao existir
        std::filesystem::path mainFile = root / "pipelines" / "main.json";
        if (!std::filesystem::exists(mainFile)) {
            nlohmann::json emptyPipeline = {
                {"version", "1.0.0"},
                {"metadata", {
                    {"graphName", manifest.name},
                    {"projectId", manifest.projectId},
                    {"description", manifest.description},
                    {"targetStandard", "C++20"}
                }},
                {"nodes", nlohmann::json::array()},
                {"edges", nlohmann::json::array()}
            };
            std::ofstream f(mainFile);
            if (f.is_open()) f << emptyPipeline.dump(2);
        }
    }

    // Grava o manifesto arestrix.project.json
    std::filesystem::path manifestPath = root / "arestrix.project.json";
    std::ofstream mf(manifestPath);
    if (!mf.is_open()) return false;
    nlohmann::json j = manifest;
    mf << j.dump(2);
    mf.close();

    currentProject_ = manifest;

    // Registra nos recentes
    RecentProjectItem item;
    item.projectId = manifest.projectId;
    item.name = manifest.name;
    item.rootPath = manifest.rootPath;
    item.manifestPath = manifestPath.string();
    item.lastOpened = nowStr;
    addRecentProject(item);

    // Sincroniza escopo nos motores de memória
    taskforge::memory::AgentBlackboard::getInstance().clearSession(manifest.projectId);

    std::cout << "[ProjectManager] Projeto '" << manifest.name << "' (" << manifest.projectId << ") criado em: " << manifest.rootPath << "\n";
    return true;
}

bool ProjectManager::loadProject(const std::string& pathOrDir) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::filesystem::path target(pathOrDir);
    std::filesystem::path manifestPath = target;
    if (std::filesystem::is_directory(target)) {
        manifestPath = target / "arestrix.project.json";
        if (!std::filesystem::exists(manifestPath)) {
            manifestPath = target / "taskforge.project.json";
        }
    }

    if (!std::filesystem::exists(manifestPath)) {
        std::filesystem::path inProjects = std::filesystem::current_path() / "projects" / pathOrDir;
        if (std::filesystem::is_directory(inProjects)) {
            if (std::filesystem::exists(inProjects / "arestrix.project.json")) {
                manifestPath = inProjects / "arestrix.project.json";
            } else if (std::filesystem::exists(inProjects / "taskforge.project.json")) {
                manifestPath = inProjects / "taskforge.project.json";
            }
        }
    }

    if (!std::filesystem::exists(manifestPath)) {
        std::cerr << "[ProjectManager] Arquivo de manifesto nao encontrado: " << manifestPath.string() << "\n";
        return false;
    }

    std::ifstream f(manifestPath);
    if (!f.is_open()) return false;

    try {
        nlohmann::json j;
        f >> j;
        ProjectManifest manifest = j.get<ProjectManifest>();
        manifest.rootPath = manifestPath.parent_path().string();
        manifest.updatedAt = getCurrentIsoTime();

        currentProject_ = manifest;

        // Atualiza recentes
        RecentProjectItem item;
        item.projectId = manifest.projectId;
        item.name = manifest.name;
        item.rootPath = manifest.rootPath;
        item.manifestPath = manifestPath.string();
        item.lastOpened = manifest.updatedAt;
        addRecentProject(item);

        std::cout << "[ProjectManager] Projeto '" << manifest.name << "' carregado com sucesso.\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ProjectManager] Erro ao ler manifesto do projeto: " << e.what() << "\n";
    }
    return false;
}

bool ProjectManager::saveProject(const ProjectManifest& manifest) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentProject_ = manifest;
    currentProject_.updatedAt = getCurrentIsoTime();

    std::filesystem::path root(currentProject_.rootPath);
    std::filesystem::path manifestPath = root / "arestrix.project.json";

    std::ofstream mf(manifestPath);
    if (!mf.is_open()) return false;
    nlohmann::json j = currentProject_;
    mf << j.dump(2);
    mf.close();

    saveRecentProjectsToDisk();
    return true;
}

ProjectManifest ProjectManager::getCurrentProject() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProject_;
}

bool ProjectManager::hasActiveProject() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !currentProject_.projectId.empty();
}

std::vector<RecentProjectItem> ProjectManager::listRecentProjects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recentProjects_;
}

bool ProjectManager::addRecentProject(const RecentProjectItem& item) {
    // Remove duplicatas se ja existir
    for (auto it = recentProjects_.begin(); it != recentProjects_.end(); ++it) {
        if (it->projectId == item.projectId || it->manifestPath == item.manifestPath) {
            recentProjects_.erase(it);
            break;
        }
    }
    recentProjects_.insert(recentProjects_.begin(), item);
    if (recentProjects_.size() > 15) {
        recentProjects_.resize(15);
    }
    saveRecentProjectsToDisk();
    return true;
}

bool ProjectManager::removeRecentProject(const std::string& projectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = recentProjects_.begin(); it != recentProjects_.end(); ++it) {
        if (it->projectId == projectId) {
            recentProjects_.erase(it);
            saveRecentProjectsToDisk();
            return true;
        }
    }
    return false;
}

std::string ProjectManager::getActiveProjectRoot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentProject_.rootPath;
}

std::string ProjectManager::resolveProjectPath(const std::string& relativePath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::path root(currentProject_.rootPath);
    return (root / relativePath).lexically_normal().string();
}

void ProjectManager::loadRecentProjectsFromDisk() {
    std::ifstream f(recentProjectsFilePath_);
    if (!f.is_open()) {
        f.open("taskforge-recent-projects.json");
        if (!f.is_open()) return;
    }

    try {
        nlohmann::json j;
        f >> j;
        if (j.is_array()) {
            recentProjects_ = j.get<std::vector<RecentProjectItem>>();
        }
    } catch (...) {}
}

void ProjectManager::saveRecentProjectsToDisk() const {
    try {
        nlohmann::json j = recentProjects_;
        std::ofstream f(recentProjectsFilePath_);
        if (f.is_open()) {
            f << j.dump(2);
        }
    } catch (...) {}
}

} // namespace project
} // namespace taskforge
