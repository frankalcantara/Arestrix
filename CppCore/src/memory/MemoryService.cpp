#include "MemoryService.hpp"
#include "MemoryExtractor.hpp"
#include "MemoryIndexer.hpp"
#include "MemorySanitizer.hpp"
#include "MemoryPlaybookCompiler.hpp"
#include "MemoryRanker.hpp"
#include "MemoryVectorIndex.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

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
    auto it = paths_.find(projectId);
    return it == paths_.end() ? std::string() : it->second;
}

bool MemoryService::configure(const std::string& projectId, bool persistent,
                              const std::string& dbPath,
                              const std::string& legacyBlackboardPath,
                              const std::string& legacyLessonsPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectId.empty() ? "default" : projectId;
    const std::string path = persistent
        ? (dbPath.empty() ? defaultPath(id) : dbPath)
        : (dbPath.empty() ? ":memory:" : dbPath);

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
        importLegacy(id, legacyBlackboardPath, legacyLessonsPath);
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
    auto it = persistent_.find(projectId);
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

void MemoryService::clearProject(const std::string& projectId) {
    purgeProject(projectId, false, true);
}

bool MemoryService::purgeProject(const std::string& projectId, bool keepAudit, bool vacuum) {
    auto* s = ensureStore(projectId);
    if (!s) return false;
    MemoryPurgeOptions options;
    options.keepAudit = keepAudit;
    options.vacuum = vacuum;
    return s->purge(scopeId(projectId), options);
}

MemoryPruneResult MemoryService::prune(const std::string& projectId, int olderThanDays) {
    auto* s = ensureStore(projectId);
    if (!s) return MemoryPruneResult{};
    return s->prune(scopeId(projectId), olderThanDays);
}

std::string MemoryService::recordLesson(const std::string& projectId, const std::string& domain,
                                        const std::string& triggerError, const std::string& lessonRule,
                                        const std::string& sourceItem, const std::string& sourceKind) {
    auto* s = ensureStore(projectId);
    return s ? s->recordLesson(scopeId(projectId), domain, triggerError, lessonRule, sourceItem, sourceKind)
             : std::string();
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
                                      bool includeBlackboard, MemoryBriefInfo* info) {
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

    std::string blackboardText;
    std::vector<std::string> blackboardKeys;
    if (includeBlackboard) {
        auto snapshot = s->workingSnapshot(scopeId(projectId), itemId, allowedKeys);
        if (snapshot.is_object() && snapshot.size() > 0) {
            blackboardText = "\n[ARESTRIX MEMORY - BLACKBOARD]\n" + snapshot.dump(2) + "\n";
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it) blackboardKeys.push_back(it.key());
        }
    }

    const size_t budget = maxTokens == 0 ? std::string::npos : maxTokens * 4;   // ~4 caracteres/token
    std::string brief;
    bool truncated = false;

    if (!selectedLessons.empty()) {
        const std::string header =
            "\n--- [ARESTRIX MEMORIA SEMANTICA: LICOES APRENDIDAS - PROJETO: " + scopeId(projectId) + "] ---\n"
            "Atencao: os itens anteriores identificaram as seguintes diretrizes para prevencao de erros:\n";
        if (header.size() <= budget) {
            brief += header;
            int lessonNumber = 1;
            for (const auto& lesson : selectedLessons) {
                std::ostringstream line;
                line << "[" << lessonNumber << "] (Dominio: " << lesson.domain
                     << ", confianca " << lesson.confidence << ") " << lesson.lessonRule << "\n";
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
                                   oneLine(memory.text, 500) + "\n";
                if (budget != std::string::npos && brief.size() + facts.size() + line.size() > budget) {
                    truncated = true;
                    break;
                }
                facts += line;
                factIds.push_back(memory.id);
            }
            if (!facts.empty()) {
                const std::string header = "\n--- [ARESTRIX MEMORIA: FATOS RELEVANTES] ---\n";
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
    if (includeLessons && !domains.empty()) {
        std::string playbookText;
        std::vector<std::string> playbookIds;
        std::set<std::string> seenTaskTypes;
        for (const auto& domain : domains) {
            if (!seenTaskTypes.insert(domain).second) continue;
            for (const auto& playbook : s->listPlaybooks(scopeId(projectId), domain, "active", 1)) {
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
                std::string line = "[PLAYBOOK] " + playbook.taskType + ": " + playbook.title +
                                   " | " + oneLine(stepsText, 400) + "\n";
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

    if (!blackboardText.empty()) {
        if (budget == std::string::npos || brief.size() + blackboardText.size() <= budget) {
            brief += blackboardText;
            if (info) info->workingKeys = blackboardKeys;
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
    auto* s = ensureStore(projectId);
    if (!s) return false;
    const std::string scope = scopeId(projectId);
    bool ok = true;
    for (const auto& id : info.lessonIds) {
        ok = s->recordHit(scope, runId, nodeId, id, "lesson", true, false) && ok;
    }
    for (const auto& id : info.factIds) {
        ok = s->recordHit(scope, runId, nodeId, id, "fact", true, false) && ok;
    }
    for (const auto& id : info.playbookIds) {
        ok = s->recordHit(scope, runId, nodeId, id, "playbook", true, false) && ok;
    }
    for (const auto& key : info.workingKeys) {
        ok = s->recordHit(scope, runId, nodeId, "working:" + key, "working", true, false) && ok;
    }
    return ok;
}

int MemoryService::markHitsUsed(const std::string& projectId, const std::string& runId,
                                const std::string& nodeId, const std::vector<std::string>& memoryIds) {
    auto* s = ensureStore(projectId);
    return s ? s->markHitsUsed(scopeId(projectId), runId, nodeId, memoryIds) : 0;
}

bool MemoryService::finishHits(const std::string& projectId, const std::string& runId,
                               const std::string& nodeId, const std::string& outcome) {
    auto* s = ensureStore(projectId);
    return s ? s->finishHits(scopeId(projectId), runId, nodeId, outcome) : false;
}

taskforge::Json MemoryService::hitSummary(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->hitSummary(scopeId(projectId)) : taskforge::Json::object();
}

void MemoryService::clearLessons(const std::string& projectId) {
    if (auto* s = ensureStore(projectId)) s->clearLessons(scopeId(projectId));
}

std::string MemoryService::recordEpisode(const std::string& projectId, const std::string& runId,
                                         const std::string& nodeId, const std::string& itemId,
                                         const std::string& kind, const std::string& content,
                                         const taskforge::Json& metadata) {
    auto* s = ensureStore(projectId);
    return s ? s->recordEpisode(scopeId(projectId), runId, nodeId, itemId, kind, content, metadata)
             : std::string();
}

std::string MemoryService::recordEpisode(const std::string& projectId, const MemoryEpisodeRecord& episode) {
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    MemoryEpisodeRecord copy = episode;
    copy.scopeId = scopeId(projectId);
    return s->recordEpisode(copy);
}

taskforge::Json MemoryService::getEpisode(const std::string& projectId, const std::string& episodeId) {
    auto* s = ensureStore(projectId);
    return s ? s->getEpisode(scopeId(projectId), episodeId) : taskforge::Json::object();
}

size_t MemoryService::extractProposals(const std::string& projectId, const std::string& episodeId) {
    auto* s = ensureStore(projectId);
    if (!s) return 0;
    const std::string scope = scopeId(projectId);
    const size_t proposals = MemoryExtractor::extractAndStore(*s, scope, episodeId);
    const taskforge::Json episode = s->getEpisode(scope, episodeId);
    if (episode.is_object() && !episode.empty()) {
        MemoryPlaybookCompiler::observe(*s, scope, episode);
    }
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

taskforge::Json MemoryService::consolidationStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    const std::string scope = scopeId(projectId);
    taskforge::Json out = s->consolidationSummary(scope);
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

    std::string indexError;
    MemoryIndexer::index(*s, *runtime.provider, runtime.config, scopeId(projectId),
                         false, 32, indexError);
    if (!indexError.empty()) {
        embeddingStartCooldown(runtime.cooldownUntil);
        return false;
    }

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
    auto* s = ensureStore(projectId);
    return s ? s->recordConsultedHits(scopeId(projectId), runId, nodeId, refs) : 0;
}

bool MemoryService::invalidateEdge(const std::string& projectId, const std::string& edgeId,
                                   const std::string& validTo, const std::string& reason,
                                   const std::string& actor) {
    auto* s = ensureStore(projectId);
    return s && s->invalidateEdge(edgeId, validTo, reason, actor);
}

bool MemoryService::recordUsage(const MemoryUsageRecord& usage) {
    if (usage.scopeId.empty()) return false;
    auto* s = ensureStore(usage.scopeId);
    if (!s) return false;
    MemoryUsageRecord copy = usage;
    copy.scopeId = scopeId(usage.scopeId);
    return s->recordUsage(copy);
}

taskforge::Json MemoryService::usageSummary(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->usageSummary(scopeId(projectId)) : taskforge::Json::object();
}

taskforge::Json MemoryService::toJson(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    taskforge::Json out = s->toJson(scopeId(projectId));
    out["projectId"] = projectId;
    out["persistent"] = isPersistent(projectId);
    out["database"] = databasePath(projectId);
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

bool MemoryService::importLegacySnapshot(const std::string& projectId, const taskforge::Json& data,
                                         std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    return s->importLegacySnapshot(scopeId(projectId), data, error);
}

bool MemoryService::clearSession(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (isPersistent(projectId)) return true;
    if (auto* s = ensureStore(projectId)) {
        if (s->resetInMemory()) return true;
        s->clearScopeData(scopeId(projectId));
        return true;
    }
    return false;
}

void MemoryService::closeProject(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stores_.erase(projectId);
    persistent_.erase(projectId);
    paths_.erase(projectId);
    embeddings_.erase(projectKey(projectId));
    projectWorkspace_.erase(projectKey(projectId));
}

bool MemoryService::importLegacy(const std::string& projectId,
                                 const std::string& blackboardPath,
                                 const std::string& lessonsPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return false;
    const std::string id = projectId.empty() ? "default" : projectId;
    const std::string flag = "legacy_imported:" + id;
    if (s->hasMeta(flag)) return true;

    const std::string safe = safeName(id);
    const std::string bbPath = blackboardPath.empty() ? ("arestrix-blackboard-" + safe + ".json") : blackboardPath;
    const std::string lsPath = lessonsPath.empty() ? ("arestrix-memory-" + safe + ".json") : lessonsPath;

    if (std::filesystem::is_regular_file(bbPath)) {
        auto parsed = taskforge::jsonutil::tryParse(taskforge::jsonutil::readFile(bbPath));
        if (!parsed || !parsed->is_object()) return false;
        for (auto item = parsed->begin(); item != parsed->end(); ++item) {
            if (!item.value().is_object()) continue;
            for (auto key = item.value().begin(); key != item.value().end(); ++key) {
                s->setWorking(scopeId(id), item.key(), key.key(), key.value());
            }
        }
    }

    if (std::filesystem::is_regular_file(lsPath)) {
        auto parsed = taskforge::jsonutil::tryParse(taskforge::jsonutil::readFile(lsPath));
        if (!parsed || !parsed->is_object()) return false;
        if (parsed->contains("lessons") && (*parsed)["lessons"].is_array()) {
            for (const auto& lesson : (*parsed)["lessons"]) {
                s->recordLesson(scopeId(id),
                                lesson.value("domain", std::string("geral")),
                                lesson.value("triggerError", std::string()),
                                lesson.value("lessonRule", std::string()),
                                lesson.value("sourceItem", std::string()),
                                "legacy");
            }
        }
    }

    s->setMeta(flag, "1");
    return true;
}

} // namespace memory
} // namespace taskforge
