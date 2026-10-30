#include "MemoryService.hpp"
#include "MemoryExtractor.hpp"
#include "MemoryIndexer.hpp"
#include "MemorySanitizer.hpp"
#include "MemoryPlaybookCompiler.hpp"
#include "MemoryRanker.hpp"
#include "MemoryStateSchema.hpp"
#include "MemoryVectorIndex.hpp"
#include "project/RunnerSecurity.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

std::string projectKey(const std::string& projectId) {
    return projectId.empty() ? "default" : projectId;
}

std::string stateOwnerFor(const taskforge::Json& info, const std::string& projectId,
                          const std::string& itemId, const std::string& taskType,
                          std::string& error) {
    const std::string kind = info.value("scopeKind", std::string());
    if (kind == "project") return projectKey(projectId);
    if (kind == "task_type") {
        if (taskType.empty()) error = "task_type ausente para o estado";
        return taskType;
    }
    if (kind == "item") {
        if (itemId.empty()) error = "item_id ausente para o estado";
        return itemId;
    }
    error = "escopo de estado desconhecido";
    return "";
}

std::string oneLine(std::string text, size_t limit = 1200) {
    for (char& c : text) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    if (text.size() > limit) {
        text.resize(limit);
        text += "...";
    }
    return text;
}

bool embeddingInCooldown(const std::chrono::steady_clock::time_point& until) {
    return until != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() < until;
}

void embeddingStartCooldown(std::chrono::steady_clock::time_point& until) {
    until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
}

std::string formatRanked(const std::vector<RankedMemory>& ranked, size_t limit,
                         std::vector<MemoryHitRef>* hits) {
    std::ostringstream out;
    size_t emitted = 0;
    for (const auto& memory : ranked) {
        if (emitted >= limit) break;
        if (memory.id.empty() || memory.text.empty()) continue;
        std::string tag = "[FATO]";
        if (memory.kind == "lesson") tag = "[LICAO]";
        else if (memory.kind == "playbook") tag = "[PLAYBOOK]";
        else if (memory.kind == "entity") tag = "[ENTIDADE]";
        else if (memory.kind == "episode") tag = "[EPISODIO]";
        out << tag << " " << oneLine(memory.text) << "\n";
        if (hits) hits->push_back(MemoryHitRef{memory.id, memory.kind == "lesson" ? "lesson" : "fact"});
        ++emitted;
    }
    return out.str();
}

} // namespace

struct MemoryService::MaintenanceRuntime {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool stop = false;
    bool busy = false;
    MemoryMaintenanceConfig config;
    long long episodes = 0;
    long long triggers = 0;
    long long runs = 0;
    long long failures = 0;
    long long actions = 0;
    std::string lastRunAt;
    std::string lastReason;
    std::string lastError;
    std::chrono::steady_clock::time_point idleDeadline{};
    taskforge::Json lastReport = taskforge::Json::object();
};

MemoryMaintenanceConfig maintenanceConfigFromJson(const taskforge::Json& value) {
    MemoryMaintenanceConfig config;
    if (!value.is_object()) return config;

    config.mode = value.value("consolidationMode", config.mode);
    if (config.mode != "idle" && config.mode != "after_episodes" && config.mode != "off") {
        config.mode = "off";
    }
    if (config.mode == "off" && value.value("consolidationEnabled", false)) {
        config.mode = "idle";
    }
    config.enabled = config.mode != "off";
    config.idleSeconds = std::max(5, std::min(86400,
        value.value("consolidationIdleSeconds", config.idleSeconds)));
    config.episodeThreshold = std::max(1, std::min(100000,
        value.value("consolidationEpisodeThreshold", config.episodeThreshold)));
    config.options = consolidationOptionsFromJson(value);
    if (value.contains("consolidationDormantDays")) {
        config.options.dormantAfterDays = std::max(0, std::min(3650,
            value.value("consolidationDormantDays", config.options.dormantAfterDays)));
    }
    return config;
}

taskforge::Json maintenanceConfigToJson(const MemoryMaintenanceConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"mode", config.mode},
        {"idleSeconds", config.idleSeconds},
        {"episodeThreshold", config.episodeThreshold},
        {"options", consolidationOptionsToJson(config.options)}};
}

MemoryService::~MemoryService() {
    stopAllMaintenance();
}

MemoryService& MemoryService::getInstance() {
    static MemoryService instance;
    return instance;
}

std::string MemoryService::safeName(const std::string& projectId) {
    std::string out = projectId.empty() ? "default" : projectId;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return out;
}

std::string MemoryService::scopeId(const std::string& projectId) const {
    return "project:" + (projectId.empty() ? "default" : projectId);
}

std::string MemoryService::defaultPath(const std::string&) const {
    if (const char* env = std::getenv("ARESTRIX_MEMORY_DB"); env && *env) return env;
    return ".arestrix/memory/memory.db";
}

std::string MemoryService::databasePath(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = paths_.find(projectKey(projectId));
    return it == paths_.end() ? std::string() : it->second;
}

std::string MemoryService::defaultTelemetryPath(const std::string& projectId) const {
    if (const char* env = std::getenv("ARESTRIX_TELEMETRY_DB"); env && *env) return env;
    auto it = paths_.find(projectKey(projectId));
    if (it != paths_.end() && !it->second.empty() && it->second != ":memory:") {
        std::filesystem::path db(it->second);
        if (db.filename() == "memory.db" && db.parent_path().filename() == "memory" &&
            db.parent_path().parent_path().filename() == ".arestrix") {
            return (db.parent_path().parent_path() / "telemetry" / "telemetry.db").string();
        }
    }
    return ":memory:";
}

bool MemoryService::configureTelemetry(const std::string& projectId, const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectKey(projectId);
    const std::string path = dbPath.empty() ? defaultTelemetryPath(id) : dbPath;
    auto existing = telemetry_.find(id);
    if (existing != telemetry_.end() && existing->second && existing->second->isOpen() &&
        telemetryPaths_.find(id) != telemetryPaths_.end() && telemetryPaths_.at(id) == path) {
        return true;
    }
    auto store = std::make_unique<MemoryStore>(path);
    std::string error;
    if (!store->open(error)) return false;
    if (!store->ensureScope(scopeId(id), "project", "", id)) return false;
    telemetry_[id] = std::move(store);
    telemetryPaths_[id] = path;
    return true;
}

std::string MemoryService::telemetryDatabasePath(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = telemetryPaths_.find(projectKey(projectId));
    return it == telemetryPaths_.end() ? std::string() : it->second;
}

MemoryStore* MemoryService::telemetryStore(const std::string& projectId) {
    return ensureTelemetry(projectId);
}

MemoryStore* MemoryService::existingStore(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = stores_.find(projectKey(projectId));
    if (it == stores_.end() || !it->second || !it->second->isOpen()) return nullptr;
    return it->second.get();
}

MemoryStore* MemoryService::ensureTelemetry(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectKey(projectId);
    auto it = telemetry_.find(id);
    if (it != telemetry_.end() && it->second && it->second->isOpen()) return it->second.get();
    if (!configureTelemetry(id, "")) return nullptr;
    it = telemetry_.find(id);
    return it == telemetry_.end() ? nullptr : it->second.get();
}

bool MemoryService::configure(const std::string& projectId, bool persistent,
                              const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectId.empty() ? "default" : projectId;
    // A0: modo nao persistente e SEMPRE :memory:. Um caminho de arquivo
    // so pode ser aberto com persistent=true; caso contrario, clearSession
    // poderia purgar o banco real do projeto.
    const std::string path = persistent
        ? (dbPath.empty() ? defaultPath(id) : dbPath)
        : ":memory:";

    auto existing = stores_.find(id);
    if (existing != stores_.end() && persistent_.find(id) != persistent_.end() &&
        persistent_.at(id) == persistent && paths_.find(id) != paths_.end() &&
        paths_.at(id) == path) {
        return existing->second && existing->second->isOpen();
    }

    auto store = std::make_unique<MemoryStore>(path);
    std::string error;
    if (!store->open(error)) {
        return false;
    }
    if (!store->ensureScope(scopeId(id), "project", "", id)) {
        return false;
    }
    stores_[id] = std::move(store);
    persistent_[id] = persistent;
    paths_[id] = path;

    if (persistent) {
        configureWorkspaceForProjectLocked(id, path);
    }
    return true;
}

