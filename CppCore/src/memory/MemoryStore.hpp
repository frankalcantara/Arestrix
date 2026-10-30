#pragma once

// Arestrix Memory Graph - armazenamento SQLite do multigrafo temporal.
//
// Escopos, nós, arestas temporais, episódios, evidências, memória de trabalho,
// lições e métricas de uso de API. A API pública é usada por MemoryService.

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
    std::vector<std::string> factIds;
    std::vector<std::string> playbookIds;
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

// Referencia de uma memoria devolvida por memory_search, para registrar uso sem
// depender de ela ter sido injetada no brief.
struct MemoryHitRef {
    std::string id;
    std::string kind;   // lesson | fact
};

// Resultado de um validador independente, ligado a um episodio.
struct MemoryValidationRecord {
    std::string id;
    std::string scopeId;
    std::string runId;
    std::string nodeId;
    std::string episodeId;
    std::string validatorId;
    std::string validatorVersion;
    bool ok = false;
    double score = 0.0;
    std::string reasonsJson = "[]";
    std::string artifactHash;
    std::string createdAt;
};

struct MemoryPurgeOptions {
    bool keepAudit = false;
    bool vacuum = true;
};

struct MemoryPruneResult {
    long long episodes = 0;
    long long hits = 0;
    long long usage = 0;
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
    std::string lastUsed;
    long long successCount = 0;
    long long failureCount = 0;
    double utility = 0.5;
    std::string dormantAt;
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

// Episodio enriquecido (M0/M1): o suficiente para extracao deterministica sem
// guardar prompt/resposta brutos. Campos JSON usam listas/objetos pequenos e sanitizados.
struct MemoryEpisodeRecord {
    std::string scopeId;
    std::string runId;
    std::string nodeId;
    std::string itemId = "pipeline";
    std::string kind;
    std::string content;
    std::string taskType;
    std::string outcome;       // success | failure
    std::string errorClass;
    std::string inputHash;
    taskforge::Json toolCalls = taskforge::Json::array();
    taskforge::Json filesTouched = taskforge::Json::array();
    taskforge::Json artifacts = taskforge::Json::array();
    taskforge::Json evidence = taskforge::Json::array();
    taskforge::Json successSignals = taskforge::Json::object();
    taskforge::Json metadata = taskforge::Json::object();
};

struct MemoryProposalRecord {
    std::string id;
    std::string scopeId;
    std::string kind;          // lesson | fact | entity | playbook | summary
    std::string payloadJson;
    std::string sourceEpisodeId;
    std::string evidenceJson;
    double confidence = 0.5;
    std::string status;        // pending | accepted | rejected | superseded
    std::string createdAt;
    std::string reviewedAt;
    std::string reviewedBy;
};

struct MemoryGateSample {
    std::string inputHash;
    std::string validatorId;
    std::string validatorVersion;
};

struct MemoryPlaybookRecord {
    std::string id;
    std::string scopeId;
    std::string taskType;
    std::string title;
    std::string stepsJson;
    std::string preconditionsJson;
    long long successCount = 0;
    long long failureCount = 0;
    double confidence = 0.5;
    std::string status;       // candidate | active | archived
    std::string sourceRef;
    std::string evidenceJson;
    std::string createdAt;
    std::string updatedAt;
};

// Objeto indexavel para busca semantica (M2). O texto e sempre sanitizado.
struct MemoryEmbeddingObject {
    std::string objectType;    // lesson | edge | node | playbook | episode
    std::string objectId;
    std::string text;
    std::string domain;
    double confidence = 0.5;
    std::string createdAt;
};

// Vetor binario serializado em little-endian (float32).
struct MemoryEmbeddingRecord {
    std::string objectType;
    std::string objectId;
    std::string model;
    int dim = 0;
    std::vector<uint8_t> vector;
    std::string contentHash;
    std::string domain;
    std::string createdAt;
};

// Candidato lexical estruturado usado pelo ranker hibrido.
struct MemorySearchCandidate {
    std::string id;
    std::string kind;          // lesson | fact | playbook | entity | episode
    std::string text;
    std::string domain;
    double confidence = 0.5;
    std::string createdAt;
};

// Utilitarios deterministicos compartilhados pelo consolidador e aliases.
std::string normalizeMemoryKey(const std::string& value);
double memoryTokenSimilarity(const std::string& a, const std::string& b);

class MemoryStore {
public:
    static constexpr size_t kMaxActiveLessons = 5000;

    explicit MemoryStore(std::string path, size_t maxActiveLessons = kMaxActiveLessons);
    ~MemoryStore();

    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;

    bool open(std::string& error);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    const std::string& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }

