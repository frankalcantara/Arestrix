#pragma once

// E18: roteador por classe de tarefa. A decisao e pura e testavel; o runtime
// so a aplica quando routing.mode != off.

#include <string>
#include <vector>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

struct ModelTier {
    std::string name;
    std::string provider;
    std::string model;
    double cost = 1.0;
};

struct RoutingConfig {
    std::string mode = "off";      // off | shadow | binary
    ModelTier localSmall;
    ModelTier localLarge;
    ModelTier frontier;
    double shadowRate = 0.0;
    double pMin = 0.5;
    bool localOnly = false;
};

struct RoutingStats {
    long long success = 0;
    long long failure = 0;
    double coverage = 0.0;
};

struct RoutingDecision {
    std::string mode = "off";
    std::string tier;
    std::string provider;
    std::string model;
    std::string shadowTier;
    double confidence = 0.5;
    double expectedCost = 0.0;
    double frontierCost = 0.0;
    bool shadow = false;
    bool blockedRemote = false;
    bool escalated = false;
    std::string reason;
};

class ModelRouter {
public:
    static RoutingConfig configFromJson(const taskforge::Json& value);
    static taskforge::Json configToJson(const RoutingConfig& config);
    static RoutingDecision decide(const RoutingStats& stats, const RoutingConfig& config,
                                  const ModelTier* executed = nullptr);
    static RoutingDecision escalate(const RoutingDecision& decision,
                                    const RoutingConfig& config,
                                    const std::string& reason);
    static taskforge::Json decisionToJson(const RoutingDecision& decision);
};

} // namespace rt
} // namespace taskforge