std::string MemoryService::workspaceScope(const std::string& workspaceId) const {
    return "workspace:" + (workspaceId.empty() ? "default" : workspaceId);
}

bool MemoryService::configureWorkspaceLocked(const std::string& workspaceId,
                                             const std::string& dbPath,
                                             const std::string& parentId,
                                             const std::string& name) {
    const std::string id = workspaceId.empty() ? "default" : workspaceId;
    if (dbPath.empty()) return false;
    auto existing = workspaceStores_.find(id);
    if (existing != workspaceStores_.end() && existing->second &&
        existing->second->isOpen() && workspacePaths_[id] == dbPath) {
        return true;
    }
    auto store = std::make_unique<MemoryStore>(dbPath);
    std::string error;
    if (!store->open(error)) return false;
    if (!parentId.empty()) {
        store->ensureScope(parentId, "institution", "", parentId);
    }
    if (!store->ensureScope(workspaceScope(id), "workspace", parentId, name.empty() ? id : name)) {
        return false;
    }
    workspaceStores_[id] = std::move(store);
    workspacePaths_[id] = dbPath;
    return true;
}

void MemoryService::configureWorkspaceForProjectLocked(const std::string& projectId,
                                                       const std::string& dbPath) {
    const std::filesystem::path projectDb(dbPath);
    if (projectDb.filename() != "memory.db") return;
    const std::filesystem::path memoryDir = projectDb.parent_path();
    const std::filesystem::path arestrixDir = memoryDir.parent_path();
    if (memoryDir.filename() != "memory" || arestrixDir.filename() != ".arestrix") return;
    const std::filesystem::path root = arestrixDir.parent_path();
    const std::filesystem::path workspaceDb = root / ".workspace" / "memory" / "memory.db";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(workspaceDb, ec)) return;
    std::string workspaceId;
    if (const char* env = std::getenv("ARESTRIX_WORKSPACE_ID"); env && *env) {
        workspaceId = env;
    } else {
        workspaceId = root.filename().string();
        if (workspaceId.empty()) workspaceId = "default";
    }
    if (!configureWorkspaceLocked(workspaceId, workspaceDb.string(), "", workspaceId)) return;
    projectWorkspace_[projectId] = workspaceId;
}

bool MemoryService::configureWorkspace(const std::string& workspaceId,
                                       const std::string& dbPath,
                                       const std::string& parentId,
                                       const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return configureWorkspaceLocked(workspaceId, dbPath, parentId, name);
}

bool MemoryService::configureProjectWorkspace(const std::string& projectId,
                                              const std::string& workspaceId,
                                              const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!configureWorkspaceLocked(workspaceId, dbPath, "", workspaceId)) return false;
    projectWorkspace_[projectId.empty() ? "default" : projectId] = workspaceId;
    return true;
}

MemoryStore* MemoryService::workspaceStore(const std::string& workspaceId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = workspaceStores_.find(workspaceId.empty() ? "default" : workspaceId);
    return it == workspaceStores_.end() ? nullptr : it->second.get();
}

void MemoryService::closeWorkspace(const std::string& workspaceId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = workspaceId.empty() ? "default" : workspaceId;
    workspaceStores_.erase(id);
    workspacePaths_.erase(id);
}

std::string MemoryService::workspaceIdForProject(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = projectWorkspace_.find(projectId.empty() ? "default" : projectId);
    return it == projectWorkspace_.end() ? std::string() : it->second;
}

taskforge::Json MemoryService::workspaceStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string workspaceId = workspaceIdForProject(projectId);
    if (workspaceId.empty()) {
        return taskforge::Json{{"configured", false}, {"projectId", projectId}};
    }
    auto* store = workspaceStore(workspaceId);
    if (!store) return taskforge::Json{{"configured", false}, {"projectId", projectId}};
    const std::string scope = workspaceScope(workspaceId);
    auto* projectStore = ensureStore(projectId);
    (void)projectStore;
    auto summary = store->consolidationSummary(scope);
    return taskforge::Json{
        {"configured", true},
        {"projectId", projectId},
        {"workspaceId", workspaceId},
        {"scope", scope},
        {"database", workspacePaths_[workspaceId]},
        {"lessons", static_cast<long long>(store->activeLessonCount(scope))},
        {"playbooks", static_cast<long long>(store->listPlaybooks(scope, "", "active", 1000).size())},
        {"activeEdges", summary.value("active", 0LL)},
        {"aliases", summary.value("aliases", 0LL)}};
}

bool MemoryService::promoteToWorkspace(const std::string& projectId, const std::string& kind,
                                       const std::string& memoryId,
                                       const taskforge::Json& evidence,
                                       const std::string& actor, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!evidence.is_array() || evidence.empty()) {
        error = "promocao sem evidencia";
        return false;
    }
    auto* source = ensureStore(projectId);
    const std::string workspaceId = workspaceIdForProject(projectId);
    auto* target = workspaceStore(workspaceId);
    if (!source || !target) {
        error = "workspace nao configurado";
        return false;
    }
    const std::string sourceScope = scopeId(projectId);
    const std::string targetScope = workspaceScope(workspaceId);
    const std::string reviewer = actor.empty() ? "human" : actor;
    taskforge::Json promotionEvidence = evidence;
    sanitizeJsonInPlace(promotionEvidence);
    std::string promotedId;
    std::string promotedKind = kind;

    if (kind == "lesson") {
        MemoryLesson lesson;
        bool found = false;
        for (const auto& item : source->getLessons(sourceScope)) {
            if (item.id == memoryId) {
                lesson = item;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "licao nao encontrada";
            return false;
        }
        promotedId = target->recordLesson(targetScope, lesson.domain, lesson.triggerError,
                                          lesson.lessonRule, projectId, "workspace_promotion");
        if (promotedId.empty()) {
            error = "falha ao promover licao";
            return false;
        }
    } else if (kind == "fact") {
        MemoryEdgeRecord edge;
        if (!source->getEdge(sourceScope, memoryId, edge) || edge.status != "active" ||
            edge.relation == "HAS_LESSON" || edge.relation == "CONTRADICTS" ||
            edge.relation == "SUPERSEDES" || edge.relation == "GENERALIZES") {
            error = "fato nao encontrado ou nao promovivel";
            return false;
        }
        const auto names = source->nodeNames(sourceScope);
        auto srcName = names.find(edge.srcId);
        auto dstName = names.find(edge.dstId);
        const std::string srcNode = srcName == names.end() ? "" :
            target->resolveNode(targetScope, srcName->second, "entity");
        const std::string dstNode = dstName == names.end() ? "" :
            target->resolveNode(targetScope, dstName->second, "entity");
        if (srcNode.empty() || (!edge.dstId.empty() && dstNode.empty())) {
            error = "falha ao resolver entidades do fato";
            return false;
        }
        auto valueOpt = taskforge::jsonutil::tryParse(edge.valueJson);
        taskforge::Json value = valueOpt ? *valueOpt : taskforge::Json(edge.valueJson);
        auto metadataOpt = taskforge::jsonutil::tryParse(edge.metadata);
        taskforge::Json metadata = metadataOpt && metadataOpt->is_object()
            ? *metadataOpt : taskforge::Json::object();
        metadata["promotionEvidence"] = promotionEvidence;
        metadata["promotedFromProject"] = projectId;
        metadata["promotedBy"] = reviewer;
        metadata["promotedAt"] = isoNow();
        promotedId = target->recordEdge(targetScope, srcNode, dstNode, edge.relation, value,
                                        edge.validFrom, edge.validTo, edge.confidence, "active",
                                        "workspace_promotion", projectId, metadata);
        if (promotedId.empty()) {
            error = "falha ao promover fato";
            return false;
        }
    } else if (kind == "playbook") {
        MemoryPlaybookRecord playbook;
        if (!source->getPlaybook(sourceScope, memoryId, playbook)) {
            error = "playbook nao encontrado";
            return false;
        }
        auto steps = taskforge::jsonutil::tryParse(playbook.stepsJson);
        auto preconditions = taskforge::jsonutil::tryParse(playbook.preconditionsJson);
        const std::string stepsJson = steps && steps->is_array() ? steps->dump() : "[]";
        const std::string existing = target->playbookIdBySignature(targetScope, playbook.taskType, stepsJson);
        if (!existing.empty()) {
            MemoryPlaybookRecord current;
            target->getPlaybook(targetScope, existing, current);
            if (!target->updatePlaybook(targetScope, existing, 0, 0,
                                        std::max(current.confidence, playbook.confidence),
                                        "active", projectId, promotionEvidence, error)) {
                return false;
            }
            promotedId = existing;
        } else {
            promotedId = target->recordPlaybook(
                targetScope, playbook.taskType, playbook.title,
                steps && steps->is_array() ? *steps : taskforge::Json::array(),
                preconditions && preconditions->is_object() ? *preconditions : taskforge::Json::object(),
                playbook.confidence, "active", projectId, promotionEvidence,
                playbook.successCount, playbook.failureCount);
            if (promotedId.empty()) {
                error = "falha ao promover playbook";
                return false;
            }
        }
    } else {
        error = "tipo de memoria nao promovivel: " + kind;
        return false;
    }

    source->recordAudit(reviewer, "promote_to_workspace", memoryId, "", targetScope + "|" + promotedId);
    target->recordAudit(reviewer, "receive_workspace_promotion", promotedId, projectId, kind);
    return true;
}

