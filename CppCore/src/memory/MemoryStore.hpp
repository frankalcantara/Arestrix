#pragma once

// Arestrix Memory Graph - armazenamento SQLite do multigrafo temporal.
//
// Esta é a primeira camada da Fase 1: escopos, nós, arestas temporais, episódios,
// evidências, memória de trabalho, lições e métricas de uso de API.
// A API pública é usada por MemoryService; os motores legados (AgentBlackboard e
// SemanticMemoryEngine) são mantidos apenas para migração/compatibilidade.

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/JsonValue.hpp"

struct sqlite3;

namespace taskforge {
namespace memory {

struct MemoryLesson {
    std::string id;
    std::string projectId;
    std::string domain;
    std::string triggerError;
    std::string lessonRule;
    std::string sourceItem;
    std::string createdAt;
    double confidence = 0.5;
};

struct MemoryBriefInfo {
    std::vector<std::string> lessonIds;
    std::vector<std::string> workingKeys;
    size_t chars = 0;
    size_t estimatedTokens = 0;
    bool truncated = false;
};

struct MemoryHitRecord {
    std::string id;
    std::string runId;
    std::string nodeId;
    std::string memoryId;
    std::string kind;
    bool injected = false;
    bool used = false;
    std::string outcome;
    std::string createdAt;
};

struct MemoryEdgeRecord {
    std::string id;
    std::string scopeId;
    std::string srcId;
    std::string dstId;
    std::string relation;
    std::string valueJson;
    std::string validFrom;
    std::string validTo;
    std::string recordedAt;
    double confidence = 0.5;
    std::string status;
    std::string supersededBy;
    std::string sourceKind;
    std::string sourceRef;
    std::string metadata;
};

struct MemoryUsageRecord {
    std::string id;
    std::string scopeId;
    std::string runId;
    std::string nodeId;
    std::string provider;
    std::string model;
    long long promptTokens = 0;
    long long completionTokens = 0;
    long long totalTokens = 0;
    long long calls = 0;
    long long latencyMs = 0;
    long long memoryContextChars = 0;
    long long memoryEnabled = 0;
    long long retries = 0;
    std::string outcome;   // success | failure
    std::string createdAt;
};

class MemoryStore {
public:
    explicit MemoryStore(std::string path);
    ~MemoryStore();

    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;

    bool open(std::string& error);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }

    // Escopos. O closure inclui o próprio escopo com depth 0.
    bool ensureScope(const std::string& scopeId, const std::string& kind,
                     const std::string& parentId, const std::string& name);
    std::vector<std::string> readScopes(const std::string& scopeId) const;

    // Memória de trabalho (Blackboard) no banco.
    bool setWorking(const std::string& scopeId, const std::string& itemId,
                    const std::string& key, const taskforge::Json& value);
    taskforge::Json getWorking(const std::string& scopeId, const std::string& itemId,
                               const std::string& key) const;
    bool hasWorking(const std::string& scopeId, const std::string& itemId,
                    const std::string& key) const;
    taskforge::Json workingSnapshot(const std::string& scopeId, const std::string& itemId,
                                    const std::vector<std::string>& allowedKeys = {}) const;
    taskforge::Json workingToJson(const std::string& scopeId) const;
    bool ingestWorkingDelta(const std::string& scopeId, const std::string& itemId,
                            const taskforge::Json& delta);
    void clearWorkingItem(const std::string& scopeId, const std::string& itemId);
    void clearScopeData(const std::string& scopeId);

    // Lições semânticas como nós/arestas do multigrafo.
    std::string recordLesson(const std::string& scopeId, const std::string& domain,
                             const std::string& triggerError, const std::string& lessonRule,
                             const std::string& sourceItem = "",
                             const std::string& sourceKind = "agent");
    std::vector<MemoryLesson> getLessons(const std::string& scopeId,
                                         const std::string& domain = "") const;
    std::string formatLessons(const std::string& scopeId,
                              const std::vector<std::string>& domains = {}) const;
    void clearLessons(const std::string& scopeId);

    // Episódios, nós e arestas genéricas (multigrafo temporal).
    std::string recordEpisode(const std::string& scopeId, const std::string& runId,
                              const std::string& nodeId, const std::string& itemId,
                              const std::string& kind, const std::string& content,
                              const taskforge::Json& metadata = taskforge::Json::object());
    std::string ensureNode(const std::string& scopeId, const std::string& nodeId,
                           const std::string& kind, const std::string& canonicalName,
                           const taskforge::Json& metadata = taskforge::Json::object());
    std::string recordEdge(const std::string& scopeId, const std::string& srcId,
                           const std::string& dstId, const std::string& relation,
                           const taskforge::Json& value = taskforge::Json(),
                           const std::string& validFrom = "", const std::string& validTo = "",
                           double confidence = 0.5, const std::string& status = "active",
                           const std::string& sourceKind = "agent",
                           const std::string& sourceRef = "",
                           const taskforge::Json& metadata = taskforge::Json::object());
    bool invalidateEdge(const std::string& edgeId, const std::string& validTo,
                        const std::string& reason, const std::string& actor = "runtime");
    std::vector<MemoryEdgeRecord> neighbors(const std::string& scopeId,
                                            const std::string& nodeId, int depth = 1,
                                            const std::string& asOf = "") const;
    std::vector<MemoryEdgeRecord> searchEdges(const std::string& scopeId,
                                              const std::string& text,
                                              size_t limit = 20) const;
    std::string searchContext(const std::string& scopeId, const std::string& text,
                              size_t limit = 10,
                              std::vector<std::string>* hitIds = nullptr) const;

    // Rastreio de memórias injetadas/consultadas e resultado da execução.
    bool recordHit(const std::string& scopeId, const std::string& runId,
                   const std::string& nodeId, const std::string& memoryId,
                   const std::string& kind, bool injected, bool used,
                   const std::string& outcome = "");
    int markHitsUsed(const std::string& scopeId, const std::string& runId,
                     const std::string& nodeId, const std::vector<std::string>& memoryIds);
    bool finishHits(const std::string& scopeId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome);
    taskforge::Json hitSummary(const std::string& scopeId) const;

    // Métricas de consumo de API (tokens, chamadas e contexto de memória).
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& scopeId) const;

    // Estado completo para UI/export.
    taskforge::Json toJson(const std::string& scopeId) const;

    bool hasMeta(const std::string& key) const;
    std::string getMeta(const std::string& key) const;
    bool setMeta(const std::string& key, const std::string& value);

private:
    bool exec(const std::string& sql, std::string* error = nullptr);
    bool createSchema(std::string& error);
    std::string nowIso() const;
    std::string newId(const std::string& prefix) const;
    std::string edgeHash(const std::string& scopeId, const std::string& srcId,
                         const std::string& dstId, const std::string& relation,
                         const std::string& valueJson, const std::string& validFrom,
                         const std::string& validTo) const;
    void audit(const std::string& actor, const std::string& action,
               const std::string& target, const std::string& before,
               const std::string& after);

    mutable std::recursive_mutex mutex_;
    sqlite3* db_ = nullptr;
    std::string path_;
    mutable std::string lastError_;
};

} // namespace memory
} // namespace taskforge
