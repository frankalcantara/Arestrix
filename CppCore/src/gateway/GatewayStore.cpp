#include "gateway/GatewayStore.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <sstream>

#include <sqlite3.h>

#include "project/RunnerSecurity.hpp"

namespace taskforge {
namespace gateway {

namespace {

std::string sqliteMessage(sqlite3* db, const char* prefix) {
    std::string message = prefix ? prefix : "sqlite";
    if (db) message += ": " + std::string(sqlite3_errmsg(db));
    return message;
}

bool exec(sqlite3* db, const char* sql, std::string& error) {
    char* detail = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &detail) != SQLITE_OK) {
        error = detail ? detail : "sqlite exec falhou";
        sqlite3_free(detail);
        return false;
    }
    return true;
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    ~Statement() { if (stmt) sqlite3_finalize(stmt); }
    bool prepare(sqlite3* db, const char* sql, std::string& error) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            error = sqliteMessage(db, "prepare");
            return false;
        }
        return true;
    }
};

std::string randomHex(std::size_t bytes) {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const unsigned value = static_cast<unsigned>(rng() & 0xff);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0f]);
    }
    return out;
}

bool validScope(const std::string& scope) {
    return scope == "key" || scope == "project" || scope == "user" ||
           scope == "provider" || scope == "model";
}

bool validPeriod(const std::string& period) {
    return period == "daily" || period == "monthly" || period == "total";
}

} // namespace

GatewayStore::~GatewayStore() {
    if (db_) sqlite3_close(db_);
}

bool GatewayStore::ensureOpen(std::string& error) const {
    if (!db_) {
        error = "banco do gateway nao aberto";
        return false;
    }
    return true;
}

bool GatewayStore::open(const std::string& path, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        error = sqliteMessage(db_, "sqlite open");
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    sqlite3_busy_timeout(db_, 5000);
    if (!exec(db_, "PRAGMA journal_mode=WAL;", error)) return false;
    if (!exec(db_, "PRAGMA foreign_keys=ON;", error)) return false;
    return true;
}

