#include "MemoryStore.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <utility>

#include "sqlite3.h"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

struct Stmt {
    sqlite3_stmt* stmt = nullptr;
    ~Stmt() { if (stmt) sqlite3_finalize(stmt); }
};

bool prepare(sqlite3* db, const std::string& sql, Stmt& out, std::string& error) {
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &out.stmt, nullptr) != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

void bindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

void bindNullableText(sqlite3_stmt* stmt, int index, const std::string& value) {
    if (value.empty()) sqlite3_bind_null(stmt, index);
    else bindText(stmt, index, value);
}

std::string columnText(sqlite3_stmt* stmt, int index) {
    const unsigned char* p = sqlite3_column_text(stmt, index);
    return p ? reinterpret_cast<const char*>(p) : std::string();
}

long long columnInt(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_int64(stmt, index);
}

double columnDouble(sqlite3_stmt* stmt, int index) {
    return sqlite3_column_double(stmt, index);
}

std::string hashHex(const std::string& value) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : value) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(h));
    return std::string(out);
}

std::string isoNowImpl() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char out[32];
    std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(out);
}

taskforge::Json parseJsonOrNull(const std::string& text) {
    if (text.empty()) return taskforge::Json();
    auto parsed = taskforge::jsonutil::tryParse(text);
    return parsed ? *parsed : taskforge::Json();
}

} // namespace

MemoryStore::MemoryStore(std::string path) : path_(std::move(path)) {}

MemoryStore::~MemoryStore() {
    close();
}

bool MemoryStore::open(std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) return true;

    if (path_ != ":memory:") {
        std::error_code ec;
        std::filesystem::path parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }

    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path_.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        error = db_ ? sqlite3_errmsg(db_) : "sqlite3_open_v2 falhou";
        lastError_ = error;
        close();
        return false;
    }

    sqlite3_busy_timeout(db_, 5000);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);

    if (!exec("PRAGMA foreign_keys=ON;", &error) ||
        !exec(path_ == ":memory:" ? "PRAGMA synchronous=NORMAL;" : "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;", &error) ||
        !createSchema(error)) {
        lastError_ = error;
        close();
        return false;
    }
    return true;
}

void MemoryStore::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool MemoryStore::exec(const std::string& sql, std::string* error) {
    char* message = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        if (error) *error = message ? message : sqlite3_errmsg(db_);
        lastError_ = message ? message : sqlite3_errmsg(db_);
        if (message) sqlite3_free(message);
        return false;
    }
    if (message) sqlite3_free(message);
    return true;
}

