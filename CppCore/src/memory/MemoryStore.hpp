#pragma once

// Arestrix Memory Graph - armazenamento SQLite do multigrafo temporal.
//
// Escopos, nós, arestas temporais, episódios, evidências, memória de trabalho,
// lições e métricas de uso de API. A API pública é usada por MemoryService.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
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
    std::vector<std::string> pathIds;
    std::vector<std::string> pathClaimIds;
    std::vector<std::string> traversalIds;
    std::vector<std::string> workingKeys;
    std::vector<std::string> facetIds;
    std::vector<std::string> stableIds;
    std::vector<std::string> volatileIds;
    std::string stableBlock;
    std::string prefixKey;
    std::string prefixIdentity;
    long long prefixEpoch = 0;
    long long removalSeqStart = 0;
    size_t chars = 0;
    size_t estimatedTokens = 0;
    bool truncated = false;
};

struct MemoryPrefixEpoch {
    std::string scopeId;
    std::string prefixKey;
    long long epoch = 0;
    std::string contentHash;
    std::string itemIdsJson = "[]";
    std::string publishedAt;
};

taskforge::Json briefInfoToJson(const MemoryBriefInfo& info);

struct MemoryHitRecord {
    std::string id;
    std::string runId;
    std::string nodeId;
    std::string memoryId;
    std::string kind;
    bool injected = false;
    bool consulted = false;
    bool used = false;
    std::string outcome;
    std::string createdAt;
};

// Fase 5: evento do livro de credito fracionario.
struct MemoryCreditEvent {
    std::string traceId;
    std::string itemKey;
    std::string taskType;
    std::string modelTier;
    double deltaSuccess = 0.0;
    double deltaFailure = 0.0;
    std::string kind;
    std::string origin;
    std::string auditId;
    bool capped = false;
};

