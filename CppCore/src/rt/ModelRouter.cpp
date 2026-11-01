#include "ModelRouter.hpp"

#include <algorithm>
#include <cmath>

namespace taskforge {
namespace rt {

namespace {

ModelTier parseTier(const taskforge::Json& value, const ModelTier& fallback) {
    if (!value.is_object()) return fallback;
    ModelTier tier = fallback;
    tier.provider = value.value("provider", tier.provider);
    tier.model = value.value("model", tier.model);
    tier.cost = std::max(0.0, value.value("cost", tier.cost));
    return tier;
}

void select(RoutingDecision& decision, const ModelTier& tier, const std::string& reason) {
    decision.tier = tier.name;
    decision.provider = tier.provider;
    decision.model = tier.model;
    decision.reason = reason;
}

} // namespace

RoutingConfig ModelRouter::configFromJson(const taskforge::Json& value) {
    RoutingConfig config;
    config.localSmall = ModelTier{"local_small", "local", "", 0.05};
    config.localLarge = ModelTier{"local_large", "local", "", 0.20};
    config.frontier = ModelTier{"frontier", "deepseek", "deepseek-flash", 1.0};
    const taskforge::Json routing = value.contains("routing") ? value["routing"] : value;
    if (!routing.is_object()) return config;
    config.mode = routing.value("mode", config.mode);
    if (config.mode != "off" && config.mode != "shadow" && config.mode != "binary") {
        config.mode = "off";
    }
    config.shadowRate = std::max(0.0, std::min(1.0, routing.value("shadowRate", 0.0)));
    config.pMin = std::max(0.0, std::min(1.0, routing.value("pMin", 0.5)));
    config.localOnly = routing.value("localOnly", false);
    if (routing.contains("tiers") && routing["tiers"].is_object()) {
        const auto& tiers = routing["tiers"];
        config.localSmall = parseTier(tiers.contains("localSmall") ? tiers["localSmall"]
                                                                   : taskforge::Json(),
                                      config.localSmall);
        config.localLarge = parseTier(tiers.contains("localLarge") ? tiers["localLarge"]
                                                                   : taskforge::Json(),
                                      config.localLarge);
        config.frontier = parseTier(tiers.contains("frontier") ? tiers["frontier"]
                                                               : taskforge::Json(),
                                    config.frontier);
    }
    return config;
}

taskforge::Json ModelRouter::configToJson(const RoutingConfig& config) {
    auto tier = [](const ModelTier& value) {
        return taskforge::Json{{"provider", value.provider},
                               {"model", value.model}, {"cost", value.cost}};
    };
    return taskforge::Json{
        {"mode", config.mode}, {"shadowRate", config.shadowRate},
        {"pMin", config.pMin}, {"localOnly", config.localOnly},
        {"tiers", taskforge::Json{{"localSmall", tier(config.localSmall)},
                                  {"localLarge", tier(config.localLarge)},
                                  {"frontier", tier(config.frontier)}}}};
}

RoutingDecision ModelRouter::decide(const RoutingStats& stats, const RoutingConfig& config,
                                    const ModelTier* executed) {
    RoutingDecision decision;
    decision.mode = config.mode;
    decision.frontierCost = config.frontier.cost;
    const ModelTier fallback = executed ? *executed : config.frontier;
    if (config.mode == "off") {
        select(decision, fallback, "routing_off");
        decision.confidence = 0.5;
        return decision;
    }
    const ModelTier& local = config.localLarge.model.empty() ? config.localSmall : config.localLarge;
    const bool localUsable = !local.provider.empty() && !local.model.empty();
    auto chooseLocal = [&](const std::string& reason) {
        if (localUsable) {
            select(decision, local, reason);
        } else {
            // Privacidade nunca cai para o nivel remoto; o runtime falha
            // fechado se nenhum nivel local estiver configurado.
            decision.blockedRemote = true;
            select(decision, local, "no_local_tier");
        }
    };
    if (config.localOnly) {
        chooseLocal("privacy_local_only");
        decision.confidence = 1.0;
        decision.expectedCost = local.cost;
        return decision;
    }
    const long long total = stats.success + stats.failure;
    if (total <= 0) {
        select(decision, config.frontier, "insufficient_data");
        decision.confidence = 0.5;
        decision.expectedCost = config.frontier.cost;
        return decision;
    }
    double p = (static_cast<double>(stats.success) + 1.0) /
               (static_cast<double>(total) + 2.0);
    p = std::max(0.0, std::min(1.0, p + 0.10 * std::max(0.0, std::min(1.0, stats.coverage))));
    decision.confidence = p;
    const double localExpected = local.cost + (1.0 - p) * config.frontier.cost;
    decision.expectedCost = localExpected;
    const bool useLocal = localUsable && p >= config.pMin && localExpected <= config.frontier.cost;
    const ModelTier& selected = useLocal ? local : config.frontier;
    if (config.mode == "shadow") {
        // Shadow decide e registra, mas a execucao continua no nivel atual.
        decision.shadow = true;
        decision.shadowTier = selected.name;
        select(decision, fallback, "shadow:" + selected.name);
        decision.expectedCost = useLocal ? localExpected : config.frontier.cost;
        return decision;
    }
    select(decision, selected, useLocal ? "local_confidence" : "frontier_expected_cost");
    decision.expectedCost = useLocal ? localExpected : config.frontier.cost;
    return decision;
}

RoutingDecision ModelRouter::escalate(const RoutingDecision& decision,
                                      const RoutingConfig& config,
                                      const std::string& reason) {
    RoutingDecision next = decision;
    next.escalated = true;
    next.reason = reason;
    if (config.localOnly) {
        next.blockedRemote = true;
        next.tier = "local";
        next.provider = config.localLarge.model.empty() ? config.localSmall.provider
                                                        : config.localLarge.provider;
        next.model = config.localLarge.model.empty() ? config.localSmall.model
                                                     : config.localLarge.model;
        return next;
    }
    next.tier = config.frontier.name;
    next.provider = config.frontier.provider;
    next.model = config.frontier.model;
    next.expectedCost = config.frontier.cost;
    return next;
}

taskforge::Json ModelRouter::decisionToJson(const RoutingDecision& decision) {
    return taskforge::Json{
        {"mode", decision.mode}, {"tier", decision.tier},
        {"provider", decision.provider}, {"model", decision.model},
        {"shadow", decision.shadow}, {"shadowTier", decision.shadowTier},
        {"confidence", decision.confidence}, {"expectedCost", decision.expectedCost},
        {"frontierCost", decision.frontierCost},
        {"blockedRemote", decision.blockedRemote}, {"escalated", decision.escalated},
        {"reason", decision.reason}};
}

} // namespace rt
} // namespace taskforge
