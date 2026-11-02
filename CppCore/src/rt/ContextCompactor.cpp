#include "rt/ContextCompactor.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "memory/MemorySanitizer.hpp"
#include "memory/MemoryService.hpp"
#include "memory/MemoryStore.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {

namespace {

constexpr size_t kMaxQueryChars = 6000;

std::string oneLine(std::string value, size_t maxChars) {
    std::string out;
    out.reserve(std::min(value.size(), maxChars + 3));
    bool pendingSpace = false;
    for (unsigned char c : value) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (!out.empty()) pendingSpace = true;
            continue;
        }
        if (c < 0x20 || c == 0x7f) continue;
        if (pendingSpace) {
            out.push_back(' ');
            pendingSpace = false;
        }
        out.push_back(static_cast<char>(c));
        if (out.size() >= maxChars) break;
    }
    if (out.size() >= maxChars && out.size() < value.size()) out += "...";
    return out;
}

std::string cleanText(const std::string& value, size_t maxChars) {
    return memory::sanitizeText(oneLine(value, maxChars), maxChars).text;
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 160) return false;
    for (unsigned char c : id) {
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '.' && c != ':') return false;
    }
    return true;
}

std::set<std::string> tokens(const std::string& text) {
    std::set<std::string> out;
    std::string current;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            if (current.size() >= 3) out.insert(current);
            current.clear();
        }
        if (out.size() >= 400) break;
    }
    if (current.size() >= 3) out.insert(current);
    return out;
}

std::string lowerAscii(std::string value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

int relevance(const DurableMemoryItem& item, const std::set<std::string>& queryTokens) {
    if (queryTokens.empty()) return 0;
    const std::string haystack = lowerAscii(item.text + " " + item.kind);
    int score = 0;
    for (const auto& token : queryTokens) {
        if (haystack.find(token) != std::string::npos) ++score;
    }
    return score;
}

std::vector<DurableMemoryItem> rankItems(std::vector<DurableMemoryItem> items,
                                         const std::string& query,
                                         size_t maxItems) {
    std::vector<DurableMemoryItem> unique;
    std::set<std::string> seen;
    for (auto& item : items) {
        if (!safeId(item.id)) continue;
        if (item.text.empty()) continue;
        if (!seen.insert(item.id).second) continue;
        item.kind = cleanText(item.kind, 24);
        item.text = cleanText(item.text, 320);
        if (item.text.empty()) continue;
        unique.push_back(std::move(item));
    }
    const auto queryTokens = tokens(query);
    std::stable_sort(unique.begin(), unique.end(),
                     [&](const DurableMemoryItem& a, const DurableMemoryItem& b) {
                         const int scoreA = relevance(a, queryTokens);
                         const int scoreB = relevance(b, queryTokens);
                         if (scoreA != scoreB) return scoreA > scoreB;
                         return a.id < b.id;
                     });
    if (unique.size() > maxItems) unique.resize(maxItems);
    return unique;
}

std::string toolActionText(const ChatToolCall& call) {
    std::string detail;
    if (call.args.is_object()) {
        for (const char* key : {"path", "query", "command", "name", "file"}) {
            if (!call.args.contains(key) || !call.args.at(key).is_string()) continue;
            const std::string value = call.args.at(key).str();
            if (!value.empty()) {
                detail = value;
                break;
            }
        }
        if (detail.empty()) {
            const std::string dump = call.args.dump();
            if (dump != "{}" && dump != "null") detail = dump;
        }
    }
    std::string out = call.name.empty() ? std::string("tool") : call.name;
    if (!detail.empty()) out += "(" + oneLine(detail, 80) + ")";
    return cleanText(out, 120);
}

std::vector<std::string> completedActions(const std::vector<ChatMessage>& messages,
                                          size_t begin, size_t end,
                                          size_t* errorResults) {
    std::vector<std::string> actions;
    size_t errors = 0;
    for (size_t i = begin; i < end; ++i) {
        const auto& message = messages[i];
        if (message.role == "tool" && message.isError) ++errors;
        if (message.role != "assistant") continue;
        for (const auto& call : message.toolCalls) {
            if (actions.size() >= 12) break;
            actions.push_back(toolActionText(call));
        }
        if (actions.size() >= 12) break;
    }
    if (errorResults) *errorResults = errors;
    return actions;
}

std::string assistantNotes(const std::vector<ChatMessage>& messages, size_t begin, size_t end) {
    std::string notes;
    for (size_t i = begin; i < end; ++i) {
        const auto& message = messages[i];
        if (message.role != "assistant" || message.text.empty() || !message.toolCalls.empty()) {
            continue;
        }
        if (!notes.empty()) notes += " ";
        notes += oneLine(message.text, 120);
        if (notes.size() >= 160) break;
    }
    return cleanText(notes, 180);
}

std::string buildSummary(const CompactionConfig& config,
                         const std::vector<ChatMessage>& messages,
                         size_t fixedPrefix, size_t cut,
                         const std::vector<DurableMemoryItem>& items,
                         std::vector<std::string>* memoryIds) {
    const size_t budget = std::max<size_t>(
        64, std::min(config.maxSummaryChars, config.maxSummaryTokens * 4));

    size_t errorResults = 0;
    const auto actions = completedActions(messages, fixedPrefix, cut, &errorResults);
    const std::string notes = assistantNotes(messages, fixedPrefix, cut);

    std::ostringstream header;
    header << "[ARESTRIX CONTEXT COMPACTION - DERIVED SUMMARY, NOT NEW EVIDENCE]\n"
           << "The original " << (cut - fixedPrefix)
           << " messages remain available to the runtime for fallback. This block replaces them "
              "only in the request sent to the model; it is derived text and must not be cited as "
              "source evidence.\n";
    if (!actions.empty()) {
        header << "Completed tool actions: ";
        for (size_t i = 0; i < actions.size(); ++i) {
            if (i) header << "; ";
            header << actions[i];
        }
        if (errorResults > 0) header << " (" << errorResults << " tool error(s))";
        header << ".\n";
    }
    if (!notes.empty()) header << "Earlier assistant notes: " << notes << "\n";
    header << "Durable memory selected (cite [MEMO:<id>] when used):\n";
    const std::string prefix = header.str();
    if (prefix.size() > budget) return std::string();

    std::string body;
    for (const auto& item : items) {
        const std::string line = "- [MEMO:" + item.id + "] (" + item.kind + ") " + item.text + "\n";
        if (prefix.size() + body.size() + line.size() > budget) break;
        body += line;
        memoryIds->push_back(item.id);
    }
    if (body.empty()) return std::string();
    return prefix + body;
}

} // namespace

