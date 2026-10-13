#include "SemanticMemoryEngine.hpp"
#include "util/Json.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace taskforge {
namespace memory {

SemanticMemoryEngine::SemanticMemoryEngine() {
    // Inicialização sob demanda
}

SemanticMemoryEngine& SemanticMemoryEngine::getInstance() {
    static SemanticMemoryEngine instance;
    return instance;
}

std::string SemanticMemoryEngine::getDefaultStoragePath(const std::string& projectId) const {
    std::string safeName = projectId.empty() ? "default" : projectId;
    for (char& c : safeName) {
        if (!isalnum(c) && c != '-' && c != '_') c = '_';
    }
    return "arestrix-memory-" + safeName + ".json";
}

void SemanticMemoryEngine::setPersistence(const std::string& projectId, bool persistent) {
    std::lock_guard<std::mutex> lock(mutex_);
    persistenceMap_[projectId] = persistent;
}

bool SemanticMemoryEngine::isPersistent(const std::string& projectId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = persistenceMap_.find(projectId);
    if (it != persistenceMap_.end()) {
        return it->second;
    }
    return false; // Por padrão, NÃO é persistente (efêmera)
}

std::string SemanticMemoryEngine::getIsoTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string SemanticMemoryEngine::recordLesson(
    const std::string& projectId,
    const std::string& domain,
    const std::string& triggerError,
    const std::string& lessonRule,
    const std::string& sourceItem
) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;

    // Evita duplicatas idênticas no mesmo projeto e domínio
    for (const auto& existing : lessons_) {
        if (existing.projectId == proj && existing.domain == domain && existing.lessonRule == lessonRule) {
            return existing.id;
        }
    }

    AgentLesson lesson;
    lesson.id = "lsn_" + std::to_string(lessons_.size() + 1);
    lesson.projectId = proj;
    lesson.domain = domain.empty() ? "geral" : domain;
    lesson.triggerError = triggerError;
    lesson.lessonRule = lessonRule;
    lesson.sourceItem = sourceItem;
    lesson.createdAt = getIsoTimestamp();

    lessons_.push_back(lesson);

    // Se o projeto for persistente, salva em disco
    auto itPersist = persistenceMap_.find(proj);
    if (itPersist != persistenceMap_.end() && itPersist->second) {
        // Persistência ativa
        std::string path = getDefaultStoragePath(proj);
        try {
            std::string tmpPath = path + ".tmp";
            std::ofstream out(tmpPath);
            if (out.is_open()) {
                taskforge::Json root = taskforge::Json::object();
                taskforge::Json arr = taskforge::Json::array();
                for (const auto& l : lessons_) {
                    if (l.projectId == proj) {
                        taskforge::Json item;
                        item["id"] = l.id;
                        item["projectId"] = l.projectId;
                        item["domain"] = l.domain;
                        item["triggerError"] = l.triggerError;
                        item["lessonRule"] = l.lessonRule;
                        item["sourceItem"] = l.sourceItem;
                        item["createdAt"] = l.createdAt;
                        arr.push_back(item);
                    }
                }
                root["lessons"] = arr;
                root["count"] = arr.size();
                out << root.dump(2);
                out.close();
                std::filesystem::rename(tmpPath, path);
            }
        } catch (...) {}
    }

    return lesson.id;
}

std::vector<AgentLesson> SemanticMemoryEngine::getLessons(const std::string& projectId, const std::string& domain) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;
    std::vector<AgentLesson> filtered;

    for (const auto& lsn : lessons_) {
        if (lsn.projectId == proj) {
            if (domain.empty() || lsn.domain == domain) {
                filtered.push_back(lsn);
            }
        }
    }
    return filtered;
}

std::string SemanticMemoryEngine::formatPromptContext(const std::string& projectId, const std::vector<std::string>& domains) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;

    std::vector<AgentLesson> matched;
    for (const auto& lsn : lessons_) {
        if (lsn.projectId == proj) {
            if (domains.empty()) {
                matched.push_back(lsn);
            } else {
                for (const auto& d : domains) {
                    if (lsn.domain == d) {
                        matched.push_back(lsn);
                        break;
                    }
                }
            }
        }
    }

    if (matched.empty()) {
        return "";
    }

    std::stringstream ss;
    ss << "\n--- [ARESTRIX MEMÓRIA SEMÂNTICA: LIÇÕES APRENDIDAS - PROJETO: " << proj << "] ---\n";
    ss << "Atenção: Os itens anteriores identificaram as seguintes diretrizes para prevenção de erros:\n";
    for (size_t i = 0; i < matched.size(); ++i) {
        ss << "[" << (i + 1) << "] (Domínio: " << matched[i].domain << ") " << matched[i].lessonRule << "\n";
    }
    ss << "--------------------------------------------------------------------\n\n";

    return ss.str();
}

