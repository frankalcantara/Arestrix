#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <filesystem>
#include <fstream>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

class AgentBlackboard {
public:
    static AgentBlackboard& getInstance();

    // Controle de Persistência por Projeto
    void setPersistence(const std::string& projectId, bool persistent);
    bool isPersistent(const std::string& projectId) const;

    // Métodos com escopo explícito por projeto
    void set(const std::string& projectId, const std::string& itemId, const std::string& key, const taskforge::Json& value);
    taskforge::Json get(const std::string& projectId, const std::string& itemId, const std::string& key) const;
    bool has(const std::string& projectId, const std::string& itemId, const std::string& key) const;
    taskforge::Json exportSnapshot(const std::string& projectId, const std::string& itemId, const std::vector<std::string>& allowedKeys = {}) const;
    bool writeSnapshotToFile(const std::string& projectId, const std::string& itemId, const std::vector<std::string>& allowedKeys, const std::string& targetPath) const;
    void ingestDelta(const std::string& projectId, const std::string& itemId, const taskforge::Json& deltaJson);
    void clearItem(const std::string& projectId, const std::string& itemId);
    void clearProject(const std::string& projectId);
    
    // Limpeza de sessão: descarta dados apenas se o projeto for efêmero (!persistent)
    void clearSession(const std::string& projectId);

    // Persistência em disco (apenas executada se isPersistent(projectId) == true)
    bool saveToDisk(const std::string& projectId, const std::string& customPath = "");
    bool loadFromDisk(const std::string& projectId, const std::string& customPath = "");

    // Sobrecargas de compatibilidade (escopo default)
    void set(const std::string& itemId, const std::string& key, const taskforge::Json& value) {
        set("default", itemId, key, value);
    }
    taskforge::Json get(const std::string& itemId, const std::string& key) const {
        return get("default", itemId, key);
    }
    bool has(const std::string& itemId, const std::string& key) const {
        return has("default", itemId, key);
    }
    taskforge::Json exportSnapshot(const std::string& itemId, const std::vector<std::string>& allowedKeys = {}) const {
        return exportSnapshot("default", itemId, allowedKeys);
    }
    bool writeSnapshotToFile(const std::string& itemId, const std::vector<std::string>& allowedKeys, const std::string& targetPath) const {
        return writeSnapshotToFile("default", itemId, allowedKeys, targetPath);
    }
    void ingestDelta(const std::string& itemId, const taskforge::Json& deltaJson) {
        ingestDelta("default", itemId, deltaJson);
    }
    void clearItem(const std::string& itemId) {
        clearItem("default", itemId);
    }

    // Limpa toda a memória de trabalho de todos os projetos
    void clearAll();

    // Retorna todo o estado do blackboard em JSON (para telemetria e UI)
    taskforge::Json toJson(const std::string& projectId = "") const;

private:
    AgentBlackboard() = default;
    ~AgentBlackboard() = default;
    AgentBlackboard(const AgentBlackboard&) = delete;
    AgentBlackboard& operator=(const AgentBlackboard&) = delete;

    std::string getDefaultStoragePath(const std::string& projectId) const;

    mutable std::shared_mutex mutex_;
    // projectId -> (itemId -> (key -> json value))
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, taskforge::Json>>> storage_;
    // projectId -> persistent flag (default: false, memória efêmera / não obrigatória)
    std::unordered_map<std::string, bool> persistenceMap_;
};

} // namespace memory
} // namespace taskforge