bool MemoryStore::createSchema(std::string& error) {
    return exec(R"SQL(
CREATE TABLE IF NOT EXISTS memory_meta (
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_scope (
  id TEXT PRIMARY KEY,
  kind TEXT NOT NULL CHECK (kind IN ('project','workspace','institution')),
  parent_id TEXT REFERENCES memory_scope(id),
  name TEXT NOT NULL,
  created_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS memory_scope_closure (
  ancestor_id TEXT NOT NULL REFERENCES memory_scope(id),
  descendant_id TEXT NOT NULL REFERENCES memory_scope(id),
  depth INTEGER NOT NULL,
  PRIMARY KEY (ancestor_id, descendant_id)
);

CREATE TABLE IF NOT EXISTS memory_node (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  kind TEXT NOT NULL,
  canonical_name TEXT NOT NULL,
  aliases_json TEXT NOT NULL DEFAULT '[]',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS memory_edge (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  src_id TEXT NOT NULL REFERENCES memory_node(id),
  dst_id TEXT REFERENCES memory_node(id),
  relation TEXT NOT NULL,
  value_json TEXT,
  valid_from TEXT,
  valid_to TEXT,
  recorded_at TEXT NOT NULL,
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL CHECK (status IN ('candidate','active','rejected','superseded')),
  superseded_by TEXT REFERENCES memory_edge(id),
  source_kind TEXT NOT NULL,
  source_ref TEXT,
  evidence_hash TEXT,
  edge_hash TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS memory_episode (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT NOT NULL,
  node_id TEXT,
  item_id TEXT,
  kind TEXT NOT NULL,
  content TEXT NOT NULL,
  sanitized INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}'
);

CREATE TABLE IF NOT EXISTS memory_evidence (
  id TEXT PRIMARY KEY,
  edge_id TEXT NOT NULL REFERENCES memory_edge(id),
  episode_id TEXT REFERENCES memory_episode(id),
  source_kind TEXT NOT NULL,
  source_ref TEXT,
  excerpt_hash TEXT,
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_audit (
  id TEXT PRIMARY KEY,
  actor TEXT NOT NULL,
  action TEXT NOT NULL,
  target TEXT NOT NULL,
  before TEXT,
  after TEXT,
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_working (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  item_id TEXT NOT NULL,
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, item_id, key)
);

CREATE TABLE IF NOT EXISTS memory_usage (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  provider TEXT,
  model TEXT,
  prompt_tokens INTEGER NOT NULL DEFAULT 0,
  completion_tokens INTEGER NOT NULL DEFAULT 0,
  total_tokens INTEGER NOT NULL DEFAULT 0,
  calls INTEGER NOT NULL DEFAULT 0,
  latency_ms INTEGER NOT NULL DEFAULT 0,
  memory_context_chars INTEGER NOT NULL DEFAULT 0,
  memory_enabled INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL
);

CREATE VIRTUAL TABLE IF NOT EXISTS memory_node_fts USING fts5(
  node_id UNINDEXED,
  name,
  aliases,
  metadata
);

CREATE INDEX IF NOT EXISTS idx_memory_edge_scope_status ON memory_edge(scope_id, status, valid_to);
CREATE INDEX IF NOT EXISTS idx_memory_edge_src_relation ON memory_edge(src_id, relation);
CREATE INDEX IF NOT EXISTS idx_memory_edge_dst_relation ON memory_edge(dst_id, relation);
CREATE INDEX IF NOT EXISTS idx_memory_edge_hash ON memory_edge(scope_id, edge_hash, status);
CREATE INDEX IF NOT EXISTS idx_memory_node_scope_kind ON memory_node(scope_id, kind);
CREATE INDEX IF NOT EXISTS idx_memory_episode_scope_time ON memory_episode(scope_id, created_at);
CREATE INDEX IF NOT EXISTS idx_memory_usage_scope_time ON memory_usage(scope_id, created_at);
)SQL", &error);
}

std::string MemoryStore::nowIso() const {
    return isoNowImpl();
}

std::string MemoryStore::newId(const std::string& prefix) const {
    static std::atomic<uint64_t> seq{0};
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + "_" + std::to_string(ms) + "_" + std::to_string(seq.fetch_add(1));
}

std::string MemoryStore::edgeHash(const std::string& scopeId, const std::string& srcId,
                                  const std::string& dstId, const std::string& relation,
                                  const std::string& valueJson, const std::string& validFrom,
                                  const std::string& validTo) const {
    return hashHex(scopeId + "|" + srcId + "|" + dstId + "|" + relation + "|" + valueJson + "|" +
                   validFrom + "|" + validTo);
}

bool MemoryStore::ensureScope(const std::string& scopeId, const std::string& kind,
                              const std::string& parentId, const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope(id, kind, parent_id, name, created_at) VALUES(?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, kind);
    bindNullableText(st.stmt, 3, parentId);
    bindText(st.stmt, 4, name);
    bindText(st.stmt, 5, nowIso());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }

    if (!parentId.empty()) {
        Stmt closure;
        if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id, descendant_id, depth) "
                           "SELECT ancestor_id, ?, depth + 1 FROM memory_scope_closure WHERE descendant_id = ?", closure, error)) {
            lastError_ = error;
            return false;
        }
        bindText(closure.stmt, 1, scopeId);
        bindText(closure.stmt, 2, parentId);
        if (sqlite3_step(closure.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
    }

    Stmt self;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id, descendant_id, depth) VALUES(?,?,0)", self, error)) {
        lastError_ = error;
        return false;
    }
    bindText(self.stmt, 1, scopeId);
    bindText(self.stmt, 2, scopeId);
    if (sqlite3_step(self.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::vector<std::string> MemoryStore::readScopes(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT ancestor_id FROM memory_scope_closure WHERE descendant_id=? ORDER BY depth ASC", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    return out;
}

bool MemoryStore::setWorking(const std::string& scopeId, const std::string& itemId,
                             const std::string& key, const taskforge::Json& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_working(scope_id,item_id,key,value_json,updated_at) VALUES(?,?,?,?,?) "
                       "ON CONFLICT(scope_id,item_id,key) DO UPDATE SET value_json=excluded.value_json, updated_at=excluded.updated_at", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemId);
    bindText(st.stmt, 3, key);
    bindText(st.stmt, 4, value.dump());
    bindText(st.stmt, 5, nowIso());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::getWorking(const std::string& scopeId, const std::string& itemId,
                                        const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return taskforge::Json();
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT value_json FROM memory_working WHERE scope_id=? AND item_id=? AND key=?", st, error)) {
        lastError_ = error;
        return taskforge::Json();
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemId);
    bindText(st.stmt, 3, key);
    if (sqlite3_step(st.stmt) == SQLITE_ROW) return parseJsonOrNull(columnText(st.stmt, 0));
    return taskforge::Json();
}

bool MemoryStore::hasWorking(const std::string& scopeId, const std::string& itemId,
                             const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT 1 FROM memory_working WHERE scope_id=? AND item_id=? AND key=? LIMIT 1", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemId);
    bindText(st.stmt, 3, key);
    return sqlite3_step(st.stmt) == SQLITE_ROW;
}

taskforge::Json MemoryStore::workingSnapshot(const std::string& scopeId, const std::string& itemId,
                                             const std::vector<std::string>& allowedKeys) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;

    std::string sql = "SELECT key, value_json FROM memory_working WHERE scope_id=? AND item_id=?";
    if (!allowedKeys.empty()) {
        sql += " AND key IN (";
        for (size_t i = 0; i < allowedKeys.size(); ++i) sql += (i ? ",?" : "?");
        sql += ")";
    }
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemId);
    for (size_t i = 0; i < allowedKeys.size(); ++i) bindText(st.stmt, static_cast<int>(i + 3), allowedKeys[i]);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        out[columnText(st.stmt, 0)] = parseJsonOrNull(columnText(st.stmt, 1));
    }
    return out;
}