void GatewayStore::close() {
    std::lock_guard<std::mutex> lock(mu_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool GatewayStore::initSchema(std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    const char* schema = R"SQL(
CREATE TABLE IF NOT EXISTS virtual_keys (
  id TEXT PRIMARY KEY,
  key_hash TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  project TEXT NOT NULL DEFAULT '',
  user TEXT NOT NULL DEFAULT '',
  enabled INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL,
  last_used_at TEXT NOT NULL DEFAULT '',
  rpm_limit INTEGER NOT NULL DEFAULT 0,
  tpm_limit INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS budgets (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  scope_type TEXT NOT NULL,
  scope_id TEXT NOT NULL,
  period TEXT NOT NULL,
  window_start TEXT NOT NULL,
  limit_usd REAL NOT NULL DEFAULT -1,
  limit_tokens INTEGER NOT NULL DEFAULT -1,
  spent_usd REAL NOT NULL DEFAULT 0,
  spent_tokens INTEGER NOT NULL DEFAULT 0,
  reserved_usd REAL NOT NULL DEFAULT 0,
  reserved_tokens INTEGER NOT NULL DEFAULT 0,
  hard_stop INTEGER NOT NULL DEFAULT 1,
  alert_at REAL NOT NULL DEFAULT 0.8,
  UNIQUE(scope_type, scope_id, period)
);
CREATE TABLE IF NOT EXISTS usage_events (
  id TEXT PRIMARY KEY,
  ts TEXT NOT NULL,
  key_id TEXT NOT NULL DEFAULT '',
  project TEXT NOT NULL DEFAULT '',
  user TEXT NOT NULL DEFAULT '',
  provider TEXT NOT NULL,
  model TEXT NOT NULL,
  input_tokens INTEGER NOT NULL DEFAULT 0,
  output_tokens INTEGER NOT NULL DEFAULT 0,
  cached_tokens INTEGER NOT NULL DEFAULT 0,
  cost_usd REAL NOT NULL DEFAULT 0,
  latency_ms INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL,
  error_code TEXT NOT NULL DEFAULT '',
  runner TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_usage_ts ON usage_events(ts);
CREATE INDEX IF NOT EXISTS idx_usage_key ON usage_events(key_id, ts);
CREATE INDEX IF NOT EXISTS idx_usage_provider ON usage_events(provider, ts);
)SQL";
    return exec(db_, schema, error);
}

bool GatewayStore::healthy(std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    Statement stmt;
    if (!stmt.prepare(db_, "SELECT 1;", error)) return false;
    return sqlite3_step(stmt.stmt) == SQLITE_ROW;
}

bool GatewayStore::begin(std::string& error) {
    return exec(db_, "BEGIN IMMEDIATE;", error);
}

bool GatewayStore::commit(std::string& error) {
    return exec(db_, "COMMIT;", error);
}

void GatewayStore::rollback() {
    std::string ignored;
    exec(db_, "ROLLBACK;", ignored);
}

bool GatewayStore::loadKeyByTokenLocked(const std::string& token, VirtualKey& key, std::string& error) {
    const std::string hash = project::RunnerSecurity::sha256(token);
    Statement stmt;
    if (!stmt.prepare(db_,
        "SELECT id, name, project, user, enabled, rpm_limit, tpm_limit, created_at, last_used_at "
        "FROM virtual_keys WHERE key_hash = ? LIMIT 1;", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    const int step = sqlite3_step(stmt.stmt);
    if (step != SQLITE_ROW) {
        error = "chave virtual invalida";
        return false;
    }
    key.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
    key.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
    key.project = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
    key.user = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3));
    key.enabled = sqlite3_column_int(stmt.stmt, 4) != 0;
    key.rpmLimit = sqlite3_column_int(stmt.stmt, 5);
    key.tpmLimit = sqlite3_column_int(stmt.stmt, 6);
    key.createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 7));
    key.lastUsedAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 8));
    if (!key.enabled) {
        error = "chave virtual revogada";
        return false;
    }

    Statement update;
    if (update.prepare(db_, "UPDATE virtual_keys SET last_used_at = ? WHERE id = ?;", error)) {
        const std::string now = nowIso();
        sqlite3_bind_text(update.stmt, 1, now.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(update.stmt, 2, key.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(update.stmt);
    }
    return true;
}

bool GatewayStore::authenticate(const std::string& token, VirtualKey& key, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    return loadKeyByTokenLocked(token, key, error);
}

bool GatewayStore::createKey(const std::string& name, const std::string& project, const std::string& user,
                             int rpmLimit, int tpmLimit, std::string& plaintextKey, std::string& keyId,
                             std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    if (name.empty()) {
        error = "nome da chave obrigatorio";
        return false;
    }
    if (rpmLimit < 0 || tpmLimit < 0) {
        error = "limites de rate nao podem ser negativos";
        return false;
    }

    plaintextKey = "av_" + randomHex(32);
    keyId = "key_" + randomHex(8);
    const std::string hash = project::RunnerSecurity::sha256(plaintextKey);

    Statement stmt;
    if (!stmt.prepare(db_,
        "INSERT INTO virtual_keys (id, key_hash, name, project, user, enabled, created_at, rpm_limit, tpm_limit) "
        "VALUES (?, ?, ?, ?, ?, 1, ?, ?, ?);", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, keyId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 3, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 4, project.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 5, user.c_str(), -1, SQLITE_TRANSIENT);
    const std::string now = nowIso();
    sqlite3_bind_text(stmt.stmt, 6, now.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.stmt, 7, rpmLimit);
    sqlite3_bind_int(stmt.stmt, 8, tpmLimit);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "insert key");
        return false;
    }
    return true;
}