bool MemoryService::isPersistent(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = persistent_.find(projectKey(projectId));
    return it != persistent_.end() && it->second;
}

MemoryStore* MemoryService::ensureStore(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = stores_.find(projectId);
    if (it != stores_.end() && it->second && it->second->isOpen()) return it->second.get();
    if (!configure(projectId, false)) return nullptr;
    it = stores_.find(projectId);
    return it == stores_.end() ? nullptr : it->second.get();
}

MemoryStore* MemoryService::store(const std::string& projectId) {
    return ensureStore(projectId);
}

bool MemoryService::setWorking(const std::string& projectId, const std::string& itemId,
                               const std::string& key, const taskforge::Json& value) {
    auto* s = ensureStore(projectId);
    return s && s->setWorking(scopeId(projectId), itemId, key, value);
}

taskforge::Json MemoryService::getWorking(const std::string& projectId, const std::string& itemId,
                                          const std::string& key) {
    auto* s = ensureStore(projectId);
    return s ? s->getWorking(scopeId(projectId), itemId, key) : taskforge::Json();
}

taskforge::Json MemoryService::workingSnapshot(const std::string& projectId, const std::string& itemId,
                                               const std::vector<std::string>& allowedKeys) {
    auto* s = ensureStore(projectId);
    return s ? s->workingSnapshot(scopeId(projectId), itemId, allowedKeys) : taskforge::Json::object();
}

bool MemoryService::ingestWorkingDelta(const std::string& projectId, const std::string& itemId,
                                       const taskforge::Json& delta) {
    auto* s = ensureStore(projectId);
    return s && s->ingestWorkingDelta(scopeId(projectId), itemId, delta);
}

void MemoryService::clearWorkingItem(const std::string& projectId, const std::string& itemId) {
    if (auto* s = ensureStore(projectId)) s->clearWorkingItem(scopeId(projectId), itemId);
}

bool MemoryService::declareStateKey(const std::string& projectId, const std::string& key,
                                    const std::string& scopeKind, const taskforge::Json& schema,
                                    long long maxBytes, long long ttlSeconds,
                                    const std::string& valueMode, long long versionedKeep,
                                    std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    return s->declareStateKey(scopeId(projectId), key, scopeKind, schema, maxBytes,
                              ttlSeconds, valueMode, versionedKeep, error);
}

taskforge::Json MemoryService::stateKeyInfo(const std::string& projectId,
                                            const std::string& key) {
    auto* s = existingStore(projectId);
    return s ? s->stateKeyInfo(scopeId(projectId), key) : taskforge::Json::object();
}

taskforge::Json MemoryService::stateKeys(const std::string& projectId) {
    auto* s = existingStore(projectId);
    return s ? s->stateKeys(scopeId(projectId)) : taskforge::Json::array();
}

taskforge::Json MemoryService::getState(const std::string& projectId, const std::string& key,
                                        const std::string& itemId, const std::string& taskType,
                                        std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->getState(scopeId(projectId), key, owner, error);
}

taskforge::Json MemoryService::setState(const std::string& projectId, const std::string& key,
                                        const std::string& itemId, const std::string& taskType,
                                        const taskforge::Json& value, bool hasExpected,
                                        long long expectedVersion, std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto schema = info.contains("schema") ? info["schema"] : taskforge::Json::object();
    taskforge::Json cleanValue = value;
    sanitizeJsonInPlace(cleanValue);
    if (!validateStateValue(schema, cleanValue, error)) {
        return taskforge::Json{{"ok", false}, {"error", error}, {"schemaError", true}};
    }
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->setState(scopeId(projectId), key, owner, cleanValue, hasExpected,
                       expectedVersion, error);
}

taskforge::Json MemoryService::listState(const std::string& projectId, const std::string& itemId,
                                         const std::string& taskType) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "banco de conhecimento indisponivel"}};
    taskforge::Json out{{"ok", true}, {"keys", taskforge::Json::array()}};
    for (const auto& info : s->stateKeys(scopeId(projectId))) {
        std::string error;
        const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
        if (owner.empty()) continue;
        taskforge::Json item = s->listState(scopeId(projectId), info.value("key", std::string()), owner);
        item["scopeKind"] = info.value("scopeKind", std::string());
        item["scopeOwner"] = owner;
        item["valueMode"] = info.value("valueMode", std::string());
        item["ttlSeconds"] = info.value("ttlSeconds", 0LL);
        out["keys"].push_back(item);
    }
    return out;
}

taskforge::Json MemoryService::stateHistory(const std::string& projectId, const std::string& key,
                                            const std::string& itemId, const std::string& taskType,
                                            size_t limit) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "banco de conhecimento indisponivel"}};
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) return taskforge::Json{{"ok", false}, {"error", "chave de estado nao declarada"}};
    std::string error;
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->stateHistory(scopeId(projectId), key, owner, limit);
}

void MemoryService::clearProject(const std::string& projectId) {
    purgeProject(projectId, false, true);
}

bool MemoryService::purgeProject(const std::string& projectId, bool keepAudit, bool vacuum) {
    MemoryPurgeOptions options;
    options.keepAudit = keepAudit;
    options.vacuum = vacuum;
    bool ok = true;
    bool any = false;
    if (auto* s = existingStore(projectId)) {
        any = true;
        ok = s->purge(scopeId(projectId), options) && ok;
    }
    if (auto* t = ensureTelemetry(projectId)) {
        any = true;
        ok = t->purge(scopeId(projectId), options) && ok;
    }
    return any && ok;
}

