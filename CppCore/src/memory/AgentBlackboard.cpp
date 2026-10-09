#include "AgentBlackboard.hpp"
#include <iostream>

namespace taskforge {
namespace memory {

AgentBlackboard& AgentBlackboard::getInstance() {
    static AgentBlackboard instance;
    return instance;
}

void AgentBlackboard::setPersistence(const std::string& projectId, bool persistent) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    persistenceMap_[projectId] = persistent;
    if (persistent) {
        // Se ativado como persistente, tenta carregar dados pré-existentes do disco
        std::string path = getDefaultStoragePath(projectId);
        if (std::filesystem::exists(path)) {
            lock.unlock();
            loadFromDisk(projectId, path);
        }
    }
}

bool AgentBlackboard::isPersistent(const std::string& projectId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = persistenceMap_.find(projectId);
    if (it != persistenceMap_.end()) {
        return it->second;
    }
    return false; // Por padrão, NÃO é persistente (efêmera)
}

std::string AgentBlackboard::getDefaultStoragePath(const std::string& projectId) const {
    std::string safeName = projectId.empty() ? "default" : projectId;
    for (char& c : safeName) {
        if (!isalnum(c) && c != '-' && c != '_') c = '_';
    }
    return "arestrix-blackboard-" + safeName + ".json";
}

void AgentBlackboard::set(const std::string& projectId, const std::string& itemId, const std::string& key, const nlohmann::json& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    storage_[projectId][itemId][key] = value;
}

nlohmann::json AgentBlackboard::get(const std::string& projectId, const std::string& itemId, const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto itProj = storage_.find(projectId);
    if (itProj != storage_.end()) {
        auto itItem = itProj->second.find(itemId);
        if (itItem != itProj->second.end()) {
            auto itKey = itItem->second.find(key);
            if (itKey != itItem->second.end()) {
                return itKey->second;
            }
        }
    }
    return nullptr;
}

bool AgentBlackboard::has(const std::string& projectId, const std::string& itemId, const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto itProj = storage_.find(projectId);
    if (itProj != storage_.end()) {
        auto itItem = itProj->second.find(itemId);
        if (itItem != itProj->second.end()) {
            return itItem->second.find(key) != itItem->second.end();
        }
    }
    return false;
}

nlohmann::json AgentBlackboard::exportSnapshot(
    const std::string& projectId,
    const std::string& itemId,
    const std::vector<std::string>& allowedKeys
) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    nlohmann::json snapshot = nlohmann::json::object();

    auto itProj = storage_.find(projectId);
    if (itProj == storage_.end()) {
        return snapshot;
    }

    auto itItem = itProj->second.find(itemId);
    if (itItem == itProj->second.end()) {
        return snapshot;
    }

    if (allowedKeys.empty()) {
        // Exporta todas as chaves do item
        for (const auto& pair : itItem->second) {
            snapshot[pair.first] = pair.second;
        }
    } else {
        // Exporta somente as chaves autorizadas
        for (const auto& k : allowedKeys) {
            auto itKey = itItem->second.find(k);
            if (itKey != itItem->second.end()) {
                snapshot[k] = itKey->second;
            }
        }
    }

    return snapshot;
}

