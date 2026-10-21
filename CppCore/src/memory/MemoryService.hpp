#pragma once

// Fachada de memória do Arestrix sobre o MemoryStore SQLite.
//
// A Fase 0 usava AgentBlackboard/SemanticMemoryEngine com JSON. A Fase 1
// centraliza tudo no banco e mantém este serviço como contrato para o gerador,
// o runtime, a UI e os testes.

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

class MemoryService {
public:
    static MemoryService& getInstance();

    // Abre (ou reabre) o store do projeto. persistent=false usa SQLite :memory:.
    // dbPath vazio usa ARESTRIX_MEMORY_DB ou .arestrix/memory/memory.db.
    bool configure(const std::string& projectId, bool persistent,
                   const std::string& dbPath = "",
                   const std::string& legacyBlackboardPath = "",
                   const std::string& legacyLessonsPath = "");
    bool isPersistent(const std::string& projectId) const;
    MemoryStore* store(const std::string& projectId);
    std::string databasePath(const std::string& projectId) const;

    // Memória de trabalho.
    bool setWorking(const std::string& projectId, const std::string& itemId,
                    const std::string& key, const taskforge::Json& value);
    taskforge::Json getWorking(const std::string& projectId, const std::string& itemId,
                               const std::string& key);
    taskforge::Json workingSnapshot(const std::string& projectId, const std::string& itemId,
                                    const std::vector<std::string>& allowedKeys = {});
    bool ingestWorkingDelta(const std::string& projectId, const std::string& itemId,
                            const taskforge::Json& delta);
    void clearWorkingItem(const std::string& projectId, const std::string& itemId);
    void clearProject(const std::string& projectId);

    // Lições semânticas.
    std::string recordLesson(const std::string& projectId, const std::string& domain,
                             const std::string& triggerError, const std::string& lessonRule,
                             const std::string& sourceItem = "",
                             const std::string& sourceKind = "agent");
    std::vector<MemoryLesson> getLessons(const std::string& projectId,
                                         const std::string& domain = "");
    std::string formatLessons(const std::string& projectId,
                              const std::vector<std::string>& domains = {});
    std::string buildBrief(const std::string& projectId, const std::string& itemId,
                           const std::vector<std::string>& domains,
                           const std::vector<std::string>& allowedKeys,
                           size_t maxTokens = 800,
                           bool includeLessons = true,
                           bool includeBlackboard = true);
    void clearLessons(const std::string& projectId);

    // Episódios, arestas e métricas.
    std::string recordEpisode(const std::string& projectId, const std::string& runId,
                              const std::string& nodeId, const std::string& itemId,
                              const std::string& kind, const std::string& content,
                              const taskforge::Json& metadata = taskforge::Json::object());
    std::string recordEdge(const std::string& projectId, const std::string& srcId,
                           const std::string& dstId, const std::string& relation,
                           const taskforge::Json& value = taskforge::Json(),
                           const std::string& validFrom = "", const std::string& validTo = "",
                           double confidence = 0.5, const std::string& status = "active",
                           const std::string& sourceKind = "agent",
                           const std::string& sourceRef = "",
                           const taskforge::Json& metadata = taskforge::Json::object());
    std::vector<MemoryEdgeRecord> neighbors(const std::string& projectId,
                                            const std::string& nodeId, int depth = 1,
                                            const std::string& asOf = "");
    std::vector<MemoryEdgeRecord> searchEdges(const std::string& projectId,
                                              const std::string& text, size_t limit = 20);
    std::string searchContext(const std::string& projectId, const std::string& text,
                              size_t limit = 10);
    bool invalidateEdge(const std::string& projectId, const std::string& edgeId,
                        const std::string& validTo, const std::string& reason,
                        const std::string& actor = "runtime");
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& projectId);

    // Estado para UI/export.
    taskforge::Json toJson(const std::string& projectId);
    bool clearSession(const std::string& projectId);
    void closeProject(const std::string& projectId);

    // Migração dos arquivos JSON da Fase 0.
    bool importLegacy(const std::string& projectId,
                      const std::string& blackboardPath = "",
                      const std::string& lessonsPath = "");

private:
    MemoryService() = default;
    ~MemoryService() = default;
    MemoryService(const MemoryService&) = delete;
    MemoryService& operator=(const MemoryService&) = delete;

    MemoryStore* ensureStore(const std::string& projectId);
    std::string scopeId(const std::string& projectId) const;
    std::string defaultPath(const std::string& projectId) const;
    static std::string safeName(const std::string& projectId);

    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::unique_ptr<MemoryStore>> stores_;
    std::map<std::string, bool> persistent_;
    std::map<std::string, std::string> paths_;
};

} // namespace memory
} // namespace taskforge