MemoryPruneResult MemoryService::prune(const std::string& projectId, int olderThanDays) {
    if (olderThanDays <= 0) return MemoryPruneResult{};
    MemoryPruneResult total;
    if (auto* t = ensureTelemetry(projectId)) {
        const auto pruned = t->prune(scopeId(projectId), olderThanDays);
        total.episodes += pruned.episodes;
        total.hits += pruned.hits;
        total.usage += pruned.usage;
    }
    if (auto* s = existingStore(projectId)) {
        const auto pruned = s->prune(scopeId(projectId), olderThanDays);
        total.episodes += pruned.episodes;
        total.hits += pruned.hits;
        total.usage += pruned.usage;
    }
    return total;
}

bool MemoryService::freezeKnowledge(const std::string& projectId, const std::string& targetPath,
                                    std::string& fingerprint, std::string& error) {
    fingerprint.clear();
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    if (!s->backupTo(targetPath, error)) return false;
    fingerprint = project::RunnerSecurity::sha256File(targetPath, error);
    if (fingerprint.empty()) return false;
    return true;
}

std::string MemoryService::recordLesson(const std::string& projectId, const std::string& domain,
                                        const std::string& triggerError, const std::string& lessonRule,
                                        const std::string& sourceItem, const std::string& sourceKind) {
    auto* s = ensureStore(projectId);
    return s ? s->recordLesson(scopeId(projectId), domain, triggerError, lessonRule, sourceItem, sourceKind)
             : std::string();
}

std::string MemoryService::proposeLesson(const std::string& projectId, const std::string& domain,
                                         const std::string& triggerError, const std::string& lessonRule,
                                         const std::string& sourceItem) {
    auto* s = ensureStore(projectId);
    if (!s) return "";
    const std::string scope = scopeId(projectId);
    taskforge::Json payload{{"domain", domain.empty() ? "geral" : domain},
                            {"triggerError", triggerError},
                            {"lessonRule", lessonRule}};
    taskforge::Json evidence = taskforge::Json::array(
        {taskforge::Json{{"kind", "agent"}, {"node", sourceItem}}});
    return s->recordProposal(scope, "lesson", payload, "", evidence, 0.6);
}

std::vector<MemoryLesson> MemoryService::getLessons(const std::string& projectId,
                                                    const std::string& domain) {
    auto* s = ensureStore(projectId);
    return s ? s->getLessons(scopeId(projectId), domain) : std::vector<MemoryLesson>();
}

size_t MemoryService::activeLessonCount(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->activeLessonCount(scopeId(projectId)) : 0;
}

std::string MemoryService::formatLessons(const std::string& projectId,
                                         const std::vector<std::string>& domains) {
    auto* s = ensureStore(projectId);
    return s ? s->formatLessons(scopeId(projectId), domains) : std::string();
}

std::string MemoryService::buildBrief(const std::string& projectId, const std::string& itemId,
                                      const std::vector<std::string>& domains,
                                      const std::vector<std::string>& allowedKeys,
                                      size_t maxTokens, bool includeLessons,
                                      bool includeBlackboard, MemoryBriefInfo* info,
                                      const std::string& taskType) {
    if (info) *info = MemoryBriefInfo{};
    if (!includeLessons && !includeBlackboard) return std::string();
    // 0 = nao injetar memoria. Nunca interpretar 0 como "sem orcamento" (infinito).
    if (maxTokens == 0) return std::string();
    auto* s = ensureStore(projectId);
    if (!s) return std::string();

    std::vector<MemoryLesson> selectedLessons;
    if (includeLessons) {
        for (const auto& lesson : s->getLessons(scopeId(projectId))) {
            if (domains.empty() || std::find(domains.begin(), domains.end(), lesson.domain) != domains.end()) {
                selectedLessons.push_back(lesson);
            }
        }
    }

    std::string workingText;
    std::vector<std::string> workingKeys;
    if (includeBlackboard) {
        auto snapshot = s->workingSnapshot(scopeId(projectId), itemId, allowedKeys);
        if (snapshot.is_object() && snapshot.size() > 0) {
            workingText = "\n[ARESTRIX MEMORY - ESTADO DA TAREFA]\n" + snapshot.dump(2) + "\n";
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it) workingKeys.push_back(it.key());
        }
    }

    const size_t budget = maxTokens == 0 ? std::string::npos : maxTokens * 4;   // ~4 caracteres/token
    std::string brief;
    bool truncated = false;

    if (!selectedLessons.empty()) {
        const std::string header =
            "\n--- [ARESTRIX MEMORIA SEMANTICA: LICOES APRENDIDAS - PROJETO: " + scopeId(projectId) + "] ---\n"
            "Atencao: os itens anteriores identificaram as seguintes diretrizes para prevencao de erros.\n"
            "Se usar um item, cite o ID [MEMO:<id>] na resposta final.\n";
        if (header.size() <= budget) {
            brief += header;
            int lessonNumber = 1;
            for (const auto& lesson : selectedLessons) {
                std::ostringstream line;
                line << "[" << lessonNumber << "] [MEMO:" << lesson.id << "] (Dominio: "
                     << lesson.domain << ", confianca " << lesson.confidence << ") "
                     << lesson.lessonRule << "\n";
                if (budget != std::string::npos && brief.size() + line.str().size() > budget) {
                    truncated = true;
                    break;
                }
                brief += line.str();
                ++lessonNumber;
                if (info) info->lessonIds.push_back(lesson.id);
            }
        } else {
            truncated = true;
        }
    }

    // Licoes promovidas para o workspace (ancestral) entram como contexto extra.
    if (includeLessons) {
        const std::string workspaceId = workspaceIdForProject(projectId);
        if (!workspaceId.empty()) {
            if (auto* workspace = workspaceStore(workspaceId)) {
                std::string workspaceLessons;
                std::set<std::string> projectRules;
                for (const auto& lesson : selectedLessons) {
                    projectRules.insert(normalizeMemoryKey(lesson.lessonRule));
                }
                for (const auto& lesson : workspace->getLessons(workspaceScope(workspaceId))) {
                    if (!domains.empty() &&
                        std::find(domains.begin(), domains.end(), lesson.domain) == domains.end()) {
                        continue;
                    }
                    if (projectRules.count(normalizeMemoryKey(lesson.lessonRule))) continue;
                    std::string line = "[WORKSPACE] (Dominio: " + lesson.domain + ") " +
                                       lesson.lessonRule + "\n";
                    if (budget != std::string::npos &&
                        brief.size() + workspaceLessons.size() + line.size() > budget) {
                        truncated = true;
                        break;
                    }
                    workspaceLessons += line;
                }
                if (!workspaceLessons.empty()) {
                    const std::string header = "\n--- [ARESTRIX WORKSPACE MEMORY: LICOES] ---\n";
                    if (budget == std::string::npos ||
                        brief.size() + header.size() + workspaceLessons.size() <= budget) {
                        brief += header + workspaceLessons;
                    } else {
                        truncated = true;
                    }
                }
            }
        }
    }

    // Fatos/playbooks ranqueados (hibrido) entram depois das licoes e antes do
    // blackboard. Sem embeddings habilitados, o bloco nem e consultado.
    if (includeLessons) {
        std::string query;
        for (const auto& domain : domains) {
            std::string normalized = domain;
            std::replace(normalized.begin(), normalized.end(), '_', ' ');
            if (!normalized.empty()) {
                if (!query.empty()) query += " ";
                query += normalized;
            }
        }
        if (query.empty() && itemId != "pipeline") query = itemId;
        std::vector<RankedMemory> ranked;
        if (!query.empty() && hybridRecall(projectId, query, 4, domains, ranked)) {
            std::string facts;
            std::vector<std::string> factIds;
            for (const auto& memory : ranked) {
                if (memory.kind != "fact" && memory.kind != "playbook") continue;
                std::string line = std::string(memory.kind == "playbook" ? "[PLAYBOOK] " : "[FATO] ") +
                                   "[MEMO:" + memory.id + "] " + oneLine(memory.text, 500) + "\n";
                if (budget != std::string::npos && brief.size() + facts.size() + line.size() > budget) {
                    truncated = true;
                    break;
                }
                facts += line;
                factIds.push_back(memory.id);
            }
            if (!facts.empty()) {
                const std::string header =
                    "\n--- [ARESTRIX MEMORIA: FATOS RELEVANTES] ---\n"
                    "Se usar um item, cite o ID [MEMO:<id>] na resposta final.\n";
                if (budget == std::string::npos || brief.size() + header.size() + facts.size() <= budget) {
                    brief += header + facts;
                    if (info) info->factIds = factIds;
                } else {
                    truncated = true;
                }
            }
        }
    }

    // Playbooks por task_type/dominio. Apenas ativos entram no brief.
    if (includeLessons && (!domains.empty() || !taskType.empty())) {
        std::string playbookText;
        std::vector<std::string> playbookIds;
        std::set<std::string> seenTaskTypes;
        std::vector<std::string> taskTypes;
        if (!taskType.empty()) taskTypes.push_back(taskType);
        for (const auto& domain : domains) {
            if (std::find(taskTypes.begin(), taskTypes.end(), domain) == taskTypes.end()) {
                taskTypes.push_back(domain);
            }
        }
        for (const auto& type : taskTypes) {
            if (!seenTaskTypes.insert(type).second) continue;
            for (const auto& playbook : s->listPlaybooks(scopeId(projectId), type, "active", 1)) {
                auto stepsOpt = taskforge::jsonutil::tryParse(playbook.stepsJson);
                std::string stepsText;
                if (stepsOpt && stepsOpt->is_array()) {
                    for (const auto& step : *stepsOpt) {
                        if (!step.is_object()) continue;
                        const std::string action = step.value("tool",
                            step.value("action", std::string()));
                        if (action.empty()) continue;
                        if (!stepsText.empty()) stepsText += " -> ";
                        stepsText += action;
                    }
                }
                if (stepsText.empty()) continue;
                std::string line = "[PLAYBOOK] [MEMO:" + playbook.id + "] " + playbook.taskType +
                                   ": " + playbook.title + " | " + oneLine(stepsText, 400) + "\n";
                if (budget != std::string::npos &&
                    brief.size() + playbookText.size() + line.size() > budget) {
                    truncated = true;
                    break;
                }
                playbookText += line;
                playbookIds.push_back(playbook.id);
            }
        }
        if (!playbookText.empty()) {
            const std::string header = "\n--- [ARESTRIX MEMORIA: PLAYBOOKS] ---\n";
            if (budget == std::string::npos ||
                brief.size() + header.size() + playbookText.size() <= budget) {
                brief += header + playbookText;
                if (info) info->playbookIds = playbookIds;
            } else {
                truncated = true;
            }
        }
    }

    if (!workingText.empty()) {
        if (budget == std::string::npos || brief.size() + workingText.size() <= budget) {
            brief += workingText;
            if (info) info->workingKeys = workingKeys;
        } else {
            truncated = true;
        }
    }

    if (truncated) brief += "\n[... truncado pelo orcamento de memoria]\n";
    if (info) {
        info->chars = brief.size();
        info->estimatedTokens = (brief.size() + 3) / 4;
        info->truncated = truncated;
    }
    return brief;
}