// Referencia de uma memoria devolvida por memory_search, para registrar uso sem
// depender de ela ter sido injetada no brief.
struct MemoryHitRef {
    std::string id;
    std::string kind;   // lesson | fact | passage
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

// E4: instantaneo autocontido da prova de um item de conhecimento. Nao guarda
// FK para episodio: sobrevive ao prune da telemetria.
struct MemoryEvidenceRecord {
    std::string id;
    std::string scopeId;
    std::string targetKind;     // lesson | fact | playbook | entity | proposal | edge | claim
    std::string targetId;
    std::string action;         // create | promote | demote | unpromote | invalidate
    std::string episodeId;
    std::string runId;
    std::string nodeId;
    std::string inputHash;
    std::string validatorId;
    std::string validatorVersion;
    bool ok = false;
    std::string artifactHash;
    std::string excerptHash;
    std::string summary;
    std::string createdAt;
};

struct MemoryPurgeOptions {
    bool keepAudit = false;
    bool vacuum = true;
    bool dropTombstones = false;
};

// Fase 3: alvo de esquecimento por versao de fonte ou por URI.
struct MemorySourceSelector {
    std::string sourceVersionId;
    std::string uri;
    bool allVersions = false;
    bool allowForgotten = false;
};

struct MemoryEraseResult {
    bool ok = false;
    std::string error;
    std::string sourceVersionId;
    std::string uri;
    size_t sources = 0;
    size_t passages = 0;
    size_t claimsRetracted = 0;
    size_t proposals = 0;
    size_t evidenceDeleted = 0;
    size_t derivativesTombstoned = 0;
    size_t jobs = 0;
    std::vector<std::string> passageIds;
    std::vector<std::string> retractedEdgeIds;
    std::vector<std::string> tombstoneIds;
    std::vector<std::string> jobIds;
};

struct MemoryJob {
    std::string id;
    std::string scopeId;
    std::string kind;
    std::string payloadJson;
    std::string status;
    int attempts = 0;
    std::string leaseOwner;
    std::string leaseUntil;
    std::string lastError;
    std::string createdAt;
    std::string updatedAt;
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
    std::string claimKey;
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
    long long cachedTokens = 0;
    long long cacheWriteTokens = 0;
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
// E11: traco imutavel de uma execucao de vertice com memoria. O trace_id e
// estavel por (scope, run, node); os passos sao anexados em ordem.
// E12: fonte de dominio e seus trechos versionados. O texto do trecho fica no
// banco de conhecimento; o brief nao o injeta nesta etapa.
struct MemorySourceRecord {
    std::string id;
    std::string scopeId;
    std::string uri;
    std::string contentHash;
    std::string kind;
    long long size = 0;
    std::string firstSeen;
    std::string lastSeen;
    std::string status = "active";
};

struct MemoryPassageRecord {
    std::string id;
    std::string sourceId;
    std::string scopeId;
    long long ordinal = 0;
    long long start = 0;
    long long end = 0;
    std::string text;
    std::string textHash;
    long long tokens = 0;
    std::string status = "active";
};

struct MemoryPassageHit {
    std::string id;
    std::string sourceId;
    std::string uri;
    long long start = 0;
    long long end = 0;
    std::string text;
};

struct MemoryPathRecord {
    std::string id;
    std::string scopeId;
    long long revision = 1;
    std::string schemaId;
    std::string taskType;
    std::string state = "candidate";
    std::string intentSample;
    std::string accessJson = "{}";
    long long success = 0;
    long long failure = 0;
    long long lastTau = 0;
    long long uses = 0;
    long long distinctInputs = 0;
    std::vector<std::string> claimIds;
    double utility = 0.0;
};

struct MemoryLensRecord {
    std::string id;
    std::string scopeId;
    std::string name;
    std::string description;
    long long version = 1;
};

struct MemoryFacetRecord {
    std::string id;
    std::string scopeId;
    std::string entityId;
    std::string lensId;
    long long revision = 1;
    std::string description;
    std::string supportingClaimsJson = "[]";
    std::string claimsHash;
    std::string status = "active";
    std::string embeddingFingerprint;
};

struct MemoryClaimCandidate {
    std::string passageId;
    std::string sourceId;
    std::string uri;
    std::string text;
    std::string passageHash;
    long long start = 0;
    long long end = 0;
};

struct MemoryTraceRecord {
    std::string traceId;
    std::string scopeId;
    std::string runId;
    std::string nodeId;
    std::string itemId;
    std::string taskType;
    std::string intentTemplateHash;
    std::string inputHash;
    long long knowledgeVersion = 0;
    std::string model;
    std::string tier;
    std::string outcome;
    std::string createdAt;
    taskforge::Json anchors = taskforge::Json::array();
    taskforge::Json steps = taskforge::Json::array();
    taskforge::Json delivered = taskforge::Json::array();
    taskforge::Json used = taskforge::Json::array();
};

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
    std::string intentTemplateHash;   // SHA-256 do promptTemplate antes das variaveis
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
    std::string fingerprint;
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

    explicit MemoryStore(std::string path, size_t maxActiveLessons = kMaxActiveLessons,
                         bool readOnly = false, bool durableSync = true);
    ~MemoryStore();

    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;

    // Fase 1: envelope RAII de escrita. Em transacao ja aberta (inclusive um
    // BEGIN cru antigo), usa SAVEPOINT/RELEASE; fora dela, BEGIN IMMEDIATE.
    class WriteTxn {
    public:
        explicit WriteTxn(MemoryStore& store, std::string& error);
        ~WriteTxn();
        WriteTxn(const WriteTxn&) = delete;
        WriteTxn& operator=(const WriteTxn&) = delete;

        bool ok() const { return active_; }
        bool commit(std::string* error = nullptr);
        void rollback();

    private:
        MemoryStore& store_;
        std::unique_lock<std::recursive_mutex> lock_;
        int depth_ = 0;
        bool savepoint_ = false;
        bool active_ = false;
        bool finished_ = false;
    };

    bool open(std::string& error);
    void close();
    bool isOpen() const { return db_ != nullptr; }
    bool isReadOnly() const { return readOnly_; }
    void setDurableCommits(bool durable) { durableSync_ = durable; }

    // Fase 0: gancho de teste chamado entre a validacao e a escrita. Nulo em
    // producao; usado pelo TwoConnectionHarness para intercalar dois processos.
    using TestInterleaveHook = std::function<void(const std::string&)>;
    void setTestInterleaveHook(TestInterleaveHook hook);