taskforge::Json MemoryStore::workingToJson(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT item_id,key,value_json FROM memory_working WHERE scope_id=? ORDER BY item_id,key", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        out[columnText(st.stmt, 0)][columnText(st.stmt, 1)] = parseJsonOrNull(columnText(st.stmt, 2));
    }
    return out;
}

bool MemoryStore::ingestWorkingDelta(const std::string& scopeId, const std::string& itemId,
                                     const taskforge::Json& delta) {
    if (!delta.is_object()) return false;
    for (auto it = delta.begin(); it != delta.end(); ++it) {
        if (!setWorking(scopeId, itemId, it.key(), it.value())) return false;
    }
    return true;
}

void MemoryStore::clearWorkingItem(const std::string& scopeId, const std::string& itemId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return;
    Stmt st;
    std::string error;
    if (!prepare(db_, "DELETE FROM memory_working WHERE scope_id=? AND item_id=?", st, error)) return;
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemId);
    sqlite3_step(st.stmt);
}

void MemoryStore::clearScopeData(const std::string& scopeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return;
    Stmt fts;
    std::string error;
    if (prepare(db_, "DELETE FROM memory_node_fts WHERE node_id IN (SELECT id FROM memory_node WHERE scope_id=?)", fts, error)) {
        bindText(fts.stmt, 1, scopeId);
        sqlite3_step(fts.stmt);
    }
    const char* sql[] = {
        "DELETE FROM memory_working WHERE scope_id=?",
        "DELETE FROM memory_edge WHERE scope_id=?",
        "DELETE FROM memory_episode WHERE scope_id=?",
        "DELETE FROM memory_node WHERE scope_id=?",
        "DELETE FROM memory_usage WHERE scope_id=?",
    };
    for (const char* statement : sql) {
        Stmt st;
        if (!prepare(db_, statement, st, error)) continue;
        bindText(st.stmt, 1, scopeId);
        sqlite3_step(st.stmt);
    }
}