bool MemoryService::recordBriefHits(const std::string& projectId, const std::string& runId,
                                    const std::string& nodeId, const MemoryBriefInfo& info) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return false;
    const std::string scope = scopeId(projectId);
    bool ok = true;
    for (const auto& id : info.lessonIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "lesson", true, false) && ok;
    }
    for (const auto& id : info.factIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "fact", true, false) && ok;
    }
    for (const auto& id : info.playbookIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "playbook", true, false) && ok;
    }
    for (const auto& key : info.workingKeys) {
        ok = t->recordHit(scope, runId, nodeId, "working:" + key, "working", true, false) && ok;
    }
    return ok;
}

int MemoryService::markHitsUsed(const std::string& projectId, const std::string& runId,
                                const std::string& nodeId, const std::vector<std::string>& memoryIds) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->markHitsUsed(scopeId(projectId), runId, nodeId, memoryIds) : 0;
}

bool MemoryService::finishHits(const std::string& projectId, const std::string& runId,
                               const std::string& nodeId, const std::string& outcome) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return false;
    const std::string scope = scopeId(projectId);
    const bool ok = t->finishHits(scope, runId, nodeId, outcome);
    // O conhecimento so e reforcado se o store do projeto ja existe; nao abrir
    // o banco de conhecimento apenas para registrar telemetria. Se os dois
    // bancos apontarem para o mesmo arquivo, o proprio finishHits ja reforcou.
    if (auto* k = existingStore(projectId)) {
        if (databasePath(projectId) != telemetryDatabasePath(projectId)) {
            std::vector<std::string> ids;
            for (const auto& ref : t->usedHitRefs(scope, runId, nodeId)) ids.push_back(ref.id);
            if (!ids.empty()) k->reinforceFromHits(scope, ids, outcome);
        }
    }
    return ok;
}

taskforge::Json MemoryService::hitSummary(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->hitSummary(scopeId(projectId)) : taskforge::Json::object();
}

void MemoryService::clearLessons(const std::string& projectId) {
    if (auto* s = ensureStore(projectId)) s->clearLessons(scopeId(projectId));
}

std::string MemoryService::recordEpisode(const std::string& projectId, const std::string& runId,
                                         const std::string& nodeId, const std::string& itemId,
                                         const std::string& kind, const std::string& content,
                                         const taskforge::Json& metadata) {
    auto* t = ensureTelemetry(projectId);
    const std::string id = t
        ? t->recordEpisode(scopeId(projectId), runId, nodeId, itemId, kind, content, metadata)
        : std::string();
    if (!id.empty()) noteMaintenanceEpisode(projectId);
    return id;
}

std::string MemoryService::recordEpisode(const std::string& projectId, const MemoryEpisodeRecord& episode) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return std::string();
    MemoryEpisodeRecord copy = episode;
    copy.scopeId = scopeId(projectId);
    const std::string id = t->recordEpisode(copy);
    if (!id.empty()) noteMaintenanceEpisode(projectId);
    return id;
}

taskforge::Json MemoryService::getEpisode(const std::string& projectId, const std::string& episodeId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->getEpisode(scopeId(projectId), episodeId) : taskforge::Json::object();
}

size_t MemoryService::extractProposals(const std::string& projectId, const std::string& episodeId) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return 0;
    const std::string scope = scopeId(projectId);
    const taskforge::Json episode = t->getEpisode(scope, episodeId);
    if (!episode.is_object() || episode.empty()) return 0;
    size_t proposals = 0;
    for (const auto& draft : MemoryExtractor::extract(episode)) {
        if (!s->recordProposal(scope, draft.kind, draft.payload, episodeId, draft.evidence,
                               draft.confidence).empty()) {
            ++proposals;
        }
    }
    MemoryPlaybookCompiler::observe(*s, scope, episode);
    return proposals;
}

taskforge::Json MemoryService::listProposals(const std::string& projectId, const std::string& status,
                                             size_t limit) {
    auto* s = ensureStore(projectId);
    taskforge::Json out = taskforge::Json::array();
    if (!s) return out;
    for (const auto& proposal : s->listProposals(scopeId(projectId), status, limit)) {
        auto payloadOpt = taskforge::jsonutil::tryParse(proposal.payloadJson);
        auto evidenceOpt = taskforge::jsonutil::tryParse(proposal.evidenceJson);
        taskforge::Json payload = payloadOpt ? *payloadOpt : taskforge::Json::object();
        taskforge::Json evidence = evidenceOpt ? *evidenceOpt : taskforge::Json::array();
        out.push_back(taskforge::Json{{"id", proposal.id},
                                      {"scopeId", proposal.scopeId},
                                      {"kind", proposal.kind},
                                      {"payload", payload},
                                      {"payloadJson", proposal.payloadJson},
                                      {"sourceEpisodeId", proposal.sourceEpisodeId},
                                      {"evidence", evidence},
                                      {"evidenceJson", proposal.evidenceJson},
                                      {"confidence", proposal.confidence},
                                      {"status", proposal.status},
                                      {"createdAt", proposal.createdAt},
                                      {"reviewedAt", proposal.reviewedAt},
                                      {"reviewedBy", proposal.reviewedBy}});
    }
    return out;
}

