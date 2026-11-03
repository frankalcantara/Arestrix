#pragma once

// Fachada de memória do Arestrix sobre o MemoryStore SQLite.
//
// Centraliza conhecimento, continuação e telemetria; o runtime, o gerador, a UI
// e a CLI usam este contrato.

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "memory/EmbeddingProvider.hpp"
#include "memory/MemoryConsolidator.hpp"
#include "memory/MemoryRanker.hpp"
#include "memory/MemoryClaimExtractor.hpp"
#include "memory/MemoryBridges.hpp"
#include "memory/MemoryPaths.hpp"
#include "memory/MemoryTraversal.hpp"
#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

// Consolidacao em background, opt-in. O worker e deterministico e nunca roda no
// hot path de uma busca; ele apenas consome episodios ja persistidos.
struct MemoryMaintenanceConfig {
    bool enabled = false;
    std::string mode = "off";   // off | idle | after_episodes
    int idleSeconds = 300;
    int episodeThreshold = 20;
    ConsolidationOptions options;
    // E13: extracao de afirmacoes fora do caminho do agente.
    ClaimExtractorConfig claimExtractor;
    size_t claimMaxPassages = 10;
    bool lensesEnabled = false;
    bool pathsEnabled = false;
    PathOptions pathOptions;
    bool bridgesEnabled = false;
    BridgeOptions bridgeOptions;
};

MemoryMaintenanceConfig maintenanceConfigFromJson(const taskforge::Json& value);
taskforge::Json maintenanceConfigToJson(const MemoryMaintenanceConfig& config);

// E16: servico de caminhos no hot path. Desligado por padrao ate o A/B.
struct PathServingConfig {
    bool enabled = false;
    double explorationRate = 0.0;
    double minUtility = 0.6;
    long long minUses = 3;
    size_t maxPaths = 2;
};

PathServingConfig pathServingConfigFromJson(const taskforge::Json& value);
taskforge::Json pathServingConfigToJson(const PathServingConfig& config);

// E17: travessia em feixe quando nenhum caminho cobre a consulta.
struct TraversalConfig {
    bool enabled = false;
    TraversalOptions options;
};
TraversalConfig traversalConfigFromJson(const taskforge::Json& value);
taskforge::Json traversalConfigToJson(const TraversalConfig& config);

class MemoryService {
public:
    ~MemoryService();
    static MemoryService& getInstance();

    // Abre (ou reabre) o store do projeto. persistent=false usa SQLite :memory:.
    // dbPath vazio usa ARESTRIX_MEMORY_DB ou .arestrix/memory/memory.db.
    bool configure(const std::string& projectId, bool persistent,
                   const std::string& dbPath = "", bool readOnly = false);
    bool isPersistent(const std::string& projectId) const;
    MemoryStore* store(const std::string& projectId);
    std::string databasePath(const std::string& projectId) const;

    // Telemetria em banco proprio (run_usage, hits, episodios, validacoes, A/B).
    // Nao depende de memoria ligada; caminho vazio usa ARESTRIX_TELEMETRY_DB
    // ou, quando o banco de conhecimento e um arquivo, o irmao
    // .arestrix/telemetry/telemetry.db. Sem essas pistas, usa :memory:.
    bool configureTelemetry(const std::string& projectId, const std::string& dbPath = "");
    std::string telemetryDatabasePath(const std::string& projectId) const;
    MemoryStore* telemetryStore(const std::string& projectId);

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
    // Copia consistente do conhecimento para targetPath e fingerprint SHA-256.
    bool freezeKnowledge(const std::string& projectId, const std::string& targetPath,
                         std::string& fingerprint, std::string& error);

