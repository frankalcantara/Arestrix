#include "MemoryAnchors.hpp"

#include <algorithm>
#include <map>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

bool accessAllows(const std::string& metadata, const std::vector<std::string>& access) {
    auto parsed = taskforge::jsonutil::tryParse(metadata);
    if (!parsed || !parsed->is_object() || !parsed->contains("access")) return true;
    const auto& policy = (*parsed)["access"];
    auto allow = [&](const std::string& value) {
        if (value.empty() || value == "public" || value == "project") return true;
        return std::find(access.begin(), access.end(), value) != access.end() ||
               std::find(access.begin(), access.end(), "*") != access.end();
    };
    if (policy.is_string()) return allow(policy.str());
    if (policy.is_array()) {
        for (const auto& item : policy) {
            if (item.is_string() && allow(item.str())) return true;
        }
        return false;
    }
    return true;
}

} // namespace

std::vector<MemoryAnchor> MemoryAnchors::resolve(
    MemoryStore& store, const std::string& scopeId, const std::string& query,
    size_t limit, const std::vector<std::string>& access,
    const std::vector<MemoryAnchor>& vectorAnchors) {
    std::map<std::string, MemoryAnchor> byId;
    auto add = [&](MemoryAnchor anchor) {
        if (anchor.id.empty() || anchor.score < 0.02) return;
        auto it = byId.find(anchor.id);
        if (it == byId.end() || anchor.score > it->second.score) {
            byId[anchor.id] = std::move(anchor);
        }
    };

    if (!query.empty()) {
        bool ambiguous = false;
        const std::string entity = store.findEntity(scopeId, query, &ambiguous);
        if (!entity.empty() && !ambiguous) {
            add(MemoryAnchor{entity, "entity", query, 1.0, "alias"});
        }
        const auto lexical = store.lexicalCandidates(scopeId, query, std::max<size_t>(1, limit * 3));
        for (size_t i = 0; i < lexical.size(); ++i) {
            const auto& candidate = lexical[i];
            const double score = 1.0 / static_cast<double>(i + 1);
            if (candidate.kind == "fact") {
                MemoryEdgeRecord edge;
                if (!store.getEdge(scopeId, candidate.id, edge)) continue;
                if (edge.status != "active" || !accessAllows(edge.metadata, access)) continue;
                const auto names = store.nodeNames(scopeId);
                auto src = names.find(edge.srcId);
                auto dst = names.find(edge.dstId);
                add(MemoryAnchor{edge.srcId, "entity",
                                 src == names.end() ? edge.srcId : src->second,
                                 score, "lexical"});
                if (!edge.dstId.empty()) {
                    add(MemoryAnchor{edge.dstId, "entity",
                                     dst == names.end() ? edge.dstId : dst->second,
                                     score * 0.9, "lexical"});
                }
            } else {
                add(MemoryAnchor{candidate.id, candidate.kind, candidate.text, score, "lexical"});
            }
        }
    }
    for (const auto& anchor : vectorAnchors) add(anchor);

    std::vector<MemoryAnchor> out;
    for (auto& [id, anchor] : byId) {
        (void)id;
        out.push_back(std::move(anchor));
    }
    std::sort(out.begin(), out.end(), [](const MemoryAnchor& a, const MemoryAnchor& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.id < b.id;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

} // namespace memory
} // namespace taskforge