    // Copia consistente do banco (VACUUM INTO) para um arquivo novo.
    bool backupTo(const std::string& targetPath, std::string& error);

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
    // Expurgo completo: working, edges, episodes, evidence, nodes, usage, hits e,
    // opcionalmente, auditoria. Com vacuum=true, compacta o arquivo SQLite.
    bool purge(const std::string& scopeId, const MemoryPurgeOptions& options = {});
    // Remove telemetria mais antiga que olderThanDays. 0 ou negativo = manter tudo.
    MemoryPruneResult prune(const std::string& scopeId, int olderThanDays);
    // Descarta o banco efemero (:memory:) inteiro e recria o schema.
    bool resetInMemory();

    // Lições semânticas como nós/arestas do multigrafo.
    std::string recordLesson(const std::string& scopeId, const std::string& domain,
                             const std::string& triggerError, const std::string& lessonRule,
                             const std::string& sourceItem = "",
                             const std::string& sourceKind = "agent");
    size_t activeLessonCount(const std::string& scopeId) const;
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
    std::string recordEpisode(const MemoryEpisodeRecord& episode);
    taskforge::Json getEpisode(const std::string& scopeId, const std::string& episodeId) const;

    // Indice vetorial L2/M2. O store nao conhece o provider: apenas persiste BLOBs.
    bool upsertEmbedding(const std::string& scopeId, const std::string& objectType,
                         const std::string& objectId, const std::string& model, int dim,
                         const std::vector<uint8_t>& vector, const std::string& contentHash,
                         const std::string& domain);
    std::vector<MemoryEmbeddingRecord> listEmbeddings(const std::string& scopeId,
                                                      const std::string& model) const;
    std::vector<MemoryEmbeddingObject> embeddingObjects(const std::string& scopeId,
                                                        size_t maxEpisodes = 200) const;
    bool deleteEmbedding(const std::string& scopeId, const std::string& objectType,
                         const std::string& objectId, const std::string& model);
    size_t embeddingCount(const std::string& scopeId, const std::string& model) const;

    // Candidatos lexicais estruturados para o ranker hibrido. O searchContext
    // continua sendo o caminho lexical historico (sem mudanca de formato).
    std::vector<MemorySearchCandidate> lexicalCandidates(
        const std::string& scopeId, const std::string& text, size_t limit = 20,
        const std::vector<std::string>& domains = {}) const;

    // Propostas L2 (M1).
    std::string recordProposal(const std::string& scopeId, const std::string& kind,
                               const taskforge::Json& payload,
                               const std::string& sourceEpisodeId,
                               const taskforge::Json& evidence,
                               double confidence);
    std::vector<MemoryProposalRecord> listProposals(const std::string& scopeId,
                                                    const std::string& status = "pending",
                                                    size_t limit = 100) const;
    bool promoteProposal(const std::string& scopeId, const std::string& proposalId,
                         const std::string& reviewer, std::string& error,
                         std::string* promotedId = nullptr);
    bool rejectProposal(const std::string& scopeId, const std::string& proposalId,
                        const std::string& reviewer, const std::string& reason,
                        std::string& error);
    // Playbooks procedurais (M4).
    std::string recordPlaybook(const std::string& scopeId, const std::string& taskType,
                               const std::string& title, const taskforge::Json& steps,
                               const taskforge::Json& preconditions, double confidence,
                               const std::string& status = "candidate",
                               const std::string& sourceRef = "",
                               const taskforge::Json& evidence = taskforge::Json::array(),
                               long long successCount = 0, long long failureCount = 0);
    std::string playbookIdBySignature(const std::string& scopeId, const std::string& taskType,
                                      const std::string& stepsJson) const;
    std::vector<MemoryPlaybookRecord> listPlaybooks(const std::string& scopeId,
                                                    const std::string& taskType = "",
                                                    const std::string& status = "",
                                                    size_t limit = 100) const;
    bool getPlaybook(const std::string& scopeId, const std::string& playbookId,
                     MemoryPlaybookRecord& out) const;
    bool updatePlaybook(const std::string& scopeId, const std::string& playbookId,
                        long long successDelta, long long failureDelta, double confidence,
                        const std::string& status, const std::string& sourceRef,
                        const taskforge::Json& evidence, std::string& error);
    // Gate B5/B6: amostras de validacao vivem no banco de telemetria.
    std::vector<MemoryGateSample> validationGateSamples(const std::string& scopeId,
                                                        const std::string& taskType,
                                                        size_t limit = 20) const;
    std::vector<bool> validationOutcomes(const std::string& scopeId, const std::string& taskType,
                                         size_t limit = 10) const;
    // Ativa um candidato apenas com validacao independente e 3 input_hash
    // distintos nas amostras fornecidas pelo banco de telemetria.
    bool autoPromotePlaybook(const std::string& scopeId, const std::string& episodeId,
                             const std::string& validatorId, const std::string& validatorVersion,
                             const std::vector<MemoryGateSample>& samples,
                             std::string& promotedId, std::string& reason);
    bool autoDemotePlaybooks(const std::string& scopeId, const std::string& taskType,
                             const std::vector<bool>& outcomes, std::string& reason);
    bool unpromotePlaybook(const std::string& scopeId, const std::string& playbookId,
                           const std::string& reason, std::string& error);

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
    std::vector<MemoryEdgeRecord> listEdges(const std::string& scopeId,
                                            const std::string& status = "active",
                                            size_t limit = 5000) const;
    bool getEdge(const std::string& scopeId, const std::string& edgeId,
                 MemoryEdgeRecord& out) const;
    bool updateEdgeState(const std::string& scopeId, const std::string& edgeId,
                         const std::string& status, const std::string& supersededBy,
                         double confidence, const taskforge::Json& metadataPatch,
                         const std::string& validTo, const std::string& dormantAt,
                         const std::string& lastUsed, std::string& error);
    std::unordered_map<std::string, std::string> nodeNames(const std::string& scopeId) const;