bool MemoryService::promoteProposal(const std::string& projectId, const std::string& proposalId,
                                    const std::string& reviewer, std::string& error,
                                    std::string* promotedId) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    return s->promoteProposal(scopeId(projectId), proposalId, reviewer, error, promotedId);
}

bool MemoryService::rejectProposal(const std::string& projectId, const std::string& proposalId,
                                   const std::string& reviewer, const std::string& reason,
                                   std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    return s->rejectProposal(scopeId(projectId), proposalId, reviewer, reason, error);
}

std::string MemoryService::recordEdge(const std::string& projectId, const std::string& srcId,
                                      const std::string& dstId, const std::string& relation,
                                      const taskforge::Json& value, const std::string& validFrom,
                                      const std::string& validTo, double confidence,
                                      const std::string& status, const std::string& sourceKind,
                                      const std::string& sourceRef, const taskforge::Json& metadata) {
    auto* s = ensureStore(projectId);
    return s ? s->recordEdge(scopeId(projectId), srcId, dstId, relation, value, validFrom, validTo,
                             confidence, status, sourceKind, sourceRef, metadata)
             : std::string();
}

std::vector<MemoryEdgeRecord> MemoryService::neighbors(const std::string& projectId,
                                                       const std::string& nodeId, int depth,
                                                       const std::string& asOf) {
    auto* s = ensureStore(projectId);
    return s ? s->neighbors(scopeId(projectId), nodeId, depth, asOf) : std::vector<MemoryEdgeRecord>();
}

std::vector<MemoryEdgeRecord> MemoryService::searchEdges(const std::string& projectId,
                                                         const std::string& text, size_t limit) {
    auto* s = ensureStore(projectId);
    return s ? s->searchEdges(scopeId(projectId), text, limit) : std::vector<MemoryEdgeRecord>();
}

std::string MemoryService::searchContext(const std::string& projectId, const std::string& text,
                                         size_t limit, const std::vector<std::string>& domains,
                                         std::vector<MemoryHitRef>* hits) {
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    std::string result;
    std::string hybrid;
    if (hybridSearch(projectId, text, limit, domains, hits, hybrid)) {
        result = std::move(hybrid);
    } else {
        result = s->searchContext(scopeId(projectId), text, limit, domains, hits);
    }

    const std::string workspaceId = workspaceIdForProject(projectId);
    if (!workspaceId.empty()) {
        if (auto* workspace = workspaceStore(workspaceId)) {
            std::string fromWorkspace = workspace->searchContext(
                workspaceScope(workspaceId), text, limit, domains, nullptr);
            if (!fromWorkspace.empty()) {
                if (!result.empty()) result += "\n";
                result += "[ARESTRIX WORKSPACE MEMORY]\n" + fromWorkspace;
            }
        }
    }
    return result;
}

MemoryService::EmbeddingRuntime& MemoryService::embeddingRuntimeLocked(
    const std::string& projectId) {
    const std::string id = projectKey(projectId);
    auto it = embeddings_.find(id);
    if (it != embeddings_.end()) return it->second;

    EmbeddingRuntime runtime;
    runtime.config = embeddingConfigFromJson(embeddingConfigToJson(defaultEmbeddingConfig()));
    runtime.provider = makeEmbeddingProvider(runtime.config);
    auto inserted = embeddings_.emplace(id, std::move(runtime));
    return inserted.first->second;
}

bool MemoryService::configureEmbeddings(const std::string& projectId,
                                        const EmbeddingConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    EmbeddingConfig normalized = embeddingConfigFromJson(embeddingConfigToJson(config));
    EmbeddingRuntime runtime;
    runtime.config = normalized;
    runtime.provider = makeEmbeddingProvider(normalized);
    embeddings_[projectKey(projectId)] = std::move(runtime);
    return true;
}

void MemoryService::setEmbeddingProvider(const std::string& projectId,
                                         std::shared_ptr<IEmbeddingProvider> provider) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    runtime.provider = provider ? std::move(provider) : std::make_shared<NullEmbeddingProvider>();
    runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
}

EmbeddingConfig MemoryService::embeddingConfig(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return embeddingRuntimeLocked(projectId).config;
}

taskforge::Json MemoryService::embeddingStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    taskforge::Json out = embeddingConfigToJson(runtime.config);
    const bool available = runtime.provider && runtime.provider->available();
    out["available"] = available;
    out["active"] = runtime.config.enabled && available;
    out["providerModel"] = runtime.provider ? runtime.provider->modelId() : std::string("none");
    out["providerDim"] = runtime.provider ? runtime.provider->dim() : 0;
    out["cooldown"] = embeddingInCooldown(runtime.cooldownUntil);
    if (auto* s = ensureStore(projectId)) {
        const std::string scope = scopeId(projectId);
        const long long indexed = static_cast<long long>(s->embeddingCount(scope, runtime.config.model));
        const long long candidates = static_cast<long long>(s->embeddingObjects(scope).size());
        out["indexed"] = indexed;
        out["candidates"] = candidates;
        out["pending"] = std::max<long long>(0, candidates - indexed);
    } else {
        out["indexed"] = 0;
        out["candidates"] = 0;
        out["pending"] = 0;
    }
    return out;
}

taskforge::Json MemoryService::testEmbeddings(const std::string& projectId,
                                              const std::string& sample) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    const auto started = std::chrono::steady_clock::now();
    taskforge::Json out = taskforge::Json::object();
    out["enabled"] = runtime.config.enabled;
    out["model"] = runtime.config.model;
    out["available"] = runtime.provider && runtime.provider->available();
    if (!runtime.provider || !runtime.provider->available()) {
        out["ok"] = false;
        out["error"] = "provider de embeddings indisponivel (configure embeddingBaseUrl/model)";
        return out;
    }
    std::vector<Embedding> embeddings;
    std::string error;
    const std::string text = sample.empty() ? "teste de memoria local" : sample;
    const bool ok = runtime.provider->embed({text}, true, embeddings, error);
    if (ok) runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
    else embeddingStartCooldown(runtime.cooldownUntil);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    out["ok"] = ok;
    out["latencyMs"] = static_cast<long long>(elapsed);
    out["error"] = error;
    if (ok && !embeddings.empty()) {
        out["dim"] = embeddings[0].dim;
        out["values"] = static_cast<long long>(embeddings[0].values.size());
        out["sample"] = text;
    }
    return out;
}