    // Deixa uma copia pronta para ser aberta somente leitura: consolida o WAL
    // e volta ao journal em disco, para que medir o hash nao dependa de -wal.
    static bool sealForReadOnly(const std::string& path, std::string& error);
    const std::string& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }

    // Copia consistente do banco (VACUUM INTO) para um arquivo novo.
    bool backupTo(const std::string& targetPath, std::string& error);
    bool vacuum(std::string& error);

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

    // D1 v2: estado de continuacao declarado e versionado.
    bool declareStateKey(const std::string& scopeId, const std::string& key,
                         const std::string& scopeKind, const taskforge::Json& schema,
                         long long maxBytes, long long ttlSeconds,
                         const std::string& valueMode, long long versionedKeep,
                         std::string& error);
    taskforge::Json stateKeyInfo(const std::string& scopeId, const std::string& key) const;
    taskforge::Json stateKeys(const std::string& scopeId) const;
    taskforge::Json getState(const std::string& scopeId, const std::string& key,
                             const std::string& scopeOwner, std::string& error) const;
    taskforge::Json setState(const std::string& scopeId, const std::string& key,
                             const std::string& scopeOwner, const taskforge::Json& value,
                             bool hasExpected, long long expectedVersion,
                             std::string& error);
    taskforge::Json listState(const std::string& scopeId, const std::string& key,
                              const std::string& scopeOwner) const;
    taskforge::Json stateHistory(const std::string& scopeId, const std::string& key,
                                 const std::string& scopeOwner, size_t limit) const;
    // Expurgo completo: working, edges, episodes, evidence, nodes, usage, hits e,
    // opcionalmente, auditoria. Com vacuum=true, compacta o arquivo SQLite.
    bool purge(const std::string& scopeId, const MemoryPurgeOptions& options = {});
    // Fase 1: tabelas com scope_id que ainda nao estao no expurgo. Vazio = cobertura ok.
    std::vector<std::string> purgeCoverageMissing() const;
    // Fase 1: marca pares funcionais sobrepostos como disputed e cria
    // CONTRADICTS entre os objetos. Nao encerra nenhum valor.
    size_t repairOverlaps(const std::string& scopeId, std::string& error);
    // Fase 2: encerramentos antigos de predicados que passaram a multi,
    // estruturais ou funcionais sem contexto obrigatorio.
    taskforge::Json listMultiClosures(const std::string& scopeId) const;
    size_t claimSupportRootCount(const std::string& scopeId, const std::string& claimKey) const;
    bool reopenEdge(const std::string& scopeId, const std::string& edgeId,
                    const std::string& reason, std::string& error);
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
    std::vector<MemoryLesson> getLessonsForDomains(const std::string& scopeId,
                                                   const std::vector<std::string>& domains) const;
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
    // Episodio mais recente de (run, no). Usado para ligar o veredito de
    // artefato ao episodio sem mudar a assinatura de runAgent.
    std::string episodeIdFor(const std::string& scopeId, const std::string& runId,
                             const std::string& nodeId) const;