    // Lições semânticas.
    std::string recordLesson(const std::string& projectId, const std::string& domain,
                             const std::string& triggerError, const std::string& lessonRule,
                             const std::string& sourceItem = "",
                             const std::string& sourceKind = "agent");
    // Fase A: licao de agente vira proposta pendente; a promocao continua manual.
    std::string proposeLesson(const std::string& projectId, const std::string& domain,
                              const std::string& triggerError, const std::string& lessonRule,
                              const std::string& sourceItem = "",
                              const std::string& errorClass = "",
                              const std::string& operationKey = "");
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
                           MemoryBriefInfo* info = nullptr,
                           const std::string& taskType = "",
                           const std::string& intent = "",
                           const std::string& runId = "",
                           const std::string& nodeId = "");
    void clearLessons(const std::string& projectId);

    // Episódios, arestas e métricas.
    std::string recordEpisode(const std::string& projectId, const std::string& runId,
                              const std::string& nodeId, const std::string& itemId,
                              const std::string& kind, const std::string& content,
                              const taskforge::Json& metadata = taskforge::Json::object());
    std::string recordEpisode(const std::string& projectId, const MemoryEpisodeRecord& episode);
    taskforge::Json getEpisode(const std::string& projectId, const std::string& episodeId);
    size_t extractProposals(const std::string& projectId, const std::string& episodeId);
    // E13: extracao sob demanda, usada pela manutencao e pelos testes. Nunca e
    // chamada pelo wrapper de execucao do agente.
    taskforge::Json extractClaims(const std::string& projectId,
                                  const ClaimExtractorConfig& config,
                                  size_t maxPassages = 10);
    // E14: garante lentes e regenera facetas; chamado pela manutencao.
    taskforge::Json rebuildFacets(const std::string& projectId, bool facetsEnabled);
    taskforge::Json rebuildPaths(const std::string& projectId, bool pathsEnabled,
                                 const PathOptions& options = {});
    taskforge::Json rebuildBridges(const std::string& projectId, bool bridgesEnabled,
                                   const BridgeOptions& options = {});
    taskforge::Json listPaths(const std::string& projectId, const std::string& state = "");
    taskforge::Json getPath(const std::string& projectId, const std::string& pathId);
    bool setPathState(const std::string& projectId, const std::string& pathId,
                      const std::string& state, std::string& error);
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
                              std::vector<MemoryHitRef>* hits = nullptr,
                              const std::string& runId = "",
                              const std::string& nodeId = "",
                              const std::vector<std::string>& lenses = {},
                              const std::string& taskType = "",
                              const std::string& intent = "");

    // E12: ingestao de fontes e trechos sem modelo de linguagem.
    size_t ingestSources(const std::string& projectId, const std::string& workspaceRoot,
                         const std::vector<std::string>& uris, size_t maxFileBytes,
                         size_t maxFiles);
    taskforge::Json sourceSummary(const std::string& projectId);

    // E11: versao do conhecimento e tracos de recuperacao.
    long long knowledgeVersion(const std::string& projectId);
    bool recordTraceStep(const std::string& projectId, const std::string& runId,
                         const std::string& nodeId, const std::string& itemId,
                         const std::string& taskType, const std::string& intentTemplateHash,
                         const std::string& inputHash, const taskforge::Json& step);
    bool closeTrace(const std::string& projectId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome,
                    const std::string& model, const std::string& tier, bool memoryEnabled);
    taskforge::Json getTrace(const std::string& projectId, const std::string& runId,
                             const std::string& nodeId);

    // L2/M2: embeddings opcionais e busca hibrida.
    bool configureEmbeddings(const std::string& projectId, const EmbeddingConfig& config);
    void setEmbeddingProvider(const std::string& projectId,
                              std::shared_ptr<IEmbeddingProvider> provider);
    EmbeddingConfig embeddingConfig(const std::string& projectId);
    taskforge::Json embeddingStatus(const std::string& projectId);
    taskforge::Json testEmbeddings(const std::string& projectId, const std::string& sample = "");
    // E3: indexa pendencias fora do caminho de busca, com orcamento de tempo e
    // quantidade. Nunca derruba a execucao; devolve o estado resultante.
    taskforge::Json indexPending(const std::string& projectId, int timeBudgetMs = 10000,
                                 size_t maxObjects = 64);
    taskforge::Json reindexEmbeddings(const std::string& projectId, bool force,
                                      std::string& error);

    // L2/M3: consolidacao, conflitos e decay.
    taskforge::Json consolidate(const std::string& projectId,
                                const ConsolidationOptions& options,
                                std::string& error);
    taskforge::Json consolidationStatus(const std::string& projectId);

    // Consolidacao em background: configuravel por projeto e desligada por padrao.
    bool configureMaintenance(const std::string& projectId,
                              const MemoryMaintenanceConfig& config);
    MemoryMaintenanceConfig maintenanceConfig(const std::string& projectId) const;
    bool configurePathServing(const std::string& projectId, const PathServingConfig& config);
    PathServingConfig pathServingConfig(const std::string& projectId) const;
    bool configureTraversal(const std::string& projectId, const TraversalConfig& config);
    TraversalConfig traversalConfig(const std::string& projectId) const;
    taskforge::Json searchGraph(const std::string& projectId, const std::string& text,
                                size_t limit, const std::string& taskType = "",
                                const std::string& runId = "", const std::string& nodeId = "",
                                std::vector<MemoryHitRef>* hits = nullptr);
    taskforge::Json routingStats(const std::string& projectId,
                                 const std::string& taskType);
    taskforge::Json maintenanceStatus(const std::string& projectId);
    // Executa uma rodada agora no chamador (usado por CLI/testes e pelo worker).
    taskforge::Json runMaintenanceOnce(const std::string& projectId,
                                       const std::string& reason,
                                       std::string& error);
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
                     const std::string& nodeId, const std::vector<std::string>& memoryIds,
                     int* invalid = nullptr);
    int recordConsultedHits(const std::string& projectId, const std::string& runId,
                            const std::string& nodeId,
                            const std::vector<MemoryHitRef>& refs);
    bool finishHits(const std::string& projectId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome,
                    bool reinforce = true);
    taskforge::Json hitSummary(const std::string& projectId);
    bool invalidateEdge(const std::string& projectId, const std::string& edgeId,
                        const std::string& validTo, const std::string& reason,
                        const std::string& actor = "runtime");
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& projectId);
    // Episodio mais recente de (run, no), para ligar o veredito sem mudar
    // a assinatura de runAgent.
    std::string episodeIdFor(const std::string& projectId, const std::string& runId,
                             const std::string& nodeId);
    // reinforce=true (memory.validatedReinforcement) troca o reforco por
    // resultado do agente pelo reforco por veredito.
    std::string recordValidation(const std::string& projectId,
                                 const MemoryValidationRecord& validation,
                                 bool reinforce = false);
    std::vector<MemoryValidationRecord> listValidations(const std::string& projectId,
                                                        const std::string& episodeId = "",
                                                        size_t limit = 100);
    // E4: evidencia autocontida, lida do banco de conhecimento (nao da telemetria).
    std::vector<MemoryEvidenceRecord> evidenceFor(const std::string& projectId,
                                                  const std::string& targetKind,
                                                  const std::string& targetId = "",
                                                  size_t limit = 100);
    taskforge::Json evidenceSummary(const std::string& projectId);
    bool unpromotePlaybook(const std::string& projectId, const std::string& playbookId,
                           const std::string& reason, std::string& error);

    // D1 v2: estado de continuacao declarado. As ferramentas do agente nunca
    // criam chaves; a declaracao e do projeto/UI/CLI.
    bool declareStateKey(const std::string& projectId, const std::string& key,
                         const std::string& scopeKind, const taskforge::Json& schema,
                         long long maxBytes, long long ttlSeconds,
                         const std::string& valueMode, long long versionedKeep,
                         std::string& error);
    taskforge::Json stateKeyInfo(const std::string& projectId, const std::string& key);
    taskforge::Json stateKeys(const std::string& projectId);
    taskforge::Json getState(const std::string& projectId, const std::string& key,
                             const std::string& itemId, const std::string& taskType,
                             std::string& error);
    taskforge::Json setState(const std::string& projectId, const std::string& key,
                             const std::string& itemId, const std::string& taskType,
                             const taskforge::Json& value, bool hasExpected,
                             long long expectedVersion, std::string& error);
    taskforge::Json listState(const std::string& projectId, const std::string& itemId,
                              const std::string& taskType);
    taskforge::Json stateHistory(const std::string& projectId, const std::string& key,
                                 const std::string& itemId, const std::string& taskType,
                                 size_t limit);

    // Fase 0: doctor somente leitura do conhecimento e da telemetria.
    taskforge::Json doctor(const std::string& projectId);
    // Fase 1: reparo opt-in de sobreposicoes (nao encerra valores).
    size_t repairOverlaps(const std::string& projectId, std::string& error);

    // Estado para UI/export.
    taskforge::Json toJson(const std::string& projectId);
    taskforge::Json toCognitiveJson(const std::string& projectId);
    bool importCognitiveJson(const std::string& projectId, const taskforge::Json& data,
                             std::string& error);
    bool clearSession(const std::string& projectId);
    void closeProject(const std::string& projectId);

