#include "MemoryPaths.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <random>
#include <set>
#include <sstream>

#include "project/RunnerSecurity.hpp"
#include "util/Json.hpp"

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

std::string oneLine(std::string value, size_t maxChars) {
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    if (value.size() > maxChars) value = value.substr(0, maxChars) + "...";
    return value;
}

bool accessAllows(const taskforge::Json& metadata,
                  const std::vector<std::string>& access) {
    if (!metadata.is_object() || !metadata.contains("access")) return true;
    const taskforge::Json& policy = metadata["access"];
    auto allowed = [&](const std::string& item) {
        if (item.empty() || item == "public" || item == "project") return true;
        if (std::find(access.begin(), access.end(), item) != access.end()) return true;
        return std::find(access.begin(), access.end(), "*") != access.end();
    };
    if (policy.is_string()) return allowed(policy.str());
    if (policy.is_array()) {
        for (const auto& item : policy) {
            if (item.is_string() && allowed(item.str())) return true;
        }
        return false;
    }
    return true;
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

bool MemoryPaths::pathEligible(MemoryStore& store, const std::string& scopeId,
                               const MemoryPathRecord& path,
                               const std::vector<std::string>& access,
                               std::string* reason) {
    if (path.state != "active") {
        if (reason) *reason = "path_not_active";
        return false;
    }
    if (path.claimIds.empty()) {
        if (reason) *reason = "path_without_claims";
        return false;
    }
    const long long now = nowEpoch();
    for (const auto& claimId : path.claimIds) {
        MemoryEdgeRecord edge;
        if (!store.getEdge(scopeId, claimId, edge)) {
            if (reason) *reason = "claim_missing";
            return false;
        }
        if (edge.status != "active") {
            if (reason) *reason = "claim_not_active";
            return false;
        }
        if (!edge.validFrom.empty() && parseIsoEpoch(edge.validFrom) > now) {
            if (reason) *reason = "claim_not_yet_valid";
            return false;
        }
        if (!edge.validTo.empty() && parseIsoEpoch(edge.validTo) > 0 &&
            parseIsoEpoch(edge.validTo) <= now) {
            if (reason) *reason = "claim_expired";
            return false;
        }
        if (!edge.dormantAt.empty()) {
            if (reason) *reason = "claim_dormant";
            return false;
        }
        const auto metadata = taskforge::jsonutil::tryParse(edge.metadata);
        if (metadata && !accessAllows(*metadata, access)) {
            if (reason) *reason = "access_denied";
            return false;
        }
    }
    if (reason) reason->clear();
    return true;
}

double MemoryPaths::thompsonSample(long long success, long long failure, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::gamma_distribution<double> alpha(static_cast<double>(std::max<long long>(0, success)) + 1.0, 1.0);
    std::gamma_distribution<double> beta(static_cast<double>(std::max<long long>(0, failure)) + 1.0, 1.0);
    const double x = std::max(1e-12, alpha(rng));
    const double y = std::max(1e-12, beta(rng));
    return x / (x + y);
}

bool MemoryPaths::shouldExplore(double rate, uint64_t seed) {
    if (rate <= 0.0) return false;
    if (rate >= 1.0) return true;
    uint64_t x = seed + 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (static_cast<double>(x % 1000000ULL) / 1000000.0) < rate;
}

taskforge::Json MemoryPaths::serve(MemoryStore& store, const std::string& scopeId,
                                   const std::string& taskType, const std::string& intent,
                                   const std::vector<std::string>& access,
                                   size_t maxPaths, double explorationRate) {
    taskforge::Json out{{"ok", true}, {"served", false}, {"exploration", false},
                        {"paths", taskforge::Json::array()}};
    maxPaths = std::max<size_t>(1, std::min<size_t>(8, maxPaths));
    const std::string intentHash = intent.empty()
        ? std::string() : project::RunnerSecurity::sha256(intent);
    struct Candidate {
        MemoryPathRecord path;
        double score = 0.0;
    };
    std::vector<Candidate> candidates;
    for (const auto& path : store.listPaths(scopeId, "active")) {
        if (!taskType.empty() && !path.taskType.empty() && path.taskType != taskType) continue;
        if (!pathEligible(store, scopeId, path, access)) continue;
        double score = path.utility;
        if (!intentHash.empty() && !path.intentSample.empty() &&
            path.intentSample == intentHash) {
            score += 0.5;
        }
        candidates.push_back(Candidate{path, score});
    }
    if (candidates.empty()) return out;
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
    bool exploration = false;
    std::vector<Candidate> selected;
    const auto nowSeed = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    if (candidates.size() > 1 && shouldExplore(explorationRate, nowSeed)) {
        exploration = true;
        size_t best = 0;
        double bestSample = -1.0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const double sample = thompsonSample(candidates[i].path.success,
                                                 candidates[i].path.failure,
                                                 nowSeed + i * 0x9e3779b97f4a7c15ULL);
            if (sample > bestSample) {
                bestSample = sample;
                best = i;
            }
        }
        selected.push_back(candidates[best]);
    } else {
        for (size_t i = 0; i < candidates.size() && i < maxPaths; ++i) {
            selected.push_back(candidates[i]);
        }
    }
    const auto names = store.nodeNames(scopeId);
    taskforge::Json paths = taskforge::Json::array();
    for (const auto& candidate : selected) {
        const auto& path = candidate.path;
        std::ostringstream text;
        text << "[MEMO:path:" << path.id << "] Caminho " << path.taskType
             << " (utilidade " << path.utility << ")\n";
        taskforge::Json claims = taskforge::Json::array();
        for (const auto& claimId : path.claimIds) {
            MemoryEdgeRecord edge;
            if (!store.getEdge(scopeId, claimId, edge)) continue;
            const auto src = names.find(edge.srcId);
            const auto dst = names.find(edge.dstId);
            text << "  - [MEMO:" << claimId << "] "
                 << (src == names.end() ? edge.srcId : src->second) << " "
                 << edge.relation << " "
                 << (dst == names.end() ? edge.dstId : dst->second);
            if (edge.validFrom.size() >= 10) text << " (desde " << edge.validFrom.substr(0, 10) << ")";
            if (!edge.sourceRef.empty()) text << " [fonte: " << oneLine(edge.sourceRef, 160) << "]";
            text << "\n";
            claims.push_back(claimId);
        }
        paths.push_back(taskforge::Json{
            {"id", path.id}, {"taskType", path.taskType}, {"utility", path.utility},
            {"uses", path.uses}, {"distinctInputs", path.distinctInputs},
            {"claims", claims}, {"text", text.str()}});
    }
    out["served"] = !paths.empty();
    out["exploration"] = exploration;
    out["paths"] = paths;
    return out;
}

taskforge::Json MemoryPaths::materializeShortcuts(MemoryStore& store, const std::string& scopeId,
                                                   double minUtility, long long minUses) {
    taskforge::Json out{{"ok", true}, {"active", 0LL}, {"suspended", 0LL}};
    for (const auto& path : store.listPaths(scopeId)) {
        const bool qualifies = path.state == "active" && path.utility >= minUtility &&
                               path.uses >= minUses && !path.claimIds.empty();
        MemoryEdgeRecord first;
        MemoryEdgeRecord last;
        if (qualifies &&
            store.getEdge(scopeId, path.claimIds.front(), first) &&
            store.getEdge(scopeId, path.claimIds.back(), last)) {
            store.syncPathShortcut(scopeId, path.id, true, first.srcId, last.dstId,
                                   path.claimIds, path.utility);
            out["active"] = out.value("active", 0LL) + 1;
        } else if (!store.shortcutForPath(scopeId, path.id).empty()) {
            store.syncPathShortcut(scopeId, path.id, false, "", "", path.claimIds,
                                   path.utility);
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
