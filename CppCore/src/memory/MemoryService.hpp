#pragma once

// Fachada de memória do Arestrix sobre o MemoryStore SQLite.
//
// A Fase 0 usava AgentBlackboard/SemanticMemoryEngine com JSON. A Fase 1
// centraliza tudo no banco e mantém este serviço como contrato para o gerador,
// o runtime, a UI e os testes.

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "memory/EmbeddingProvider.hpp"
#include "memory/MemoryConsolidator.hpp"
#include "memory/MemoryRanker.hpp"
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
    bool purgeProject(const std::string& projectId, bool keepAudit = false, bool vacuum = true);
    MemoryPruneResult prune(const std::string& projectId, int olderThanDays);

    // Lições semânticas.
    std::string recordLesson(const std::string& projectId, const std::string& domain,
                             const std::string& triggerError, const std::string& lessonRule,
                             const std::string& sourceItem = "",
                             const std::string& sourceKind = "agent");
    size_t activeLessonCount(const std::string& projectId);
    std::vector<MemoryLesson> getLessons(const std::string& projectId,
                                         const std::string& domain = "");
    std::string formatLessons(const std::string& projectId,
                              const std::vector<std::string>& domains = {});
    std::string buildBrief(const std::string& projectId, const std::string& itemId,
                           const std::vector<std::string>& domains,
                           const std::vector<std::string>& allowedKeys,
                           size_t maxTokens = 800,
                           bool includeLessons = true,
                           bool includeBlackboard = true,
                           MemoryBriefInfo* info = nullptr);
    void clearLessons(const std::string& projectId);

    // Episódios, arestas e métricas.
    std::string recordEpisode(const std::string& projectId, const std::string& runId,
                              const std::string& nodeId, const std::string& itemId,
                              const std::string& kind, const std::string& content,
                              const taskforge::Json& metadata = taskforge::Json::object());
    std::string recordEpisode(const std::string& projectId, const MemoryEpisodeRecord& episode);
    taskforge::Json getEpisode(const std::string& projectId, const std::string& episodeId);
    size_t extractProposals(const std::string& projectId, const std::string& episodeId);
    taskforge::Json listProposals(const std::string& projectId, const std::string& status = "pending",
                                  size_t limit = 100);
    bool promoteProposal(const std::string& projectId, const std::string& proposalId,
                         const std::string& reviewer, std::string& error,
                         std::string* promotedId = nullptr);
    bool rejectProposal(const std::string& projectId, const std::string& proposalId,
                        const std::string& reviewer, const std::string& reason,
                        std::string& error);
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
                              size_t limit = 10,
                              const std::vector<std::string>& domains = {},
                              std::vector<MemoryHitRef>* hits = nullptr);

    // L2/M2: embeddings opcionais e busca hibrida.
    bool configureEmbeddings(const std::string& projectId, const EmbeddingConfig& config);
    void setEmbeddingProvider(const std::string& projectId,
                              std::shared_ptr<IEmbeddingProvider> provider);
    EmbeddingConfig embeddingConfig(const std::string& projectId);
    taskforge::Json embeddingStatus(const std::string& projectId);
    taskforge::Json testEmbeddings(const std::string& projectId, const std::string& sample = "");
    taskforge::Json reindexEmbeddings(const std::string& projectId, bool force,
                                      std::string& error);

    // L2/M3: consolidacao, conflitos e decay.
    taskforge::Json consolidate(const std::string& projectId,
                                const ConsolidationOptions& options,
                                std::string& error);
    taskforge::Json consolidationStatus(const std::string& projectId);
    taskforge::Json listPlaybooks(const std::string& projectId,
                                  const std::string& taskType = "",
                                  const std::string& status = "",
                                  size_t limit = 100);

    // L2/M5: workspace com banco proprio e promocao explicita projeto -> workspace.
    bool configureWorkspace(const std::string& workspaceId, const std::string& dbPath,
                            const std::string& parentId = "",
                            const std::string& name = "");
    bool configureProjectWorkspace(const std::string& projectId,
                                   const std::string& workspaceId,
                                   const std::string& dbPath);
    MemoryStore* workspaceStore(const std::string& workspaceId);
    void closeWorkspace(const std::string& workspaceId);
    std::string workspaceIdForProject(const std::string& projectId);
    taskforge::Json workspaceStatus(const std::string& projectId);
    bool promoteToWorkspace(const std::string& projectId, const std::string& kind,
                            const std::string& memoryId,
                            const taskforge::Json& evidence,
                            const std::string& actor, std::string& error);

    // Rastreio de memórias injetadas/consultadas por execução.
    bool recordBriefHits(const std::string& projectId, const std::string& runId,
                         const std::string& nodeId, const MemoryBriefInfo& info);
    int markHitsUsed(const std::string& projectId, const std::string& runId,
                     const std::string& nodeId, const std::vector<std::string>& memoryIds);
    int recordConsultedHits(const std::string& projectId, const std::string& runId,
                            const std::string& nodeId,
                            const std::vector<MemoryHitRef>& refs);
    bool finishHits(const std::string& projectId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome);
    taskforge::Json hitSummary(const std::string& projectId);
    bool invalidateEdge(const std::string& projectId, const std::string& edgeId,
                        const std::string& validTo, const std::string& reason,
                        const std::string& actor = "runtime");
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& projectId);

    // Estado para UI/export.
    taskforge::Json toJson(const std::string& projectId);
    taskforge::Json toCognitiveJson(const std::string& projectId);
    bool importCognitiveJson(const std::string& projectId, const taskforge::Json& data,
                             std::string& error);
    bool importLegacySnapshot(const std::string& projectId, const taskforge::Json& data,
                              std::string& error);
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

    struct EmbeddingRuntime {
        EmbeddingConfig config;
        std::shared_ptr<IEmbeddingProvider> provider;
        // Circuit breaker: nao repete uma chamada que acabou de falhar a cada
        // agente/busca. Test/reindex explicitos ignoram e resetam o cooldown.
        std::chrono::steady_clock::time_point cooldownUntil{};
    };

    MemoryStore* ensureStore(const std::string& projectId);
    std::string scopeId(const std::string& projectId) const;
    std::string defaultPath(const std::string& projectId) const;
    static std::string safeName(const std::string& projectId);
    EmbeddingRuntime& embeddingRuntimeLocked(const std::string& projectId);
    bool hybridSearch(const std::string& projectId, const std::string& text, size_t limit,
                      const std::vector<std::string>& domains,
                      std::vector<MemoryHitRef>* hits, std::string& output);
    bool configureWorkspaceLocked(const std::string& workspaceId, const std::string& dbPath,
                                  const std::string& parentId, const std::string& name);
    std::string workspaceScope(const std::string& workspaceId) const;
    void configureWorkspaceForProjectLocked(const std::string& projectId,
                                            const std::string& dbPath);
    bool hybridRecall(const std::string& projectId, const std::string& text, size_t limit,
                      const std::vector<std::string>& domains,
                      std::vector<RankedMemory>& output);

    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::unique_ptr<MemoryStore>> stores_;
    std::map<std::string, bool> persistent_;
    std::map<std::string, std::string> paths_;
    std::map<std::string, EmbeddingRuntime> embeddings_;
    std::map<std::string, std::unique_ptr<MemoryStore>> workspaceStores_;
    std::map<std::string, std::string> workspacePaths_;
    std::map<std::string, std::string> projectWorkspace_;
};

} // namespace memory
} // namespace taskforge