    // Indice vetorial L2/M2. O store nao conhece o provider: apenas persiste BLOBs.
    bool upsertEmbedding(const std::string& scopeId, const std::string& objectType,
                         const std::string& objectId, const std::string& model, int dim,
                         const std::vector<uint8_t>& vector, const std::string& contentHash,
                         const std::string& domain, const std::string& fingerprint = "");
    std::vector<MemoryEmbeddingRecord> listEmbeddings(const std::string& scopeId,
                                                      const std::string& model,
                                                      const std::string& fingerprint = "") const;
    std::vector<MemoryEmbeddingObject> embeddingObjects(const std::string& scopeId,
                                                        size_t maxEpisodes = 200) const;
    bool deleteEmbedding(const std::string& scopeId, const std::string& objectType,
                         const std::string& objectId, const std::string& model);
    size_t embeddingCount(const std::string& scopeId, const std::string& model,
                          const std::string& fingerprint = "") const;

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
                               double confidence,
                               const std::string& operationKey = "");
    std::vector<MemoryProposalRecord> listProposals(const std::string& scopeId,
                                                    const std::string& status = "pending",
                                                    size_t limit = 100) const;
    bool promoteProposal(const std::string& scopeId, const std::string& proposalId,
                         const std::string& reviewer, std::string& error,
                         std::string* promotedId = nullptr,
                         const std::string& operationKey = "");
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
    taskforge::Json recompilePlaybook(const std::string& scopeId, const std::string& taskType);
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
                           const taskforge::Json& metadata = taskforge::Json::object(),
                           const std::string& contextKey = "",
                           const std::string& claimKey = "");
    bool invalidateEdge(const std::string& edgeId, const std::string& validTo,
                        const std::string& reason, const std::string& actor = "runtime");
    std::vector<MemoryEdgeRecord> neighbors(const std::string& scopeId,
                                            const std::string& nodeId, int depth = 1,
                                            const std::string& asOf = "") const;
    std::vector<MemoryEdgeRecord> incidentEdges(const std::string& scopeId,
                                                const std::string& nodeId,
                                                size_t limit = 50) const;
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
    std::string nodeKind(const std::string& scopeId, const std::string& nodeId) const;
    // E13: resolve uma mencao existente sem criar no. Devolve vazio quando nao
    // ha correspondencia ou quando a mencao e ambigua.
    std::string findEntity(const std::string& scopeId, const std::string& mention,
                           bool* ambiguous = nullptr) const;

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
                              std::vector<MemoryHitRef>* hits = nullptr,
                              const std::vector<std::string>& lenses = {}) const;

    // Rastreio de memórias injetadas/consultadas e resultado da execução.
    bool recordHit(const std::string& scopeId, const std::string& runId,
                   const std::string& nodeId, const std::string& memoryId,
                   const std::string& kind, bool injected, bool used,
                   const std::string& outcome = "");
    // E2: used=1 so por citacao. IDs que nao foram entregues na execucao sao
    // ignorados e contados em invalid (e em memory_usage.invalid_citations).
    int markHitsUsed(const std::string& scopeId, const std::string& runId,
                     const std::string& nodeId, const std::vector<std::string>& memoryIds,
                     int* invalid = nullptr);
    // Registra memórias descobertas por memory_search. Atualiza o hit existente
    // (se a memoria ja tinha sido injetada) ou insere injected=0/used=1.
    int recordConsultedHits(const std::string& scopeId, const std::string& runId,
                            const std::string& nodeId,
                            const std::vector<MemoryHitRef>& refs);
    // reinforce=false (E1, interruptor memory.validatedReinforcement) apenas
    // atualiza last_used; utility/contagens esperam o veredito independente.
    bool finishHits(const std::string& scopeId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome,
                    bool reinforce = true);
    // Hits efetivamente usados, para reforcar o conhecimento em outro store.
    std::vector<MemoryHitRef> usedHitRefs(const std::string& scopeId, const std::string& runId,
                                          const std::string& nodeId) const;
    std::vector<MemoryHitRecord> listHits(const std::string& scopeId,
                                          const std::string& runId,
                                          const std::string& nodeId) const;
    // Fase 5: grava eventos idempotentes e atualiza o agregado na mesma
    // transacao; usage_version so muda quando ao menos um evento e novo.
    bool recordCreditEvents(const std::string& scopeId,
                            const std::vector<MemoryCreditEvent>& events);
    taskforge::Json utilityFor(const std::string& scopeId, const std::string& itemKey,
                               const std::string& taskType) const;
    taskforge::Json creditSummary(const std::string& scopeId) const;
    // Soma absoluta de credito de uma origem para item/classe desde `since`.
    double creditTotalForOrigin(const std::string& scopeId, const std::string& itemKey,
                                const std::string& taskType, const std::string& origin,
                                const std::string& since) const;
    // Reconstroi o agregado a partir dos eventos; idempotente.
    bool rebuildItemUtility(const std::string& scopeId);
    // Reforco/decay de arestas de conhecimento a partir dos IDs usados.
    bool reinforceFromHits(const std::string& scopeId,
                           const std::vector<std::string>& memoryIds,
                           const std::string& outcome);
    // E1: sem veredito, o uso so atualiza last_used (sem utility/contagens).
    bool touchHitsLastUsed(const std::string& scopeId,
                           const std::vector<std::string>& memoryIds,
                           const std::string& at = "");
    taskforge::Json hitSummary(const std::string& scopeId) const;
    taskforge::Json consolidationSummary(const std::string& scopeId) const;
    bool recordAudit(const std::string& actor, const std::string& action,
                     const std::string& target, const std::string& before,
                     const std::string& after);

    // Métricas de consumo de API (tokens, chamadas e contexto de memória).
    bool recordUsage(const MemoryUsageRecord& usage);
    taskforge::Json usageSummary(const std::string& scopeId) const;

    // E10: marco de recorrencia por task_type sobre a telemetria.
    taskforge::Json recurrenceReport(const std::string& scopeId, const std::string& since,
                                     double minVerdictRate, double minCitationRate,
                                     long long minExecutions, long long minDistinctInputs,
                                     double minReuseRate) const;

    // E11: versao monotona do conhecimento e tracos de recuperacao.
    long long knowledgeVersion() const;
    bool appendTrace(const MemoryTraceRecord& trace, const taskforge::Json& step,
                     size_t maxSteps = 8, size_t maxCandidates = 50);
    bool closeTrace(const std::string& scopeId, const std::string& runId,
                    const std::string& nodeId, const std::string& outcome,
                    const std::string& model, const std::string& tier,
                    bool memoryEnabled);
    taskforge::Json getTrace(const std::string& scopeId, const std::string& runId,
                             const std::string& nodeId) const;
    std::vector<MemoryTraceRecord> listTraces(const std::string& scopeId,
                                              size_t limit = 10000) const;
    std::vector<std::string> approvedTraceIds(const std::string& scopeId,
                                              size_t limit = 10000) const;

    // E15: caminhos canonicos e suas estatisticas.
    std::string upsertPath(const std::string& scopeId, const std::string& taskType,
                           const std::string& signature,
                           const std::vector<std::string>& claimIds,
                           const std::string& intentSample,
                           const std::string& accessJson, bool* created = nullptr);
    bool recordPathEvent(const std::string& pathId, const std::string& traceId,
                         const std::string& inputHash, bool success, long long tau,
                         long long minDistinctInputs);
    std::vector<MemoryPathRecord> listPaths(const std::string& scopeId,
                                            const std::string& state = "") const;
    taskforge::Json getPath(const std::string& scopeId, const std::string& pathId) const;
    bool setPathState(const std::string& scopeId, const std::string& pathId,
                      const std::string& state, const std::string& reason);
    bool bridgeProposalSeen(const std::string& scopeId, const std::string& pairKey) const;
    std::vector<std::string> pathsForClaim(const std::string& claimId) const;
    std::vector<std::string> replacementClaimIds(const std::string& scopeId,
                                                 const std::string& claimId) const;
    std::vector<std::string> predicateClaimIds(const std::string& scopeId,
                                               const std::string& claimId) const;
    bool revalidatePathClaim(const std::string& scopeId, const std::string& pathId,
                             const std::string& oldClaimId, const std::string& newClaimId);
    bool syncPathShortcut(const std::string& scopeId, const std::string& pathId,
                          bool active, const std::string& srcId, const std::string& dstId,
                          const std::vector<std::string>& claimIds, double utility);
    taskforge::Json shortcutForPath(const std::string& scopeId,
                                    const std::string& pathId) const;
    taskforge::Json pathSummary(const std::string& scopeId) const;

    // E12: ingere uma versao de fonte e pesquisa trechos ativos.
    std::string ingestSource(const MemorySourceRecord& source,
                             const std::vector<MemoryPassageRecord>& passages,
                             size_t* insertedPassages, std::string& error,
                             bool allowForgotten = false);

    // Fase 3: linhagem, lapides, fila de trabalhos e exclusao por fonte.
    bool recordLineage(const std::string& scopeId, const std::string& derivedKind,
                       const std::string& derivedId, const std::string& inputKind,
                       const std::string& inputId);
    size_t backfillLineage(const std::string& scopeId, taskforge::Json* report = nullptr);
    MemoryEraseResult eraseSource(const std::string& scopeId,
                                  const MemorySourceSelector& selector,
                                  const std::string& reason,
                                  const std::string& requestedBy);
    std::string enqueueJob(const std::string& scopeId, const std::string& kind,
                           const taskforge::Json& payload);
    std::vector<MemoryJob> claimJobs(const std::string& scopeId, size_t limit,
                                     const std::string& owner, int leaseSeconds);
    bool completeJob(const std::string& jobId);
    bool failJob(const std::string& jobId, const std::string& error, int maxAttempts = 5);
    taskforge::Json jobSummary(const std::string& scopeId) const;
    taskforge::Json tombstoneSummary(const std::string& scopeId) const;
    taskforge::Json lineageSummary(const std::string& scopeId) const;
    long long currentRemovalSeq(const std::string& scopeId) const;
    long long latestPrefixEpoch(const std::string& scopeId, const std::string& prefixKey) const;
    bool publishPrefixEpoch(const std::string& scopeId, const std::string& prefixKey,
                            const std::string& contentHash, const std::string& itemIdsJson,
                            long long* epoch = nullptr);
    taskforge::Json listPrefixEpochs(const std::string& scopeId,
                                     const std::string& prefixKey = "",
                                     size_t limit = 20) const;
    taskforge::Json removedSince(const std::string& scopeId, long long removalSeqStart,
                                 const std::vector<std::pair<std::string, std::string>>& delivered) const;
    bool purgeTelemetry(const std::string& scopeId, const std::string& payloadJson,
                        std::string& error);
    std::vector<MemoryPassageHit> searchPassages(const std::string& scopeId,
                                                 const std::string& text, size_t limit = 10) const;
    taskforge::Json sourceSummary(const std::string& scopeId) const;

    // E13: passagens citadas em execucoes aprovadas e bookkeeping da extracao.
    std::vector<std::string> usedPassageIds(const std::string& scopeId, size_t limit = 100) const;
    std::vector<MemoryClaimCandidate> passagesForIds(const std::string& scopeId,
                                                     const std::vector<std::string>& ids,
                                                     size_t limit = 100) const;
    bool claimExtractionSeen(const std::string& scopeId, const std::string& passageHash,
                             const std::string& fingerprint) const;
    bool markClaimExtraction(const std::string& scopeId, const std::string& passageId,
                             const std::string& passageHash, const std::string& fingerprint,
                             const std::string& status, const std::string& error = "");

    // E14: catalogo de lentes, vinculo por aresta e facetas deterministicas.
    std::string ensureLens(const std::string& scopeId, const std::string& name,
                           const std::string& description = "");
    bool renameLens(const std::string& scopeId, const std::string& lensId,
                    const std::string& newName, const std::string& description);
    std::vector<MemoryLensRecord> listLenses(const std::string& scopeId) const;
    bool linkEdgeLens(const std::string& edgeId, const std::string& lensId);
    std::vector<std::string> edgeLensNames(const std::string& edgeId) const;
    std::vector<std::string> edgeIdsForLenses(const std::string& scopeId,
                                              const std::vector<std::string>& lensNames) const;
    // Garante uma lente para arestas antigas/importadas que nao passaram por
    // recordEdge (ex.: seed direto no SQLite).
    size_t backfillLenses(const std::string& scopeId);
    std::string upsertFacet(const MemoryFacetRecord& facet, bool* changed);
    std::vector<MemoryFacetRecord> listFacets(const std::string& scopeId,
                                              const std::string& entityId = "",
                                              const std::string& lensId = "") const;
    taskforge::Json facetSummary(const std::string& scopeId) const;

    // Validacao independente ligada a um episodio (telemetria).
    // E4: instantaneos de evidencia (conhecimento, sem FK para telemetria).
    std::string recordEvidence(const MemoryEvidenceRecord& evidence);
    std::vector<MemoryEvidenceRecord> listEvidence(const std::string& scopeId,
                                                   const std::string& targetKind = "",
                                                   const std::string& targetId = "",
                                                   size_t limit = 100) const;
    taskforge::Json evidenceSummary(const std::string& scopeId) const;
    // Fase 0: verificacoes somente leitura para o doctor. Nao escreve.
    taskforge::Json diagnostics(const std::string& scopeId) const;
    size_t evidenceCount(const std::string& scopeId, const std::string& targetKind,
                         const std::string& targetId) const;

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
    bool readOnly_ = false;
    bool durableSync_ = true;
    int writeTxnDepth_ = 0;
    mutable std::string lastError_;
    TestInterleaveHook testInterleaveHook_;
};

} // namespace memory
} // namespace taskforge