std::string MemoryStore::ensureNode(const std::string& scopeId, const std::string& nodeId,
                                    const std::string& kind, const std::string& canonicalName,
                                    const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,created_at,updated_at,metadata) "
                       "VALUES(?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, nodeId);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, kind);
    bindText(st.stmt, 4, canonicalName);
    bindText(st.stmt, 5, "[]");
    bindText(st.stmt, 6, now);
    bindText(st.stmt, 7, now);
    bindText(st.stmt, 8, metadata.dump());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }

    Stmt update;
    if (prepare(db_, "UPDATE memory_node SET canonical_name=?, metadata=?, updated_at=? WHERE id=?", update, error)) {
        bindText(update.stmt, 1, canonicalName);
        bindText(update.stmt, 2, metadata.dump());
        bindText(update.stmt, 3, now);
        bindText(update.stmt, 4, nodeId);
        sqlite3_step(update.stmt);
    }

    Stmt delFts;
    if (prepare(db_, "DELETE FROM memory_node_fts WHERE node_id=?", delFts, error)) {
        bindText(delFts.stmt, 1, nodeId);
        sqlite3_step(delFts.stmt);
    }
    Stmt fts;
    if (prepare(db_, "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)", fts, error)) {
        bindText(fts.stmt, 1, nodeId);
        bindText(fts.stmt, 2, canonicalName);
        bindText(fts.stmt, 3, "");
        bindText(fts.stmt, 4, metadata.dump());
        sqlite3_step(fts.stmt);
    }
    return nodeId;
}

std::string MemoryStore::recordEdge(const std::string& scopeId, const std::string& srcId,
                                    const std::string& dstId, const std::string& relation,
                                    const taskforge::Json& value, const std::string& validFrom,
                                    const std::string& validTo, double confidence,
                                    const std::string& status, const std::string& sourceKind,
                                    const std::string& sourceRef, const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    const std::string valueJson = value.is_null() ? "" : value.dump();
    const std::string from = validFrom.empty() ? nowIso() : validFrom;
    const std::string hash = edgeHash(scopeId, srcId, dstId, relation, valueJson, from, validTo);

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND edge_hash=? AND status='active' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, hash);
        if (sqlite3_step(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0);
    }

    const std::string id = newId("edge");
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,valid_from,valid_to,recorded_at,confidence,status,source_kind,source_ref,edge_hash,metadata) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, srcId);
    bindNullableText(st.stmt, 4, dstId);
    bindText(st.stmt, 5, relation);
    bindNullableText(st.stmt, 6, valueJson);
    bindNullableText(st.stmt, 7, from);
    bindNullableText(st.stmt, 8, validTo);
    bindText(st.stmt, 9, nowIso());
    sqlite3_bind_double(st.stmt, 10, confidence);
    bindText(st.stmt, 11, status);
    bindText(st.stmt, 12, sourceKind);
    bindNullableText(st.stmt, 13, sourceRef);
    bindText(st.stmt, 14, hash);
    bindText(st.stmt, 15, metadata.dump());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    return id;
}