bool GatewayStore::revokeKey(const std::string& keyId, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    Statement stmt;
    if (!stmt.prepare(db_, "UPDATE virtual_keys SET enabled = 0 WHERE id = ?;", error)) return false;
    sqlite3_bind_text(stmt.stmt, 1, keyId.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "revoke key");
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool GatewayStore::listKeys(Json& out, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    Statement stmt;
    if (!stmt.prepare(db_,
        "SELECT id, name, project, user, enabled, created_at, last_used_at, rpm_limit, tpm_limit "
        "FROM virtual_keys ORDER BY created_at;", error)) {
        return false;
    }
    out = Json::array();
    int step = sqlite3_step(stmt.stmt);
    while (step == SQLITE_ROW) {
        Json key = Json::object();
        key["id"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
        key["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
        key["project"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
        key["user"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3));
        key["enabled"] = sqlite3_column_int(stmt.stmt, 4) != 0;
        key["createdAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 5));
        key["lastUsedAt"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 6));
        key["rpmLimit"] = sqlite3_column_int(stmt.stmt, 7);
        key["tpmLimit"] = sqlite3_column_int(stmt.stmt, 8);
        out.push_back(key);
        step = sqlite3_step(stmt.stmt);
    }
    return true;
}

bool GatewayStore::setBudget(const std::string& scopeType, const std::string& scopeId,
                             const std::string& period, double limitUsd, long long limitTokens,
                             bool hardStop, double alertAt, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    if (!validScope(scopeType)) {
        error = "scope_type invalido";
        return false;
    }
    if (!validPeriod(period)) {
        error = "period invalido";
        return false;
    }
    if (scopeId.empty()) {
        error = "scope_id obrigatorio";
        return false;
    }
    if (limitUsd < 0 && limitTokens < 0) {
        error = "defina limite em USD e/ou tokens";
        return false;
    }
    if (alertAt <= 0 || alertAt > 1) alertAt = 0.8;

    Statement stmt;
    if (!stmt.prepare(db_,
        "INSERT INTO budgets (scope_type, scope_id, period, window_start, limit_usd, limit_tokens, hard_stop, alert_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(scope_type, scope_id, period) DO UPDATE SET "
        "limit_usd = excluded.limit_usd, limit_tokens = excluded.limit_tokens, "
        "hard_stop = excluded.hard_stop, alert_at = excluded.alert_at;", error)) {
        return false;
    }
    const std::string window = periodWindow(period);
    sqlite3_bind_text(stmt.stmt, 1, scopeType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 2, scopeId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 3, period.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 4, window.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.stmt, 5, limitUsd >= 0 ? limitUsd : -1.0);
    sqlite3_bind_int64(stmt.stmt, 6, limitTokens >= 0 ? limitTokens : -1);
    sqlite3_bind_int(stmt.stmt, 7, hardStop ? 1 : 0);
    sqlite3_bind_double(stmt.stmt, 8, alertAt);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "set budget");
        return false;
    }
    return true;
}

Json GatewayStore::budgetJson(const BudgetRow& row) const {
    Json out = Json::object();
    out["scopeType"] = row.scopeType;
    out["scopeId"] = row.scopeId;
    out["period"] = row.period;
    out["windowStart"] = row.windowStart;
    out["limitUsd"] = row.limitUsd >= 0 ? Json(row.limitUsd) : Json();
    out["limitTokens"] = row.limitTokens >= 0 ? Json(row.limitTokens) : Json();
    out["spentUsd"] = row.spentUsd;
    out["spentTokens"] = row.spentTokens;
    out["reservedUsd"] = row.reservedUsd;
    out["reservedTokens"] = row.reservedTokens;
    out["hardStop"] = row.hardStop;
    out["alertAt"] = row.alertAt;
    return out;
}

bool GatewayStore::loadBudgetsLocked(const RequestContext& context, std::vector<BudgetRow>& rows,
                                     std::string& error) {
    rows.clear();
    std::vector<std::pair<std::string, std::string>> scopes;
    scopes.emplace_back("key", context.keyId);
    if (!context.project.empty()) scopes.emplace_back("project", context.project);
    if (!context.user.empty()) scopes.emplace_back("user", context.user);
    if (!context.provider.empty()) scopes.emplace_back("provider", context.provider);
    if (!context.model.empty()) scopes.emplace_back("model", context.model);

    for (const auto& [scopeType, scopeId] : scopes) {
        if (scopeId.empty()) continue;
        for (const char* period : {"daily", "monthly", "total"}) {
            Statement stmt;
            if (!stmt.prepare(db_,
                "SELECT id, scope_type, scope_id, period, window_start, limit_usd, limit_tokens, "
                "spent_usd, spent_tokens, reserved_usd, reserved_tokens, hard_stop, alert_at "
                "FROM budgets WHERE scope_type = ? AND scope_id = ? AND period = ? LIMIT 1;", error)) {
                return false;
            }
            sqlite3_bind_text(stmt.stmt, 1, scopeType.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.stmt, 2, scopeId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt.stmt, 3, period, -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt.stmt) != SQLITE_ROW) continue;

            BudgetRow row;
            row.id = sqlite3_column_int64(stmt.stmt, 0);
            row.scopeType = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
            row.scopeId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
            row.period = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3));
            row.windowStart = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 4));
            row.limitUsd = sqlite3_column_type(stmt.stmt, 5) == SQLITE_NULL ? -1.0 : sqlite3_column_double(stmt.stmt, 5);
            row.limitTokens = sqlite3_column_type(stmt.stmt, 6) == SQLITE_NULL ? -1 : sqlite3_column_int64(stmt.stmt, 6);
            row.spentUsd = sqlite3_column_double(stmt.stmt, 7);
            row.spentTokens = sqlite3_column_int64(stmt.stmt, 8);
            row.reservedUsd = sqlite3_column_double(stmt.stmt, 9);
            row.reservedTokens = sqlite3_column_int64(stmt.stmt, 10);
            row.hardStop = sqlite3_column_int(stmt.stmt, 11) != 0;
            row.alertAt = sqlite3_column_double(stmt.stmt, 12);
            rows.push_back(std::move(row));
        }
    }
    return true;
}

