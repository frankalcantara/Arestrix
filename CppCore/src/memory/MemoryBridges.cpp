#include "MemoryBridges.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string firstLens(MemoryStore& store, const std::string& scope,
                      const std::string& edgeId) {
    const auto lenses = store.edgeLensNames(edgeId);
    return lenses.empty() ? std::string() : lenses.front();
}

std::string accessClass(const MemoryEdgeRecord& edge) {
    auto parsed = taskforge::jsonutil::tryParse(edge.metadata);
    if (!parsed || !parsed->is_object() || !parsed->contains("access")) return "";
    const auto& access = (*parsed)["access"];
    if (access.is_string()) return access.str();
    if (access.is_array() && access.size() > 0) {
        std::vector<std::string> values;
        for (const auto& item : access) {
            if (item.is_string()) values.push_back(item.str());
        }
        std::sort(values.begin(), values.end());
        std::string joined;
        for (const auto& value : values) {
            if (!joined.empty()) joined += ",";
            joined += value;
        }
        return joined;
    }
    return "";
}

} // namespace

taskforge::Json MemoryBridges::detect(MemoryStore& store, const std::string& scopeId,
                                      const std::vector<MemoryTraceRecord>& traces,
                                      const BridgeOptions& options) {
    taskforge::Json out{{"ok", true}, {"proposed", 0LL}, {"deduped", 0LL},
                        {"discarded", 0LL}, {"discardedEvents", 0LL},
                        {"proposals", taskforge::Json::array()}};
    struct ClaimInfo {
        std::string id;
        std::string access;
        std::string traceId;
    };
    std::map<std::string, std::map<std::string, std::vector<ClaimInfo>>> grouped;
    for (const auto& trace : traces) {
        if (trace.outcome == "failure") continue;
        for (const auto& item : trace.used) {
            if (!item.is_string()) continue;
            const std::string claimId = item.str();
            MemoryEdgeRecord edge;
            if (!store.getEdge(scopeId, claimId, edge)) continue;
            if (edge.status != "active") continue;
            const std::string lens = firstLens(store, scopeId, claimId);
            if (lens.empty()) continue;
            grouped[edge.srcId][lens].push_back(
                ClaimInfo{claimId, accessClass(edge), trace.traceId});
        }
    }
    std::map<std::string, size_t> perEntity;
    auto discard = [&](const std::string& reason) {
        out["discarded"] = out.value("discarded", 0LL) + 1;
        out["discardedEvents"] = out.value("discardedEvents", 0LL) + 1;
        (void)reason;
    };
    for (const auto& [entityId, lenses] : grouped) {
        if (lenses.size() < 2) continue;
        std::vector<std::string> lensIds;
        for (const auto& [lens, claims] : lenses) {
            if (!claims.empty()) lensIds.push_back(lens);
        }
        std::sort(lensIds.begin(), lensIds.end());
        for (size_t i = 0; i < lensIds.size(); ++i) {
            for (size_t j = i + 1; j < lensIds.size(); ++j) {
                if (out.value("proposed", 0LL) >= static_cast<long long>(options.maxProposals)) {
                    discard("round_budget");
                    continue;
                }
                if (perEntity[entityId] >= options.maxPerEntity) {
                    discard("entity_budget");
                    continue;
                }
                const auto& left = lenses.at(lensIds[i]);
                const auto& right = lenses.at(lensIds[j]);
                std::string leftAccess;
                std::string rightAccess;
                for (const auto& claim : left) {
                    if (!claim.access.empty()) leftAccess = claim.access;
                }
                for (const auto& claim : right) {
                    if (!claim.access.empty()) rightAccess = claim.access;
                }
                if (!leftAccess.empty() && !rightAccess.empty() && leftAccess != rightAccess) {
                    discard("access_incompatible");
                    continue;
                }
                const std::string pairKey = entityId + "|" + lensIds[i] + "|" + lensIds[j];
                if (store.bridgeProposalSeen(scopeId, pairKey)) {
                    out["deduped"] = out.value("deduped", 0LL) + 1;
                    continue;
                }
                taskforge::Json claimArray = taskforge::Json::array();
                taskforge::Json evidence = taskforge::Json::array();
                std::set<std::string> pathIds;
                for (const auto* side : {&left, &right}) {
                    for (const auto& claim : *side) {
                        claimArray.push_back(claim.id);
                        evidence.push_back(taskforge::Json{{"kind", "claim"}, {"id", claim.id}});
                        for (const auto& pathId : store.pathsForClaim(claim.id)) {
                            pathIds.insert(pathId);
                        }
                    }
                }
                for (const auto& pathId : pathIds) {
                    evidence.push_back(taskforge::Json{{"kind", "path"}, {"id", pathId}});
                }
                const auto names = store.nodeNames(scopeId);
                auto entityName = names.find(entityId);
                taskforge::Json payload{
                    {"pairKey", pairKey},
                    {"entityId", entityId},
                    {"entity", entityName == names.end() ? entityId : entityName->second},
                    {"lenses", taskforge::Json::array({lensIds[i], lensIds[j]})},
                    {"claimIds", claimArray},
                    {"utilityPaths", static_cast<long long>(pathIds.size())},
                    {"conditions", taskforge::Json::array(
                         {"same_entity", "distinct_lenses", "compatible_access"})}};
                const std::string proposal = store.recordProposal(
                    scopeId, "bridge_hypothesis", payload, "", evidence, 0.6);
                if (proposal.empty()) {
                    discard("proposal_write_failed");
                    continue;
                }
                ++perEntity[entityId];
                out["proposed"] = out.value("proposed", 0LL) + 1;
                out["proposals"].push_back(taskforge::Json{
                    {"id", proposal}, {"pairKey", pairKey},
                    {"entity", payload["entity"]}, {"lenses", payload["lenses"]},
                    {"utilityPaths", payload["utilityPaths"]}});
            }
        }
    }
    return out;
}

} // namespace memory
} // namespace taskforge