taskforge::Json MemoryService::reindexEmbeddings(const std::string& projectId, bool force,
                                                 std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    auto& runtime = embeddingRuntimeLocked(projectId);
    taskforge::Json out = taskforge::Json::object();
    if (!s) {
        error = "banco de memoria indisponivel";
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    if (!runtime.provider || !runtime.provider->available()) {
        error = "provider de embeddings indisponivel";
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    const auto stats = MemoryIndexer::index(*s, *runtime.provider, runtime.config,
                                            scopeId(projectId), force, 0, error);
    if (error.empty() && stats.failed > 0) {
        error = std::to_string(stats.failed) + " embedding(s) falharam";
    }
    if (error.empty()) runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
    else embeddingStartCooldown(runtime.cooldownUntil);
    out["ok"] = error.empty();
    out["model"] = runtime.config.model;
    out["candidates"] = static_cast<long long>(stats.candidates);
    out["indexed"] = static_cast<long long>(stats.indexed);
    out["skipped"] = static_cast<long long>(stats.skipped);
    out["removed"] = static_cast<long long>(stats.removed);
    out["failed"] = static_cast<long long>(stats.failed);
    out["error"] = error;
    return out;
}

taskforge::Json MemoryService::consolidate(const std::string& projectId,
                                           const ConsolidationOptions& options,
                                           std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto report = MemoryConsolidator::run(*s, scopeId(projectId), options, error);
    taskforge::Json out = report.toJson();
    out["projectId"] = projectId;
    if (!error.empty()) out["error"] = error;
    return out;
}

bool MemoryService::configureMaintenance(const std::string& projectId,
                                         const MemoryMaintenanceConfig& config) {
    const std::string id = projectKey(projectId);
    stopMaintenance(id);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!ensureStore(id)) return false;
    auto persistentIt = persistent_.find(id);
    if (persistentIt == persistent_.end() || !persistentIt->second ||
        !config.enabled || config.mode == "off") {
        return true;
    }
    try {
        startMaintenanceLocked(id, config);
    } catch (const std::exception&) {
        maintenance_.erase(id);
        return false;
    }
    return true;
}

MemoryMaintenanceConfig MemoryService::maintenanceConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) return MemoryMaintenanceConfig{};
    std::lock_guard<std::mutex> runtimeLock(it->second->mutex);
    return it->second->config;
}

void MemoryService::noteMaintenanceEpisode(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) return;
    auto* runtime = it->second.get();
    std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
    if (runtime->config.mode == "off") return;
    ++runtime->episodes;
    if (runtime->config.mode == "idle") {
        runtime->idleDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(runtime->config.idleSeconds);
    }
    runtime->cv.notify_all();
}

void MemoryService::startMaintenanceLocked(const std::string& projectId,
                                           const MemoryMaintenanceConfig& config) {
    auto runtime = std::make_unique<MaintenanceRuntime>();
    runtime->config = config;
    MaintenanceRuntime* raw = runtime.get();
    maintenance_[projectId] = std::move(runtime);
    raw->thread = std::thread([this, projectId, raw]() {
        maintenanceLoop(projectId, raw);
    });
}

void MemoryService::stopMaintenance(const std::string& projectId) {
    std::unique_ptr<MaintenanceRuntime> runtime;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = maintenance_.find(projectKey(projectId));
        if (it == maintenance_.end()) return;
        runtime = std::move(it->second);
        maintenance_.erase(it);
    }
    if (!runtime) return;
    {
        std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
        runtime->stop = true;
    }
    runtime->cv.notify_all();
    if (runtime->thread.joinable()) runtime->thread.join();
}

void MemoryService::stopAllMaintenance() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& entry : maintenance_) ids.push_back(entry.first);
    }
    for (const auto& id : ids) stopMaintenance(id);
}

void MemoryService::maintenanceLoop(const std::string& projectId,
                                    MaintenanceRuntime* runtime) {
    std::unique_lock<std::mutex> lock(runtime->mutex);
    while (!runtime->stop) {
        const MemoryMaintenanceConfig config = runtime->config;
        if (config.mode == "after_episodes") {
            runtime->cv.wait(lock, [&]() {
                return runtime->stop || runtime->episodes >= config.episodeThreshold;
            });
            if (runtime->stop) break;
        } else if (config.mode == "idle") {
            runtime->cv.wait(lock, [&]() {
                return runtime->stop || runtime->episodes > 0;
            });
            if (runtime->stop) break;
            auto deadline = runtime->idleDeadline;
            while (!runtime->stop && runtime->episodes > 0) {
                const auto state = runtime->cv.wait_until(lock, deadline);
                if (state == std::cv_status::timeout) break;
                if (runtime->stop) break;
                deadline = runtime->idleDeadline;
            }
            if (runtime->stop || runtime->episodes == 0) continue;
        } else {
            break;
        }

        runtime->busy = true;
        ++runtime->triggers;
        runtime->episodes = 0;
        const MemoryMaintenanceConfig runConfig = runtime->config;
        lock.unlock();

        std::string error;
        taskforge::Json report = consolidate(projectId, runConfig.options, error);

        lock.lock();
        runtime->busy = false;
        ++runtime->runs;
        runtime->lastRunAt = isoNow();
        runtime->lastReason = runConfig.mode;
        if (error.empty()) {
            runtime->lastError.clear();
        } else {
            ++runtime->failures;
            runtime->lastError = error;
        }
        runtime->lastReport = report;
        runtime->cv.notify_all();
    }
}

taskforge::Json MemoryService::maintenanceStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) {
        return taskforge::Json{{"enabled", false}, {"mode", "off"}};
    }
    MaintenanceRuntime* runtime = it->second.get();
    std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
    taskforge::Json out = maintenanceConfigToJson(runtime->config);
    out["busy"] = runtime->busy;
    out["episodesSinceRun"] = runtime->episodes;
    out["triggers"] = runtime->triggers;
    out["runs"] = runtime->runs;
    out["failures"] = runtime->failures;
    out["lastRunAt"] = runtime->lastRunAt;
    out["lastReason"] = runtime->lastReason;
    out["lastError"] = runtime->lastError;
    out["lastReport"] = runtime->lastReport;
    if (runtime->config.mode == "after_episodes") {
        out["nextInEpisodes"] = std::max<long long>(
            0, runtime->config.episodeThreshold - runtime->episodes);
    }
    if (runtime->config.mode == "idle" && runtime->episodes > 0) {
        const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            runtime->idleDeadline - std::chrono::steady_clock::now()).count();
        out["nextInSeconds"] = std::max<long long>(0, remaining);
    }
    return out;
}

taskforge::Json MemoryService::runMaintenanceOnce(const std::string& projectId,
                                                  const std::string& reason,
                                                  std::string& error) {
    MemoryMaintenanceConfig config;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = maintenance_.find(projectKey(projectId));
        if (it != maintenance_.end() && it->second) {
            std::lock_guard<std::mutex> runtimeLock(it->second->mutex);
            config = it->second->config;
        }
    }
    const taskforge::Json report = consolidate(projectId, config.options, error);

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it != maintenance_.end() && it->second) {
        MaintenanceRuntime* runtime = it->second.get();
        std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
        ++runtime->runs;
        runtime->lastRunAt = isoNow();
        runtime->lastReason = reason;
        runtime->lastReport = report;
        if (error.empty()) {
            runtime->lastError.clear();
        } else {
            ++runtime->failures;
            runtime->lastError = error;
        }
    }
    return report;
}

taskforge::Json MemoryService::consolidationStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    const std::string scope = scopeId(projectId);
    taskforge::Json out = s->consolidationSummary(scope);
    out["maintenance"] = maintenanceStatus(projectId);
    taskforge::Json edges = taskforge::Json::array();
    const auto names = s->nodeNames(scope);
    for (const auto& edge : s->listEdges(scope, "", 20000)) {
        if (edge.relation != "CONTRADICTS") continue;
        auto valueOpt = taskforge::jsonutil::tryParse(edge.valueJson);
        taskforge::Json value = valueOpt && valueOpt->is_object()
            ? *valueOpt : taskforge::Json::object();
        auto src = names.find(edge.srcId);
        auto dst = names.find(edge.dstId);
        edges.push_back(taskforge::Json{
            {"id", edge.id},
            {"srcId", edge.srcId},
            {"srcName", src == names.end() ? std::string() : src->second},
            {"dstId", edge.dstId},
            {"dstName", dst == names.end() ? std::string() : dst->second},
            {"edgeId", value.value("edgeId", std::string())},
            {"edgeStatus", value.value("status", std::string())},
            {"confidence", value.value("confidence", edge.confidence)},
            {"status", edge.status}});
    }
    out["conflictEdges"] = edges;
    return out;
}