bool GatewayStore::resumeBudgetWindowLocked(BudgetRow& row, std::string& error) {
    const std::string current = periodWindow(row.period);
    if (row.windowStart == current) return true;

    row.windowStart = current;
    row.spentUsd = 0.0;
    row.spentTokens = 0;
    row.reservedUsd = 0.0;
    row.reservedTokens = 0;
    Statement stmt;
    if (!stmt.prepare(db_,
        "UPDATE budgets SET window_start = ?, spent_usd = 0, spent_tokens = 0, "
        "reserved_usd = 0, reserved_tokens = 0 WHERE id = ?;", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, current.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.stmt, 2, row.id);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "reset budget window");
        return false;
    }
    return true;
}

bool GatewayStore::updateBudgetLocked(const BudgetRow& row, std::string& error) {
    Statement stmt;
    if (!stmt.prepare(db_,
        "UPDATE budgets SET window_start = ?, spent_usd = ?, spent_tokens = ?, "
        "reserved_usd = ?, reserved_tokens = ? WHERE id = ?;", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, row.windowStart.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt.stmt, 2, row.spentUsd);
    sqlite3_bind_int64(stmt.stmt, 3, row.spentTokens);
    sqlite3_bind_double(stmt.stmt, 4, row.reservedUsd);
    sqlite3_bind_int64(stmt.stmt, 5, row.reservedTokens);
    sqlite3_bind_int64(stmt.stmt, 6, row.id);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "update budget");
        return false;
    }
    return true;
}

