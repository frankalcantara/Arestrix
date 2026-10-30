#include "MemoryPlaybookCompiler.hpp"

#include <algorithm>
#include <cmath>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string field(const taskforge::Json& object, const char* key,
                  const std::string& fallback = "") {
    return object.is_object() && object.contains(key) && object[key].is_string()
        ? object[key].str() : fallback;
}

taskforge::Json evidenceFor(const taskforge::Json& episode) {
    taskforge::Json evidence = episode.contains("evidence") && episode["evidence"].is_array()
        ? episode["evidence"] : taskforge::Json::array();
    evidence.push_back(taskforge::Json{{"kind", "episode"},
                                       {"episodeId", field(episode, "id")},
                                       {"outcome", field(episode, "outcome")}});
    return evidence;
}

} // namespace

bool MemoryPlaybookCompiler::observe(MemoryStore& store, const std::string& scopeId,
                                     const taskforge::Json& episode) {
    if (!episode.is_object()) return false;
    const std::string taskType = field(episode, "taskType", "geral");
    const std::string outcome = field(episode, "outcome");
    const std::string episodeId = field(episode, "id");
    if (episodeId.empty() || taskType.empty()) return false;
    if (outcome != "success" && outcome != "failure") return false;

    const taskforge::Json tools = episode.contains("toolCalls") && episode["toolCalls"].is_array()
        ? episode["toolCalls"] : taskforge::Json::array();
    taskforge::Json steps = taskforge::Json::array();
    for (const auto& tool : tools) {
        if (!tool.is_object()) continue;
        const std::string name = field(tool, "name");
        if (name.empty()) continue;
        steps.push_back(taskforge::Json{{"tool", name}});
    }
    if (steps.empty()) return false;

    const std::string stepsJson = steps.dump();
    const std::string existingId = store.playbookIdBySignature(scopeId, taskType, stepsJson);
    const taskforge::Json evidence = evidenceFor(episode);

    if (outcome == "success") {
        if (!existingId.empty()) {
            MemoryPlaybookRecord current;
            std::string error;
            if (!store.getPlaybook(scopeId, existingId, current)) return false;
            const double confidence = std::min(1.0, std::max(current.confidence, 0.5) + 0.1);
            // Fase A: playbook nunca ativa sozinho; a ativacao exige validador na Fase B.
            return store.updatePlaybook(scopeId, existingId, 1, 0, confidence, "candidate",
                                        episodeId, evidence, error);
        }
        return !store.recordPlaybook(scopeId, taskType, taskType + " playbook", steps,
                                     taskforge::Json::object(), 0.6, "candidate",
                                     episodeId, evidence, 1, 0).empty();
    }

    if (existingId.empty()) return false;
    MemoryPlaybookRecord current;
    std::string error;
    if (!store.getPlaybook(scopeId, existingId, current)) return false;
    const double confidence = std::max(0.1, current.confidence - 0.15);
    return store.updatePlaybook(scopeId, existingId, 0, 1, confidence, "candidate",
                                episodeId, evidence, error);
}

} // namespace memory
} // namespace taskforge
