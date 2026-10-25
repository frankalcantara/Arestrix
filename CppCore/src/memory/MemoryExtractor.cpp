#include "MemoryExtractor.hpp"

#include <algorithm>
#include <set>

namespace taskforge {
namespace memory {

namespace {

double clampConfidence(double value) {
    return std::max(0.05, std::min(0.95, value));
}

std::string stringField(const taskforge::Json& object, const char* key,
                        const std::string& fallback = "") {
    if (!object.is_object() || !object.contains(key)) return fallback;
    const auto& value = object[key];
    return value.is_string() ? value.str() : fallback;
}

std::string errorText(const taskforge::Json& episode) {
    std::string error = stringField(episode, "errorClass");
    if (!error.empty()) return error;
    const taskforge::Json metadata = episode.contains("metadata") && episode["metadata"].is_object()
        ? episode["metadata"] : taskforge::Json::object();
    error = stringField(metadata, "error");
    if (!error.empty()) return error;
    return stringField(episode, "content", "falha sem detalhe");
}

taskforge::Json baseEvidence(const taskforge::Json& episode, const std::string& kind) {
    taskforge::Json evidence = taskforge::Json::array();
    evidence.push_back(taskforge::Json{{"kind", kind},
                                       {"episodeId", stringField(episode, "id")},
                                       {"runId", stringField(episode, "runId")},
                                       {"nodeId", stringField(episode, "nodeId")},
                                       {"outcome", stringField(episode, "outcome")}});
    return evidence;
}

} // namespace

std::vector<MemoryProposalDraft> MemoryExtractor::extract(const taskforge::Json& episode) {
    std::vector<MemoryProposalDraft> proposals;
    if (!episode.is_object() || stringField(episode, "id").empty()) return proposals;

    const std::string taskType = stringField(episode, "taskType", "geral");
    const std::string outcome = stringField(episode, "outcome");
    const bool success = outcome == "success";
    const taskforge::Json tools = episode.contains("toolCalls") && episode["toolCalls"].is_array()
        ? episode["toolCalls"] : taskforge::Json::array();
    const taskforge::Json files = episode.contains("filesTouched") && episode["filesTouched"].is_array()
        ? episode["filesTouched"] : taskforge::Json::array();
    const taskforge::Json artifacts = episode.contains("artifacts") && episode["artifacts"].is_array()
        ? episode["artifacts"] : taskforge::Json::array();

    double confidence = 0.5;
    if (success) confidence += 0.2;
    else confidence -= 0.3;
    if (!artifacts.empty()) confidence += 0.1;
    confidence = clampConfidence(confidence);

    const taskforge::Json evidence = baseEvidence(episode, "episode");

    if (!success) {
        const std::string error = errorText(episode);
        std::string rule = "Evitar falha em " + taskType + ": " + error;
        if (rule.size() > 4000) rule.resize(4000);
        MemoryProposalDraft lesson;
        lesson.kind = "lesson";
        lesson.confidence = confidence;
        lesson.payload = taskforge::Json{{"domain", taskType.empty() ? "geral" : taskType},
                                         {"triggerError", error},
                                         {"lessonRule", rule}};
        lesson.evidence = evidence;
        proposals.push_back(std::move(lesson));
        return proposals;
    }

    std::set<std::string> seen;

    auto addFact = [&](const std::string& predicate, const std::string& object,
                       const taskforge::Json& extraEvidence) {
        if (object.empty()) return;
        taskforge::Json payload = taskforge::Json{{"subject", taskType},
                                                  {"predicate", predicate},
                                                  {"object", object}};
        const std::string key = "fact|" + payload.dump();
        if (!seen.insert(key).second) return;
        MemoryProposalDraft draft;
        draft.kind = "fact";
        draft.confidence = confidence;
        draft.payload = std::move(payload);
        draft.evidence = evidence;
        draft.evidence.push_back(extraEvidence);
        proposals.push_back(std::move(draft));
    };

    for (const auto& tool : tools) {
        if (!tool.is_object() || !tool.value("ok", false)) continue;
        const std::string name = stringField(tool, "name");
        addFact("USES_TOOL", name, taskforge::Json{{"kind", "tool"}, {"name", name}});

        const std::string key = "entity|tool|" + name;
        if (!name.empty() && seen.insert(key).second && proposals.size() < 20) {
            MemoryProposalDraft entity;
            entity.kind = "entity";
            entity.confidence = confidence;
            entity.payload = taskforge::Json{{"name", name}, {"entityKind", "tool"}};
            entity.evidence = evidence;
            entity.evidence.push_back(taskforge::Json{{"kind", "tool"}, {"name", name}});
            proposals.push_back(std::move(entity));
        }
    }

    for (const auto& artifact : artifacts) {
        if (!artifact.is_string()) continue;
        addFact("PRODUCES", artifact.str(),
                taskforge::Json{{"kind", "artifact"}, {"path", artifact.str()}});
    }

    if (!tools.empty()) {
        taskforge::Json steps = taskforge::Json::array();
        for (const auto& tool : tools) {
            if (!tool.is_object()) continue;
            const std::string name = stringField(tool, "name");
            if (!name.empty()) steps.push_back(taskforge::Json{{"tool", name}});
        }
        if (!steps.empty()) {
            MemoryProposalDraft playbook;
            playbook.kind = "playbook";
            playbook.confidence = confidence;
            playbook.payload = taskforge::Json{{"taskType", taskType},
                                               {"title", taskType + " playbook"},
                                               {"steps", steps},
                                               {"preconditions", taskforge::Json::object()}};
            playbook.evidence = evidence;
            proposals.push_back(std::move(playbook));
        }
    }

    taskforge::Json summary = taskforge::Json{{"taskType", taskType},
                                              {"title", taskType + " success"},
                                              {"outcome", outcome},
                                              {"tools", tools},
                                              {"filesTouched", files},
                                              {"artifacts", artifacts}};
    MemoryProposalDraft summaryDraft;
    summaryDraft.kind = "summary";
    summaryDraft.confidence = confidence;
    summaryDraft.payload = std::move(summary);
    summaryDraft.evidence = evidence;
    proposals.push_back(std::move(summaryDraft));

    if (proposals.size() > 20) proposals.resize(20);
    return proposals;
}

size_t MemoryExtractor::extractAndStore(MemoryStore& store, const std::string& scopeId,
                                        const std::string& episodeId) {
    const taskforge::Json episode = store.getEpisode(scopeId, episodeId);
    if (!episode.is_object() || episode.empty()) return 0;
    size_t stored = 0;
    for (const auto& draft : extract(episode)) {
        if (!store.recordProposal(scopeId, draft.kind, draft.payload, episodeId,
                                  draft.evidence, draft.confidence).empty()) {
            ++stored;
        }
    }
    return stored;
}

} // namespace memory
} // namespace taskforge