bool GatewayStore::checkAndReserve(const RequestContext& context, double estimatedUsd,
                                   long long estimatedTokens, BudgetDecision& decision, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    decision = BudgetDecision{};
    decision.estimatedUsd = estimatedUsd;
    decision.estimatedTokens = estimatedTokens;

    if (!begin(error)) return false;

    std::vector<BudgetRow> rows;
    if (!loadBudgetsLocked(context, rows, error)) {
        rollback();
        return false;
    }

    for (auto& row : rows) {
        if (!resumeBudgetWindowLocked(row, error)) {
            rollback();
            return false;
        }

        const double projectedUsd = row.spentUsd + row.reservedUsd + estimatedUsd;
        const long long projectedTokens = row.spentTokens + row.reservedTokens + estimatedTokens;
        const bool overUsd = row.limitUsd >= 0 && projectedUsd > row.limitUsd + 1e-9;
        const bool overTokens = row.limitTokens >= 0 && projectedTokens > row.limitTokens;

        if (overUsd || overTokens) {
            if (row.hardStop) {
                rollback();
                decision.allowed = false;
                decision.reason = overUsd ? "budget_usd_exceeded" : "budget_tokens_exceeded";
                for (const auto& budget : rows) decision.budgets.push_back(budgetJson(budget));
                return true;
            }
            decision.warning = true;
        }

        if (row.limitUsd >= 0 && row.alertAt > 0 && projectedUsd >= row.limitUsd * row.alertAt) {
            decision.warning = true;
        }
        if (row.limitTokens >= 0 && row.alertAt > 0 &&
            static_cast<double>(projectedTokens) >= static_cast<double>(row.limitTokens) * row.alertAt) {
            decision.warning = true;
        }
    }

    for (auto& row : rows) {
        row.reservedUsd += estimatedUsd;
        row.reservedTokens += estimatedTokens;
        if (!updateBudgetLocked(row, error)) {
            rollback();
            return false;
        }
    }

    if (!commit(error)) {
        rollback();
        return false;
    }

    for (const auto& row : rows) decision.budgets.push_back(budgetJson(row));
    return true;
}

bool GatewayStore::reconcile(const RequestContext& context, const TokenUsage& usage, double actualUsd,
                             double estimatedUsd, long long estimatedTokens, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    if (!begin(error)) return false;

    std::vector<BudgetRow> rows;
    if (!loadBudgetsLocked(context, rows, error)) {
        rollback();
        return false;
    }

    for (auto& row : rows) {
        if (!resumeBudgetWindowLocked(row, error)) {
            rollback();
            return false;
        }
        row.reservedUsd = std::max(0.0, row.reservedUsd - estimatedUsd);
        row.reservedTokens = std::max<long long>(0, row.reservedTokens - estimatedTokens);
        row.spentUsd += actualUsd;
        row.spentTokens += usage.total();
        if (!updateBudgetLocked(row, error)) {
            rollback();
            return false;
        }
    }

    if (!commit(error)) {
        rollback();
        return false;
    }
    return true;
}