bool AgentBlackboard::writeSnapshotToFile(
    const std::string& projectId,
    const std::string& itemId, 
    const std::vector<std::string>& allowedKeys, 
    const std::string& targetPath
) const {
    try {
        nlohmann::json snap = exportSnapshot(projectId, itemId, allowedKeys);
        std::filesystem::path path(targetPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream out(targetPath);
        if (!out.is_open()) {
            return false;
        }
        out << snap.dump(2);
        out.close();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[AgentBlackboard Erro] Falha ao gravar snapshot: " << ex.what() << "\n";
        return false;
    }
}

void AgentBlackboard::ingestDelta(const std::string& projectId, const std::string& itemId, const nlohmann::json& deltaJson) {
    if (!deltaJson.is_object()) {
        return;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto it = deltaJson.begin(); it != deltaJson.end(); ++it) {
        storage_[projectId][itemId][it.key()] = it.value();
    }
}

void AgentBlackboard::clearItem(const std::string& projectId, const std::string& itemId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto itProj = storage_.find(projectId);
    if (itProj != storage_.end()) {
        itProj->second.erase(itemId);
    }
}

void AgentBlackboard::clearProject(const std::string& projectId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    storage_.erase(projectId);
}

void AgentBlackboard::clearSession(const std::string& projectId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto itPersist = persistenceMap_.find(projectId);
    bool persistent = (itPersist != persistenceMap_.end() && itPersist->second);
    
    // Se o projeto for efêmero (não persistente), descarta toda a memória da sessão
    if (!persistent) {
        storage_.erase(projectId);
    }
}

void AgentBlackboard::clearAll() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    storage_.clear();
}

bool AgentBlackboard::saveToDisk(const std::string& projectId, const std::string& customPath) {
    if (!isPersistent(projectId)) {
        // Memória efêmera: não grava em disco
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto itProj = storage_.find(projectId);
    if (itProj == storage_.end()) {
        return false;
    }

    std::string path = customPath.empty() ? getDefaultStoragePath(projectId) : customPath;
    try {
        std::string tmpPath = path + ".tmp";
        std::ofstream out(tmpPath);
        if (!out.is_open()) {
            return false;
        }

        nlohmann::json root = nlohmann::json::object();
        for (const auto& itemPair : itProj->second) {
            nlohmann::json itemObj = nlohmann::json::object();
            for (const auto& kvPair : itemPair.second) {
                itemObj[kvPair.first] = kvPair.second;
            }
            root[itemPair.first] = itemObj;
        }

        out << root.dump(2);
        out.close();

        std::filesystem::rename(tmpPath, path);
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[AgentBlackboard Erro] Falha ao persistir no disco: " << ex.what() << "\n";
        return false;
    }
}

bool AgentBlackboard::loadFromDisk(const std::string& projectId, const std::string& customPath) {
    std::string path = customPath.empty() ? getDefaultStoragePath(projectId) : customPath;
    if (!std::filesystem::exists(path)) {
        if (customPath.empty()) {
            std::string legacyPath = "taskforge-blackboard-" + (projectId.empty() ? "default" : projectId) + ".json";
            if (std::filesystem::exists(legacyPath)) {
                path = legacyPath;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    try {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }

        nlohmann::json root;
        in >> root;
        in.close();

        if (!root.is_object()) {
            return false;
        }

        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto& projStorage = storage_[projectId];
        for (auto itItem = root.begin(); itItem != root.end(); ++itItem) {
            if (itItem.value().is_object()) {
                for (auto itKey = itItem.value().begin(); itKey != itItem.value().end(); ++itKey) {
                    projStorage[itItem.key()][itKey.key()] = itKey.value();
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

nlohmann::json AgentBlackboard::toJson(const std::string& projectId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    nlohmann::json result = nlohmann::json::object();

    if (!projectId.empty()) {
        auto itProj = storage_.find(projectId);
        if (itProj != storage_.end()) {
            for (const auto& itemPair : itProj->second) {
                nlohmann::json itemObj = nlohmann::json::object();
                for (const auto& kvPair : itemPair.second) {
                    itemObj[kvPair.first] = kvPair.second;
                }
                result[itemPair.first] = itemObj;
            }
        }
        return result;
    }

    // Todos os projetos
    for (const auto& projPair : storage_) {
        nlohmann::json projObj = nlohmann::json::object();
        for (const auto& itemPair : projPair.second) {
            nlohmann::json itemObj = nlohmann::json::object();
            for (const auto& kvPair : itemPair.second) {
                itemObj[kvPair.first] = kvPair.second;
            }
            projObj[itemPair.first] = itemObj;
        }
        result[projPair.first] = projObj;
    }
    return result;
}

} // namespace memory
} // namespace taskforge
