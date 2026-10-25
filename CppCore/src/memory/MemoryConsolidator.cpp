#include "MemoryConsolidator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

constexpr const char* kConsolidator = "consolidator";

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

std::string isoDaysAgo(int days) {
    const auto now = std::chrono::system_clock::now();
    const auto value = now - std::chrono::hours(24 * std::max(0, days));
    const std::time_t stamp = std::chrono::system_clock::to_time_t(value);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &stamp);
#else
    gmtime_r(&stamp, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

std::string shortHash(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(out);
}

taskforge::Json parseObject(const std::string& text) {
    auto parsed = taskforge::jsonutil::tryParse(text);
    return parsed && parsed->is_object() ? *parsed : taskforge::Json::object();
}

std::string metaString(const taskforge::Json& metadata, const char* key) {
    return metadata.is_object() && metadata.contains(key) && metadata[key].is_string()
        ? metadata[key].str() : std::string();
}

bool isLesson(const MemoryEdgeRecord& edge) {
    return edge.relation == "HAS_LESSON";
}

bool isMarker(const MemoryEdgeRecord& edge) {
    return edge.relation == "CONTRADICTS" || edge.relation == "SUPERSEDES" ||
           edge.relation == "GENERALIZES";
}

struct EdgeView {
    MemoryEdgeRecord edge;
    std::string srcName;
    std::string dstName;
    std::string domain;
    taskforge::Json metadata;
};

std::vector<EdgeView> loadViews(MemoryStore& store, const std::string& scope,
                                const std::string& status = "active") {
    std::vector<EdgeView> out;
    const auto names = store.nodeNames(scope);
    for (auto& edge : store.listEdges(scope, status, 20000)) {
        EdgeView view;
        view.edge = std::move(edge);
        auto src = names.find(view.edge.srcId);
        if (src != names.end()) view.srcName = src->second;
        auto dst = names.find(view.edge.dstId);
        if (dst != names.end()) view.dstName = dst->second;
        view.metadata = parseObject(view.edge.metadata);
        view.domain = metaString(view.metadata, "domain");
        out.push_back(std::move(view));
    }
    return out;
}

void addAction(ConsolidationReport& report, const std::string& action,
               size_t limit) {
    if (report.actions.size() < limit) report.actions.push_back(action);
}

taskforge::Json mergedEvidencePatch(const EdgeView& canonical, const EdgeView& duplicate) {
    taskforge::Json patch = taskforge::Json::object();
    taskforge::Json evidence = canonical.metadata.contains("evidence") &&
        canonical.metadata["evidence"].is_array()
        ? canonical.metadata["evidence"] : taskforge::Json::array();
    std::set<std::string> seen;
    for (const auto& item : evidence) seen.insert(item.dump());
    if (duplicate.metadata.contains("evidence") && duplicate.metadata["evidence"].is_array()) {
        for (const auto& item : duplicate.metadata["evidence"]) {
            if (seen.insert(item.dump()).second) evidence.push_back(item);
        }
    }
    if (!evidence.empty()) patch["evidence"] = evidence;
    return patch;
}

bool supersede(MemoryStore& store, const std::string& scope,
               EdgeView& canonical, const EdgeView& duplicate,
               const std::string& reason, ConsolidationReport& report,
               std::string& error, size_t maxActions) {
    const std::string now = isoNow();
    taskforge::Json canonicalPatch = mergedEvidencePatch(canonical, duplicate);
    if (!canonicalPatch.empty()) {
        if (!store.updateEdgeState(scope, canonical.edge.id, canonical.edge.status,
                                   canonical.edge.supersededBy, canonical.edge.confidence,
                                   canonicalPatch, canonical.edge.validTo,
                                   canonical.edge.dormantAt, canonical.edge.lastUsed, error)) {
            ++report.errors;
        } else if (canonicalPatch.contains("evidence")) {
            canonical.metadata["evidence"] = canonicalPatch["evidence"];
        }
    }
    taskforge::Json patch = taskforge::Json::object();
    patch["dedupReason"] = reason;
    patch["dedupedAt"] = now;
    patch["dedupedBy"] = kConsolidator;
    if (!store.updateEdgeState(scope, duplicate.edge.id, "superseded", canonical.edge.id,
                               std::min(canonical.edge.confidence, duplicate.edge.confidence),
                               patch, now, "", duplicate.edge.lastUsed, error)) {
        ++report.errors;
        return false;
    }
    ++report.duplicates;
    addAction(report, "dedup:" + duplicate.edge.id + "->" + canonical.edge.id, maxActions);
    store.recordAudit(kConsolidator, "dedup_supersede", duplicate.edge.id, "", canonical.edge.id);

    if (!duplicate.dstName.empty() && !canonical.edge.dstId.empty() &&
        duplicate.edge.dstId != canonical.edge.dstId) {
        if (store.addAlias(scope, duplicate.dstName, canonical.edge.dstId, "dedup")) ++report.aliases;
        taskforge::Json marker = taskforge::Json{
            {"canonicalEdge", canonical.edge.id}, {"supersededEdge", duplicate.edge.id},
            {"reason", reason}};
        store.recordEdge(scope, canonical.edge.srcId, duplicate.edge.dstId, "SUPERSEDES",
                         marker, now, "", 0.9, "active", kConsolidator, "",
                         taskforge::Json{{"canonicalEdge", canonical.edge.id},
                                         {"supersededEdge", duplicate.edge.id}});
    }
    return true;
}

void deduplicateLessons(MemoryStore& store, const std::string& scope,
                        const ConsolidationOptions& options,
                        ConsolidationReport& report, std::string& error) {
    auto views = loadViews(store, scope);
    std::vector<EdgeView> lessons;
    for (auto& view : views) {
        if (isLesson(view.edge)) lessons.push_back(std::move(view));
    }
    std::vector<std::vector<EdgeView>> groups;
    for (auto& lesson : lessons) {
        const std::string domain = normalizeMemoryKey(lesson.domain);
        bool placed = false;
        for (auto& group : groups) {
            if (normalizeMemoryKey(group.front().domain) != domain) continue;
            const double similarity = memoryTokenSimilarity(group.front().dstName, lesson.dstName);
            if (normalizeMemoryKey(group.front().dstName) == normalizeMemoryKey(lesson.dstName) ||
                similarity >= options.nearDuplicateSimilarity) {
                group.push_back(lesson);
                placed = true;
                break;
            }
        }
        if (!placed) groups.push_back({lesson});
    }
    for (auto& group : groups) {
        if (group.size() < 2) continue;
        size_t canonical = 0;
        for (size_t i = 1; i < group.size(); ++i) {
            if (group[i].edge.confidence > group[canonical].edge.confidence ||
                (group[i].edge.confidence == group[canonical].edge.confidence &&
                 group[i].edge.recordedAt < group[canonical].edge.recordedAt)) {
                canonical = i;
            }
        }
        for (size_t i = 0; i < group.size(); ++i) {
            if (i == canonical) continue;
            supersede(store, scope, group[canonical], group[i], "duplicate_lesson", report,
                      error, options.maxActions);
            if (report.actions.size() >= options.maxActions) return;
        }
    }
}

void deduplicateFacts(MemoryStore& store, const std::string& scope,
                      const ConsolidationOptions& options,
                      ConsolidationReport& report, std::string& error) {
    auto views = loadViews(store, scope);
    std::map<std::string, std::vector<EdgeView>> groups;
    for (auto& view : views) {
        if (isLesson(view.edge) || isMarker(view.edge)) continue;
        std::string object = view.dstName;
        if (!view.edge.valueJson.empty() && view.edge.valueJson != "null") {
            object += "|" + view.edge.valueJson;
        }
        if (object.empty()) object = view.edge.valueJson;
        const std::string key = normalizeMemoryKey(view.srcName) + "|" +
                                normalizeMemoryKey(view.edge.relation) + "|" +
                                normalizeMemoryKey(object) + "|" +
                                normalizeMemoryKey(view.domain);
        groups[key].push_back(std::move(view));
    }
    for (auto& [key, group] : groups) {
        (void)key;
        if (group.size() < 2) continue;
        size_t canonical = 0;
        for (size_t i = 1; i < group.size(); ++i) {
            if (group[i].edge.confidence > group[canonical].edge.confidence ||
                (group[i].edge.confidence == group[canonical].edge.confidence &&
                 group[i].edge.recordedAt < group[canonical].edge.recordedAt)) {
                canonical = i;
            }
        }
        for (size_t i = 0; i < group.size(); ++i) {
            if (i == canonical) continue;
            supersede(store, scope, group[canonical], group[i], "duplicate_fact", report,
                      error, options.maxActions);
            if (report.actions.size() >= options.maxActions) return;
        }
    }
}

void detectFactContradictions(MemoryStore& store, const std::string& scope,
                              const ConsolidationOptions& options,
                              ConsolidationReport& report, std::string& error) {
    auto views = loadViews(store, scope);
    std::map<std::string, std::vector<EdgeView>> groups;
    for (auto& view : views) {
        if (isLesson(view.edge) || isMarker(view.edge)) continue;
        const std::string key = normalizeMemoryKey(view.srcName) + "|" +
                                normalizeMemoryKey(view.edge.relation);
        groups[key].push_back(std::move(view));
    }

    std::set<std::string> existingConflicts;
    for (const auto& view : loadViews(store, scope)) {
        if (view.edge.relation != "CONTRADICTS") continue;
        const taskforge::Json value = parseObject(view.edge.valueJson);
        existingConflicts.insert(view.edge.srcId + "|" + view.edge.dstId + "|" +
                                 value.value("edgeId", std::string()));
    }

    const std::string now = isoNow();
    for (auto& [key, group] : groups) {
        std::set<std::string> objects;
        for (const auto& view : group) {
            objects.insert(normalizeMemoryKey(view.dstName.empty() ? view.edge.valueJson : view.dstName));
        }
        if (objects.size() < 2) continue;

        const std::string groupId = "cg_" + shortHash(key);
        bool alreadyMarked = true;
        for (const auto& view : group) {
            if (metaString(view.metadata, "conflictGroup") != groupId) {
                alreadyMarked = false;
                break;
            }
        }
        if (alreadyMarked) continue;

        size_t winner = 0;
        for (size_t i = 1; i < group.size(); ++i) {
            if (group[i].edge.confidence > group[winner].edge.confidence ||
                (group[i].edge.confidence == group[winner].edge.confidence &&
                 group[i].edge.recordedAt < group[winner].edge.recordedAt)) {
                winner = i;
            }
        }

        const std::string conflictNode = store.resolveNode(
            scope, "Conflito: " + group[winner].srcName + " " + group[winner].edge.relation,
            "conflict", taskforge::Json{{"domain", group[winner].domain},
                                         {"conflictGroup", groupId},
                                         {"source", kConsolidator}});
        if (conflictNode.empty()) {
            ++report.errors;
            continue;
        }

        for (size_t i = 0; i < group.size(); ++i) {
            taskforge::Json patch = taskforge::Json::object();
            patch["conflict"] = true;
            patch["conflictGroup"] = groupId;
            patch["conflictWith"] = i == winner
                ? group[(winner + 1) % group.size()].edge.id
                : group[winner].edge.id;
            double confidence = group[i].edge.confidence;
            std::string status = group[i].edge.status;
            if (i != winner) {
                status = "candidate";
                confidence = std::max(0.05, confidence * 0.8);
            } else {
                confidence = std::max(0.05, confidence * 0.95);
            }
            if (!store.updateEdgeState(scope, group[i].edge.id, status, group[i].edge.supersededBy,
                                       confidence, patch, group[i].edge.validTo,
                                       group[i].edge.dormantAt, group[i].edge.lastUsed, error)) {
                ++report.errors;
                continue;
            }
            if (group[i].edge.dstId.empty()) continue;
            const std::string conflictKey = conflictNode + "|" + group[i].edge.dstId + "|" +
                                            group[i].edge.id;
            if (existingConflicts.count(conflictKey)) continue;
            taskforge::Json value = taskforge::Json{{"edgeId", group[i].edge.id},
                                                    {"status", status},
                                                    {"confidence", confidence}};
            if (!store.recordEdge(scope, conflictNode, group[i].edge.dstId, "CONTRADICTS",
                                  value, now, "", 0.8, "active", kConsolidator, "",
                                  taskforge::Json{{"conflictGroup", groupId},
                                                  {"edgeId", group[i].edge.id}}).empty()) {
                existingConflicts.insert(conflictKey);
            }
        }
        ++report.contradictions;
        addAction(report, "conflict:" + groupId, options.maxActions);
        store.recordAudit(kConsolidator, "detect_contradiction", groupId, "", group[winner].edge.id);
        if (report.actions.size() >= options.maxActions) return;
    }
}

bool negativeLesson(const EdgeView& view) {
    const std::string text = " " + normalizeMemoryKey(view.dstName) + " ";
    return text.find(" nao ") != std::string::npos ||
           text.find(" nunca ") != std::string::npos ||
           text.find(" evite ") != std::string::npos ||
           text.find(" proibido ") != std::string::npos;
}

void detectLessonContradictions(MemoryStore& store, const std::string& scope,
                                const ConsolidationOptions& options,
                                ConsolidationReport& report, std::string& error) {
    auto views = loadViews(store, scope);
    std::vector<EdgeView> lessons;
    for (auto& view : views) {
        if (isLesson(view.edge)) lessons.push_back(std::move(view));
    }

    std::set<std::string> existing;
    for (const auto& view : loadViews(store, scope)) {
        if (view.edge.relation == "CONTRADICTS") {
            existing.insert(view.edge.srcId + "|" + view.edge.dstId);
        }
    }

    const std::string now = isoNow();
    for (size_t i = 0; i < lessons.size(); ++i) {
        for (size_t j = i + 1; j < lessons.size(); ++j) {
            if (normalizeMemoryKey(lessons[i].domain) != normalizeMemoryKey(lessons[j].domain)) continue;
            if (negativeLesson(lessons[i]) == negativeLesson(lessons[j])) continue;
            const std::string triggerA = normalizeMemoryKey(
                metaString(lessons[i].metadata, "triggerError"));
            const std::string triggerB = normalizeMemoryKey(
                metaString(lessons[j].metadata, "triggerError"));
            if (triggerA.empty() || triggerA != triggerB) continue;
            if (memoryTokenSimilarity(lessons[i].dstName, lessons[j].dstName) < 0.6) continue;

            const size_t winner = lessons[i].edge.confidence >= lessons[j].edge.confidence ? i : j;
            const size_t loser = winner == i ? j : i;
            const std::string groupId = "lg_" + shortHash(
                normalizeMemoryKey(lessons[i].domain) + "|" + triggerA + "|" +
                normalizeMemoryKey(lessons[i].dstName) + "|" + normalizeMemoryKey(lessons[j].dstName));
            const std::string conflictNode = store.resolveNode(
                scope, "Conflito de licao: " + lessons[winner].domain, "conflict",
                taskforge::Json{{"domain", lessons[winner].domain}, {"conflictGroup", groupId}});
            if (conflictNode.empty()) {
                ++report.errors;
                continue;
            }
            taskforge::Json winnerPatch = taskforge::Json{{"conflict", true},
                                                           {"conflictGroup", groupId},
                                                           {"conflictWith", lessons[loser].edge.id}};
            if (!store.updateEdgeState(scope, lessons[winner].edge.id, "active",
                                       lessons[winner].edge.supersededBy,
                                       std::max(0.05, lessons[winner].edge.confidence * 0.95),
                                       winnerPatch, lessons[winner].edge.validTo,
                                       lessons[winner].edge.dormantAt, lessons[winner].edge.lastUsed,
                                       error)) {
                ++report.errors;
            }
            taskforge::Json patch = taskforge::Json{{"conflict", true},
                                                    {"conflictGroup", groupId},
                                                    {"conflictWith", lessons[winner].edge.id}};
            if (!store.updateEdgeState(scope, lessons[loser].edge.id, "candidate",
                                       lessons[loser].edge.supersededBy,
                                       std::max(0.05, lessons[loser].edge.confidence * 0.8),
                                       patch, lessons[loser].edge.validTo,
                                       lessons[loser].edge.dormantAt, lessons[loser].edge.lastUsed,
                                       error)) {
                ++report.errors;
                continue;
            }
            for (size_t k : {winner, loser}) {
                if (lessons[k].edge.dstId.empty()) continue;
                if (existing.count(conflictNode + "|" + lessons[k].edge.dstId)) continue;
                if (!store.recordEdge(scope, conflictNode, lessons[k].edge.dstId, "CONTRADICTS",
                                      taskforge::Json{{"edgeId", lessons[k].edge.id},
                                                      {"status", k == loser ? "candidate" : "active"}},
                                      now, "", 0.8, "active", kConsolidator, "",
                                      taskforge::Json{{"conflictGroup", groupId},
                                                      {"edgeId", lessons[k].edge.id}}).empty()) {
                    existing.insert(conflictNode + "|" + lessons[k].edge.dstId);
                }
            }
            ++report.contradictions;
            addAction(report, "lesson-conflict:" + groupId, options.maxActions);
            store.recordAudit(kConsolidator, "detect_lesson_contradiction", groupId, "",
                              lessons[loser].edge.id);
            if (report.actions.size() >= options.maxActions) return;
        }
    }
}

void expireFacts(MemoryStore& store, const std::string& scope,
                 const ConsolidationOptions& options, ConsolidationReport& report,
                 std::string& error) {
    const std::string now = isoNow();
    for (auto& view : loadViews(store, scope)) {
        if (isMarker(view.edge)) continue;
        if (view.edge.validTo.empty() || view.edge.validTo > now) continue;
        taskforge::Json patch = taskforge::Json{{"expired", true}, {"expiredAt", now}};
        if (!store.updateEdgeState(scope, view.edge.id, "superseded", "", view.edge.confidence,
                                   patch, view.edge.validTo, view.edge.dormantAt,
                                   view.edge.lastUsed, error)) {
            ++report.errors;
            continue;
        }
        ++report.expired;
        addAction(report, "expire:" + view.edge.id, options.maxActions);
        store.recordAudit(kConsolidator, "expire_fact", view.edge.id, "", now);
        if (report.actions.size() >= options.maxActions) return;
    }
}

void applyDecay(MemoryStore& store, const std::string& scope,
                const ConsolidationOptions& options, ConsolidationReport& report,
                std::string& error) {
    if (options.dormantAfterDays <= 0) return;
    const std::string cutoff = isoDaysAgo(options.dormantAfterDays);
    const std::string now = isoNow();
    for (auto& view : loadViews(store, scope)) {
        if (isLesson(view.edge) || isMarker(view.edge)) continue;
        if (!view.edge.dormantAt.empty()) continue;
        const std::string baseline = view.edge.lastUsed.empty()
            ? view.edge.recordedAt : view.edge.lastUsed;
        if (baseline.empty() || baseline >= cutoff) continue;
        taskforge::Json patch = taskforge::Json{{"dormantReason", "unused"},
                                                 {"dormantAt", now}};
        const double confidence = std::max(0.05, view.edge.confidence * 0.9);
        if (!store.updateEdgeState(scope, view.edge.id, "active", view.edge.supersededBy,
                                   confidence, patch, view.edge.validTo, now,
                                   view.edge.lastUsed, error)) {
            ++report.errors;
            continue;
        }
        ++report.dormant;
        addAction(report, "dormant:" + view.edge.id, options.maxActions);
        store.recordAudit(kConsolidator, "decay_dormant", view.edge.id, "", now);
        if (report.actions.size() >= options.maxActions) return;
    }
}

void generalizeLessons(MemoryStore& store, const std::string& scope,
                       const ConsolidationOptions& options, ConsolidationReport& report,
                       std::string& error) {
    (void)error;
    if (options.generalizeMinSources <= 0) return;
    auto views = loadViews(store, scope);
    std::map<std::string, std::vector<EdgeView>> byDomain;
    for (auto& view : views) {
        if (isLesson(view.edge)) byDomain[normalizeMemoryKey(view.domain)].push_back(std::move(view));
    }

    std::set<std::string> existing;
    for (const auto& view : loadViews(store, scope)) {
        if (view.edge.relation == "GENERALIZES") {
            existing.insert(view.edge.srcId + "|" + view.edge.dstId);
        }
    }

    const std::string now = isoNow();
    for (auto& [key, group] : byDomain) {
        (void)key;
        if (group.size() < static_cast<size_t>(options.generalizeMinSources)) continue;
        const std::string domain = group.front().domain;
        const std::string summaryNode = store.resolveNode(
            scope, "Resumo: " + domain, "summary",
            taskforge::Json{{"domain", domain},
                            {"sourceCount", static_cast<long long>(group.size())},
                            {"generatedBy", kConsolidator}});
        if (summaryNode.empty()) {
            ++report.errors;
            continue;
        }
        for (const auto& lesson : group) {
            if (lesson.edge.dstId.empty()) continue;
            const std::string edgeKey = summaryNode + "|" + lesson.edge.dstId;
            if (existing.count(edgeKey)) continue;
            if (store.recordEdge(scope, summaryNode, lesson.edge.dstId, "GENERALIZES",
                                 taskforge::Json{{"domain", domain},
                                                 {"sourceEdge", lesson.edge.id},
                                                 {"sourceCount", static_cast<long long>(group.size())}},
                                 now, "", 0.8, "active", kConsolidator, "",
                                 taskforge::Json{{"domain", domain},
                                                 {"sourceEdge", lesson.edge.id}}).empty()) {
                ++report.errors;
                continue;
            }
            existing.insert(edgeKey);
            ++report.generalizations;
            addAction(report, "generalize:" + summaryNode + "->" + lesson.edge.dstId,
                      options.maxActions);
            if (report.actions.size() >= options.maxActions) return;
        }
        store.recordAudit(kConsolidator, "generalize_domain", summaryNode, "", domain);
    }
}

} // namespace

