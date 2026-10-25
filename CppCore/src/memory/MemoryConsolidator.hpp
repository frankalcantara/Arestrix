#pragma once

// Consolidacao L2/M3: dedup deterministico, conflitos, expiracao, decay e
// generalizacao. Nao apaga memoria: rebaixa para candidate/superseded/dormant,
// preserva evidencia e registra auditoria.

#include <cstddef>
#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct ConsolidationOptions {
    bool deduplicate = true;
    bool detectContradictions = true;
    bool expireFacts = true;
    bool applyDecay = true;
    bool generalize = true;
    int dormantAfterDays = 180;
    int generalizeMinSources = 3;
    double nearDuplicateSimilarity = 0.9;
    size_t maxActions = 2000;
};

struct ConsolidationReport {
    size_t duplicates = 0;
    size_t contradictions = 0;
    size_t expired = 0;
    size_t dormant = 0;
    size_t aliases = 0;
    size_t generalizations = 0;
    size_t errors = 0;
    std::vector<std::string> actions;

    bool ok() const { return errors == 0; }
    taskforge::Json toJson() const;
};

class MemoryConsolidator {
public:
    static ConsolidationReport run(MemoryStore& store, const std::string& scopeId,
                                   const ConsolidationOptions& options,
                                   std::string& error);
};

ConsolidationOptions consolidationOptionsFromJson(const taskforge::Json& value);
taskforge::Json consolidationOptionsToJson(const ConsolidationOptions& options);

} // namespace memory
} // namespace taskforge