private:
    MemoryService() = default;
    MemoryService(const MemoryService&) = delete;
    MemoryService& operator=(const MemoryService&) = delete;

    struct EmbeddingRuntime {
        EmbeddingConfig config;
        std::shared_ptr<IEmbeddingProvider> provider;
        // Circuit breaker: nao repete uma chamada que acabou de falhar a cada
        // agente/busca. Test/reindex explicitos ignoram e resetam o cooldown.
        std::chrono::steady_clock::time_point cooldownUntil{};
        bool reindexing = false;
        std::string lastIndexError;
    };

    struct BriefCacheEntry {
        std::string value;
        MemoryBriefInfo info;
    };
    std::map<std::string, BriefCacheEntry> briefCache_;
    void invalidateBriefCache();

    struct MaintenanceRuntime;

    MemoryStore* ensureStore(const std::string& projectId);
    MemoryStore* existingStore(const std::string& projectId);
    MemoryStore* ensureTelemetry(const std::string& projectId);
    std::string defaultTelemetryPath(const std::string& projectId) const;
    std::string scopeId(const std::string& projectId) const;
    std::string defaultPath(const std::string& projectId) const;
    static std::string safeName(const std::string& projectId);
    EmbeddingRuntime& embeddingRuntimeLocked(const std::string& projectId);
    bool hybridSearch(const std::string& projectId, const std::string& text, size_t limit,
                      const std::vector<std::string>& domains,
                      std::vector<MemoryHitRef>* hits, std::string& output,
                      taskforge::Json* traceCandidates = nullptr,
                      const std::vector<std::string>& lenses = {});
    taskforge::Json servePaths(const std::string& projectId, const std::string& taskType,
                               const std::string& intent,
                               const std::vector<std::string>& access);
    bool configureWorkspaceLocked(const std::string& workspaceId, const std::string& dbPath,
                                  const std::string& parentId, const std::string& name);
    std::string workspaceScope(const std::string& workspaceId) const;
    void configureWorkspaceForProjectLocked(const std::string& projectId,
                                            const std::string& dbPath);
    bool hybridRecall(const std::string& projectId, const std::string& text, size_t limit,
                      const std::vector<std::string>& domains,
                      std::vector<RankedMemory>& output,
                      taskforge::Json* traceCandidates = nullptr,
                      const std::vector<std::string>& lenses = {});

    // Worker de manutencao: o stop ocorre fora de mutex_ para evitar deadlock
    // com uma rodada de consolidacao em andamento.
    void startMaintenanceLocked(const std::string& projectId,
                                const MemoryMaintenanceConfig& config);
    void stopMaintenance(const std::string& projectId);
    void stopAllMaintenance();
    void maintenanceLoop(const std::string& projectId, MaintenanceRuntime* runtime);
    void noteMaintenanceEpisode(const std::string& projectId);

    mutable std::recursive_mutex mutex_;
    std::map<std::string, std::unique_ptr<MemoryStore>> stores_;
    std::map<std::string, bool> persistent_;
    std::map<std::string, std::string> paths_;
    std::map<std::string, bool> readOnly_;
    std::map<std::string, std::unique_ptr<MemoryStore>> telemetry_;
    std::map<std::string, std::string> telemetryPaths_;
    std::map<std::string, EmbeddingRuntime> embeddings_;
    std::map<std::string, std::unique_ptr<MemoryStore>> workspaceStores_;
    std::map<std::string, std::string> workspacePaths_;
    std::map<std::string, std::string> projectWorkspace_;
    std::map<std::string, std::unique_ptr<MaintenanceRuntime>> maintenance_;
    std::map<std::string, PathServingConfig> pathServing_;   // E16
    std::map<std::string, TraversalConfig> traversal_;       // E17
};

} // namespace memory
} // namespace taskforge
