#include "MemoryFacets.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "project/RunnerSecurity.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string metadataString(const taskforge::Json& metadata, const char* key,
                           const std::string& fallback = "") {
    if (!metadata.is_object() || !metadata.contains(key)) return fallback;
    const auto& value = metadata[key];
    return value.is_string() ? value.str() : fallback;
}

std::string edgeObjectText(const MemoryEdgeRecord& edge,
                           const std::unordered_map<std::string, std::string>& names) {
    if (!edge.dstId.empty()) {
        auto it = names.find(edge.dstId);
        if (it != names.end()) return it->second;
    }
    auto value = taskforge::jsonutil::tryParse(edge.valueJson);
    if (value && value->is_object()) {
        if (value->contains("value") && (*value)["value"].is_string()) {
            return (*value)["value"].str();
        }
        if (value->contains("value")) return (*value)["value"].dump();
    }
    return edge.valueJson.empty() ? edge.relation : edge.valueJson;
}

std::string lensNameFor(const MemoryEdgeRecord& edge, const taskforge::Json& metadata) {
    std::string lens = metadataString(metadata, "lens");
    if (lens.empty()) lens = metadataString(metadata, "domain");
    if (lens.empty()) lens = edge.relation;
    if (lens.empty()) lens = "default";
    return lens;
}

} // namespace

taskforge::Json MemoryFacets::rebuild(MemoryStore& store, const std::string& scopeId,
                                      const EmbeddingConfig& embedding, bool facetsEnabled) {
    taskforge::Json out{{"ok", true}, {"lenses", 0LL}, {"facets", 0LL},
                        {"changed", 0LL}, {"edgesWithLens", 0LL}};
    if (scopeId.empty()) {
        out["ok"] = false;
        out["error"] = "escopo vazio";
        return out;
    }
    const auto names = store.nodeNames(scopeId);
    const auto edges = store.listEdges(scopeId, "", 10000);
    std::map<std::string, std::string> lensIds;
    for (const auto& lens : store.listLenses(scopeId)) lensIds[lens.name] = lens.id;

    struct ClaimView {
        std::string edgeId;
        std::string entityId;
        std::string lens;
        std::string predicate;
        std::string object;
        std::string quote;
        std::string sourceUri;
        std::string validFrom;
        std::string validTo;
    };
    std::vector<ClaimView> claims;
    size_t edgesWithLens = 0;
    for (const auto& edge : edges) {
        if (edge.status != "active") continue;
        auto metadataOpt = taskforge::jsonutil::tryParse(edge.metadata);
        const taskforge::Json metadata = metadataOpt && metadataOpt->is_object()
            ? *metadataOpt : taskforge::Json::object();
        auto lensNames = store.edgeLensNames(edge.id);
        if (lensNames.empty()) {
            const std::string lensName = lensNameFor(edge, metadata);
            std::string lensId = store.ensureLens(scopeId, lensName);
            if (lensId.empty()) continue;
            store.linkEdgeLens(edge.id, lensId);
            lensIds[lensName] = lensId;
            lensNames.push_back(lensName);
        }
        ++edgesWithLens;
        if (!metadata.value("claim", false)) continue;
        for (const auto& lens : lensNames) {
            ClaimView claim;
            claim.edgeId = edge.id;
            claim.entityId = edge.srcId;
            claim.lens = lens;
            claim.predicate = edge.relation;
            claim.object = edgeObjectText(edge, names);
            claim.quote = metadataString(metadata, "quote");
            claim.sourceUri = metadataString(metadata, "sourceUri");
            claim.validFrom = edge.validFrom;
            claim.validTo = edge.validTo;
            claims.push_back(std::move(claim));
        }
    }

    if (!facetsEnabled) {
        out["lenses"] = static_cast<long long>(lensIds.size());
        out["edgesWithLens"] = static_cast<long long>(edgesWithLens);
        return out;
    }

    std::map<std::string, std::vector<ClaimView>> byEntityLens;
    for (const auto& claim : claims) {
        byEntityLens[claim.entityId + "\x1f" + claim.lens].push_back(claim);
    }
    const std::string symmetricFingerprint =
        embeddingFingerprintForRole(embedding, true);
    size_t changedCount = 0;
    size_t facetCount = 0;
    for (auto& [key, group] : byEntityLens) {
        if (group.empty()) continue;
        const std::string entityId = group.front().entityId;
        const std::string lensName = group.front().lens;
        auto lensIt = lensIds.find(lensName);
        if (lensIt == lensIds.end()) continue;
        std::sort(group.begin(), group.end(), [](const ClaimView& a, const ClaimView& b) {
            if (a.edgeId != b.edgeId) return a.edgeId < b.edgeId;
            return a.quote < b.quote;
        });
        taskforge::Json supporting = taskforge::Json::array();
        std::string hashMaterial;
        for (const auto& claim : group) {
            supporting.push_back(taskforge::Json{
                {"edgeId", claim.edgeId}, {"claimId", claim.edgeId},
                {"predicate", claim.predicate}, {"object", claim.object},
                {"quote", claim.quote}, {"sourceUri", claim.sourceUri},
                {"validFrom", claim.validFrom}, {"validTo", claim.validTo}});
            hashMaterial += claim.edgeId + "|" + claim.predicate + "|" + claim.object + "|" +
                            claim.quote + "\n";
        }
        std::string entityName = entityId;
        auto entityIt = names.find(entityId);
        if (entityIt != names.end()) entityName = entityIt->second;
        std::string description = entityName + " — " + lensName + ": ";
        size_t emitted = 0;
        for (const auto& claim : group) {
            if (emitted >= 3) break;
            if (emitted > 0) description += "; ";
            description += claim.predicate + " " + claim.object;
            if (!claim.quote.empty()) description += " (\"" + claim.quote + "\")";
            ++emitted;
        }
        MemoryFacetRecord facet;
        facet.scopeId = scopeId;
        facet.entityId = entityId;
        facet.lensId = lensIt->second;
        facet.description = description;
        facet.supportingClaimsJson = supporting.dump();
        facet.claimsHash = project::RunnerSecurity::sha256(hashMaterial);
        facet.embeddingFingerprint = symmetricFingerprint;
        bool changed = false;
        const std::string id = store.upsertFacet(facet, &changed);
        if (!id.empty()) {
            ++facetCount;
            if (changed) ++changedCount;
        }
    }
    out["lenses"] = static_cast<long long>(lensIds.size());
    out["facets"] = static_cast<long long>(facetCount);
    out["changed"] = static_cast<long long>(changedCount);
    out["edgesWithLens"] = static_cast<long long>(edgesWithLens);
    return out;
}

} // namespace memory
} // namespace taskforge
