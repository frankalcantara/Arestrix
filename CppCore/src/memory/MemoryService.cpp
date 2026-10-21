#include "MemoryService.hpp"

#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

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

    if (persistent) importLegacy(id, legacyBlackboardPath, legacyLessonsPath);
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
    if (auto* s = ensureStore(projectId)) s->clearScopeData(scopeId(projectId));
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

std::string MemoryService::formatLessons(const std::string& projectId,
                                         const std::vector<std::string>& domains) {
    auto* s = ensureStore(projectId);
    return s ? s->formatLessons(scopeId(projectId), domains) : std::string();
}

std::string MemoryService::buildBrief(const std::string& projectId, const std::string& itemId,
                                      const std::vector<std::string>& domains,
                                      const std::vector<std::string>& allowedKeys,
                                      size_t maxTokens, bool includeLessons,
                                      bool includeBlackboard) {
    if (!includeLessons && !includeBlackboard) return std::string();
    std::string brief;
    if (includeLessons) brief += formatLessons(projectId, domains);
    if (includeBlackboard) {
        auto snapshot = workingSnapshot(projectId, itemId, allowedKeys);
        if (snapshot.is_object() && snapshot.size() > 0) {
            brief += "\n[ARESTRIX MEMORY - BLACKBOARD]\n";
            brief += snapshot.dump(2);
            brief += "\n";
        }
    }
    if (brief.empty() || maxTokens == 0) return brief;

    const size_t budget = maxTokens * 4;   // aproximacao: ~4 caracteres por token
    if (brief.size() <= budget) return brief;
    size_t cut = brief.rfind('\n', budget);
    if (cut == std::string::npos || cut < budget / 2) cut = budget;
    return brief.substr(0, cut) + "\n[... truncado pelo orcamento de memoria]\n";
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
                                         size_t limit) {
    auto* s = ensureStore(projectId);
    return s ? s->searchContext(scopeId(projectId), text, limit) : std::string();
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

bool MemoryService::clearSession(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (isPersistent(projectId)) return true;
    if (auto* s = ensureStore(projectId)) {
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