    // Resolucao de entidades/aliases (M3). Nomes equivalentes apontam para o
    // mesmo node; aliases nunca sobrescrevem um mapeamento existente.
    std::string resolveNode(const std::string& scopeId, const std::string& name,
                            const std::string& kind,
                            const taskforge::Json& metadata = taskforge::Json::object());
    bool addAlias(const std::string& scopeId, const std::string& alias,
                  const std::string& nodeId, const std::string& source);
    std::string nodeByAlias(const std::string& scopeId, const std::string& alias) const;

    // Politicas invariantes por escopo (M5). Filhos nao sobrescrevem politicas
    // imutaveis de ancestrais.
    bool setPolicy(const std::string& scopeId, const std::string& key,
                   const taskforge::Json& value, bool immutable,
                   const std::string& actor, std::string& error);
    taskforge::Json effectivePolicy(const std::string& scopeId, const std::string& key) const;

    std::string searchContext(const std::string& scopeId, const std::string& text,
                              size_t limit = 10,
                              const std::vector<std::string>& domains = {},
                              std::vector<MemoryHitRef>* hits = nullptr) const;

    // Rastreio de memórias injetadas/consultadas e resultado da execução.
    bool recordHit(const std::string& scopeId, const std::string& runId,
                   const std::string& nodeId, const std::string& memoryId,
                   const std::string& kind, bool injected, bool used,
                   const std::string& outcome = "");
    int markHitsUsed(const std::string& scopeId, const std::string& runId,
                     const std::string& nodeId, const std::vector<std::string>& memoryIds);
    // Registra memórias descobertas por memory_search. Atualiza o hit existente
    // (se a memoria ja tinha sido injetada) ou insere injected=0/used=1.
    int recordConsultedHits(const std::string& scopeId, const std::string& runId,
                            const std::string& nodeId,
                            const std::vector<MemoryHitRef>& refs);
    bool finishHits(const std::string& scopeId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome);
    // Hits efetivamente usados, para reforcar o conhecimento em outro store.
    std::vector<MemoryHitRef> usedHitRefs(const std::string& scopeId, const std::string& runId,
                                          const std::string& nodeId) const;
    // Reforco/decay de arestas de conhecimento a partir dos IDs usados.
    bool reinforceFromHits(const std::string& scopeId,
                           const std::vector<std::string>& memoryIds,
                           const std::string& outcome);
    taskforge::Json hitSummary(const std::string& scopeId) const;
    taskforge::Json consolidationSummary(const std::string& scopeId) const;
    bool recordAudit(const std::string& actor, const std::string& action,
                     const std::string& target, const std::string& before,
                     const std::string& after);

    // Métricas de consumo de API (tokens, chamadas e contexto de memória).
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& scopeId) const;

    // Validacao independente ligada a um episodio (telemetria).
    std::string recordValidation(const MemoryValidationRecord& validation);
    std::vector<MemoryValidationRecord> listValidations(const std::string& scopeId,
                                                        const std::string& episodeId = "",
                                                        size_t limit = 100) const;

    // Estado completo para UI/export.
    taskforge::Json toJson(const std::string& scopeId) const;

    // Snapshot cognitivo completo (scopes, closure, nodes, edges, working) sem telemetria.
    taskforge::Json toCognitiveJson(const std::string& scopeId) const;
    bool importCognitiveJson(const std::string& scopeId, const taskforge::Json& data,
                             std::string& error);

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
    bool loadProposal(const std::string& scopeId, const std::string& proposalId,
                      MemoryProposalRecord& out) const;

    mutable std::recursive_mutex mutex_;
    sqlite3* db_ = nullptr;
    std::string path_;
    size_t maxActiveLessons_ = kMaxActiveLessons;
    mutable std::string lastError_;
};

} // namespace memory
} // namespace taskforge