std::string MemoryStore::recordLesson(const std::string& scopeId, const std::string& domain,
                                      const std::string& triggerError, const std::string& lessonRule,
                                      const std::string& sourceItem, const std::string& sourceKind) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || lessonRule.empty()) return "";

    const std::string dom = domain.empty() ? "geral" : domain;
    const std::string domainId = "node_" + hashHex(scopeId + "|domain|" + dom);
    const std::string ruleId = "node_" + hashHex(scopeId + "|rule|" + lessonRule);
    ensureNode(scopeId, domainId, "domain", dom);
    ensureNode(scopeId, ruleId, "rule", lessonRule);

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND dst_id=? "
                       "AND relation='HAS_LESSON' AND status='active' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, domainId);
        bindText(existing.stmt, 3, ruleId);
        if (sqlite3_step(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0);
    }

    taskforge::Json metadata = taskforge::Json::object();
    metadata["domain"] = dom;
    metadata["triggerError"] = triggerError;
    metadata["lessonRule"] = lessonRule;
    metadata["sourceItem"] = sourceItem;
    return recordEdge(scopeId, domainId, ruleId, "HAS_LESSON", taskforge::Json(), "", "",
                      0.9, "active", sourceKind, sourceItem, metadata);
}

std::vector<MemoryLesson> MemoryStore::getLessons(const std::string& scopeId,
                                                  const std::string& domain) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryLesson> out;
    if (!db_) return out;
    std::string sql =
        "SELECT e.id, e.scope_id, d.canonical_name, r.canonical_name, e.recorded_at, e.metadata, e.source_ref "
        "FROM memory_edge e "
        "JOIN memory_node d ON d.id=e.src_id "
        "JOIN memory_node r ON r.id=e.dst_id "
        "WHERE e.scope_id=? AND e.relation='HAS_LESSON' AND e.status='active'";
    if (!domain.empty()) sql += " AND d.canonical_name=?";
    sql += " ORDER BY e.recorded_at, e.id";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (!domain.empty()) bindText(st.stmt, 2, domain);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        MemoryLesson lesson;
        lesson.id = columnText(st.stmt, 0);
        lesson.projectId = columnText(st.stmt, 1);
        lesson.domain = columnText(st.stmt, 2);
        lesson.lessonRule = columnText(st.stmt, 3);
        lesson.createdAt = columnText(st.stmt, 4);
        taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 5));
        lesson.triggerError = metadata.value("triggerError", std::string());
        lesson.sourceItem = metadata.value("sourceItem", columnText(st.stmt, 6));
        out.push_back(std::move(lesson));
    }
    return out;
}

std::string MemoryStore::formatLessons(const std::string& scopeId,
                                       const std::vector<std::string>& domains) const {
    std::vector<MemoryLesson> lessons = getLessons(scopeId);
    if (!domains.empty()) {
        std::vector<MemoryLesson> filtered;
        for (auto& lesson : lessons) {
            if (std::find(domains.begin(), domains.end(), lesson.domain) != domains.end()) {
                filtered.push_back(lesson);
            }
        }
        lessons = std::move(filtered);
    }
    if (lessons.empty()) return "";
    std::ostringstream out;
    out << "\n--- [ARESTRIX MEMORIA SEMANTICA: LICOES APRENDIDAS - PROJETO: " << scopeId << "] ---\n";
    out << "Atencao: os itens anteriores identificaram as seguintes diretrizes para prevencao de erros:\n";
    for (size_t i = 0; i < lessons.size(); ++i) {
        out << "[" << (i + 1) << "] (Dominio: " << lessons[i].domain << ") " << lessons[i].lessonRule << "\n";
    }
    out << "--------------------------------------------------------------------\n\n";
    return out.str();
}

void MemoryStore::clearLessons(const std::string& scopeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return;
    Stmt st;
    std::string error;
    if (!prepare(db_, "DELETE FROM memory_edge WHERE scope_id=? AND relation='HAS_LESSON'", st, error)) return;
    bindText(st.stmt, 1, scopeId);
    sqlite3_step(st.stmt);
    audit("runtime", "clear_lessons", scopeId, "", "");
}

std::string MemoryStore::recordEpisode(const std::string& scopeId, const std::string& runId,
                                       const std::string& nodeId, const std::string& itemId,
                                       const std::string& kind, const std::string& content,
                                       const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    const std::string id = newId("ep");
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_episode(id,scope_id,run_id,node_id,item_id,kind,content,sanitized,created_at,metadata) "
                       "VALUES(?,?,?,?,?,?,?,1,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, runId);
    bindNullableText(st.stmt, 4, nodeId);
    bindNullableText(st.stmt, 5, itemId);
    bindText(st.stmt, 6, kind);
    bindText(st.stmt, 7, content);
    bindText(st.stmt, 8, nowIso());
    bindText(st.stmt, 9, metadata.dump());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    return id;
}