bool GatewayStore::insertUsageLocked(const UsageEvent& event, std::string& error) {
    Statement stmt;
    if (!stmt.prepare(db_,
        "INSERT INTO usage_events (id, ts, key_id, project, user, provider, model, input_tokens, "
        "output_tokens, cached_tokens, cost_usd, latency_ms, status, error_code, runner) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, event.context.requestId.c_str(), -1, SQLITE_TRANSIENT);
    const std::string ts = nowIso();
    sqlite3_bind_text(stmt.stmt, 2, ts.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 3, event.context.keyId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 4, event.context.project.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 5, event.context.user.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 6, event.context.provider.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 7, event.context.model.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt.stmt, 8, event.usage.input);
    sqlite3_bind_int64(stmt.stmt, 9, event.usage.output);
    sqlite3_bind_int64(stmt.stmt, 10, event.usage.cached);
    sqlite3_bind_double(stmt.stmt, 11, event.costUsd);
    sqlite3_bind_int64(stmt.stmt, 12, event.latencyMs);
    sqlite3_bind_text(stmt.stmt, 13, event.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 14, event.errorCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 15, event.context.runner.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.stmt) != SQLITE_DONE) {
        error = sqliteMessage(db_, "insert usage");
        return false;
    }
    return true;
}

bool GatewayStore::recordUsage(const UsageEvent& event, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    return insertUsageLocked(event, error);
}

Json GatewayStore::usageTotalsLocked(const std::string& keyId, std::string& error) {
    Statement stmt;
    const char* sql = keyId.empty()
        ? "SELECT COUNT(*), COALESCE(SUM(cost_usd),0), COALESCE(SUM(input_tokens),0), "
          "COALESCE(SUM(output_tokens),0) FROM usage_events;"
        : "SELECT COUNT(*), COALESCE(SUM(cost_usd),0), COALESCE(SUM(input_tokens),0), "
          "COALESCE(SUM(output_tokens),0) FROM usage_events WHERE key_id = ?;";
    if (!stmt.prepare(db_, sql, error)) return Json::object();
    if (!keyId.empty()) sqlite3_bind_text(stmt.stmt, 1, keyId.c_str(), -1, SQLITE_TRANSIENT);
    Json out = Json::object();
    if (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
        out["requests"] = sqlite3_column_int64(stmt.stmt, 0);
        out["costUsd"] = sqlite3_column_double(stmt.stmt, 1);
        out["inputTokens"] = sqlite3_column_int64(stmt.stmt, 2);
        out["outputTokens"] = sqlite3_column_int64(stmt.stmt, 3);
    }
    return out;
}

bool GatewayStore::usageReport(const std::string& since, const std::string& keyId,
                               const std::string& provider, Json& out, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;

    const std::string from = since.empty() ? "1970-01-01T00:00:00Z" : since;
    std::string where = " WHERE ts >= ?";
    if (!keyId.empty()) where += " AND key_id = ?";
    if (!provider.empty()) where += " AND provider = ?";

    auto bindFilters = [&](Statement& stmt) {
        int index = 1;
        sqlite3_bind_text(stmt.stmt, index++, from.c_str(), -1, SQLITE_TRANSIENT);
        if (!keyId.empty()) sqlite3_bind_text(stmt.stmt, index++, keyId.c_str(), -1, SQLITE_TRANSIENT);
        if (!provider.empty()) sqlite3_bind_text(stmt.stmt, index++, provider.c_str(), -1, SQLITE_TRANSIENT);
    };

    out = Json::object();
    out["since"] = from;

    {
        Statement stmt;
        const std::string sql =
            "SELECT COUNT(*), COALESCE(SUM(cost_usd),0), COALESCE(SUM(input_tokens),0), "
            "COALESCE(SUM(output_tokens),0) FROM usage_events" + where + ";";
        if (!stmt.prepare(db_, sql.c_str(), error)) return false;
        bindFilters(stmt);
        if (sqlite3_step(stmt.stmt) == SQLITE_ROW) {
            Json totals = Json::object();
            totals["requests"] = sqlite3_column_int64(stmt.stmt, 0);
            totals["costUsd"] = sqlite3_column_double(stmt.stmt, 1);
            totals["inputTokens"] = sqlite3_column_int64(stmt.stmt, 2);
            totals["outputTokens"] = sqlite3_column_int64(stmt.stmt, 3);
            out["totals"] = totals;
        }
    }

    {
        Statement stmt;
        const std::string sql =
            "SELECT provider, COUNT(*), COALESCE(SUM(cost_usd),0), "
            "COALESCE(SUM(input_tokens + output_tokens),0) FROM usage_events" + where +
            " GROUP BY provider ORDER BY 3 DESC;";
        if (!stmt.prepare(db_, sql.c_str(), error)) return false;
        bindFilters(stmt);
        Json rows = Json::array();
        int step = sqlite3_step(stmt.stmt);
        while (step == SQLITE_ROW) {
            Json item = Json::object();
            item["provider"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
            item["requests"] = sqlite3_column_int64(stmt.stmt, 1);
            item["costUsd"] = sqlite3_column_double(stmt.stmt, 2);
            item["tokens"] = sqlite3_column_int64(stmt.stmt, 3);
            rows.push_back(item);
            step = sqlite3_step(stmt.stmt);
        }
        out["byProvider"] = rows;
    }

    {
        Statement stmt;
        const std::string sql =
            "SELECT key_id, COUNT(*), COALESCE(SUM(cost_usd),0), "
            "COALESCE(SUM(input_tokens + output_tokens),0) FROM usage_events" + where +
            " GROUP BY key_id ORDER BY 3 DESC;";
        if (!stmt.prepare(db_, sql.c_str(), error)) return false;
        bindFilters(stmt);
        Json rows = Json::array();
        int step = sqlite3_step(stmt.stmt);
        while (step == SQLITE_ROW) {
            Json item = Json::object();
            item["keyId"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 0));
            item["requests"] = sqlite3_column_int64(stmt.stmt, 1);
            item["costUsd"] = sqlite3_column_double(stmt.stmt, 2);
            item["tokens"] = sqlite3_column_int64(stmt.stmt, 3);
            rows.push_back(item);
            step = sqlite3_step(stmt.stmt);
        }
        out["byKey"] = rows;
    }

    return true;
}

bool GatewayStore::usageForToken(const std::string& token, Json& out, std::string& error) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ensureOpen(error)) return false;
    VirtualKey key;
    if (!loadKeyByTokenLocked(token, key, error)) return false;

    out = Json::object();
    Json keyInfo = Json::object();
    keyInfo["id"] = key.id;
    keyInfo["name"] = key.name;
    keyInfo["project"] = key.project;
    keyInfo["user"] = key.user;
    keyInfo["rpmLimit"] = key.rpmLimit;
    keyInfo["tpmLimit"] = key.tpmLimit;
    out["key"] = keyInfo;
    out["usage"] = usageTotalsLocked(key.id, error);

    Statement stmt;
    if (!stmt.prepare(db_,
        "SELECT id, scope_type, scope_id, period, window_start, limit_usd, limit_tokens, "
        "spent_usd, spent_tokens, reserved_usd, reserved_tokens, hard_stop, alert_at "
        "FROM budgets WHERE (scope_type = 'key' AND scope_id = ?) "
        "OR (scope_type = 'project' AND scope_id = ?) "
        "OR (scope_type = 'user' AND scope_id = ?) ORDER BY scope_type, period;", error)) {
        return false;
    }
    sqlite3_bind_text(stmt.stmt, 1, key.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 2, key.project.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt.stmt, 3, key.user.c_str(), -1, SQLITE_TRANSIENT);
    Json budgets = Json::array();
    int step = sqlite3_step(stmt.stmt);
    while (step == SQLITE_ROW) {
        BudgetRow row;
        row.id = sqlite3_column_int64(stmt.stmt, 0);
        row.scopeType = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 1));
        row.scopeId = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 2));
        row.period = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 3));
        row.windowStart = reinterpret_cast<const char*>(sqlite3_column_text(stmt.stmt, 4));
        row.limitUsd = sqlite3_column_type(stmt.stmt, 5) == SQLITE_NULL ? -1.0 : sqlite3_column_double(stmt.stmt, 5);
        row.limitTokens = sqlite3_column_type(stmt.stmt, 6) == SQLITE_NULL ? -1 : sqlite3_column_int64(stmt.stmt, 6);
        row.spentUsd = sqlite3_column_double(stmt.stmt, 7);
        row.spentTokens = sqlite3_column_int64(stmt.stmt, 8);
        row.reservedUsd = sqlite3_column_double(stmt.stmt, 9);
        row.reservedTokens = sqlite3_column_int64(stmt.stmt, 10);
        row.hardStop = sqlite3_column_int(stmt.stmt, 11) != 0;
        row.alertAt = sqlite3_column_double(stmt.stmt, 12);
        budgets.push_back(budgetJson(row));
        step = sqlite3_step(stmt.stmt);
    }
    out["budgets"] = budgets;
    return true;
}

} // namespace gateway
} // namespace taskforge
