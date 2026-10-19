#pragma once

// Persistencia do arestrix-gateway: chaves virtuais, budgets e ledger de uso.
// SQLite single-node; a interface esconde o sqlite3 do restante do gateway.

#include <mutex>
#include <string>
#include <vector>

#include "gateway/GatewayTypes.hpp"

struct sqlite3;

namespace taskforge {
namespace gateway {

class GatewayStore {
public:
    GatewayStore() = default;
    ~GatewayStore();
    GatewayStore(const GatewayStore&) = delete;
    GatewayStore& operator=(const GatewayStore&) = delete;

    bool open(const std::string& path, std::string& error);
    void close();
    bool initSchema(std::string& error);
    bool healthy(std::string& error);

    bool authenticate(const std::string& token, VirtualKey& key, std::string& error);

    // Reserva o custo maximo estimado em todos os budgets aplicaveis. Se algum budget
    // com hard_stop estourar, nada e reservado e decision.allowed = false.
    bool checkAndReserve(const RequestContext& context, double estimatedUsd,
                         long long estimatedTokens, BudgetDecision& decision, std::string& error);

    // Converte a reserva no consumo real.
    bool reconcile(const RequestContext& context, const TokenUsage& usage, double actualUsd,
                   double estimatedUsd, long long estimatedTokens, std::string& error);

    bool recordUsage(const UsageEvent& event, std::string& error);

    bool createKey(const std::string& name, const std::string& project, const std::string& user,
                   int rpmLimit, int tpmLimit, std::string& plaintextKey, std::string& keyId,
                   std::string& error);
    bool revokeKey(const std::string& keyId, std::string& error);
    bool listKeys(Json& out, std::string& error);

    bool setBudget(const std::string& scopeType, const std::string& scopeId,
                   const std::string& period, double limitUsd, long long limitTokens,
                   bool hardStop, double alertAt, std::string& error);

    bool usageReport(const std::string& since, const std::string& keyId,
                     const std::string& provider, Json& out, std::string& error);
    bool usageForToken(const std::string& token, Json& out, std::string& error);

private:
    struct BudgetRow {
        long long id = 0;
        std::string scopeType;
        std::string scopeId;
        std::string period;
        std::string windowStart;
        double limitUsd = -1.0;
        long long limitTokens = -1;
        double spentUsd = 0.0;
        long long spentTokens = 0;
        double reservedUsd = 0.0;
        long long reservedTokens = 0;
        bool hardStop = true;
        double alertAt = 0.8;
    };

    bool ensureOpen(std::string& error) const;
    bool begin(std::string& error);
    bool commit(std::string& error);
    void rollback();
    bool loadBudgetsLocked(const RequestContext& context, std::vector<BudgetRow>& rows,
                           std::string& error);
    bool resumeBudgetWindowLocked(BudgetRow& row, std::string& error);
    bool updateBudgetLocked(const BudgetRow& row, std::string& error);
    bool insertUsageLocked(const UsageEvent& event, std::string& error);
    bool loadKeyByTokenLocked(const std::string& token, VirtualKey& key, std::string& error);
    Json budgetJson(const BudgetRow& row) const;
    Json usageTotalsLocked(const std::string& keyId, std::string& error);

    sqlite3* db_ = nullptr;
    std::mutex mu_;
};

} // namespace gateway
} // namespace taskforge