taskforge::Json MemoryService::listPlaybooks(const std::string& projectId,
                                             const std::string& taskType,
                                             const std::string& status, size_t limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    taskforge::Json out = taskforge::Json::array();
    if (!s) return out;
    for (const auto& playbook : s->listPlaybooks(scopeId(projectId), taskType, status, limit)) {
        auto steps = taskforge::jsonutil::tryParse(playbook.stepsJson);
        auto preconditions = taskforge::jsonutil::tryParse(playbook.preconditionsJson);
        auto evidence = taskforge::jsonutil::tryParse(playbook.evidenceJson);
        out.push_back(taskforge::Json{
            {"id", playbook.id},
            {"taskType", playbook.taskType},
            {"title", playbook.title},
            {"steps", steps && steps->is_array() ? *steps : taskforge::Json::array()},
            {"preconditions", preconditions && preconditions->is_object()
                ? *preconditions : taskforge::Json::object()},
            {"successCount", playbook.successCount},
            {"failureCount", playbook.failureCount},
            {"confidence", playbook.confidence},
            {"status", playbook.status},
            {"sourceRef", playbook.sourceRef},
            {"evidence", evidence && evidence->is_array() ? *evidence : taskforge::Json::array()},
            {"createdAt", playbook.createdAt},
            {"updatedAt", playbook.updatedAt}});
    }
    return out;
}

bool MemoryService::hybridRecall(const std::string& projectId, const std::string& text,
                                 size_t limit, const std::vector<std::string>& domains,
                                 std::vector<RankedMemory>& output) {
    if (text.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return false;
    auto& runtime = embeddingRuntimeLocked(projectId);
    if (!runtime.config.enabled || !runtime.provider || !runtime.provider->available()) return false;
    if (embeddingInCooldown(runtime.cooldownUntil)) return false;

    // C4: a busca e somente leitura; indexacao acontece em reindex/importacao.
    std::vector<Embedding> queryEmbeddings;
    std::string embedError;
    if (!runtime.provider->embed({text}, true, queryEmbeddings, embedError) ||
        queryEmbeddings.empty()) {
        embeddingStartCooldown(runtime.cooldownUntil);
        return false;
    }

    const size_t candidateLimit = std::max<size_t>(limit * 3, 20);
    auto lexical = s->lexicalCandidates(scopeId(projectId), text, candidateLimit, domains);
    auto vector = MemoryVectorIndex::search(*s, scopeId(projectId), runtime.config.model,
                                            queryEmbeddings[0].values, candidateLimit, domains);
    output = MemoryRanker::fuse(lexical, vector, limit);
    return true;
}

bool MemoryService::hybridSearch(const std::string& projectId, const std::string& text,
                                 size_t limit, const std::vector<std::string>& domains,
                                 std::vector<MemoryHitRef>* hits, std::string& output) {
    std::vector<RankedMemory> ranked;
    if (!hybridRecall(projectId, text, limit, domains, ranked)) return false;
    output = formatRanked(ranked, limit, hits);
    return true;
}

int MemoryService::recordConsultedHits(const std::string& projectId, const std::string& runId,
                                       const std::string& nodeId,
                                       const std::vector<MemoryHitRef>& refs) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->recordConsultedHits(scopeId(projectId), runId, nodeId, refs) : 0;
}

bool MemoryService::invalidateEdge(const std::string& projectId, const std::string& edgeId,
                                   const std::string& validTo, const std::string& reason,
                                   const std::string& actor) {
    auto* s = ensureStore(projectId);
    return s && s->invalidateEdge(edgeId, validTo, reason, actor);
}

bool MemoryService::recordUsage(const MemoryUsageRecord& usage) {
    if (usage.scopeId.empty()) return false;
    auto* t = ensureTelemetry(usage.scopeId);
    if (!t) return false;
    MemoryUsageRecord copy = usage;
    copy.scopeId = scopeId(usage.scopeId);
    return t->recordUsage(copy);
}

taskforge::Json MemoryService::usageSummary(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->usageSummary(scopeId(projectId)) : taskforge::Json::object();
}

std::string MemoryService::recordValidation(const std::string& projectId,
                                            const MemoryValidationRecord& validation) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return "";
    MemoryValidationRecord copy = validation;
    if (copy.scopeId.empty()) copy.scopeId = scopeId(projectId);
    const std::string id = t->recordValidation(copy);
    // B5/B6: a validacao independente e o unico gatilho de promocao de playbook.
    if (!id.empty() && copy.ok && !copy.episodeId.empty()) {
        if (auto* knowledge = existingStore(projectId)) {
            const std::string scope = scopeId(projectId);
            const taskforge::Json episode = t->getEpisode(scope, copy.episodeId);
            const std::string taskType = episode.value("taskType", std::string());
            const auto samples = t->validationGateSamples(scope, taskType, 20);
            std::string promotedId;
            std::string reason;
            knowledge->autoPromotePlaybook(scope, copy.episodeId,
                                           copy.validatorId, copy.validatorVersion,
                                           samples, promotedId, reason);
            const auto outcomes = t->validationOutcomes(scope, taskType, 10);
            std::string demoteReason;
            knowledge->autoDemotePlaybooks(scope, taskType, outcomes, demoteReason);
        }
    }
    return id;
}

std::vector<MemoryValidationRecord> MemoryService::listValidations(const std::string& projectId,
                                                                   const std::string& episodeId,
                                                                   size_t limit) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->listValidations(scopeId(projectId), episodeId, limit)
             : std::vector<MemoryValidationRecord>();
}

bool MemoryService::unpromotePlaybook(const std::string& projectId, const std::string& playbookId,
                                      const std::string& reason, std::string& error) {
    auto* s = existingStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    return s->unpromotePlaybook(scopeId(projectId), playbookId, reason, error);
}

taskforge::Json MemoryService::toJson(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    taskforge::Json out = s->toJson(scopeId(projectId));
    auto* t = ensureTelemetry(projectId);
    if (t) {
        out["usage"] = t->usageSummary(scopeId(projectId));
        out["hits"] = t->hitSummary(scopeId(projectId));
    }
    out["projectId"] = projectId;
    out["persistent"] = isPersistent(projectId);
    out["database"] = databasePath(projectId);
    out["telemetry"] = telemetryDatabasePath(projectId);
    out["maintenance"] = maintenanceStatus(projectId);
    return out;
}

taskforge::Json MemoryService::toCognitiveJson(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    taskforge::Json out = s->toCognitiveJson(scopeId(projectId));
    out["projectId"] = projectId;
    return out;
}

bool MemoryService::importCognitiveJson(const std::string& projectId, const taskforge::Json& data,
                                        std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    return s->importCognitiveJson(scopeId(projectId), data, error);
}

bool MemoryService::clearSession(const std::string& projectId) {
    if (isPersistent(projectId)) return true;
    stopMaintenance(projectId);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (auto* s = ensureStore(projectId)) {
        if (s->resetInMemory()) return true;
        // Defesa em profundidade: nunca purgar um store de arquivo. Se isto
        // acontecer, o regime nao persistente abriu um arquivo por engano.
        std::cerr << "[MEMORIA] clearSession ignorado: store nao efemero ("
                  << s->path() << ")\n";
        return false;
    }
    return false;
}

void MemoryService::closeProject(const std::string& projectId) {
    stopMaintenance(projectId);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stores_.erase(projectKey(projectId));
    persistent_.erase(projectKey(projectId));
    paths_.erase(projectKey(projectId));
    telemetry_.erase(projectKey(projectId));
    telemetryPaths_.erase(projectKey(projectId));
    embeddings_.erase(projectKey(projectId));
    projectWorkspace_.erase(projectKey(projectId));
}

} // namespace memory
} // namespace taskforge
