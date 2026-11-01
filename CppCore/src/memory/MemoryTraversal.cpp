#include "MemoryTraversal.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <sstream>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string oneLine(std::string value, size_t maxChars) {
    for (char& ch : value) {
        if (ch == '\n' || ch == '\r') ch = ' ';
    }
    if (value.size() > maxChars) value = value.substr(0, maxChars) + "...";
    return value;
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

struct State {
    std::string nodeId;
    std::vector<std::string> nodes;
    std::vector<std::string> edges;
    double score = 0.0;
    size_t depth = 0;
};

struct Found {
    MemoryEdgeRecord edge;
    double score = 0.0;
    size_t depth = 0;
    std::string via;
};

} // namespace

taskforge::Json MemoryTraversal::run(MemoryStore& store, const std::string& scopeId,
                                     const std::string& query, const std::string& taskType,
                                     const TraversalOptions& options,
                                     const std::vector<std::string>& access,
                                     const std::vector<MemoryAnchor>& vectorAnchors) {
    taskforge::Json out{{"ok", true}, {"abstained", true},
                        {"nodes", taskforge::Json::array()},
                        {"coverage", taskforge::Json::object()},
                        {"text", std::string()}};
    const auto started = std::chrono::steady_clock::now();
    auto elapsedMs = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
    };
    const auto anchors = MemoryAnchors::resolve(store, scopeId, query,
                                                std::max<size_t>(1, options.beamWidth),
                                                access, vectorAnchors);
    if (anchors.empty()) {
        out["coverage"] = taskforge::Json{{"bestPathUtility", 0.0},
                                          {"sustainedFraction", 0.0},
                                          {"conflicts", 0LL},
                                          {"intentDistance", 1.0},
                                          {"abstained", true}};
        return out;
    }

    const auto names = store.nodeNames(scopeId);
    std::map<std::string, MemoryPathRecord> pathsById;
    for (const auto& path : store.listPaths(scopeId)) pathsById[path.id] = path;
    std::map<std::string, Found> found;
    std::vector<State> beam;
    for (const auto& anchor : anchors) {
        State state;
        state.nodeId = anchor.id;
        state.nodes.push_back(anchor.id);
        state.score = anchor.score;
        beam.push_back(std::move(state));
    }

    size_t expanded = 0;
    size_t chars = 0;
    for (size_t depth = 0; depth < options.maxDepth; ++depth) {
        if (beam.empty()) break;
        std::vector<State> next;
        for (const auto& state : beam) {
            if (expanded >= options.expandedLimit || elapsedMs() >= options.timeBudgetMs ||
                chars >= options.maxChars) {
                break;
            }
            const size_t candidateLimit = options.exact
                ? std::max<size_t>(64, options.expandedLimit)
                : options.candidateLimit;
            const auto incident = store.incidentEdges(scopeId, state.nodeId, candidateLimit);
            for (const auto& edge : incident) {
                if (++expanded > options.expandedLimit || elapsedMs() >= options.timeBudgetMs) break;
                if (!accessAllows(edge.metadata, access)) continue;
                if (std::find(state.edges.begin(), state.edges.end(), edge.id) != state.edges.end()) {
                    continue;
                }
                if (std::find(state.nodes.begin(), state.nodes.end(), edge.srcId) != state.nodes.end() &&
                    std::find(state.nodes.begin(), state.nodes.end(), edge.dstId) != state.nodes.end()) {
                    continue;   // ciclo
                }
                const std::string other = edge.srcId == state.nodeId ? edge.dstId : edge.srcId;
                if (other.empty()) continue;
                auto srcName = names.find(edge.srcId);
                auto dstName = names.find(edge.dstId);
                const std::string blob = (srcName == names.end() ? edge.srcId : srcName->second) + " " +
                                         edge.relation + " " +
                                         (dstName == names.end() ? edge.dstId : dstName->second) + " " +
                                         edge.valueJson;
                const double lexical = memoryTokenSimilarity(query, blob);
                double pathPrior = 0.0;
                for (const auto& pathId : store.pathsForClaim(edge.id)) {
                    auto pathIt = pathsById.find(pathId);
                    if (pathIt == pathsById.end()) continue;
                    const auto& path = pathIt->second;
                    if (taskType.empty() || path.taskType.empty() || path.taskType == taskType) {
                        pathPrior = std::max(pathPrior, path.utility);
                    }
                }
                double score = 0.45 * lexical + 0.20 * std::max(0.0, std::min(1.0, edge.utility)) +
                               0.20 * pathPrior + 0.15 / static_cast<double>(depth + 1);
                if (edge.relation == "CONTRADICTS") score -= 0.20;
                if (score < options.minScore) continue;
                auto foundIt = found.find(edge.id);
                if (foundIt == found.end() || score > foundIt->second.score) {
                    found[edge.id] = Found{edge, score, depth + 1,
                                           state.nodes.empty() ? std::string() : state.nodes.back()};
                }
                State child;
                child.nodeId = other;
                child.nodes = state.nodes;
                child.nodes.push_back(other);
                child.edges = state.edges;
                child.edges.push_back(edge.id);
                child.score = state.score + score;
                child.depth = depth + 1;
                next.push_back(std::move(child));
            }
        }
        std::sort(next.begin(), next.end(), [](const State& a, const State& b) {
            return a.score > b.score;
        });
        if (next.size() > options.beamWidth) next.resize(options.beamWidth);
        beam = std::move(next);
    }

    std::vector<Found> results;
    for (auto& [id, item] : found) {
        (void)id;
        results.push_back(std::move(item));
    }
    std::sort(results.begin(), results.end(), [](const Found& a, const Found& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.edge.id < b.edge.id;
    });
    if (results.size() > options.maxResults) results.resize(options.maxResults);

    taskforge::Json nodes = taskforge::Json::array();
    std::ostringstream text;
    size_t conflicts = 0;
    double bestPathUtility = 0.0;
    size_t claimItems = 0;
    size_t sustained = 0;
    for (const auto& item : results) {
        const auto& edge = item.edge;
        const auto src = names.find(edge.srcId);
        const auto dst = names.find(edge.dstId);
        const std::string srcName = src == names.end() ? edge.srcId : src->second;
        const std::string dstName = dst == names.end() ? edge.dstId : dst->second;
        std::string line = "[MEMO:" + edge.id + "] " + srcName + " " + edge.relation + " " +
                           dstName;
        if (!edge.validFrom.empty() && edge.validFrom.size() >= 10) {
            line += " (desde " + edge.validFrom.substr(0, 10) + ")";
        }
        if (!edge.sourceRef.empty()) line += " [fonte: " + oneLine(edge.sourceRef, 160) + "]";
        line += "\n";
        if (chars + line.size() > options.maxChars) break;
        chars += line.size();
        text << line;
        if (edge.relation == "CONTRADICTS") ++conflicts;
        const auto metadata = taskforge::jsonutil::tryParse(edge.metadata);
        if (metadata && metadata->is_object() && metadata->value("claim", false)) ++claimItems;
        for (const auto& pathId : store.pathsForClaim(edge.id)) {
            auto pathIt = pathsById.find(pathId);
            if (pathIt == pathsById.end()) continue;
            bestPathUtility = std::max(bestPathUtility, pathIt->second.utility);
            if (pathIt->second.state == "active") ++sustained;
        }
        nodes.push_back(taskforge::Json{
            {"id", edge.id}, {"kind", "fact"}, {"src", edge.srcId}, {"dst", edge.dstId},
            {"relation", edge.relation}, {"score", item.score}, {"depth", item.depth},
            {"justification", "beam:" + std::to_string(item.depth) + ":lexical+support+path"},
            {"source", edge.sourceRef}, {"validFrom", edge.validFrom},
            {"validTo", edge.validTo}});
    }
    const bool abstained = nodes.empty();
    out["abstained"] = abstained;
    out["nodes"] = nodes;
    out["text"] = text.str();
    out["coverage"] = taskforge::Json{
        {"bestPathUtility", bestPathUtility},
        {"sustainedFraction", claimItems == 0 ? (abstained ? 0.0 : 1.0)
                                              : static_cast<double>(sustained) /
                                                static_cast<double>(claimItems)},
        {"conflicts", static_cast<long long>(conflicts)},
        {"intentDistance", bestPathUtility <= 0.0 ? 1.0 : 1.0 - bestPathUtility},
        {"abstained", abstained}};
    return out;
}

} // namespace memory
} // namespace taskforge