bool MemoryStore::invalidateEdge(const std::string& edgeId, const std::string& validTo,
                                 const std::string& reason, const std::string& actor) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_edge SET status='superseded', valid_to=?, metadata=json_set(COALESCE(metadata,'{}'), '$.invalidationReason', ?) WHERE id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, validTo.empty() ? nowIso() : validTo);
    bindText(st.stmt, 2, reason);
    bindText(st.stmt, 3, edgeId);
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    audit(actor, "invalidate_edge", edgeId, "", reason);
    return true;
}

std::vector<MemoryEdgeRecord> MemoryStore::neighbors(const std::string& scopeId,
                                                      const std::string& nodeId, int depth,
                                                      const std::string& asOf) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEdgeRecord> out;
    if (!db_ || nodeId.empty()) return out;
    const std::string t = asOf.empty() ? "9999-12-31T23:59:59Z" : asOf;
    const int maxDepth = std::max(0, depth);
    const char* sql =
        "WITH RECURSIVE walk(node_id, depth) AS ("
        "  SELECT ?, 0 "
        "  UNION "
        "  SELECT e.dst_id, w.depth+1 FROM memory_edge e JOIN walk w ON e.src_id=w.node_id "
        "  WHERE w.depth < ? AND e.scope_id=? AND e.status='active' "
        "    AND (e.valid_from IS NULL OR e.valid_from <= ?) "
        "    AND (e.valid_to IS NULL OR e.valid_to > ?) "
        ") "
        "SELECT e.id,e.scope_id,e.src_id,e.dst_id,e.relation,e.value_json,e.valid_from,e.valid_to,"
        "e.recorded_at,e.confidence,e.status,e.superseded_by,e.source_kind,e.source_ref,e.metadata "
        "FROM memory_edge e JOIN walk w ON e.src_id=w.node_id "
        "WHERE e.scope_id=? AND e.status='active' "
        "  AND (e.valid_from IS NULL OR e.valid_from <= ?) "
        "  AND (e.valid_to IS NULL OR e.valid_to > ?) "
        "ORDER BY w.depth, e.recorded_at LIMIT 500";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, nodeId);
    sqlite3_bind_int(st.stmt, 2, maxDepth);
    bindText(st.stmt, 3, scopeId);
    bindText(st.stmt, 4, t);
    bindText(st.stmt, 5, t);
    bindText(st.stmt, 6, scopeId);
    bindText(st.stmt, 7, t);
    bindText(st.stmt, 8, t);
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        MemoryEdgeRecord edge;
        edge.id = columnText(st.stmt, 0);
        edge.scopeId = columnText(st.stmt, 1);
        edge.srcId = columnText(st.stmt, 2);
        edge.dstId = columnText(st.stmt, 3);
        edge.relation = columnText(st.stmt, 4);
        edge.valueJson = columnText(st.stmt, 5);
        edge.validFrom = columnText(st.stmt, 6);
        edge.validTo = columnText(st.stmt, 7);
        edge.recordedAt = columnText(st.stmt, 8);
        edge.confidence = columnDouble(st.stmt, 9);
        edge.status = columnText(st.stmt, 10);
        edge.supersededBy = columnText(st.stmt, 11);
        edge.sourceKind = columnText(st.stmt, 12);
        edge.sourceRef = columnText(st.stmt, 13);
        edge.metadata = columnText(st.stmt, 14);
        out.push_back(std::move(edge));
    }
    return out;
}