ContextCompactor::ContextCompactor(CompactionConfig config, DurableItemProvider provider)
    : config_(config), provider_(std::move(provider)) {}

size_t ContextCompactor::messageChars(const ChatMessage& message) {
    size_t chars = message.text.size() + message.reasoning.size() + message.toolCallId.size();
    for (const auto& call : message.toolCalls) {
        chars += call.id.size() + call.name.size() + call.args.dump().size();
    }
    return chars;
}

size_t ContextCompactor::historyChars(const std::vector<ChatMessage>& messages) {
    size_t chars = 0;
    for (const auto& message : messages) chars += messageChars(message);
    return chars;
}

size_t ContextCompactor::estimateTokens(size_t chars) {
    return (chars + 3) / 4;
}

CompactionResult ContextCompactor::compact(const std::vector<ChatMessage>& messages) const {
    CompactionResult result;
    result.messages = messages;
    const size_t historyCharsBefore = historyChars(messages);
    if (!config_.enabled) {
        result.fallbackReason = "switch_off";
        return result;
    }
    if (config_.useLocalModel) {
        result.fallbackReason = "local_model_not_configured";
        return result;
    }
    if (historyCharsBefore < config_.minHistoryChars) {
        result.fallbackReason = "history_below_threshold";
        return result;
    }
    // Prefixo fixo: mensagens de sistema e a tarefa atual (primeira mensagem
    // nao-sistema) nunca sao compactadas.
    size_t fixedPrefix = 0;
    while (fixedPrefix < messages.size() && messages[fixedPrefix].role == "system") ++fixedPrefix;
    if (fixedPrefix >= messages.size()) {
        result.fallbackReason = "no_task";
        return result;
    }
    ++fixedPrefix;   // inclui a tarefa atual
    const size_t keepRecent = std::max<size_t>(1, config_.keepRecentMessages);
    if (messages.size() <= fixedPrefix + keepRecent) {
        result.fallbackReason = "history_below_keep_window";
        return result;
    }
    size_t cut = messages.size() - keepRecent;
    // Nunca termina o prefixo num assistant com tool_calls pendentes e nunca
    // deixa um tool result orfao na primeira mensagem preservada.
    while (cut > fixedPrefix) {
        const bool lastRemovedHasCalls = !messages[cut - 1].toolCalls.empty();
        const bool firstKeptIsTool = cut < messages.size() && messages[cut].role == "tool";
        if (!lastRemovedHasCalls && !firstKeptIsTool) break;
        --cut;
    }
    if (cut <= fixedPrefix) {
        result.fallbackReason = "no_complete_turn";
        return result;
    }

    std::string query;
    for (size_t i = fixedPrefix; i < cut; ++i) {
        const auto& message = messages[i];
        if (!message.text.empty()) {
            if (!query.empty()) query += "\n";
            query += message.text;
        }
        for (const auto& call : message.toolCalls) {
            if (!query.empty()) query += "\n";
            query += call.name;
            query += " ";
            query += call.args.dump();
        }
        if (query.size() >= kMaxQueryChars) break;
    }
    if (query.size() > kMaxQueryChars) query.resize(kMaxQueryChars);

    std::vector<DurableMemoryItem> items;
    try {
        if (provider_) items = provider_(query);
    } catch (const std::exception& ex) {
        result.fallbackReason = std::string("provider_error: ") + ex.what();
        return result;
    } catch (...) {
        result.fallbackReason = "provider_error";
        return result;
    }
    if (items.empty()) {
        result.fallbackReason = "insufficient_memory";
        return result;
    }
    items = rankItems(std::move(items), query, std::max<size_t>(1, config_.maxItems));
    if (items.empty()) {
        result.fallbackReason = "insufficient_memory";
        return result;
    }

    std::vector<std::string> memoryIds;
    const std::string summary =
        buildSummary(config_, messages, fixedPrefix, cut, items, &memoryIds);
    if (summary.empty()) {
        result.fallbackReason = "summary_budget_exceeded";
        return result;
    }

    size_t removedChars = 0;
    size_t turns = 0;
    for (size_t i = fixedPrefix; i < cut; ++i) {
        removedChars += messageChars(messages[i]);
        if (messages[i].role == "assistant") ++turns;
    }
    // O bloco precisa ser menor que o material removido; caso contrario nao ha
    // reducao e o historico original e mantido.
    if (summary.size() >= removedChars) {
        result.fallbackReason = "no_reduction";
        return result;
    }

    std::vector<ChatMessage> compacted;
    compacted.reserve(messages.size() - cut + fixedPrefix + 1);
    for (size_t i = 0; i < fixedPrefix; ++i) compacted.push_back(messages[i]);
    ChatMessage block;
    block.role = "user";
    block.text = summary;
    compacted.push_back(std::move(block));
    for (size_t i = cut; i < messages.size(); ++i) compacted.push_back(messages[i]);

    result.messages = std::move(compacted);
    result.originalChars = removedChars;
    result.compactedChars = summary.size();
    result.removedMessages = cut - fixedPrefix;
    result.compactedTurns = turns;
    result.memoryIds = std::move(memoryIds);
    result.applied = true;
    return result;
}