void SemanticMemoryEngine::clearLessons(const std::string& projectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;

    std::vector<AgentLesson> remaining;
    for (const auto& lsn : lessons_) {
        if (lsn.projectId != proj) {
            remaining.push_back(lsn);
        }
    }
    lessons_ = std::move(remaining);

    // Se arquivo persistente existir, remove ou atualiza
    std::string path = getDefaultStoragePath(proj);
    if (std::filesystem::exists(path)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

void SemanticMemoryEngine::clearSession(const std::string& projectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;
    auto itPersist = persistenceMap_.find(proj);
    bool persistent = (itPersist != persistenceMap_.end() && itPersist->second);

    // Se não for persistente (efêmero), limpa lições da sessão
    if (!persistent) {
        std::vector<AgentLesson> remaining;
        for (const auto& lsn : lessons_) {
            if (lsn.projectId != proj) {
                remaining.push_back(lsn);
            }
        }
        lessons_ = std::move(remaining);
    }
}

bool SemanticMemoryEngine::saveToDisk(const std::string& projectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;

    if (!persistenceMap_[proj]) {
        // Projeto configurado como efêmero: não grava em disco
        return false;
    }

    std::string path = getDefaultStoragePath(proj);
    try {
        std::string tmpPath = path + ".tmp";
        std::ofstream out(tmpPath);
        if (!out.is_open()) {
            return false;
        }

        taskforge::Json root = taskforge::Json::object();
        taskforge::Json arr = taskforge::Json::array();
        for (const auto& l : lessons_) {
            if (l.projectId == proj) {
                taskforge::Json item;
                item["id"] = l.id;
                item["projectId"] = l.projectId;
                item["domain"] = l.domain;
                item["triggerError"] = l.triggerError;
                item["lessonRule"] = l.lessonRule;
                item["sourceItem"] = l.sourceItem;
                item["createdAt"] = l.createdAt;
                arr.push_back(item);
            }
        }
        root["lessons"] = arr;
        root["count"] = arr.size();

        out << root.dump(2);
        out.close();

        std::filesystem::rename(tmpPath, path);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[SemanticMemoryEngine Erro] Falha ao persistir: " << ex.what() << "\n";
        return false;
    }
}

bool SemanticMemoryEngine::loadFromDisk(const std::string& projectId) {
    std::string proj = projectId.empty() ? "default" : projectId;
    std::string path = getDefaultStoragePath(proj);

    if (!std::filesystem::exists(path)) {
        std::string legacyPath = "taskforge-memory-" + proj + ".json";
        if (std::filesystem::exists(legacyPath)) {
            path = legacyPath;
        } else {
            return false;
        }
    }

    try {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }

        taskforge::Json j = taskforge::jsonutil::parseStream(in);
        in.close();

        fromJson(j, proj);
        return true;
    } catch (...) {
        return false;
    }
}

taskforge::Json SemanticMemoryEngine::toJson(const std::string& projectId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    taskforge::Json root = taskforge::Json::object();
    taskforge::Json arr = taskforge::Json::array();

    for (const auto& l : lessons_) {
        if (projectId.empty() || l.projectId == projectId) {
            taskforge::Json item;
            item["id"] = l.id;
            item["projectId"] = l.projectId;
            item["domain"] = l.domain;
            item["triggerError"] = l.triggerError;
            item["lessonRule"] = l.lessonRule;
            item["sourceItem"] = l.sourceItem;
            item["createdAt"] = l.createdAt;
            arr.push_back(item);
        }
    }
    root["lessons"] = arr;
    root["count"] = arr.size();
    return root;
}

void SemanticMemoryEngine::fromJson(const taskforge::Json& j, const std::string& projectId) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string proj = projectId.empty() ? "default" : projectId;

    if (!j.is_object() || !j.contains("lessons") || !j["lessons"].is_array()) {
        return;
    }

    // Remove lições antigas do mesmo projeto antes de carregar
    std::vector<AgentLesson> kept;
    for (const auto& l : lessons_) {
        if (l.projectId != proj) {
            kept.push_back(l);
        }
    }
    lessons_ = std::move(kept);

    for (const auto& item : j["lessons"]) {
        AgentLesson l;
        l.id = item.value("id", "");
        l.projectId = item.value("projectId", proj);
        l.domain = item.value("domain", "geral");
        l.triggerError = item.value("triggerError", "");
        l.lessonRule = item.value("lessonRule", "");
        l.sourceItem = item.value("sourceItem", "");
        l.createdAt = item.value("createdAt", "");
        lessons_.push_back(l);
    }
}

} // namespace memory
} // namespace taskforge