taskforge::Json ConsolidationReport::toJson() const {
    return taskforge::Json{{"ok", ok()},
                           {"duplicates", static_cast<long long>(duplicates)},
                           {"contradictions", static_cast<long long>(contradictions)},
                           {"expired", static_cast<long long>(expired)},
                           {"dormant", static_cast<long long>(dormant)},
                           {"aliases", static_cast<long long>(aliases)},
                           {"generalizations", static_cast<long long>(generalizations)},
                           {"errors", static_cast<long long>(errors)},
                           {"actions", actions}};
}

ConsolidationReport MemoryConsolidator::run(MemoryStore& store, const std::string& scopeId,
                                            const ConsolidationOptions& options,
                                            std::string& error) {
    ConsolidationReport report;
    error.clear();
    if (!store.isOpen()) {
        error = "banco de memoria fechado";
        report.errors = 1;
        return report;
    }

    if (options.expireFacts) {
        std::string stepError;
        expireFacts(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
    }
    if (options.deduplicate) {
        std::string stepError;
        deduplicateLessons(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
        stepError.clear();
        deduplicateFacts(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
    }
    if (options.detectContradictions) {
        std::string stepError;
        detectFactContradictions(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
        stepError.clear();
        detectLessonContradictions(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
    }
    if (options.applyDecay) {
        std::string stepError;
        applyDecay(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
    }
    if (options.generalize) {
        std::string stepError;
        generalizeLessons(store, scopeId, options, report, stepError);
        if (!stepError.empty() && error.empty()) error = stepError;
    }

    if (report.errors > 0 && error.empty()) {
        error = std::to_string(report.errors) + " acao(oes) de consolidacao falharam";
    }
    store.recordAudit(kConsolidator, "consolidate", scopeId, "", report.toJson().dump());
    return report;
}

ConsolidationOptions consolidationOptionsFromJson(const taskforge::Json& value) {
    ConsolidationOptions options;
    if (!value.is_object()) return options;
    options.deduplicate = value.value("deduplicate", options.deduplicate);
    options.detectContradictions = value.value("detectContradictions", options.detectContradictions);
    options.expireFacts = value.value("expireFacts", options.expireFacts);
    options.applyDecay = value.value("applyDecay", options.applyDecay);
    options.generalize = value.value("generalize", options.generalize);
    options.dormantAfterDays = std::max(0, std::min(3650,
        value.value("dormantAfterDays", options.dormantAfterDays)));
    options.generalizeMinSources = std::max(1, std::min(100,
        value.value("generalizeMinSources", options.generalizeMinSources)));
    options.nearDuplicateSimilarity = std::max(0.5, std::min(1.0,
        value.value("nearDuplicateSimilarity", options.nearDuplicateSimilarity)));
    options.maxActions = static_cast<size_t>(std::max(1, std::min(100000,
        value.value("maxActions", static_cast<int>(options.maxActions)))));
    return options;
}

taskforge::Json consolidationOptionsToJson(const ConsolidationOptions& options) {
    return taskforge::Json{{"deduplicate", options.deduplicate},
                           {"detectContradictions", options.detectContradictions},
                           {"expireFacts", options.expireFacts},
                           {"applyDecay", options.applyDecay},
                           {"generalize", options.generalize},
                           {"dormantAfterDays", options.dormantAfterDays},
                           {"generalizeMinSources", options.generalizeMinSources},
                           {"nearDuplicateSimilarity", options.nearDuplicateSimilarity},
                           {"maxActions", static_cast<long long>(options.maxActions)}};
}

} // namespace memory
} // namespace taskforge
