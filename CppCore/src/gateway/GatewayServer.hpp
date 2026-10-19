#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>

#include "gateway/GatewayStore.hpp"
#include "gateway/GatewayTypes.hpp"

namespace taskforge {
namespace gateway {

class GatewayServer {
public:
    GatewayServer(GatewayConfig config, GatewayStore& store);
    bool run(std::string& error);

private:
    struct RateEvent {
        std::chrono::steady_clock::time_point at;
        long long tokens = 0;
        std::string requestId;
    };
    struct Metrics {
        std::atomic<long long> requests{0};
        std::atomic<long long> success{0};
        std::atomic<long long> errors{0};
        std::atomic<long long> budgetRejections{0};
        std::atomic<long long> rateLimited{0};
        std::atomic<long long> inputTokens{0};
        std::atomic<long long> outputTokens{0};
        std::atomic<long long> costMicros{0};
    };

    bool checkRateLimit(const VirtualKey& key, const std::string& requestId,
                        long long estimatedTokens, std::string& error);
    void finishRateLimit(const std::string& keyId, const std::string& requestId, long long tokens);
    long long estimateInputTokens(const std::string& body);
    TokenUsage parseUsage(const ProviderRoute& route, const std::string& body) const;
    double computeCost(const ProviderRoute& route, const Price& price, const TokenUsage& usage) const;
    void logEvent(const UsageEvent& event, const BudgetDecision& budget);

    GatewayConfig config_;
    GatewayStore& store_;
    Metrics metrics_;
    std::mutex rateMu_;
    std::map<std::string, std::deque<RateEvent>> rateEvents_;
};

} // namespace gateway
} // namespace taskforge
