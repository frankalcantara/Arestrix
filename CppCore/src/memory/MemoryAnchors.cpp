#include "MemoryAnchors.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::vector<std::string> queryTerms(const std::string& query, size_t limit) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    std::string current;
    auto flush = [&]() {
        if (current.size() < 3) {
            current.clear();
            return;
        }
        static const std::set<std::string> stopWords{
            "a", "ao", "aos", "as", "com", "como", "da", "das", "de", "do",
            "dos", "e", "em", "entre", "na", "nas", "no", "nos", "o", "os",
            "ou", "para", "por", "que", "se", "sem", "um", "uma"};
        if (stopWords.count(current) == 0 && seen.insert(current).second) {
            out.push_back(current);
        }
        current.clear();
    };
    for (unsigned char c : query) {
        if (std::isalnum(c) || c == '_') {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else {
            flush();
        }
        if (out.size() >= limit) break;
    }
    flush();
    return out;
}

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

    auto addText = [&](const std::string& text, double weight) {
        if (text.empty() || weight <= 0.0) return;
        bool ambiguous = false;
        const std::string entity = store.findEntity(scopeId, text, &ambiguous);
        if (!entity.empty() && !ambiguous) {
            add(MemoryAnchor{entity, "entity", text, weight, "alias"});
        }
        const auto lexical = store.lexicalCandidates(
            scopeId, text, std::max<size_t>(1, limit * 3));
        for (size_t i = 0; i < lexical.size(); ++i) {
            const auto& candidate = lexical[i];
            const double score = weight / static_cast<double>(i + 1);
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
    };

    if (!query.empty()) {
        addText(query, 1.0);
        // Prompts reais sao frases, nao nomes canonicos. Se a frase inteira
        // nao resolve, tenta os termos relevantes para a travessia encontrar
        // uma ancora em vez de abster sempre.
        if (byId.size() < limit) {
            for (const auto& term : queryTerms(query, 12)) {
                addText(term, 0.8);
                if (byId.size() >= limit) break;
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