std::vector<DurableMemoryItem> memoryDurableItems(
    memory::MemoryService& service, const std::string& projectId,
    const std::vector<std::string>& domains, const std::string& taskType,
    const std::string& query) {
    (void)query;
    std::vector<DurableMemoryItem> items;
    memory::MemoryStore* store = service.store(projectId);
    if (!store) return items;
    const std::string scope = "project:" + projectId;

    // Licoes ativas: maior confianca primeiro, desempate pelo id.
    std::vector<memory::MemoryLesson> lessons = domains.empty()
        ? store->getLessons(scope)
        : store->getLessonsForDomains(scope, domains);
    std::stable_sort(lessons.begin(), lessons.end(),
                     [](const memory::MemoryLesson& a, const memory::MemoryLesson& b) {
                         if (a.confidence != b.confidence) return a.confidence > b.confidence;
                         return a.id < b.id;
                     });
    size_t lessonCount = 0;
    for (const auto& lesson : lessons) {
        if (lessonCount++ >= 8) break;
        items.push_back({lesson.id, "lesson", lesson.lessonRule});
    }

    // Claims ativas do multigrafo. Somente leitura; nenhuma aresta e criada.
    const auto names = store->nodeNames(scope);
    size_t claimCount = 0;
    for (const auto& edge : store->listEdges(scope, "active", 500)) {
        if (claimCount >= 10) break;
        bool isClaim = edge.sourceKind == "claim";
        if (!isClaim) {
            auto metadata = jsonutil::tryParse(edge.metadata);
            if (metadata && metadata->is_object()) isClaim = metadata->value("claim", false);
        }
        if (!isClaim) continue;
        auto nameOf = [&](const std::string& id) {
            auto it = names.find(id);
            return it == names.end() ? id : it->second;
        };
        std::string text = nameOf(edge.srcId) + " " + edge.relation + " " + nameOf(edge.dstId);
        auto value = jsonutil::tryParse(edge.valueJson);
        if (value && value->is_object() && value->size() > 0) {
            for (auto it = value->begin(); it != value->end(); ++it) {
                if (!it.value().is_string()) continue;
                const std::string detail = it.value().str();
                if (!detail.empty()) text += " (" + detail + ")";
                break;
            }
        }
        items.push_back({edge.id, "claim", text});
        ++claimCount;
    }

    // Caminhos ativos.
    const auto paths = service.listPaths(projectId, "active");
    size_t pathCount = 0;
    for (const auto& path : paths) {
        if (pathCount++ >= 5 || !path.is_object()) break;
        const std::string id = path.value("id", std::string());
        const std::string type = path.value("taskType", std::string("path"));
        std::ostringstream text;
        text << "task=" << type << ", utility=" << path.value("utility", 0.0)
             << ", uses=" << path.value("uses", 0LL);
        items.push_back({id, "path", text.str()});
    }

    // Facetas ativas.
    size_t facetCount = 0;
    for (const auto& facet : store->listFacets(scope)) {
        if (facetCount >= 6) break;
        if (facet.status != "active" || facet.description.empty()) continue;
        items.push_back({facet.id, "facet", facet.description});
        ++facetCount;
    }

    // Passagens ja citadas em execucoes validadas.
    const auto passageIds = store->usedPassageIds(scope, 20);
    if (!passageIds.empty()) {
        const auto passages = store->passagesForIds(scope, passageIds, 8);
        for (const auto& passage : passages) {
            std::string text = passage.text;
            if (!passage.uri.empty()) text += " [" + passage.uri + "]";
            items.push_back({passage.passageId, "passage", text});
        }
    }

    // Tracos validados por um veredito independente.
    const auto traceIds = store->approvedTraceIds(scope, 5);
    if (!traceIds.empty()) {
        std::set<std::string> approved(traceIds.begin(), traceIds.end());
        size_t traceCount = 0;
        for (const auto& trace : store->listTraces(scope, 2000)) {
            if (traceCount >= 3) break;
            if (!approved.count(trace.traceId)) continue;
            std::string text = "task=" + trace.taskType + ", outcome=" + trace.outcome;
            if (!trace.model.empty()) text += ", model=" + trace.model;
            items.push_back({trace.traceId, "trace", text});
            ++traceCount;
        }
    }

    // Playbooks ativos do task_type consultado.
    size_t playbookCount = 0;
    for (const auto& playbook : store->listPlaybooks(scope, taskType, "active", 3)) {
        if (playbookCount++ >= 3) break;
        items.push_back({playbook.id, "playbook", playbook.title + " | " + playbook.stepsJson});
    }

    return items;
}

} // namespace rt
} // namespace taskforge
