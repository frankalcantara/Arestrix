#include "MemoryRanker.hpp"

#include <algorithm>
#include <map>

namespace taskforge {
namespace memory {

namespace {

std::string vectorKind(const std::string& objectType) {
    if (objectType == "lesson") return "lesson";
    if (objectType == "edge") return "fact";
    if (objectType == "node") return "entity";
    if (objectType == "playbook") return "playbook";
    if (objectType == "episode") return "episode";
    return objectType;
}

} // namespace

std::vector<RankedMemory> MemoryRanker::fuse(const std::vector<MemorySearchCandidate>& lexical,
                                             const std::vector<MemoryVectorHit>& vector,
                                             size_t limit, double k) {
    if (limit == 0) return {};
    struct Entry {
        RankedMemory memory;
        double score = 0.0;
        bool lexical = false;
        bool vector = false;
    };
    std::map<std::string, Entry> merged;
    auto keyOf = [](const std::string& kind, const std::string& id) {
        return kind + "|" + id;
    };

    for (size_t i = 0; i < lexical.size(); ++i) {
        const auto& candidate = lexical[i];
        if (candidate.id.empty()) continue;
        const std::string key = keyOf(candidate.kind, candidate.id);
        auto& entry = merged[key];
        entry.memory.id = candidate.id;
        entry.memory.kind = candidate.kind;
        entry.memory.text = candidate.text;
        entry.memory.domain = candidate.domain;
        entry.memory.confidence = candidate.confidence;
        entry.memory.createdAt = candidate.createdAt;
        entry.score += 1.0 / (k + static_cast<double>(i + 1));
        entry.lexical = true;
    }

    for (size_t i = 0; i < vector.size(); ++i) {
        const auto& hit = vector[i];
        if (hit.objectId.empty()) continue;
        const std::string kind = vectorKind(hit.objectType);
        const std::string key = keyOf(kind, hit.objectId);
        auto& entry = merged[key];
        entry.memory.id = hit.objectId;
        entry.memory.kind = kind;
        if (entry.memory.text.empty()) entry.memory.text = hit.text;
        if (entry.memory.domain.empty()) entry.memory.domain = hit.domain;
        entry.memory.confidence = std::max(entry.memory.confidence, hit.confidence);
        if (entry.memory.createdAt.empty()) entry.memory.createdAt = hit.createdAt;
        entry.score += 1.0 / (k + static_cast<double>(i + 1));
        entry.vector = true;
    }

    std::vector<RankedMemory> out;
    out.reserve(merged.size());
    for (auto& [key, entry] : merged) {
        (void)key;
        entry.memory.score = entry.score;
        if (entry.lexical && entry.vector) entry.memory.sources = "lexical+vector";
        else if (entry.vector) entry.memory.sources = "vector";
        else entry.memory.sources = "lexical";
        out.push_back(std::move(entry.memory));
    }

    std::sort(out.begin(), out.end(), [](const RankedMemory& a, const RankedMemory& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.confidence != b.confidence) return a.confidence > b.confidence;
        return a.createdAt > b.createdAt;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

} // namespace memory
} // namespace taskforge