std::vector<MemoryEdgeRecord> MemoryStore::searchEdges(const std::string& scopeId,
                                                        const std::string& text,
                                                        size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEdgeRecord> out;
    if (!db_ || text.empty()) return out;

    std::string phrase = text;
    std::string escaped;
    for (char c : phrase) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    const std::string match = "\"" + escaped + "\"";
    const char* sql =
        "SELECT e.id,e.scope_id,e.src_id,e.dst_id,e.relation,e.value_json,e.valid_from,e.valid_to,"
        "e.recorded_at,e.confidence,e.status,e.superseded_by,e.source_kind,e.source_ref,e.metadata "
        "FROM memory_edge e "
        "WHERE e.scope_id=? AND e.status='active' AND ("
        "  e.src_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
        "  e.dst_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) "
        ") ORDER BY e.recorded_at DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, match);
    bindText(st.stmt, 3, match);
    sqlite3_bind_int(st.stmt, 4, static_cast<int>(limit));
    while (sqlite3_step(st.stmt) == SQLITE_ROW) {
        MemoryEdgeRecord edge;
        edge.id = columnText(st.stmt, 0);
        edge.scopeId = columnText(st.stmt, 1);
        edge.srcId = columnText(st.stmt, 2);
        edge.dstId = columnText(st.stmt, 3);
        edge.relation = columnText(st.stmt, 4);
        edge.valueJson = columnText(st.stmt, 5);
        edge.validFrom = columnText(st.stmt, 6);
        edge.validTo = columnText(st.stmt, 7);
        edge.recordedAt = columnText(st.stmt, 8);
        edge.confidence = columnDouble(st.stmt, 9);
        edge.status = columnText(st.stmt, 10);
        edge.supersededBy = columnText(st.stmt, 11);
        edge.sourceKind = columnText(st.stmt, 12);
        edge.sourceRef = columnText(st.stmt, 13);
        edge.metadata = columnText(st.stmt, 14);
        out.push_back(std::move(edge));
    }
    return out;
}

std::string MemoryStore::searchContext(const std::string& scopeId, const std::string& text,
                                       size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || text.empty()) return "";
    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    const std::string match = "\"" + escaped + "\"";
    std::ostringstream out;
    bool any = false;

    for (const auto& lesson : getLessons(scopeId)) {
        std::string hay = lesson.domain + " " + lesson.lessonRule + " " + lesson.triggerError;
        std::string needle = text;
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (hay.find(needle) != std::string::npos) {
            out << "[LICAO] (" << lesson.domain << ") " << lesson.lessonRule << "\n";
            any = true;
        }
    }

    const char* sql =
        "SELECT s.canonical_name, e.relation, COALESCE(d.canonical_name, e.value_json, ''), "
        "e.valid_from, e.valid_to, e.confidence "
        "FROM memory_edge e "
        "JOIN memory_node s ON s.id=e.src_id "
        "LEFT JOIN memory_node d ON d.id=e.dst_id "
        "WHERE e.scope_id=? AND e.status='active' AND ("
        "  e.src_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
        "  e.dst_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
        "  e.value_json LIKE ?"
        ") ORDER BY e.recorded_at DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (prepare(db_, sql, st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, match);
        bindText(st.stmt, 3, match);
        bindText(st.stmt, 4, "%" + text + "%");
        sqlite3_bind_int(st.stmt, 5, static_cast<int>(limit));
        while (sqlite3_step(st.stmt) == SQLITE_ROW) {
            out << "[FATO] " << columnText(st.stmt, 0) << " --" << columnText(st.stmt, 1) << "--> "
                << columnText(st.stmt, 2);
            const std::string from = columnText(st.stmt, 3);
            const std::string to = columnText(st.stmt, 4);
            if (!from.empty() || !to.empty()) out << " [" << from << " .. " << (to.empty() ? "agora" : to) << "]";
            out << " (confianca " << columnDouble(st.stmt, 5) << ")\n";
            any = true;
        }
    } else {
        lastError_ = error;
    }

    return any ? out.str() : std::string();
}

