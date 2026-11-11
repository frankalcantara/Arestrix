#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

// Porteiro de politica da memoria. Decide se o agente deve receber o brief,
// consultar a memoria por ferramenta, aprender ou nao usar memoria. A decisao
// e independente do provedor: o runtime injeta a funcao de modelo, entao trocar
// Jev por um modelo local/API e apenas configuracao.
struct MemoryGateConfig {
    std::string mode = "off";        // off | shadow | enforce
    std::string provider;            // vazio = decisao deterministica, sem modelo
    std::string model;
    int maxSearches = 2;
    double minConfidence = 0.6;
    std::string fallback = "brief";  // none | brief | search
    int maxBriefChars = 1200;

    static MemoryGateConfig fromJson(const taskforge::Json& raw);
    bool enabled() const {
        return mode == "brief" || mode == "shadow" || mode == "enforce";
    }
    bool enforce() const { return mode == "brief" || mode == "enforce"; }
    std::string fingerprint() const;
};

struct MemoryGateInput {
    std::string projectId;
    std::string nodeId;
    std::string taskType;
    std::string theme;
    std::string taskInput;
    std::string brief;
    std::string requiredPattern;
    std::size_t briefChars = 0;
    int lessons = 0;
    int facts = 0;
    int playbooks = 0;
    int paths = 0;
    int facets = 0;
    bool memoryModeConsult = true;
    bool memoryWriteEnabled = false;
    bool localOnly = false;
};

struct MemoryGateModelRequest {
    std::string system;
    std::string user;
    int maxTokens = 160;
};

struct MemoryGateModelResult {
    bool ok = false;
    std::string text;
    std::string error;
    long long inTokens = 0;
    long long outTokens = 0;
    long long cachedTokens = 0;
};

using MemoryGateModelFn =
    std::function<MemoryGateModelResult(const MemoryGateModelRequest&)>;

struct MemoryGateDecision {
    std::string useMemory = "brief";  // none | brief | search | learn
    int maxSearches = 0;
    std::string query;
    std::string reason;
    double confidence = 0.0;
    std::string source = "deterministic";  // off | deterministic | model | fallback
    std::string error;
    long long latencyMs = 0;
    long long modelInTokens = 0;
    long long modelOutTokens = 0;
    long long modelCachedTokens = 0;
    bool applied = false;
    bool cached = false;

    taskforge::Json toJson() const;
};

class MemoryGate {
public:
    // A funcao de modelo e opcional. Sem ela, ou quando provider/model estao
    // vazios, a decisao e puramente deterministica.
    static MemoryGateDecision decide(const MemoryGateInput& input,
                                     const MemoryGateConfig& config,
                                     const MemoryGateModelFn& model = {});
};

}  // namespace memory
}  // namespace taskforge
