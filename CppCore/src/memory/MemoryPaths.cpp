#include "MemoryPaths.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <set>

namespace taskforge {
namespace memory {

namespace {

long long parseIsoEpoch(const std::string& value) {
    if (value.size() < 19) return 0;
    std::tm tm{};
    if (std::sscanf(value.c_str(), "%d-%d-%dT%d:%d:%d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
#ifdef _WIN32
    return static_cast<long long>(_mkgmtime(&tm));
#else
    return static_cast<long long>(timegm(&tm));
#endif
}

long long nowEpoch() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
}

std::vector<std::string> idArray(const taskforge::Json& value) {
    std::vector<std::string> ids;
    if (!value.is_array()) return ids;
    for (const auto& item : value) {
        if (item.is_string() && !item.str().empty()) ids.push_back(item.str());
    }
    return ids;
}

} // namespace

double MemoryPaths::utility(long long success, long long failure, long long lastTau,
                            long long nowTau, const PathOptions& options) {
    const double prior = std::max(0.01, options.prior);
    const double base = (static_cast<double>(success) + prior) /
                        (static_cast<double>(success + failure) + 2.0 * prior);
    const double delta = static_cast<double>(std::max<long long>(0, nowTau - lastTau));
    const double decay = std::exp(-std::max(0.0, options.mu) * delta);
    return std::max(0.0, std::min(1.0, base * decay));
}

taskforge::Json MemoryPaths::consolidate(MemoryStore& store, const std::string& scopeId,
                                         const std::vector<MemoryTraceRecord>& traces,
                                         const PathOptions& options) {
    taskforge::Json out{{"ok", true}, {"processed", 0LL}, {"created", 0LL},
                        {"events", 0LL}, {"lowResolution", 0LL}};
    if (scopeId.empty()) {
        out["ok"] = false;
        out["error"] = "escopo vazio";
        return out;
    }
    for (const auto& trace : traces) {
        if (trace.outcome == "failure") continue;
        auto ids = idArray(trace.used);
        bool lowResolution = ids.empty();
        if (lowResolution) ids = idArray(trace.delivered);
        std::set<std::string> unique;
        std::vector<std::string> claims;
        for (const auto& id : ids) {
            if (!unique.insert(id).second) continue;
            MemoryEdgeRecord edge;
            if (!store.getEdge(scopeId, id, edge)) continue;
            if (edge.status != "active") continue;
            claims.push_back(id);
        }
        if (claims.empty()) continue;
        std::sort(claims.begin(), claims.end());
        const std::string taskType = trace.taskType.empty() ? "geral" : trace.taskType;
        std::string signature = taskType + "|";
        for (size_t i = 0; i < claims.size(); ++i) {
            MemoryEdgeRecord edge;
            if (!store.getEdge(scopeId, claims[i], edge)) continue;
            const auto lenses = store.edgeLensNames(claims[i]);
            const std::string lens = lenses.empty() ? std::string() : lenses.front();
            if (i > 0) signature += ";";
            signature += store.nodeKind(scopeId, edge.srcId) + ">" +
                         store.nodeKind(scopeId, edge.dstId) + ":" +
                         edge.relation + ":" + lens;
        }
        bool created = false;
        const std::string pathId = store.upsertPath(
            scopeId, taskType, signature, claims, trace.intentTemplateHash,
            taskforge::Json{{"lowResolution", lowResolution}}.dump(), &created);
        if (pathId.empty()) continue;
        long long tau = parseIsoEpoch(trace.createdAt);
        if (tau <= 0) tau = nowEpoch();
        store.recordPathEvent(pathId, trace.traceId, trace.inputHash, true, tau,
                              options.minDistinctInputs);
        out["processed"] = out.value("processed", 0LL) + 1;
        if (created) out["created"] = out.value("created", 0LL) + 1;
        if (lowResolution) out["lowResolution"] = out.value("lowResolution", 0LL) + 1;
        out["events"] = out.value("events", 0LL) + 1;
    }
    return out;
}

taskforge::Json MemoryPaths::reconcile(MemoryStore& store, const std::string& scopeId,
                                       const std::string& reason) {
    taskforge::Json out{{"ok", true}, {"suspended", 0LL}, {"reactivated", 0LL},
                        {"retired", 0LL}};
    const long long now = nowEpoch();
    for (const auto& path : store.listPaths(scopeId)) {
        if (path.state == "retired" || path.claimIds.empty()) continue;
        bool allActive = true;
        bool incompatible = false;
        std::vector<std::pair<std::string, std::string>> replacements;
        for (const auto& claimId : path.claimIds) {
            MemoryEdgeRecord edge;
            const bool active = store.getEdge(scopeId, claimId, edge) &&
                edge.status == "active" &&
                (edge.validTo.empty() || parseIsoEpoch(edge.validTo) == 0 ||
                 parseIsoEpoch(edge.validTo) > now);
            if (active) continue;
            allActive = false;
            const auto compatible = store.replacementClaimIds(scopeId, claimId);
            if (!compatible.empty()) {
                replacements.emplace_back(claimId, compatible.front());
                continue;
            }
            if (!store.predicateClaimIds(scopeId, claimId).empty()) incompatible = true;
        }
        if (allActive) {
            if (path.state == "suspended" &&
                store.setPathState(scopeId, path.id, "active", "dependencies_revalidated")) {
                out["reactivated"] = out.value("reactivated", 0LL) + 1;
            }
            continue;
        }
        if (!replacements.empty() && !incompatible) {
            bool changed = false;
            for (const auto& [oldClaim, newClaim] : replacements) {
                changed = store.revalidatePathClaim(scopeId, path.id, oldClaim, newClaim) || changed;
            }
            if (changed) {
                if (store.setPathState(scopeId, path.id, "active", "claim_revalidated")) {
                    out["reactivated"] = out.value("reactivated", 0LL) + 1;
                }
                continue;
            }
        }
        if (incompatible) {
            if (store.setPathState(scopeId, path.id, "retired", "incompatible_substitution")) {
                out["retired"] = out.value("retired", 0LL) + 1;
            }
        } else if (path.state == "active" &&
                   store.setPathState(scopeId, path.id, "suspended", reason)) {
            out["suspended"] = out.value("suspended", 0LL) + 1;
        }
    }
    return out;
}

taskforge::Json MemoryPaths::suspendForClaim(MemoryStore& store, const std::string& scopeId,
                                              const std::string& claimId,
                                              const std::string& reason) {
    taskforge::Json out{{"ok", true}, {"suspended", 0LL}};
    for (const auto& pathId : store.pathsForClaim(claimId)) {
        if (store.setPathState(scopeId, pathId, "suspended", reason)) {
            out["suspended"] = out.value("suspended", 0LL) + 1;
        }
    }
    return out;
}

} // namespace memory
} // namespace taskforge