bool MemoryStore::recordUsage(const MemoryUsageRecord& usage) {    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_usage(id,scope_id,run_id,node_id,provider,model,prompt_tokens,completion_tokens,total_tokens,calls,latency_ms,memory_context_chars,memory_enabled,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, usage.id.empty() ? newId("use") : usage.id);
    bindText(st.stmt, 2, usage.scopeId);
    bindNullableText(st.stmt, 3, usage.runId);
    bindNullableText(st.stmt, 4, usage.nodeId);
    bindText(st.stmt, 5, usage.provider);
    bindText(st.stmt, 6, usage.model);
    sqlite3_bind_int64(st.stmt, 7, usage.promptTokens);
    sqlite3_bind_int64(st.stmt, 8, usage.completionTokens);
    sqlite3_bind_int64(st.stmt, 9, usage.totalTokens);
    sqlite3_bind_int64(st.stmt, 10, usage.calls);
    sqlite3_bind_int64(st.stmt, 11, usage.latencyMs);
    sqlite3_bind_int64(st.stmt, 12, usage.memoryContextChars);
    sqlite3_bind_int64(st.stmt, 13, usage.memoryEnabled);
    bindText(st.stmt, 14, nowIso());
    if (sqlite3_step(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::usageSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
                       "COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), COALESCE(AVG(latency_ms),0), "
                       "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(memory_enabled),0) "
                       "FROM memory_usage WHERE scope_id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (sqlite3_step(st.stmt) == SQLITE_ROW) {
        out["runs"] = columnInt(st.stmt, 0);
        out["promptTokens"] = columnInt(st.stmt, 1);
        out["completionTokens"] = columnInt(st.stmt, 2);
        out["totalTokens"] = columnInt(st.stmt, 3);
        out["calls"] = columnInt(st.stmt, 4);
        out["avgLatencyMs"] = columnDouble(st.stmt, 5);
        out["memoryContextChars"] = columnInt(st.stmt, 6);
        out["memoryEnabledRuns"] = columnInt(st.stmt, 7);
    }
    return out;
}

taskforge::Json MemoryStore::toJson(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    out["scopeId"] = scopeId;
    out["working"] = workingToJson(scopeId);
    taskforge::Json lessons = taskforge::Json::array();
    for (const auto& lesson : getLessons(scopeId)) {
        taskforge::Json item = taskforge::Json::object();
        item["id"] = lesson.id;
        item["domain"] = lesson.domain;
        item["triggerError"] = lesson.triggerError;
        item["lessonRule"] = lesson.lessonRule;
        item["sourceItem"] = lesson.sourceItem;
        item["createdAt"] = lesson.createdAt;
        lessons.push_back(item);
    }
    out["lessons"] = lessons;
    out["usage"] = usageSummary(scopeId);
    return out;
}

bool MemoryStore::hasMeta(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT 1 FROM memory_meta WHERE key=? LIMIT 1", st, error)) return false;
    bindText(st.stmt, 1, key);
    return sqlite3_step(st.stmt) == SQLITE_ROW;
}

std::string MemoryStore::getMeta(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT value FROM memory_meta WHERE key=?", st, error)) return "";
    bindText(st.stmt, 1, key);
    return sqlite3_step(st.stmt) == SQLITE_ROW ? columnText(st.stmt, 0) : "";
}

bool MemoryStore::setMeta(const std::string& key, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, key);
    bindText(st.stmt, 2, value);
    return sqlite3_step(st.stmt) == SQLITE_DONE;
}

void MemoryStore::audit(const std::string& actor, const std::string& action,
                        const std::string& target, const std::string& before,
                        const std::string& after) {
    if (!db_) return;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_audit(id,actor,action,target,before,after,created_at) VALUES(?,?,?,?,?,?,?)", st, error)) return;
    bindText(st.stmt, 1, newId("aud"));
    bindText(st.stmt, 2, actor);
    bindText(st.stmt, 3, action);
    bindText(st.stmt, 4, target);
    bindNullableText(st.stmt, 5, before);
    bindNullableText(st.stmt, 6, after);
    bindText(st.stmt, 7, nowIso());
    sqlite3_step(st.stmt);
}

} // namespace memory
} // namespace taskforge
