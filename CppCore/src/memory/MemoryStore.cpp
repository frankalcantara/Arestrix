#include "MemoryStore.hpp"
#include "MemoryPredicates.hpp"
#include "MemorySanitizer.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>
#include <sstream>
#include <thread>
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

// sqlite3_busy_timeout cobre a espera normal; este retry cobre disputas curtas
// entre processos (Studio + pipeline filho + CLI) sem perder a escrita.
int stepRetry(sqlite3_stmt* stmt, int attempts = 3) {
    int rc = sqlite3_step(stmt);
    for (int attempt = 1; attempt < attempts && (rc == SQLITE_BUSY || rc == SQLITE_LOCKED); ++attempt) {
        sqlite3_reset(stmt);
        std::this_thread::sleep_for(std::chrono::milliseconds(10 * attempt * attempt));
        rc = sqlite3_step(stmt);
    }
    return rc;
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

// Normalizacao deterministica para aliases/dedup. Cobre os acentos mais comuns
// do portugues sem depender de locale/ICU.
std::string normalizeComparable(const std::string& value) {
    static const struct { const char* from; const char* to; } replacements[] = {
        {"\xC3\xA1", "a"}, {"\xC3\xA0", "a"}, {"\xC3\xA2", "a"}, {"\xC3\xA3", "a"}, {"\xC3\xA4", "a"},
        {"\xC3\x81", "a"}, {"\xC3\x80", "a"}, {"\xC3\x82", "a"}, {"\xC3\x83", "a"}, {"\xC3\x84", "a"},
        {"\xC3\xA9", "e"}, {"\xC3\xA8", "e"}, {"\xC3\xAA", "e"}, {"\xC3\xAB", "e"},
        {"\xC3\x89", "e"}, {"\xC3\x88", "e"}, {"\xC3\x8A", "e"}, {"\xC3\x8B", "e"},
        {"\xC3\xAD", "i"}, {"\xC3\xAC", "i"}, {"\xC3\xAE", "i"}, {"\xC3\xAF", "i"},
        {"\xC3\x8D", "i"}, {"\xC3\x8C", "i"}, {"\xC3\x8E", "i"}, {"\xC3\x8F", "i"},
        {"\xC3\xB3", "o"}, {"\xC3\xB2", "o"}, {"\xC3\xB4", "o"}, {"\xC3\xB5", "o"}, {"\xC3\xB6", "o"},
        {"\xC3\x93", "o"}, {"\xC3\x92", "o"}, {"\xC3\x94", "o"}, {"\xC3\x95", "o"}, {"\xC3\x96", "o"},
        {"\xC3\xBA", "u"}, {"\xC3\xB9", "u"}, {"\xC3\xBB", "u"}, {"\xC3\xBC", "u"},
        {"\xC3\x9A", "u"}, {"\xC3\x99", "u"}, {"\xC3\x9B", "u"}, {"\xC3\x9C", "u"},
        {"\xC3\xA7", "c"}, {"\xC3\x87", "c"},
    };
    std::string folded = value;
    for (const auto& replacement : replacements) {
        size_t pos = 0;
        while ((pos = folded.find(replacement.from, pos)) != std::string::npos) {
            folded.replace(pos, std::strlen(replacement.from), replacement.to);
            pos += std::strlen(replacement.to);
        }
    }
    std::string out;
    out.reserve(folded.size());
    bool space = true;
    for (unsigned char c : folded) {
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out.push_back(static_cast<char>(c));
            space = false;
        } else if (!space) {
            out.push_back(' ');
            space = true;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::vector<std::string> comparableTokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::istringstream in(normalizeComparable(value));
    std::string token;
    while (in >> token) tokens.push_back(token);
    return tokens;
}

double tokenJaccard(const std::string& a, const std::string& b) {
    auto left = comparableTokens(a);
    auto right = comparableTokens(b);
    if (left.empty() || right.empty()) return 0.0;
    std::set<std::string> aSet(left.begin(), left.end());
    std::set<std::string> bSet(right.begin(), right.end());
    size_t intersection = 0;
    for (const auto& token : aSet) {
        if (bSet.count(token)) ++intersection;
    }
    const size_t unionSize = aSet.size() + bSet.size() - intersection;
    return unionSize == 0 ? 0.0 : static_cast<double>(intersection) / static_cast<double>(unionSize);
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

void addColumnIfMissing(sqlite3* db, const char* table, const char* column, const char* ddl) {
    std::string sql = std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + ddl + ";";
    char* detail = nullptr;
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &detail);
    if (detail) sqlite3_free(detail);
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

std::string isoDaysAgoImpl(int days) {
    const auto now = std::chrono::system_clock::now() - std::chrono::hours(24LL * std::max(0, days));
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

std::string isoAfterSecondsImpl(long long seconds) {
    const auto when = std::chrono::system_clock::now() + std::chrono::seconds(std::max<long long>(0, seconds));
    const std::time_t t = std::chrono::system_clock::to_time_t(when);
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

std::string normalizeMemoryKey(const std::string& value) {
    return normalizeComparable(value);
}

double memoryTokenSimilarity(const std::string& a, const std::string& b) {
    return tokenJaccard(a, b);
}

MemoryStore::MemoryStore(std::string path, size_t maxActiveLessons)
    : path_(std::move(path)),
      maxActiveLessons_(maxActiveLessons == 0 ? kMaxActiveLessons : maxActiveLessons) {}

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

    if (!exec("PRAGMA foreign_keys=ON; PRAGMA secure_delete=ON;", &error) ||
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

bool MemoryStore::backupTo(const std::string& targetPath, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || targetPath.empty()) {
        error = "banco indisponivel";
        return false;
    }
    std::error_code ec;
    const std::filesystem::path target(targetPath);
    if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path(), ec);
    if (std::filesystem::exists(target, ec)) std::filesystem::remove(target, ec);
    std::string escaped;
    escaped.reserve(targetPath.size());
    for (char c : targetPath) {
        if (c == '\'') escaped += "''";
        else escaped += c;
    }
    return exec("VACUUM INTO '" + escaped + "';", &error);
}

bool MemoryStore::exec(const std::string& sql, std::string* error) {
    char* message = nullptr;
    int rc = SQLITE_ERROR;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (message) {
            sqlite3_free(message);
            message = nullptr;
        }
        rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message);
        if (rc == SQLITE_OK) break;
        if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10 * (attempt + 1) * (attempt + 1)));
    }
    if (rc != SQLITE_OK) {
        if (error) *error = message ? message : sqlite3_errmsg(db_);
        lastError_ = message ? message : sqlite3_errmsg(db_);
        if (message) sqlite3_free(message);
        return false;
    }
    if (message) sqlite3_free(message);
    return true;
}

bool MemoryStore::createSchema(std::string& error) {
    if (!exec(R"SQL(
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
  metadata TEXT NOT NULL DEFAULT '{}',
  last_used TEXT,
  success_count INTEGER NOT NULL DEFAULT 0,
  failure_count INTEGER NOT NULL DEFAULT 0,
  utility REAL NOT NULL DEFAULT 0.5,
  dormant_at TEXT
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

-- D1 v2: estado de continuacao declarado pelo projeto. O agente nunca cria
-- chaves; ele le/grava apenas as chaves declaradas.
CREATE TABLE IF NOT EXISTS memory_state_key (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  key TEXT NOT NULL,
  scope_kind TEXT NOT NULL CHECK (scope_kind IN ('project','task_type','item')),
  schema_json TEXT NOT NULL DEFAULT '{}',
  schema_hash TEXT NOT NULL DEFAULT '',
  max_bytes INTEGER NOT NULL DEFAULT 16384,
  ttl_seconds INTEGER NOT NULL DEFAULT 0,
  value_mode TEXT NOT NULL DEFAULT 'replace' CHECK (value_mode IN ('replace','versioned')),
  versioned_keep INTEGER NOT NULL DEFAULT 5,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, key)
);

CREATE TABLE IF NOT EXISTS memory_state (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  scope_kind TEXT NOT NULL,
  scope_owner TEXT NOT NULL,
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  version INTEGER NOT NULL DEFAULT 1,
  schema_hash TEXT NOT NULL DEFAULT '',
  expires_at TEXT,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, scope_kind, scope_owner, key)
);

CREATE TABLE IF NOT EXISTS memory_state_history (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  scope_kind TEXT NOT NULL,
  scope_owner TEXT NOT NULL,
  key TEXT NOT NULL,
  version INTEGER NOT NULL,
  value_json TEXT NOT NULL,
  schema_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_memory_state_key_scope ON memory_state_key(scope_id);
CREATE INDEX IF NOT EXISTS idx_memory_state_scope ON memory_state(scope_id, scope_kind, scope_owner);
CREATE INDEX IF NOT EXISTS idx_memory_state_history_key ON memory_state_history(scope_id, key, version);

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
  retries INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_hit (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  memory_id TEXT NOT NULL,
  kind TEXT NOT NULL,
  injected INTEGER NOT NULL DEFAULT 0,
  used INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_validation (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT,
  node_id TEXT,
  episode_id TEXT NOT NULL,
  validator_id TEXT NOT NULL,
  validator_version TEXT NOT NULL DEFAULT '',
  ok INTEGER NOT NULL DEFAULT 0,
  score REAL,
  reasons_json TEXT NOT NULL DEFAULT '[]',
  artifact_hash TEXT,
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, episode_id, validator_id, validator_version)
);

-- L2/M0: propostas, embeddings, playbooks e aliases. As tabelas nascem vazias e
-- nao alteram o comportamento da recuperacao atual.
CREATE TABLE IF NOT EXISTS memory_proposal (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  kind TEXT NOT NULL,
  payload_json TEXT NOT NULL,
  source_episode_id TEXT,
  evidence_json TEXT NOT NULL DEFAULT '[]',
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL DEFAULT 'pending',
  created_at TEXT NOT NULL,
  reviewed_at TEXT,
  reviewed_by TEXT
);

CREATE TABLE IF NOT EXISTS memory_embedding (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  object_type TEXT NOT NULL,
  object_id TEXT NOT NULL,
  model TEXT NOT NULL,
  dim INTEGER NOT NULL,
  vector BLOB NOT NULL,
  content_hash TEXT NOT NULL,
  domain TEXT NOT NULL DEFAULT '',
  fingerprint TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, object_type, object_id, model)
);

CREATE TABLE IF NOT EXISTS memory_playbook (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  task_type TEXT NOT NULL,
  title TEXT NOT NULL,
  steps_json TEXT NOT NULL,
  preconditions_json TEXT NOT NULL DEFAULT '{}',
  success_count INTEGER NOT NULL DEFAULT 0,
  failure_count INTEGER NOT NULL DEFAULT 0,
  confidence REAL NOT NULL DEFAULT 0.5,
  status TEXT NOT NULL DEFAULT 'active',
  source_ref TEXT,
  evidence_json TEXT NOT NULL DEFAULT '[]',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_policy (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  key TEXT NOT NULL,
  value_json TEXT NOT NULL,
  immutable INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL,
  updated_by TEXT NOT NULL,
  PRIMARY KEY (scope_id, key)
);

CREATE TABLE IF NOT EXISTS memory_alias (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  alias TEXT NOT NULL,
  node_id TEXT NOT NULL,
  source TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (scope_id, alias)
);

CREATE VIRTUAL TABLE IF NOT EXISTS memory_node_fts USING fts5(
  node_id UNINDEXED,
  name,
  aliases,
  metadata
);

CREATE INDEX IF NOT EXISTS idx_memory_edge_scope_status ON memory_edge(scope_id, status, valid_to);
CREATE INDEX IF NOT EXISTS idx_memory_edge_dormant ON memory_edge(scope_id, status, dormant_at);
CREATE INDEX IF NOT EXISTS idx_memory_edge_src_relation ON memory_edge(src_id, relation);
CREATE INDEX IF NOT EXISTS idx_memory_edge_dst_relation ON memory_edge(dst_id, relation);
CREATE INDEX IF NOT EXISTS idx_memory_edge_hash ON memory_edge(scope_id, edge_hash, status);
CREATE INDEX IF NOT EXISTS idx_memory_node_scope_kind ON memory_node(scope_id, kind);
CREATE INDEX IF NOT EXISTS idx_memory_episode_scope_time ON memory_episode(scope_id, created_at);
CREATE INDEX IF NOT EXISTS idx_memory_usage_scope_time ON memory_usage(scope_id, created_at);
CREATE INDEX IF NOT EXISTS idx_memory_hit_scope_run ON memory_hit(scope_id, run_id, node_id);
CREATE INDEX IF NOT EXISTS idx_memory_hit_memory ON memory_hit(scope_id, memory_id);
CREATE INDEX IF NOT EXISTS idx_memory_validation_episode ON memory_validation(scope_id, episode_id);
CREATE INDEX IF NOT EXISTS idx_memory_proposal_scope_status ON memory_proposal(scope_id, status, created_at);
CREATE INDEX IF NOT EXISTS idx_memory_proposal_episode ON memory_proposal(source_episode_id);
CREATE INDEX IF NOT EXISTS idx_memory_embedding_object ON memory_embedding(scope_id, object_type, object_id);
CREATE INDEX IF NOT EXISTS idx_memory_playbook_task ON memory_playbook(scope_id, task_type, status);
CREATE INDEX IF NOT EXISTS idx_memory_alias_node ON memory_alias(scope_id, node_id);
CREATE INDEX IF NOT EXISTS idx_memory_policy_scope ON memory_policy(scope_id, key);
)SQL", &error)) {
        return false;
    }
    addColumnIfMissing(db_, "memory_usage", "retries", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_usage", "outcome", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_edge", "utility", "REAL NOT NULL DEFAULT 0.5");
    addColumnIfMissing(db_, "memory_episode", "task_type", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "outcome", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "error_class", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "input_hash", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "tool_calls_json", "TEXT NOT NULL DEFAULT '[]'");
    addColumnIfMissing(db_, "memory_episode", "files_touched_json", "TEXT NOT NULL DEFAULT '[]'");
    addColumnIfMissing(db_, "memory_episode", "artifacts_json", "TEXT NOT NULL DEFAULT '[]'");
    addColumnIfMissing(db_, "memory_episode", "evidence_json", "TEXT NOT NULL DEFAULT '[]'");
    addColumnIfMissing(db_, "memory_episode", "success_signals_json", "TEXT NOT NULL DEFAULT '{}'");
    addColumnIfMissing(db_, "memory_episode", "redactions", "INTEGER NOT NULL DEFAULT 0");
    // M2: dominio no indice vetorial para respeitar memoryDomains na busca hibrida.
    addColumnIfMissing(db_, "memory_embedding", "domain", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_embedding", "fingerprint", "TEXT NOT NULL DEFAULT ''");
    // M3: reforco/decay sem apagar memoria.
    addColumnIfMissing(db_, "memory_edge", "last_used", "TEXT");
    addColumnIfMissing(db_, "memory_edge", "success_count", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_edge", "failure_count", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_edge", "dormant_at", "TEXT");
    addColumnIfMissing(db_, "memory_playbook", "source_ref", "TEXT");
    addColumnIfMissing(db_, "memory_playbook", "evidence_json", "TEXT NOT NULL DEFAULT '[]'");

    // Migracao: consolida hits duplicados de versoes anteriores e cria a chave unica
    // (scope, run, node, memoria). Sem isso, consultas concorrentes podem duplicar linhas.
    std::string migrationError;
    bool hasUniqueHitIndex = false;
    {
        sqlite3_stmt* probe = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT 1 FROM sqlite_master WHERE type='index' AND name='idx_memory_hit_unique';",
                               -1, &probe, nullptr) == SQLITE_OK) {
            hasUniqueHitIndex = sqlite3_step(probe) == SQLITE_ROW;
            sqlite3_finalize(probe);
        }
    }
    if (!hasUniqueHitIndex) {
        if (!exec(R"SQL(
UPDATE memory_hit SET
  used = (SELECT MAX(x.used) FROM memory_hit x
          WHERE x.scope_id=memory_hit.scope_id AND x.run_id IS memory_hit.run_id
            AND x.node_id IS memory_hit.node_id AND x.memory_id=memory_hit.memory_id),
  injected = (SELECT MAX(x.injected) FROM memory_hit x
              WHERE x.scope_id=memory_hit.scope_id AND x.run_id IS memory_hit.run_id
                AND x.node_id IS memory_hit.node_id AND x.memory_id=memory_hit.memory_id)
WHERE rowid IN (SELECT MIN(rowid) FROM memory_hit
                GROUP BY scope_id, run_id, node_id, memory_id);
DELETE FROM memory_hit WHERE rowid NOT IN
  (SELECT MIN(rowid) FROM memory_hit GROUP BY scope_id, run_id, node_id, memory_id);
)SQL", &migrationError)) {
            lastError_ = migrationError;
            std::fprintf(stderr, "[MemoryStore] dedupe memory_hit falhou: %s\n", migrationError.c_str());
        }
        if (!exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_memory_hit_unique "
                  "ON memory_hit(scope_id, run_id, node_id, memory_id);", &migrationError)) {
            lastError_ = migrationError;
            std::fprintf(stderr, "[MemoryStore] indice memory_hit falhou: %s\n", migrationError.c_str());
        }
    }
    return true;
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
    if (stepRetry(st.stmt) != SQLITE_DONE) {
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
        if (stepRetry(closure.stmt) != SQLITE_DONE) {
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
    if (stepRetry(self.stmt) != SQLITE_DONE) {
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
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    return out;
}

bool MemoryStore::setWorking(const std::string& scopeId, const std::string& itemId,
                             const std::string& key, const taskforge::Json& value) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    taskforge::Json cleanValue = value;
    const size_t redactions = sanitizeJsonInPlace(cleanValue);
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
    bindText(st.stmt, 4, cleanValue.dump());
    bindText(st.stmt, 5, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (redactions > 0) audit("runtime", "redact", scopeId, "", "working:" + key);
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
    if (stepRetry(st.stmt) == SQLITE_ROW) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, 0));
        sanitizeJsonInPlace(value);
        return value;
    }
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
    return stepRetry(st.stmt) == SQLITE_ROW;
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
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, 1));
        sanitizeJsonInPlace(value);
        out[columnText(st.stmt, 0)] = value;
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
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, 2));
        sanitizeJsonInPlace(value);
        out[columnText(st.stmt, 0)][columnText(st.stmt, 1)] = value;
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
    stepRetry(st.stmt);
}

bool MemoryStore::declareStateKey(const std::string& scopeId, const std::string& key,
                                  const std::string& scopeKind, const taskforge::Json& schema,
                                  long long maxBytes, long long ttlSeconds,
                                  const std::string& valueMode, long long versionedKeep,
                                  std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || key.empty() || key.size() > 128) {
        error = "chave de estado invalida";
        return false;
    }
    if (scopeKind != "project" && scopeKind != "task_type" && scopeKind != "item") {
        error = "escopo de estado invalido";
        return false;
    }
    if (valueMode != "replace" && valueMode != "versioned") {
        error = "modo de valor invalido";
        return false;
    }
    taskforge::Json cleanSchema = schema.is_object() ? schema : taskforge::Json::object();
    sanitizeJsonInPlace(cleanSchema);
    const std::string schemaJson = cleanSchema.dump();
    const std::string schemaHash = hashHex(schemaJson);
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_state_key(scope_id,key,scope_kind,schema_json,schema_hash,"
                       "max_bytes,ttl_seconds,value_mode,versioned_keep,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(scope_id,key) DO UPDATE SET "
                       "scope_kind=excluded.scope_kind,schema_json=excluded.schema_json,"
                       "schema_hash=excluded.schema_hash,max_bytes=excluded.max_bytes,"
                       "ttl_seconds=excluded.ttl_seconds,value_mode=excluded.value_mode,"
                       "versioned_keep=excluded.versioned_keep,updated_at=excluded.updated_at",
                 st, error)) {
        return false;
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, key);
    bindText(st.stmt, 3, scopeKind);
    bindText(st.stmt, 4, schemaJson);
    bindText(st.stmt, 5, schemaHash);
    sqlite3_bind_int64(st.stmt, 6, std::max<long long>(1, std::min<long long>(65536, maxBytes)));
    sqlite3_bind_int64(st.stmt, 7, std::max<long long>(0, ttlSeconds));
    bindText(st.stmt, 8, valueMode);
    sqlite3_bind_int64(st.stmt, 9, std::max<long long>(1, std::min<long long>(100, versionedKeep)));
    bindText(st.stmt, 10, now);
    bindText(st.stmt, 11, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    audit("human", "declare_state_key", key, "", scopeKind);
    return true;
}

taskforge::Json MemoryStore::stateKeyInfo(const std::string& scopeId,
                                          const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_ || key.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT key,scope_kind,schema_json,schema_hash,max_bytes,ttl_seconds,"
                       "value_mode,versioned_keep,updated_at FROM memory_state_key "
                       "WHERE scope_id=? AND key=?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, key);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    out["key"] = columnText(st.stmt, 0);
    out["scopeKind"] = columnText(st.stmt, 1);
    taskforge::Json schema = parseJsonOrNull(columnText(st.stmt, 2));
    out["schema"] = schema.is_object() ? schema : taskforge::Json::object();
    out["schemaHash"] = columnText(st.stmt, 3);
    out["maxBytes"] = columnInt(st.stmt, 4);
    out["ttlSeconds"] = columnInt(st.stmt, 5);
    out["valueMode"] = columnText(st.stmt, 6);
    out["versionedKeep"] = columnInt(st.stmt, 7);
    out["updatedAt"] = columnText(st.stmt, 8);
    return out;
}

taskforge::Json MemoryStore::stateKeys(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::array();
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT key,scope_kind,schema_json,schema_hash,max_bytes,ttl_seconds,"
                       "value_mode,versioned_keep,updated_at FROM memory_state_key "
                       "WHERE scope_id=? ORDER BY key", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        taskforge::Json schema = parseJsonOrNull(columnText(st.stmt, 2));
        out.push_back(taskforge::Json{{"key", columnText(st.stmt, 0)},
                                      {"scopeKind", columnText(st.stmt, 1)},
                                      {"schema", schema.is_object() ? schema : taskforge::Json::object()},
                                      {"schemaHash", columnText(st.stmt, 3)},
                                      {"maxBytes", columnInt(st.stmt, 4)},
                                      {"ttlSeconds", columnInt(st.stmt, 5)},
                                      {"valueMode", columnText(st.stmt, 6)},
                                      {"versionedKeep", columnInt(st.stmt, 7)},
                                      {"updatedAt", columnText(st.stmt, 8)}});
    }
    return out;
}

taskforge::Json MemoryStore::getState(const std::string& scopeId, const std::string& key,
                                      const std::string& scopeOwner,
                                      std::string& error) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"ok", true}, {"key", key}, {"found", false}};
    if (!db_ || key.empty() || scopeOwner.empty()) {
        error = "chave ou escopo do estado ausente";
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    const taskforge::Json info = stateKeyInfo(scopeId, key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    const std::string scopeKind = info.value("scopeKind", std::string());
    Stmt st;
    if (!prepare(db_, "SELECT value_json,version,schema_hash,COALESCE(expires_at,''),updated_at "
                       "FROM memory_state WHERE scope_id=? AND scope_kind=? AND scope_owner=? AND key=?",
                 st, error)) {
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, scopeKind);
    bindText(st.stmt, 3, scopeOwner);
    bindText(st.stmt, 4, key);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    const std::string expiresAt = columnText(st.stmt, 3);
    if (!expiresAt.empty() && expiresAt <= nowIso()) {
        out["expired"] = true;
        return out;
    }
    taskforge::Json value = parseJsonOrNull(columnText(st.stmt, 0));
    sanitizeJsonInPlace(value);
    out["found"] = true;
    out["value"] = value;
    out["version"] = columnInt(st.stmt, 1);
    out["stale"] = columnText(st.stmt, 2) != info.value("schemaHash", std::string());
    out["untrusted"] = true;
    out["scopeKind"] = scopeKind;
    out["scopeOwner"] = scopeOwner;
    out["expiresAt"] = expiresAt;
    out["updatedAt"] = columnText(st.stmt, 4);
    return out;
}

taskforge::Json MemoryStore::setState(const std::string& scopeId, const std::string& key,
                                      const std::string& scopeOwner,
                                      const taskforge::Json& value, bool hasExpected,
                                      long long expectedVersion, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"ok", false}, {"key", key}};
    if (!db_ || key.empty() || scopeOwner.empty()) {
        error = "chave ou escopo do estado ausente";
        out["error"] = error;
        return out;
    }
    const taskforge::Json info = stateKeyInfo(scopeId, key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        out["error"] = error;
        return out;
    }
    taskforge::Json cleanValue = value;
    sanitizeJsonInPlace(cleanValue);
    const std::string valueJson = cleanValue.dump();
    const long long maxBytes = info.value("maxBytes", 16384LL);
    if (static_cast<long long>(valueJson.size()) > maxBytes) {
        error = "valor do estado excede o limite de bytes";
        out["error"] = error;
        return out;
    }
    const std::string scopeKind = info.value("scopeKind", std::string());
    const std::string schemaHash = info.value("schemaHash", std::string());
    Stmt current;
    if (!prepare(db_, "SELECT version FROM memory_state WHERE scope_id=? AND scope_kind=? "
                       "AND scope_owner=? AND key=?", current, error)) {
        out["error"] = error;
        return out;
    }
    bindText(current.stmt, 1, scopeId);
    bindText(current.stmt, 2, scopeKind);
    bindText(current.stmt, 3, scopeOwner);
    bindText(current.stmt, 4, key);
    const bool exists = stepRetry(current.stmt) == SQLITE_ROW;
    const long long currentVersion = exists ? columnInt(current.stmt, 0) : 0;
    if (exists) {
        if (!hasExpected) {
            error = "chave ja existe; informe expected_version para sobrescrever";
            out["error"] = error;
            out["conflict"] = true;
            out["currentVersion"] = currentVersion;
            return out;
        }
        if (expectedVersion != currentVersion) {
            error = "versao desatualizada";
            out["error"] = error;
            out["conflict"] = true;
            out["currentVersion"] = currentVersion;
            return out;
        }
    } else if (hasExpected) {
        error = "chave nova deve ser criada sem expected_version";
        out["error"] = error;
        out["conflict"] = true;
        out["currentVersion"] = 0;
        return out;
    }
    const long long newVersion = currentVersion + 1;
    const long long ttlSeconds = info.value("ttlSeconds", 0LL);
    const std::string expiresAt = ttlSeconds > 0 ? isoAfterSecondsImpl(ttlSeconds) : std::string();
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_state(scope_id,scope_kind,scope_owner,key,value_json,version,"
                       "schema_hash,expires_at,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,scope_kind,scope_owner,key) DO UPDATE SET "
                       "value_json=excluded.value_json,version=excluded.version,"
                       "schema_hash=excluded.schema_hash,expires_at=excluded.expires_at,"
                       "updated_at=excluded.updated_at", st, error)) {
        out["error"] = error;
        return out;
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, scopeKind);
    bindText(st.stmt, 3, scopeOwner);
    bindText(st.stmt, 4, key);
    bindText(st.stmt, 5, valueJson);
    sqlite3_bind_int64(st.stmt, 6, newVersion);
    bindText(st.stmt, 7, schemaHash);
    bindNullableText(st.stmt, 8, expiresAt);
    bindText(st.stmt, 9, now);
    bindText(st.stmt, 10, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        out["error"] = error;
        return out;
    }

    if (info.value("valueMode", std::string("replace")) == "versioned") {
        Stmt hist;
        if (prepare(db_, "INSERT INTO memory_state_history(id,scope_id,scope_kind,scope_owner,key,"
                          "version,value_json,schema_hash,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
                    hist, error)) {
            bindText(hist.stmt, 1, newId("state"));
            bindText(hist.stmt, 2, scopeId);
            bindText(hist.stmt, 3, scopeKind);
            bindText(hist.stmt, 4, scopeOwner);
            bindText(hist.stmt, 5, key);
            sqlite3_bind_int64(hist.stmt, 6, newVersion);
            bindText(hist.stmt, 7, valueJson);
            bindText(hist.stmt, 8, schemaHash);
            bindText(hist.stmt, 9, now);
            stepRetry(hist.stmt);
        }
        const long long keep = std::max<long long>(1, info.value("versionedKeep", 5LL));
        Stmt prune;
        if (prepare(db_, "DELETE FROM memory_state_history WHERE scope_id=? AND scope_kind=? "
                          "AND scope_owner=? AND key=? AND version NOT IN (SELECT version FROM "
                          "memory_state_history WHERE scope_id=? AND scope_kind=? AND scope_owner=? "
                          "AND key=? ORDER BY version DESC LIMIT ?)", prune, error)) {
            bindText(prune.stmt, 1, scopeId);
            bindText(prune.stmt, 2, scopeKind);
            bindText(prune.stmt, 3, scopeOwner);
            bindText(prune.stmt, 4, key);
            bindText(prune.stmt, 5, scopeId);
            bindText(prune.stmt, 6, scopeKind);
            bindText(prune.stmt, 7, scopeOwner);
            bindText(prune.stmt, 8, key);
            sqlite3_bind_int64(prune.stmt, 9, keep);
            stepRetry(prune.stmt);
        }
    }
    audit("runtime", "state_set", key, "", scopeOwner);
    out["ok"] = true;
    out["version"] = newVersion;
    out["expiresAt"] = expiresAt;
    out["schemaHash"] = schemaHash;
    return out;
}

taskforge::Json MemoryStore::listState(const std::string& scopeId, const std::string& key,
                                       const std::string& scopeOwner) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"ok", true}, {"key", key}, {"found", false}};
    if (!db_ || key.empty() || scopeOwner.empty()) {
        out["ok"] = false;
        out["error"] = "chave ou escopo do estado ausente";
        return out;
    }
    const taskforge::Json info = stateKeyInfo(scopeId, key);
    if (info.empty()) {
        out["ok"] = false;
        out["error"] = "chave de estado nao declarada: " + key;
        return out;
    }
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT version,schema_hash,COALESCE(expires_at,''),updated_at "
                       "FROM memory_state WHERE scope_id=? AND scope_kind=? AND scope_owner=? AND key=?",
                 st, error)) {
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, info.value("scopeKind", std::string()));
    bindText(st.stmt, 3, scopeOwner);
    bindText(st.stmt, 4, key);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    const std::string expiresAt = columnText(st.stmt, 2);
    out["found"] = true;
    out["version"] = columnInt(st.stmt, 0);
    out["stale"] = columnText(st.stmt, 1) != info.value("schemaHash", std::string());
    out["expiresAt"] = expiresAt;
    out["expired"] = !expiresAt.empty() && expiresAt <= nowIso();
    out["scopeKind"] = info.value("scopeKind", std::string());
    out["scopeOwner"] = scopeOwner;
    out["updatedAt"] = columnText(st.stmt, 3);
    return out;
}

taskforge::Json MemoryStore::stateHistory(const std::string& scopeId, const std::string& key,
                                          const std::string& scopeOwner, size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"ok", true}, {"key", key}, {"versions", taskforge::Json::array()}};
    const taskforge::Json info = stateKeyInfo(scopeId, key);
    if (info.empty()) {
        out["ok"] = false;
        out["error"] = "chave de estado nao declarada: " + key;
        return out;
    }
    if (info.value("valueMode", std::string("replace")) != "versioned") {
        out["ok"] = false;
        out["error"] = "chave nao e versionada";
        return out;
    }
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT version,value_json,schema_hash,created_at FROM memory_state_history "
                       "WHERE scope_id=? AND scope_kind=? AND scope_owner=? AND key=? "
                       "ORDER BY version DESC LIMIT ?", st, error)) {
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, info.value("scopeKind", std::string()));
    bindText(st.stmt, 3, scopeOwner);
    bindText(st.stmt, 4, key);
    sqlite3_bind_int(st.stmt, 5, static_cast<int>(std::min<size_t>(limit, 100)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, 1));
        sanitizeJsonInPlace(value);
        out["versions"].push_back(taskforge::Json{{"version", columnInt(st.stmt, 0)},
                                                  {"value", value},
                                                  {"schemaHash", columnText(st.stmt, 2)},
                                                  {"createdAt", columnText(st.stmt, 3)},
                                                  {"untrusted", true}});
    }
    return out;
}

void MemoryStore::clearScopeData(const std::string& scopeId) {
    purge(scopeId, MemoryPurgeOptions{false, false});
}

bool MemoryStore::purge(const std::string& scopeId, const MemoryPurgeOptions& options) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;

    bool ok = true;
    auto execScoped = [&](const char* sql, int scopeBinds) {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) {
            lastError_ = error;
            ok = false;
            return;
        }
        for (int i = 1; i <= scopeBinds; ++i) bindText(st.stmt, i, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            ok = false;
        }
    };

    // Evidencias referenciam edges e episodes; apagar primeiro evita violacao de FK.
    execScoped("DELETE FROM memory_evidence WHERE "
               "edge_id IN (SELECT id FROM memory_edge WHERE scope_id=?) OR "
               "episode_id IN (SELECT id FROM memory_episode WHERE scope_id=?)", 2);
    execScoped("DELETE FROM memory_hit WHERE scope_id=?", 1);
    if (!options.keepAudit) {
        execScoped("DELETE FROM memory_audit WHERE target=? OR "
                   "target IN (SELECT id FROM memory_edge WHERE scope_id=?) OR "
                   "target IN (SELECT id FROM memory_episode WHERE scope_id=?)", 3);
    }
    execScoped("DELETE FROM memory_usage WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_state_history WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_state WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_state_key WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_working WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_validation WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_embedding WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_alias WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_proposal WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_playbook WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_policy WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_node_fts WHERE node_id IN "
               "(SELECT id FROM memory_node WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_edge WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_episode WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_node WHERE scope_id=?", 1);

    if (options.vacuum) {
        std::string vacuumError;
        if (!exec("VACUUM;", &vacuumError)) {
            lastError_ = vacuumError;
            ok = false;
        }
    }
    return ok;
}

MemoryPruneResult MemoryStore::prune(const std::string& scopeId, int olderThanDays) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPruneResult result;
    if (!db_ || olderThanDays <= 0) return result;

    const std::string cutoff = isoDaysAgoImpl(olderThanDays);
    auto deleteOlder = [&](const char* sql, long long& count) {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) {
            lastError_ = error;
            return;
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, cutoff);
        if (stepRetry(st.stmt) == SQLITE_DONE) {
            count = static_cast<long long>(sqlite3_changes(db_));
        } else {
            lastError_ = sqlite3_errmsg(db_);
        }
    };

    deleteOlder("DELETE FROM memory_episode WHERE scope_id=? AND created_at < ?", result.episodes);
    deleteOlder("DELETE FROM memory_hit WHERE scope_id=? AND created_at < ?", result.hits);
    deleteOlder("DELETE FROM memory_usage WHERE scope_id=? AND created_at < ?", result.usage);
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "DELETE FROM memory_validation WHERE scope_id=? AND created_at < ?", st, error)) {
            bindText(st.stmt, 1, scopeId);
            bindText(st.stmt, 2, cutoff);
            stepRetry(st.stmt);
        }
    }
    return result;
}

bool MemoryStore::resetInMemory() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (path_ != ":memory:") return false;
    close();
    std::string error;
    return open(error);
}

std::string MemoryStore::ensureNode(const std::string& scopeId, const std::string& nodeId,
                                    const std::string& kind, const std::string& canonicalName,
                                    const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    const SanitizeResult cleanName = sanitizeText(canonicalName, 4000);
    taskforge::Json cleanMetadata = metadata;
    const size_t redactions = sanitizeJsonInPlace(cleanMetadata);
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
    bindText(st.stmt, 4, cleanName.text);
    bindText(st.stmt, 5, "[]");
    bindText(st.stmt, 6, now);
    bindText(st.stmt, 7, now);
    bindText(st.stmt, 8, cleanMetadata.dump());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }

    Stmt update;
    if (prepare(db_, "UPDATE memory_node SET canonical_name=?, metadata=?, updated_at=? WHERE id=?", update, error)) {
        bindText(update.stmt, 1, cleanName.text);
        bindText(update.stmt, 2, cleanMetadata.dump());
        bindText(update.stmt, 3, now);
        bindText(update.stmt, 4, nodeId);
        stepRetry(update.stmt);
    }

    Stmt delFts;
    if (prepare(db_, "DELETE FROM memory_node_fts WHERE node_id=?", delFts, error)) {
        bindText(delFts.stmt, 1, nodeId);
        stepRetry(delFts.stmt);
    }
    Stmt fts;
    if (prepare(db_, "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)", fts, error)) {
        bindText(fts.stmt, 1, nodeId);
        bindText(fts.stmt, 2, cleanName.text);
        bindText(fts.stmt, 3, "");
        bindText(fts.stmt, 4, cleanMetadata.dump());
        stepRetry(fts.stmt);
    }
    if (!cleanName.text.empty()) {
        addAlias(scopeId, normalizeComparable(cleanName.text), nodeId, "canonical");
    }
    if (cleanName.redacted || redactions > 0) audit("runtime", "redact", scopeId, "", "node:" + nodeId);
    return nodeId;
}

std::string MemoryStore::nodeByAlias(const std::string& scopeId, const std::string& alias) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string normalized = normalizeComparable(alias);
    if (!db_ || normalized.empty()) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT node_id FROM memory_alias WHERE scope_id=? AND alias=? LIMIT 1", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, normalized);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnText(st.stmt, 0) : std::string();
}

bool MemoryStore::addAlias(const std::string& scopeId, const std::string& alias,
                           const std::string& nodeId, const std::string& source) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string normalized = normalizeComparable(alias);
    if (!db_ || normalized.empty() || nodeId.empty()) return false;
    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT node_id FROM memory_alias WHERE scope_id=? AND alias=? LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, normalized);
        if (stepRetry(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0) == nodeId;
    }
    Stmt st;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_alias(scope_id,alias,node_id,source) VALUES(?,?,?,?)",
                 st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, normalized);
    bindText(st.stmt, 3, nodeId);
    bindText(st.stmt, 4, source);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::string MemoryStore::resolveNode(const std::string& scopeId, const std::string& name,
                                     const std::string& kind,
                                     const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string normalized = normalizeComparable(name);
    if (!db_ || normalized.empty()) return "";

    const std::string existing = nodeByAlias(scopeId, normalized);
    if (!existing.empty()) return existing;

    // Migracao/preenchimento: nodes antigos podem nao ter alias ainda.
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT id, canonical_name FROM memory_node WHERE scope_id=? ORDER BY id LIMIT 5000",
                st, error)) {
        bindText(st.stmt, 1, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            const std::string id = columnText(st.stmt, 0);
            if (normalizeComparable(columnText(st.stmt, 1)) == normalized) {
                addAlias(scopeId, normalized, id, "resolved");
                return id;
            }
        }
    }

    const std::string nodeId = "node_" + hashHex(scopeId + "|alias|" +
                                                 (kind.empty() ? "entity" : kind) + "|" + normalized);
    return ensureNode(scopeId, nodeId, kind.empty() ? "entity" : kind, name, metadata);
}

std::string MemoryStore::recordEdge(const std::string& scopeId, const std::string& srcId,
                                    const std::string& dstId, const std::string& relation,
                                    const taskforge::Json& value, const std::string& validFrom,
                                    const std::string& validTo, double confidence,
                                    const std::string& status, const std::string& sourceKind,
                                    const std::string& sourceRef, const taskforge::Json& metadata) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    taskforge::Json cleanValue = value;
    taskforge::Json cleanMetadata = metadata;
    size_t redactions = sanitizeJsonInPlace(cleanValue);
    redactions += sanitizeJsonInPlace(cleanMetadata);
    const std::string valueJson = cleanValue.is_null() ? "" : cleanValue.dump();
    const std::string from = validFrom.empty() ? nowIso() : validFrom;
    const std::string hash = edgeHash(scopeId, srcId, dstId, relation, valueJson, from, validTo);

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND edge_hash=? AND status='active' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, hash);
        if (stepRetry(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0);
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
    bindText(st.stmt, 15, cleanMetadata.dump());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    if (redactions > 0) audit("runtime", "redact", scopeId, "", "edge:" + id);
    return id;
}

std::string MemoryStore::recordLesson(const std::string& scopeId, const std::string& domain,
                                      const std::string& triggerError, const std::string& lessonRule,
                                      const std::string& sourceItem, const std::string& sourceKind) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || lessonRule.empty()) return "";

    // A regra/trigger podem vir de erro de provider ou de texto de agente. Sanitiza antes
    // de calcular hash, gravar no grafo ou expor no brief.
    const SanitizeResult cleanRule = sanitizeText(lessonRule, 4000);
    const SanitizeResult cleanTrigger = sanitizeText(triggerError, 4000);
    const SanitizeResult cleanSource = sanitizeText(sourceItem, 4000);
    const SanitizeResult cleanDomain = sanitizeText(domain.empty() ? "geral" : domain, 128);
    if (cleanRule.text.empty()) return "";

    const std::string dom = cleanDomain.text;
    const std::string domainId = "node_" + hashHex(scopeId + "|domain|" + dom);
    const std::string ruleId = "node_" + hashHex(scopeId + "|rule|" + cleanRule.text);

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND dst_id=? "
                       "AND relation='HAS_LESSON' AND status='active' AND dormant_at IS NULL LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, domainId);
        bindText(existing.stmt, 3, ruleId);
        if (stepRetry(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0);
    }

    // Teto global so para escritas de agentes/tools. Restauracao de snapshot e migracao
    // legada precisam poder reconstruir um projeto que ja tinha mais licoes.
    if (sourceKind == "agent" && activeLessonCount(scopeId) >= maxActiveLessons_) {
        lastError_ = "limite global de licoes ativas atingido";
        return "";
    }

    ensureNode(scopeId, domainId, "domain", dom);
    ensureNode(scopeId, ruleId, "rule", cleanRule.text);

    taskforge::Json metadata = taskforge::Json::object();
    metadata["domain"] = dom;
    metadata["triggerError"] = cleanTrigger.text;
    metadata["lessonRule"] = cleanRule.text;
    metadata["sourceItem"] = cleanSource.text;
    const std::string id = recordEdge(scopeId, domainId, ruleId, "HAS_LESSON", taskforge::Json(), "", "",
                                      0.9, "active", sourceKind, cleanSource.text, metadata);
    if (!id.empty() && (cleanRule.redacted || cleanTrigger.redacted || cleanSource.redacted || cleanDomain.redacted)) {
        audit("runtime", "redact", scopeId, "", "lesson:" + id);
    }
    return id;
}

size_t MemoryStore::activeLessonCount(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND relation='HAS_LESSON' AND status='active' AND dormant_at IS NULL",
                 st, error)) {
        lastError_ = error;
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    return stepRetry(st.stmt) == SQLITE_ROW ? static_cast<size_t>(columnInt(st.stmt, 0)) : 0;
}

std::vector<MemoryLesson> MemoryStore::getLessons(const std::string& scopeId,
                                                  const std::string& domain) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryLesson> out;
    if (!db_) return out;
    std::string sql =
        "SELECT e.id, e.scope_id, d.canonical_name, r.canonical_name, e.recorded_at, e.metadata, e.source_ref, e.confidence "
        "FROM memory_edge e "
        "JOIN memory_node d ON d.id=e.src_id "
        "JOIN memory_node r ON r.id=e.dst_id "
        "WHERE e.scope_id=? AND e.relation='HAS_LESSON' AND e.status='active' AND e.dormant_at IS NULL";
    if (!domain.empty()) sql += " AND d.canonical_name=?";
    sql += " ORDER BY e.confidence DESC, e.recorded_at DESC, e.id DESC";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (!domain.empty()) bindText(st.stmt, 2, domain);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryLesson lesson;
        lesson.id = columnText(st.stmt, 0);
        lesson.projectId = columnText(st.stmt, 1);
        lesson.domain = sanitizeText(columnText(st.stmt, 2), 128).text;
        lesson.lessonRule = sanitizeText(columnText(st.stmt, 3), 4000).text;
        lesson.createdAt = columnText(st.stmt, 4);
        taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 5));
        lesson.triggerError = sanitizeText(metadata.value("triggerError", std::string()), 4000).text;
        lesson.sourceItem = sanitizeText(metadata.value("sourceItem", columnText(st.stmt, 6)), 4000).text;
        lesson.confidence = columnDouble(st.stmt, 7);
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
    stepRetry(st.stmt);
    audit("runtime", "clear_lessons", scopeId, "", "");
}

std::string MemoryStore::recordEpisode(const std::string& scopeId, const std::string& runId,
                                       const std::string& nodeId, const std::string& itemId,
                                       const std::string& kind, const std::string& content,
                                       const taskforge::Json& metadata) {
    MemoryEpisodeRecord episode;
    episode.scopeId = scopeId;
    episode.runId = runId;
    episode.nodeId = nodeId;
    episode.itemId = itemId;
    episode.kind = kind;
    episode.content = content;
    episode.metadata = metadata;
    return recordEpisode(episode);
}

std::string MemoryStore::recordEpisode(const MemoryEpisodeRecord& episode) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || episode.scopeId.empty()) return "";

    const SanitizeResult cleanContent = sanitizeText(episode.content, 4000);
    const SanitizeResult cleanTaskType = sanitizeText(episode.taskType, 256);
    const SanitizeResult cleanOutcome = sanitizeText(episode.outcome, 64);
    const SanitizeResult cleanErrorClass = sanitizeText(episode.errorClass, 128);
    const SanitizeResult cleanInputHash = sanitizeText(episode.inputHash, 128);
    taskforge::Json cleanToolCalls = episode.toolCalls.is_array() ? episode.toolCalls : taskforge::Json::array();
    taskforge::Json cleanFiles = episode.filesTouched.is_array() ? episode.filesTouched : taskforge::Json::array();
    taskforge::Json cleanArtifacts = episode.artifacts.is_array() ? episode.artifacts : taskforge::Json::array();
    taskforge::Json cleanEvidence = episode.evidence.is_array() ? episode.evidence : taskforge::Json::array();
    taskforge::Json cleanSignals = episode.successSignals.is_object() ? episode.successSignals : taskforge::Json::object();
    taskforge::Json cleanMetadata = episode.metadata.is_object() ? episode.metadata : taskforge::Json::object();
    size_t redactions = cleanContent.redactions + cleanTaskType.redactions + cleanOutcome.redactions +
                        cleanErrorClass.redactions + cleanInputHash.redactions;
    redactions += sanitizeJsonInPlace(cleanToolCalls);
    redactions += sanitizeJsonInPlace(cleanFiles);
    redactions += sanitizeJsonInPlace(cleanArtifacts);
    redactions += sanitizeJsonInPlace(cleanEvidence);
    redactions += sanitizeJsonInPlace(cleanSignals);
    redactions += sanitizeJsonInPlace(cleanMetadata);

    const std::string id = newId("ep");
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_episode(id,scope_id,run_id,node_id,item_id,kind,content,sanitized,"
                       "task_type,outcome,error_class,input_hash,tool_calls_json,files_touched_json,"
                       "artifacts_json,evidence_json,success_signals_json,redactions,created_at,metadata) "
                       "VALUES(?,?,?,?,?,?,?,1,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, episode.scopeId);
    bindText(st.stmt, 3, episode.runId);
    bindNullableText(st.stmt, 4, episode.nodeId);
    bindNullableText(st.stmt, 5, episode.itemId);
    bindText(st.stmt, 6, episode.kind.empty() ? "agent" : episode.kind);
    bindText(st.stmt, 7, cleanContent.text);
    bindText(st.stmt, 8, cleanTaskType.text);
    bindText(st.stmt, 9, cleanOutcome.text);
    bindText(st.stmt, 10, cleanErrorClass.text);
    bindText(st.stmt, 11, cleanInputHash.text);
    bindText(st.stmt, 12, cleanToolCalls.dump());
    bindText(st.stmt, 13, cleanFiles.dump());
    bindText(st.stmt, 14, cleanArtifacts.dump());
    bindText(st.stmt, 15, cleanEvidence.dump());
    bindText(st.stmt, 16, cleanSignals.dump());
    sqlite3_bind_int64(st.stmt, 17, static_cast<sqlite3_int64>(redactions));
    bindText(st.stmt, 18, nowIso());
    bindText(st.stmt, 19, cleanMetadata.dump());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    if (redactions > 0) audit("runtime", "redact", episode.scopeId, "", "episode:" + id);
    return id;
}

taskforge::Json MemoryStore::getEpisode(const std::string& scopeId, const std::string& episodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_ || episodeId.empty()) return out;

    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,run_id,COALESCE(node_id,''),COALESCE(item_id,''),kind,content,sanitized,"
                       "task_type,outcome,error_class,input_hash,tool_calls_json,files_touched_json,artifacts_json,"
                       "evidence_json,success_signals_json,redactions,created_at,metadata "
                       "FROM memory_episode WHERE scope_id=? AND id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, episodeId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;

    out["id"] = columnText(st.stmt, 0);
    out["scopeId"] = columnText(st.stmt, 1);
    out["runId"] = columnText(st.stmt, 2);
    out["nodeId"] = columnText(st.stmt, 3);
    out["itemId"] = columnText(st.stmt, 4);
    out["kind"] = columnText(st.stmt, 5);
    out["content"] = sanitizeText(columnText(st.stmt, 6), 4000).text;
    out["sanitized"] = columnInt(st.stmt, 7) != 0;
    out["taskType"] = sanitizeText(columnText(st.stmt, 8), 256).text;
    out["outcome"] = sanitizeText(columnText(st.stmt, 9), 64).text;
    out["errorClass"] = sanitizeText(columnText(st.stmt, 10), 128).text;
    out["inputHash"] = sanitizeText(columnText(st.stmt, 11), 128).text;
    auto readArray = [&](int index) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, index));
        if (!value.is_array()) value = taskforge::Json::array();
        sanitizeJsonInPlace(value);
        return value;
    };
    out["toolCalls"] = readArray(12);
    out["filesTouched"] = readArray(13);
    out["artifacts"] = readArray(14);
    out["evidence"] = readArray(15);
    taskforge::Json signals = parseJsonOrNull(columnText(st.stmt, 16));
    if (!signals.is_object()) signals = taskforge::Json::object();
    sanitizeJsonInPlace(signals);
    out["successSignals"] = signals;
    out["redactions"] = columnInt(st.stmt, 17);
    out["createdAt"] = columnText(st.stmt, 18);
    taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 19));
    if (!metadata.is_object()) metadata = taskforge::Json::object();
    sanitizeJsonInPlace(metadata);
    out["metadata"] = metadata;
    return out;
}

std::string MemoryStore::recordValidation(const MemoryValidationRecord& validation) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || validation.scopeId.empty() || validation.episodeId.empty() ||
        validation.validatorId.empty()) {
        return "";
    }
    taskforge::Json reasons = taskforge::Json::array();
    if (!validation.reasonsJson.empty()) {
        if (auto parsed = taskforge::jsonutil::tryParse(validation.reasonsJson)) {
            if (parsed->is_array()) reasons = *parsed;
        }
    }
    sanitizeJsonInPlace(reasons);
    const std::string id = validation.id.empty() ? newId("val") : validation.id;
    const std::string now = validation.createdAt.empty() ? nowIso() : validation.createdAt;

    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_validation(id,scope_id,run_id,node_id,episode_id,"
                       "validator_id,validator_version,ok,score,reasons_json,artifact_hash,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,episode_id,validator_id,validator_version) DO UPDATE SET "
                       "run_id=excluded.run_id,node_id=excluded.node_id,ok=excluded.ok,"
                       "score=excluded.score,reasons_json=excluded.reasons_json,"
                       "artifact_hash=excluded.artifact_hash,created_at=excluded.created_at", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, validation.scopeId);
    bindNullableText(st.stmt, 3, validation.runId);
    bindNullableText(st.stmt, 4, validation.nodeId);
    bindText(st.stmt, 5, validation.episodeId);
    bindText(st.stmt, 6, validation.validatorId);
    bindText(st.stmt, 7, validation.validatorVersion);
    sqlite3_bind_int(st.stmt, 8, validation.ok ? 1 : 0);
    sqlite3_bind_double(st.stmt, 9, validation.score);
    bindText(st.stmt, 10, reasons.dump());
    bindNullableText(st.stmt, 11, validation.artifactHash);
    bindText(st.stmt, 12, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    Stmt idStmt;
    if (prepare(db_, "SELECT id FROM memory_validation WHERE scope_id=? AND episode_id=? "
                      "AND validator_id=? AND validator_version=?", idStmt, error)) {
        bindText(idStmt.stmt, 1, validation.scopeId);
        bindText(idStmt.stmt, 2, validation.episodeId);
        bindText(idStmt.stmt, 3, validation.validatorId);
        bindText(idStmt.stmt, 4, validation.validatorVersion);
        if (stepRetry(idStmt.stmt) == SQLITE_ROW) return columnText(idStmt.stmt, 0);
    }
    return id;
}

std::vector<MemoryGateSample> MemoryStore::validationGateSamples(const std::string& scopeId,
                                                                const std::string& taskType,
                                                                size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryGateSample> out;
    if (!db_ || taskType.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT e.input_hash,v.validator_id,v.validator_version "
                       "FROM memory_validation v JOIN memory_episode e ON e.id=v.episode_id "
                       "WHERE v.scope_id=? AND e.scope_id=? AND e.task_type=? AND v.ok=1 "
                       "AND e.input_hash<>'' ORDER BY v.created_at DESC LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, taskType);
    sqlite3_bind_int(st.stmt, 4, static_cast<int>(std::min<size_t>(limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back(MemoryGateSample{columnText(st.stmt, 0), columnText(st.stmt, 1),
                                       columnText(st.stmt, 2)});
    }
    return out;
}

std::vector<bool> MemoryStore::validationOutcomes(const std::string& scopeId,
                                                 const std::string& taskType,
                                                 size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<bool> out;
    if (!db_ || taskType.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT v.ok FROM memory_validation v JOIN memory_episode e "
                       "ON e.id=v.episode_id WHERE v.scope_id=? AND e.scope_id=? "
                       "AND e.task_type=? ORDER BY v.created_at DESC LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, taskType);
    sqlite3_bind_int(st.stmt, 4, static_cast<int>(std::min<size_t>(limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnInt(st.stmt, 0) != 0);
    return out;
}

std::vector<MemoryValidationRecord> MemoryStore::listValidations(const std::string& scopeId,
                                                                 const std::string& episodeId,
                                                                 size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryValidationRecord> out;
    if (!db_) return out;
    const size_t maxResults = limit == 0 ? 100 : limit;
    std::string sql = "SELECT id,scope_id,COALESCE(run_id,''),COALESCE(node_id,''),episode_id,"
                      "validator_id,validator_version,ok,COALESCE(score,0),reasons_json,"
                      "COALESCE(artifact_hash,''),created_at FROM memory_validation WHERE scope_id=?";
    if (!episodeId.empty()) sql += " AND episode_id=?";
    sql += " ORDER BY created_at DESC, id DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    int index = 1;
    bindText(st.stmt, index++, scopeId);
    if (!episodeId.empty()) bindText(st.stmt, index++, episodeId);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(maxResults));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryValidationRecord record;
        record.id = columnText(st.stmt, 0);
        record.scopeId = columnText(st.stmt, 1);
        record.runId = columnText(st.stmt, 2);
        record.nodeId = columnText(st.stmt, 3);
        record.episodeId = columnText(st.stmt, 4);
        record.validatorId = columnText(st.stmt, 5);
        record.validatorVersion = columnText(st.stmt, 6);
        record.ok = columnInt(st.stmt, 7) != 0;
        record.score = columnDouble(st.stmt, 8);
        taskforge::Json reasons = parseJsonOrNull(columnText(st.stmt, 9));
        if (!reasons.is_array()) reasons = taskforge::Json::array();
        sanitizeJsonInPlace(reasons);
        record.reasonsJson = reasons.dump();
        record.artifactHash = columnText(st.stmt, 10);
        record.createdAt = columnText(st.stmt, 11);
        out.push_back(std::move(record));
    }
    return out;
}

bool MemoryStore::upsertEmbedding(const std::string& scopeId, const std::string& objectType,
                                  const std::string& objectId, const std::string& model, int dim,
                                  const std::vector<uint8_t>& vector, const std::string& contentHash,
                                  const std::string& domain, const std::string& fingerprint) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || objectType.empty() || objectId.empty() || model.empty()) return false;
    if (dim <= 0 || vector.size() != static_cast<size_t>(dim) * sizeof(float)) return false;
    if (contentHash.empty()) return false;

    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_embedding(scope_id,object_type,object_id,model,dim,vector,"
                       "content_hash,domain,fingerprint,created_at) VALUES(?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,object_type,object_id,model) DO UPDATE SET "
                       "dim=excluded.dim, vector=excluded.vector, content_hash=excluded.content_hash, "
                       "domain=excluded.domain, fingerprint=excluded.fingerprint, "
                       "created_at=excluded.created_at", st, error)) {
        lastError_ = error;
        return false;
    }
    const SanitizeResult cleanDomain = sanitizeText(domain, 128);
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, objectType);
    bindText(st.stmt, 3, objectId);
    bindText(st.stmt, 4, model);
    sqlite3_bind_int(st.stmt, 5, dim);
    sqlite3_bind_blob(st.stmt, 6, vector.data(), static_cast<int>(vector.size()), SQLITE_TRANSIENT);
    bindText(st.stmt, 7, contentHash);
    bindText(st.stmt, 8, cleanDomain.text);
    bindText(st.stmt, 9, fingerprint);
    bindText(st.stmt, 10, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::vector<MemoryEmbeddingRecord> MemoryStore::listEmbeddings(const std::string& scopeId,
                                                               const std::string& model,
                                                               const std::string& fingerprint) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEmbeddingRecord> out;
    if (!db_ || model.empty()) return out;
    Stmt st;
    std::string error;
    std::string sql = "SELECT object_type,object_id,model,dim,vector,content_hash,COALESCE(domain,''),"
                      "COALESCE(fingerprint,''),created_at FROM memory_embedding WHERE scope_id=? AND model=?";
    if (!fingerprint.empty()) sql += " AND fingerprint=?";
    if (!prepare(db_, sql.c_str(), st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, model);
    if (!fingerprint.empty()) bindText(st.stmt, 3, fingerprint);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryEmbeddingRecord record;
        record.objectType = columnText(st.stmt, 0);
        record.objectId = columnText(st.stmt, 1);
        record.model = columnText(st.stmt, 2);
        record.dim = static_cast<int>(columnInt(st.stmt, 3));
        const void* blob = sqlite3_column_blob(st.stmt, 4);
        const int bytes = sqlite3_column_bytes(st.stmt, 4);
        if (blob && bytes > 0) {
            const auto* first = static_cast<const uint8_t*>(blob);
            record.vector.assign(first, first + bytes);
        }
        record.contentHash = columnText(st.stmt, 5);
        record.domain = columnText(st.stmt, 6);
        record.fingerprint = columnText(st.stmt, 7);
        record.createdAt = columnText(st.stmt, 8);
        if (record.dim > 0 &&
            record.vector.size() == static_cast<size_t>(record.dim) * sizeof(float)) {
            out.push_back(std::move(record));
        }
    }
    return out;
}

size_t MemoryStore::embeddingCount(const std::string& scopeId, const std::string& model,
                                   const std::string& fingerprint) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || model.empty()) return 0;
    Stmt st;
    std::string error;
    std::string sql = "SELECT COUNT(*) FROM memory_embedding WHERE scope_id=? AND model=?";
    if (!fingerprint.empty()) sql += " AND fingerprint=?";
    if (!prepare(db_, sql.c_str(), st, error)) {
        lastError_ = error;
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, model);
    if (!fingerprint.empty()) bindText(st.stmt, 3, fingerprint);
    return stepRetry(st.stmt) == SQLITE_ROW ? static_cast<size_t>(columnInt(st.stmt, 0)) : 0;
}

bool MemoryStore::deleteEmbedding(const std::string& scopeId, const std::string& objectType,
                                  const std::string& objectId, const std::string& model) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "DELETE FROM memory_embedding WHERE scope_id=? AND object_type=? "
                       "AND object_id=? AND model=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, objectType);
    bindText(st.stmt, 3, objectId);
    bindText(st.stmt, 4, model);
    return stepRetry(st.stmt) == SQLITE_DONE;
}

std::vector<MemoryEmbeddingObject> MemoryStore::embeddingObjects(const std::string& scopeId,
                                                                 size_t maxEpisodes) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEmbeddingObject> out;
    if (!db_) return out;

    for (const auto& lesson : getLessons(scopeId)) {
        MemoryEmbeddingObject object;
        object.objectType = "lesson";
        object.objectId = lesson.id;
        object.domain = lesson.domain;
        object.text = sanitizeText(lesson.domain + " | " + lesson.triggerError + " | " + lesson.lessonRule,
                                   8000).text;
        object.confidence = lesson.confidence;
        object.createdAt = lesson.createdAt;
        if (!object.text.empty()) out.push_back(std::move(object));
    }

    Stmt edges;
    std::string error;
    if (prepare(db_, "SELECT e.id, s.canonical_name, e.relation, COALESCE(d.canonical_name,''), "
                      "COALESCE(e.value_json,''), e.confidence, e.metadata, e.recorded_at "
                      "FROM memory_edge e JOIN memory_node s ON s.id=e.src_id "
                      "LEFT JOIN memory_node d ON d.id=e.dst_id "
                      "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
                      "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES') "
                      "AND (e.valid_from IS NULL OR e.valid_from <= ?) "
                      "AND (e.valid_to IS NULL OR e.valid_to > ?) LIMIT 2000", edges, error)) {
        const std::string now = nowIso();
        bindText(edges.stmt, 1, scopeId);
        bindText(edges.stmt, 2, now);
        bindText(edges.stmt, 3, now);
        while (stepRetry(edges.stmt) == SQLITE_ROW) {
            MemoryEmbeddingObject object;
            object.objectType = "edge";
            object.objectId = columnText(edges.stmt, 0);
            object.text = sanitizeText(columnText(edges.stmt, 1) + " " + columnText(edges.stmt, 2) + " " +
                                       columnText(edges.stmt, 3) + " " + columnText(edges.stmt, 4),
                                       8000).text;
            object.confidence = columnDouble(edges.stmt, 5);
            taskforge::Json metadata = parseJsonOrNull(columnText(edges.stmt, 6));
            object.domain = metadata.is_object() ? metadata.value("domain", std::string()) : std::string();
            object.createdAt = columnText(edges.stmt, 7);
            if (!object.text.empty()) out.push_back(std::move(object));
        }
    }

    Stmt playbooks;
    if (prepare(db_, "SELECT id,task_type,title,steps_json,preconditions_json,confidence,updated_at "
                      "FROM memory_playbook WHERE scope_id=? AND status='active' LIMIT 500", playbooks, error)) {
        bindText(playbooks.stmt, 1, scopeId);
        while (stepRetry(playbooks.stmt) == SQLITE_ROW) {
            MemoryEmbeddingObject object;
            object.objectType = "playbook";
            object.objectId = columnText(playbooks.stmt, 0);
            object.domain = columnText(playbooks.stmt, 1);
            object.text = sanitizeText(columnText(playbooks.stmt, 2) + " " + columnText(playbooks.stmt, 3) +
                                       " " + columnText(playbooks.stmt, 4), 8000).text;
            object.confidence = columnDouble(playbooks.stmt, 5);
            object.createdAt = columnText(playbooks.stmt, 6);
            if (!object.text.empty()) out.push_back(std::move(object));
        }
    }

    Stmt nodes;
    if (prepare(db_, "SELECT id,canonical_name,aliases_json,metadata,updated_at FROM memory_node "
                      "WHERE scope_id=? AND kind IN ('summary') LIMIT 500", nodes, error)) {
        bindText(nodes.stmt, 1, scopeId);
        while (stepRetry(nodes.stmt) == SQLITE_ROW) {
            MemoryEmbeddingObject object;
            object.objectType = "node";
            object.objectId = columnText(nodes.stmt, 0);
            object.text = sanitizeText(columnText(nodes.stmt, 1) + " " + columnText(nodes.stmt, 2) +
                                       " " + columnText(nodes.stmt, 3), 8000).text;
            taskforge::Json metadata = parseJsonOrNull(columnText(nodes.stmt, 3));
            object.domain = metadata.is_object() ? metadata.value("domain", std::string()) : std::string();
            object.createdAt = columnText(nodes.stmt, 4);
            if (!object.text.empty()) out.push_back(std::move(object));
        }
    }

    if (maxEpisodes > 0) {
        Stmt episodes;
        if (prepare(db_, "SELECT id,task_type,outcome,content,created_at FROM memory_episode "
                          "WHERE scope_id=? AND kind LIKE '%summary%' ORDER BY created_at DESC LIMIT ?",
                    episodes, error)) {
            bindText(episodes.stmt, 1, scopeId);
            sqlite3_bind_int(episodes.stmt, 2, static_cast<int>(std::min<size_t>(maxEpisodes, 1000)));
            while (stepRetry(episodes.stmt) == SQLITE_ROW) {
                MemoryEmbeddingObject object;
                object.objectType = "episode";
                object.objectId = columnText(episodes.stmt, 0);
                object.domain = columnText(episodes.stmt, 1);
                object.text = sanitizeText(columnText(episodes.stmt, 1) + " " + columnText(episodes.stmt, 2) +
                                           " " + columnText(episodes.stmt, 3), 8000).text;
                object.createdAt = columnText(episodes.stmt, 4);
                if (!object.text.empty()) out.push_back(std::move(object));
            }
        }
    }

    return out;
}

std::vector<MemorySearchCandidate> MemoryStore::lexicalCandidates(
    const std::string& scopeId, const std::string& text, size_t limit,
    const std::vector<std::string>& domains) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemorySearchCandidate> out;
    if (!db_ || text.empty()) return out;
    const size_t maxResults = std::max<size_t>(1, limit);
    auto allowed = [&](const std::string& domain) {
        return domains.empty() || std::find(domains.begin(), domains.end(), domain) != domains.end();
    };

    std::string needle = text;
    std::transform(needle.begin(), needle.end(), needle.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& lesson : getLessons(scopeId)) {
        if (!allowed(lesson.domain)) continue;
        std::string hay = lesson.domain + " " + lesson.lessonRule + " " + lesson.triggerError;
        std::transform(hay.begin(), hay.end(), hay.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (hay.find(needle) != std::string::npos) {
            MemorySearchCandidate candidate;
            candidate.id = lesson.id;
            candidate.kind = "lesson";
            candidate.text = "(Dominio: " + lesson.domain + ") " + lesson.lessonRule;
            candidate.domain = lesson.domain;
            candidate.confidence = lesson.confidence;
            candidate.createdAt = lesson.createdAt;
            out.push_back(std::move(candidate));
            if (out.size() >= maxResults) break;
        }
    }

    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    const std::string match = "\"" + escaped + "\"";
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT e.id, s.canonical_name, e.relation, COALESCE(d.canonical_name, e.value_json, ''), "
                      "e.valid_from, e.valid_to, e.confidence, e.metadata, e.recorded_at "
                      "FROM memory_edge e JOIN memory_node s ON s.id=e.src_id "
                      "LEFT JOIN memory_node d ON d.id=e.dst_id "
                      "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
                      "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES') AND ("
                      "  e.src_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
                      "  e.dst_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
                      "  e.value_json LIKE ?"
                      ") AND (e.valid_from IS NULL OR e.valid_from <= ?) "
                      "  AND (e.valid_to IS NULL OR e.valid_to > ?) "
                      "ORDER BY e.recorded_at DESC LIMIT ?", st, error)) {
        const std::string now = nowIso();
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, match);
        bindText(st.stmt, 3, match);
        bindText(st.stmt, 4, "%" + text + "%");
        bindText(st.stmt, 5, now);
        bindText(st.stmt, 6, now);
        sqlite3_bind_int(st.stmt, 7, static_cast<int>(std::max<size_t>(maxResults * 4, 40)));
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 7));
            const std::string domain = metadata.is_object()
                ? metadata.value("domain", std::string()) : std::string();
            if (!domains.empty() && !allowed(domain)) continue;
            MemorySearchCandidate candidate;
            candidate.id = columnText(st.stmt, 0);
            candidate.kind = "fact";
            candidate.text = columnText(st.stmt, 1) + " --" + columnText(st.stmt, 2) + "--> " +
                             columnText(st.stmt, 3);
            candidate.domain = domain;
            candidate.confidence = columnDouble(st.stmt, 6);
            candidate.createdAt = columnText(st.stmt, 8);
            out.push_back(std::move(candidate));
            if (out.size() >= maxResults) break;
        }
    }

    if (out.size() < maxResults) {
        Stmt playbooks;
        if (prepare(db_, "SELECT id,task_type,title,steps_json,preconditions_json,confidence,updated_at "
                          "FROM memory_playbook WHERE scope_id=? AND status='active' AND "
                          "(title LIKE ? OR task_type LIKE ? OR steps_json LIKE ?) LIMIT ?",
                    playbooks, error)) {
            bindText(playbooks.stmt, 1, scopeId);
            bindText(playbooks.stmt, 2, "%" + text + "%");
            bindText(playbooks.stmt, 3, "%" + text + "%");
            bindText(playbooks.stmt, 4, "%" + text + "%");
            sqlite3_bind_int(playbooks.stmt, 5, static_cast<int>(maxResults));
            while (stepRetry(playbooks.stmt) == SQLITE_ROW) {
                MemorySearchCandidate candidate;
                candidate.id = columnText(playbooks.stmt, 0);
                candidate.kind = "playbook";
                candidate.domain = columnText(playbooks.stmt, 1);
                candidate.text = columnText(playbooks.stmt, 2) + " " + columnText(playbooks.stmt, 3) +
                                 " " + columnText(playbooks.stmt, 4);
                candidate.confidence = columnDouble(playbooks.stmt, 5);
                candidate.createdAt = columnText(playbooks.stmt, 6);
                if (!allowed(candidate.domain)) continue;
                out.push_back(std::move(candidate));
                if (out.size() >= maxResults) break;
            }
        }
    }
    return out;
}

std::string MemoryStore::recordPlaybook(const std::string& scopeId, const std::string& taskType,
                                        const std::string& title, const taskforge::Json& steps,
                                        const taskforge::Json& preconditions, double confidence,
                                        const std::string& status, const std::string& sourceRef,
                                        const taskforge::Json& evidence,
                                        long long successCount, long long failureCount) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || taskType.empty()) return "";

    const SanitizeResult cleanTask = sanitizeText(taskType, 256);
    const SanitizeResult cleanTitle = sanitizeText(title.empty() ? taskType : title, 512);
    taskforge::Json cleanSteps = steps.is_array() ? steps : taskforge::Json::array();
    taskforge::Json cleanPre = preconditions.is_object() ? preconditions : taskforge::Json::object();
    taskforge::Json cleanEvidence = evidence.is_array() ? evidence : taskforge::Json::array();
    sanitizeJsonInPlace(cleanSteps);
    sanitizeJsonInPlace(cleanPre);
    sanitizeJsonInPlace(cleanEvidence);
    const SanitizeResult cleanSource = sanitizeText(sourceRef, 4000);

    const std::string id = newId("playbook");
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_playbook(id,scope_id,task_type,title,steps_json,preconditions_json,"
                       "success_count,failure_count,confidence,status,source_ref,evidence_json,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, cleanTask.text);
    bindText(st.stmt, 4, cleanTitle.text);
    bindText(st.stmt, 5, cleanSteps.dump());
    bindText(st.stmt, 6, cleanPre.dump());
    sqlite3_bind_int64(st.stmt, 7, std::max<long long>(0, successCount));
    sqlite3_bind_int64(st.stmt, 8, std::max<long long>(0, failureCount));
    sqlite3_bind_double(st.stmt, 9, std::max(0.0, std::min(1.0, confidence)));
    bindText(st.stmt, 10, status.empty() ? "candidate" : status);
    bindNullableText(st.stmt, 11, cleanSource.text);
    bindText(st.stmt, 12, cleanEvidence.dump());
    bindText(st.stmt, 13, now);
    bindText(st.stmt, 14, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    audit("playbook", "record_playbook", id, "", cleanTask.text);
    return id;
}

std::string MemoryStore::playbookIdBySignature(const std::string& scopeId,
                                               const std::string& taskType,
                                               const std::string& stepsJson) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id FROM memory_playbook WHERE scope_id=? AND task_type=? "
                       "AND steps_json=? AND status IN ('candidate','active') "
                       "ORDER BY success_count DESC, updated_at DESC LIMIT 1", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, taskType);
    bindText(st.stmt, 3, stepsJson);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnText(st.stmt, 0) : std::string();
}

std::vector<MemoryPlaybookRecord> MemoryStore::listPlaybooks(const std::string& scopeId,
                                                             const std::string& taskType,
                                                             const std::string& status,
                                                             size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryPlaybookRecord> out;
    if (!db_) return out;
    std::string sql = "SELECT id,scope_id,task_type,title,steps_json,preconditions_json,"
                      "success_count,failure_count,confidence,status,COALESCE(source_ref,''),"
                      "evidence_json,created_at,updated_at FROM memory_playbook WHERE scope_id=?";
    if (!taskType.empty()) sql += " AND task_type=?";
    if (!status.empty() && status != "all") sql += " AND status=?";
    sql += " ORDER BY success_count DESC, confidence DESC, updated_at DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    int index = 1;
    bindText(st.stmt, index++, scopeId);
    if (!taskType.empty()) bindText(st.stmt, index++, taskType);
    if (!status.empty() && status != "all") bindText(st.stmt, index++, status);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(std::min<size_t>(limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryPlaybookRecord record;
        record.id = columnText(st.stmt, 0);
        record.scopeId = columnText(st.stmt, 1);
        record.taskType = columnText(st.stmt, 2);
        record.title = columnText(st.stmt, 3);
        record.stepsJson = columnText(st.stmt, 4);
        record.preconditionsJson = columnText(st.stmt, 5);
        record.successCount = columnInt(st.stmt, 6);
        record.failureCount = columnInt(st.stmt, 7);
        record.confidence = columnDouble(st.stmt, 8);
        record.status = columnText(st.stmt, 9);
        record.sourceRef = columnText(st.stmt, 10);
        record.evidenceJson = columnText(st.stmt, 11);
        record.createdAt = columnText(st.stmt, 12);
        record.updatedAt = columnText(st.stmt, 13);
        out.push_back(std::move(record));
    }
    return out;
}

bool MemoryStore::getPlaybook(const std::string& scopeId, const std::string& playbookId,
                              MemoryPlaybookRecord& out) const {
    for (auto& record : listPlaybooks(scopeId, "", "all", 1000)) {
        if (record.id == playbookId) {
            out = std::move(record);
            return true;
        }
    }
    return false;
}

bool MemoryStore::updatePlaybook(const std::string& scopeId, const std::string& playbookId,
                                 long long successDelta, long long failureDelta,
                                 double confidence, const std::string& status,
                                 const std::string& sourceRef,
                                 const taskforge::Json& evidence, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPlaybookRecord current;
    if (!getPlaybook(scopeId, playbookId, current)) {
        error = "playbook nao encontrado";
        return false;
    }
    taskforge::Json evidenceArray = parseJsonOrNull(current.evidenceJson);
    if (!evidenceArray.is_array()) evidenceArray = taskforge::Json::array();
    std::set<std::string> seen;
    for (const auto& item : evidenceArray) seen.insert(item.dump());
    if (evidence.is_array()) {
        for (const auto& item : evidence) {
            if (seen.insert(item.dump()).second) evidenceArray.push_back(item);
        }
    }
    sanitizeJsonInPlace(evidenceArray);

    Stmt st;
    if (!prepare(db_, "UPDATE memory_playbook SET success_count=?,failure_count=?,confidence=?,"
                       "status=?,source_ref=COALESCE(?,source_ref),evidence_json=?,updated_at=? "
                       "WHERE scope_id=? AND id=?", st, error)) {
        return false;
    }
    sqlite3_bind_int64(st.stmt, 1, std::max<long long>(0, current.successCount + successDelta));
    sqlite3_bind_int64(st.stmt, 2, std::max<long long>(0, current.failureCount + failureDelta));
    sqlite3_bind_double(st.stmt, 3, std::max(0.0, std::min(1.0, confidence)));
    bindText(st.stmt, 4, status.empty() ? current.status : status);
    bindNullableText(st.stmt, 5, sourceRef);
    bindText(st.stmt, 6, evidenceArray.dump());
    bindText(st.stmt, 7, nowIso());
    bindText(st.stmt, 8, scopeId);
    bindText(st.stmt, 9, playbookId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    audit("playbook", "update_playbook", playbookId, "", status);
    return true;
}

bool MemoryStore::autoPromotePlaybook(const std::string& scopeId, const std::string& episodeId,
                                      const std::string& validatorId,
                                      const std::string& validatorVersion,
                                      const std::vector<MemoryGateSample>& samples,
                                      std::string& promotedId, std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    promotedId.clear();
    reason.clear();
    if (!db_ || scopeId.empty() || episodeId.empty() || validatorId.empty()) {
        reason = "validacao incompleta";
        return false;
    }

    Stmt find;
    std::string error;
    if (!prepare(db_, "SELECT id,task_type,confidence,evidence_json FROM memory_playbook "
                       "WHERE scope_id=? AND source_ref=? AND status='candidate' "
                       "ORDER BY updated_at DESC LIMIT 1", find, error)) {
        reason = error;
        return false;
    }
    bindText(find.stmt, 1, scopeId);
    bindText(find.stmt, 2, episodeId);
    if (stepRetry(find.stmt) != SQLITE_ROW) {
        reason = "sem playbook candidato para o episodio";
        return false;
    }
    const std::string playbookId = columnText(find.stmt, 0);
    const std::string taskType = columnText(find.stmt, 1);
    const double confidence = columnDouble(find.stmt, 2);
    const std::string evidenceJson = columnText(find.stmt, 3);

    // Amostras das ultimas execucoes validadas do mesmo task_type.
    std::set<std::string> inputs;
    bool validatorStable = true;
    for (const auto& sample : samples) {
        inputs.insert(sample.inputHash);
        if (sample.validatorId != validatorId ||
            sample.validatorVersion != validatorVersion) {
            validatorStable = false;
        }
    }
    if (samples.empty() || !validatorStable) {
        reason = "validador mudou; playbook volta a candidato";
        return false;
    }
    if (inputs.size() < 3) {
        reason = "entradas distintas insuficientes (" + std::to_string(inputs.size()) + "/3)";
        return false;
    }

    // No maximo uma promocao por task_type em janela movel de 24h.
    Stmt rate;
    if (!prepare(db_, "SELECT 1 FROM memory_audit WHERE action='auto_promote' AND target=? "
                       "AND created_at>=strftime('%Y-%m-%dT%H:%M:%SZ','now','-1 day') LIMIT 1",
                 rate, error)) {
        reason = error;
        return false;
    }
    bindText(rate.stmt, 1, taskType);
    if (stepRetry(rate.stmt) == SQLITE_ROW) {
        reason = "limite de 1 promocao por task_type em 24h";
        return false;
    }

    taskforge::Json evidence = parseJsonOrNull(evidenceJson);
    if (!evidence.is_array()) evidence = taskforge::Json::array();
    evidence.push_back(taskforge::Json{{"kind", "validation"},
                                       {"episodeId", episodeId},
                                       {"validatorId", validatorId},
                                       {"validatorVersion", validatorVersion}});
    sanitizeJsonInPlace(evidence);

    Stmt update;
    if (!prepare(db_, "UPDATE memory_playbook SET status='active',"
                       "confidence=?,evidence_json=?,updated_at=? "
                       "WHERE scope_id=? AND id=?", update, error)) {
        reason = error;
        return false;
    }
    sqlite3_bind_double(update.stmt, 1, std::min(1.0, confidence + 0.1));
    bindText(update.stmt, 2, evidence.dump());
    bindText(update.stmt, 3, nowIso());
    bindText(update.stmt, 4, scopeId);
    bindText(update.stmt, 5, playbookId);
    if (stepRetry(update.stmt) != SQLITE_DONE) {
        reason = sqlite3_errmsg(db_);
        return false;
    }
    audit("gate", "auto_promote", taskType, "", playbookId);
    promotedId = playbookId;
    return true;
}

bool MemoryStore::autoDemotePlaybooks(const std::string& scopeId, const std::string& taskType,
                                      const std::vector<bool>& outcomes, std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reason.clear();
    if (!db_ || taskType.empty() || outcomes.empty()) return false;
    bool twoFailures = outcomes.size() >= 2 && !outcomes[0] && !outcomes[1];
    size_t failures = 0;
    for (bool ok : outcomes) if (!ok) ++failures;
    bool highFailureRate = outcomes.size() >= 10 && failures * 10 > outcomes.size() * 3;
    if (!twoFailures && !highFailureRate) return false;

    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_playbook SET status='candidate',updated_at=? "
                       "WHERE scope_id=? AND task_type=? AND status='active'", st, error)) {
        reason = error;
        return false;
    }
    bindText(st.stmt, 1, nowIso());
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, taskType);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        reason = sqlite3_errmsg(db_);
        return false;
    }
    if (sqlite3_changes(db_) == 0) return false;
    reason = twoFailures ? "duas falhas consecutivas" : "taxa de falha acima de 30%";
    audit("gate", "auto_demote", taskType, "", reason);
    return true;
}

bool MemoryStore::unpromotePlaybook(const std::string& scopeId, const std::string& playbookId,
                                    const std::string& reason, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || playbookId.empty()) {
        error = "playbook invalido";
        return false;
    }
    Stmt st;
    if (!prepare(db_, "UPDATE memory_playbook SET status='candidate',updated_at=? "
                       "WHERE scope_id=? AND id=? AND status='active'", st, error)) {
        return false;
    }
    bindText(st.stmt, 1, nowIso());
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, playbookId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    if (sqlite3_changes(db_) == 0) {
        error = "playbook ativo nao encontrado";
        return false;
    }
    audit("gate", "unpromote", playbookId, "", reason);
    return true;
}

std::string MemoryStore::recordProposal(const std::string& scopeId, const std::string& kind,
                                        const taskforge::Json& payload,
                                        const std::string& sourceEpisodeId,
                                        const taskforge::Json& evidence,
                                        double confidence) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || kind.empty()) return "";

    taskforge::Json cleanPayload = payload.is_object() ? payload : taskforge::Json::object();
    taskforge::Json cleanEvidence = evidence.is_array() ? evidence : taskforge::Json::array();
    sanitizeJsonInPlace(cleanPayload);
    sanitizeJsonInPlace(cleanEvidence);
    const std::string payloadDump = cleanPayload.dump();
    const double clamped = std::max(0.0, std::min(1.0, confidence));

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_proposal WHERE scope_id=? AND kind=? AND payload_json=? "
                       "AND status='pending' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, kind);
        bindText(existing.stmt, 3, payloadDump);
        if (stepRetry(existing.stmt) == SQLITE_ROW) return columnText(existing.stmt, 0);
    }

    const std::string id = newId("prop");
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_proposal(id,scope_id,kind,payload_json,source_episode_id,evidence_json,"
                       "confidence,status,created_at) VALUES(?,?,?,?,?,?,?,'pending',?)", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, kind);
    bindText(st.stmt, 4, payloadDump);
    bindNullableText(st.stmt, 5, sourceEpisodeId);
    bindText(st.stmt, 6, cleanEvidence.dump());
    sqlite3_bind_double(st.stmt, 7, clamped);
    bindText(st.stmt, 8, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    audit("extractor", "proposal", scopeId, "", kind + ":" + id);
    return id;
}

std::vector<MemoryProposalRecord> MemoryStore::listProposals(const std::string& scopeId,
                                                             const std::string& status,
                                                             size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryProposalRecord> out;
    if (!db_) return out;

    std::string sql = "SELECT id,scope_id,kind,payload_json,COALESCE(source_episode_id,''),evidence_json,"
                      "confidence,status,COALESCE(reviewed_at,''),COALESCE(reviewed_by,''),created_at "
                      "FROM memory_proposal WHERE scope_id=?";
    if (!status.empty()) sql += " AND status=?";
    sql += " ORDER BY created_at DESC, id DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    int index = 2;
    if (!status.empty()) bindText(st.stmt, index++, status);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(std::min<size_t>(limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryProposalRecord proposal;
        proposal.id = columnText(st.stmt, 0);
        proposal.scopeId = columnText(st.stmt, 1);
        proposal.kind = columnText(st.stmt, 2);
        proposal.payloadJson = sanitizeText(columnText(st.stmt, 3), 8000).text;
        proposal.sourceEpisodeId = columnText(st.stmt, 4);
        proposal.evidenceJson = sanitizeText(columnText(st.stmt, 5), 8000).text;
        proposal.confidence = columnDouble(st.stmt, 6);
        proposal.status = columnText(st.stmt, 7);
        proposal.reviewedAt = columnText(st.stmt, 8);
        proposal.reviewedBy = columnText(st.stmt, 9);
        proposal.createdAt = columnText(st.stmt, 10);
        out.push_back(std::move(proposal));
    }
    return out;
}

bool MemoryStore::loadProposal(const std::string& scopeId, const std::string& proposalId,
                               MemoryProposalRecord& out) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || proposalId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,kind,payload_json,COALESCE(source_episode_id,''),evidence_json,"
                       "confidence,status,COALESCE(reviewed_at,''),COALESCE(reviewed_by,''),created_at "
                       "FROM memory_proposal WHERE scope_id=? AND id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, proposalId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return false;
    out.id = columnText(st.stmt, 0);
    out.scopeId = columnText(st.stmt, 1);
    out.kind = columnText(st.stmt, 2);
    out.payloadJson = columnText(st.stmt, 3);
    out.sourceEpisodeId = columnText(st.stmt, 4);
    out.evidenceJson = columnText(st.stmt, 5);
    out.confidence = columnDouble(st.stmt, 6);
    out.status = columnText(st.stmt, 7);
    out.reviewedAt = columnText(st.stmt, 8);
    out.reviewedBy = columnText(st.stmt, 9);
    out.createdAt = columnText(st.stmt, 10);
    return true;
}

bool MemoryStore::promoteProposal(const std::string& scopeId, const std::string& proposalId,
                                  const std::string& reviewer, std::string& error,
                                  std::string* promotedId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco de memoria fechado";
        return false;
    }
    MemoryProposalRecord proposal;
    if (!loadProposal(scopeId, proposalId, proposal)) {
        error = "proposta nao encontrada";
        return false;
    }
    if (proposal.status != "pending") {
        error = "proposta nao esta pendente: " + proposal.status;
        return false;
    }

    taskforge::Json payload = parseJsonOrNull(proposal.payloadJson);
    taskforge::Json evidence = parseJsonOrNull(proposal.evidenceJson);
    if (!payload.is_object()) {
        error = "payload da proposta invalido";
        return false;
    }
    if (proposal.kind != "lesson" && (!evidence.is_array() || evidence.empty())) {
        error = "proposta sem evidencia nao pode ser promovida";
        return false;
    }

    std::string promoted;
    if (proposal.kind == "lesson") {
        const std::string domain = payload.value("domain", std::string("geral"));
        const std::string trigger = payload.value("triggerError", std::string());
        const std::string rule = payload.value("lessonRule", payload.value("rule", std::string()));
        if (rule.empty()) {
            error = "proposta de licao sem regra";
            return false;
        }
        promoted = recordLesson(scopeId, domain, trigger, rule, proposal.sourceEpisodeId, "promotion");
        if (promoted.empty()) {
            error = "falha ao gravar licao promovida";
            return false;
        }
    } else if (proposal.kind == "fact") {
        const std::string subject = payload.value("subject", std::string());
        const std::string predicate = payload.value("predicate", std::string());
        const std::string object = payload.value("object", std::string());
        if (subject.empty() || predicate.empty() || object.empty()) {
            error = "fato promovido sem subject/predicate/object";
            return false;
        }
        const std::string subjectId = resolveNode(scopeId, subject, "entity");
        const std::string objectId = resolveNode(scopeId, object, "entity");
        if (subjectId.empty() || objectId.empty()) {
            error = "falha ao criar entidades do fato promovido";
            return false;
        }

        Stmt existing;
        if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND dst_id=? "
                           "AND relation=? AND status='active' LIMIT 1", existing, error)) {
            bindText(existing.stmt, 1, scopeId);
            bindText(existing.stmt, 2, subjectId);
            bindText(existing.stmt, 3, objectId);
            bindText(existing.stmt, 4, predicate);
            if (stepRetry(existing.stmt) == SQLITE_ROW) {
                promoted = columnText(existing.stmt, 0);
            }
        }
        if (promoted.empty() && isSingleValuedPredicate(predicate)) {
            // C5: um predicado funcional aceita um unico valor ativo. O valor
            // anterior e encerrado, nao apagado, e fica no historico.
            Stmt previous;
            std::vector<std::string> previousIds;
            if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND relation=? "
                               "AND status='active' AND (dst_id IS NULL OR dst_id<>?)", previous, error)) {
                bindText(previous.stmt, 1, scopeId);
                bindText(previous.stmt, 2, subjectId);
                bindText(previous.stmt, 3, predicate);
                bindText(previous.stmt, 4, objectId);
                while (stepRetry(previous.stmt) == SQLITE_ROW) {
                    previousIds.push_back(columnText(previous.stmt, 0));
                }
            }
            for (const auto& previousId : previousIds) {
                invalidateEdge(previousId, proposal.createdAt, "single_valued_predicate", "promotion");
            }
        }
        if (promoted.empty()) {
            taskforge::Json metadata = taskforge::Json::object();
            metadata["proposalId"] = proposal.id;
            metadata["evidence"] = evidence;
            promoted = recordEdge(scopeId, subjectId, objectId, predicate, taskforge::Json(),
                                  proposal.createdAt, "", proposal.confidence, "active",
                                  "promotion", proposal.id, metadata);
        }
        if (promoted.empty()) {
            error = "falha ao gravar fato promovido";
            return false;
        }
    } else if (proposal.kind == "entity") {
        const std::string name = payload.value("name", std::string());
        const std::string entityKind = payload.value("entityKind", std::string("entity"));
        if (name.empty()) {
            error = "entidade promovida sem nome";
            return false;
        }
        promoted = resolveNode(scopeId, name, entityKind);
        if (promoted.empty()) {
            error = "falha ao criar entidade promovida";
            return false;
        }
    } else if (proposal.kind == "playbook") {
        const std::string taskType = payload.value("taskType", std::string("geral"));
        const std::string title = payload.value("title", taskType);
        taskforge::Json steps = payload.contains("steps") ? payload["steps"] : taskforge::Json::array();
        taskforge::Json preconditions = payload.contains("preconditions") ? payload["preconditions"] : taskforge::Json::object();
        sanitizeJsonInPlace(steps);
        sanitizeJsonInPlace(preconditions);
        const std::string stepsJson = steps.dump();
        const std::string existingPlaybook = playbookIdBySignature(scopeId, taskType, stepsJson);
        if (!existingPlaybook.empty()) {
            MemoryPlaybookRecord current;
            double confidence = proposal.confidence;
            if (getPlaybook(scopeId, existingPlaybook, current)) {
                confidence = std::max(current.confidence, proposal.confidence);
            }
            if (!updatePlaybook(scopeId, existingPlaybook, 1, 0, confidence, "active",
                                proposal.sourceEpisodeId, evidence, error)) {
                return false;
            }
            promoted = existingPlaybook;
        } else {
            promoted = recordPlaybook(scopeId, taskType, title, steps, preconditions,
                                      proposal.confidence, "active",
                                      proposal.sourceEpisodeId, evidence, 1, 0);
            if (promoted.empty()) {
                error = "falha ao gravar playbook promovido";
                return false;
            }
        }
    } else if (proposal.kind == "summary") {
        const std::string title = payload.value("title", payload.value("taskType", std::string("summary")));
        if (title.empty()) {
            error = "sumario promovido sem titulo";
            return false;
        }
        promoted = resolveNode(scopeId, title, "summary");
        if (promoted.empty()) {
            error = "falha ao criar sumario promovido";
            return false;
        }
    } else {
        error = "tipo de proposta desconhecido: " + proposal.kind;
        return false;
    }

    Stmt update;
    if (!prepare(db_, "UPDATE memory_proposal SET status='accepted', reviewed_at=?, reviewed_by=? "
                       "WHERE scope_id=? AND id=? AND status='pending'", update, error)) {
        return false;
    }
    bindText(update.stmt, 1, nowIso());
    bindText(update.stmt, 2, reviewer.empty() ? "human" : reviewer);
    bindText(update.stmt, 3, scopeId);
    bindText(update.stmt, 4, proposal.id);
    if (stepRetry(update.stmt) != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        error = "proposta mudou de estado durante a promocao";
        return false;
    }
    audit(reviewer.empty() ? "human" : reviewer, "promote_proposal", proposal.id, "", promoted);
    if (promotedId) *promotedId = promoted;
    return true;
}

bool MemoryStore::rejectProposal(const std::string& scopeId, const std::string& proposalId,
                                 const std::string& reviewer, const std::string& reason,
                                 std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco de memoria fechado";
        return false;
    }
    Stmt st;
    if (!prepare(db_, "UPDATE memory_proposal SET status='rejected', reviewed_at=?, reviewed_by=? "
                       "WHERE scope_id=? AND id=? AND status='pending'", st, error)) {
        return false;
    }
    bindText(st.stmt, 1, nowIso());
    bindText(st.stmt, 2, reviewer.empty() ? "human" : reviewer);
    bindText(st.stmt, 3, scopeId);
    bindText(st.stmt, 4, proposalId);
    if (stepRetry(st.stmt) != SQLITE_DONE || sqlite3_changes(db_) != 1) {
        error = "proposta nao encontrada ou nao esta pendente";
        return false;
    }
    audit(reviewer.empty() ? "human" : reviewer, "reject_proposal", proposalId, "", reason);
    return true;
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
    if (stepRetry(st.stmt) != SQLITE_DONE) {
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
        "  WHERE w.depth < ? AND e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
        "    AND (e.valid_from IS NULL OR e.valid_from <= ?) "
        "    AND (e.valid_to IS NULL OR e.valid_to > ?) "
        ") "
        "SELECT e.id,e.scope_id,e.src_id,e.dst_id,e.relation,e.value_json,e.valid_from,e.valid_to,"
        "e.recorded_at,e.confidence,e.status,e.superseded_by,e.source_kind,e.source_ref,e.metadata "
        "FROM memory_edge e JOIN walk w ON e.src_id=w.node_id "
        "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
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
    while (stepRetry(st.stmt) == SQLITE_ROW) {
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
        "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL AND ("
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
    while (stepRetry(st.stmt) == SQLITE_ROW) {
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

std::vector<MemoryEdgeRecord> MemoryStore::listEdges(const std::string& scopeId,
                                                     const std::string& status,
                                                     size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEdgeRecord> out;
    if (!db_) return out;
    std::string sql = "SELECT id,scope_id,src_id,COALESCE(dst_id,''),relation,COALESCE(value_json,''),"
                      "COALESCE(valid_from,''),COALESCE(valid_to,''),recorded_at,confidence,status,"
                      "COALESCE(superseded_by,''),source_kind,COALESCE(source_ref,''),metadata,"
                      "COALESCE(last_used,''),success_count,failure_count,utility,COALESCE(dormant_at,'') "
                      "FROM memory_edge WHERE scope_id=?";
    if (!status.empty()) sql += " AND status=?";
    sql += " ORDER BY recorded_at,id LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    int index = 1;
    bindText(st.stmt, index++, scopeId);
    if (!status.empty()) bindText(st.stmt, index++, status);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(std::min<size_t>(limit, 20000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
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
        edge.lastUsed = columnText(st.stmt, 15);
        edge.successCount = columnInt(st.stmt, 16);
        edge.failureCount = columnInt(st.stmt, 17);
        edge.utility = columnDouble(st.stmt, 18);
        edge.dormantAt = columnText(st.stmt, 19);
        out.push_back(std::move(edge));
    }
    return out;
}

bool MemoryStore::getEdge(const std::string& scopeId, const std::string& edgeId,
                          MemoryEdgeRecord& out) const {
    for (auto& edge : listEdges(scopeId, "", 20000)) {
        if (edge.id == edgeId) {
            out = std::move(edge);
            return true;
        }
    }
    return false;
}

bool MemoryStore::updateEdgeState(const std::string& scopeId, const std::string& edgeId,
                                  const std::string& status, const std::string& supersededBy,
                                  double confidence, const taskforge::Json& metadataPatch,
                                  const std::string& validTo, const std::string& dormantAt,
                                  const std::string& lastUsed, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryEdgeRecord current;
    if (!getEdge(scopeId, edgeId, current)) {
        error = "aresta nao encontrada";
        return false;
    }
    taskforge::Json metadata = parseJsonOrNull(current.metadata);
    if (!metadata.is_object()) metadata = taskforge::Json::object();
    if (metadataPatch.is_object()) {
        for (auto it = metadataPatch.begin(); it != metadataPatch.end(); ++it) {
            metadata[it.key()] = it.value();
        }
    }
    sanitizeJsonInPlace(metadata);

    Stmt st;
    if (!prepare(db_, "UPDATE memory_edge SET status=?,superseded_by=?,confidence=?,metadata=?,"
                       "valid_to=?,dormant_at=?,last_used=? WHERE scope_id=? AND id=?", st, error)) {
        return false;
    }
    bindText(st.stmt, 1, status.empty() ? current.status : status);
    bindNullableText(st.stmt, 2, supersededBy);
    sqlite3_bind_double(st.stmt, 3, std::max(0.0, std::min(1.0, confidence)));
    bindText(st.stmt, 4, metadata.dump());
    bindNullableText(st.stmt, 5, validTo);
    bindNullableText(st.stmt, 6, dormantAt);
    bindNullableText(st.stmt, 7, lastUsed);
    bindText(st.stmt, 8, scopeId);
    bindText(st.stmt, 9, edgeId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::unordered_map<std::string, std::string> MemoryStore::nodeNames(
    const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::unordered_map<std::string, std::string> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,canonical_name FROM memory_node WHERE scope_id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out[columnText(st.stmt, 0)] = columnText(st.stmt, 1);
    }
    return out;
}

std::string MemoryStore::searchContext(const std::string& scopeId, const std::string& text,
                                       size_t limit, const std::vector<std::string>& domains,
                                       std::vector<MemoryHitRef>* hits) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || text.empty()) return "";

    const size_t maxResults = limit == 0 ? 10 : limit;
    auto domainAllowed = [&](const std::string& domain) {
        return domains.empty() ||
               std::find(domains.begin(), domains.end(), domain) != domains.end();
    };

    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    const std::string match = "\"" + escaped + "\"";
    std::ostringstream out;
    bool any = false;
    size_t emitted = 0;

    for (const auto& lesson : getLessons(scopeId)) {
        if (!domainAllowed(lesson.domain)) continue;
        std::string hay = lesson.domain + " " + lesson.lessonRule + " " + lesson.triggerError;
        std::string needle = text;
        std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (hay.find(needle) != std::string::npos) {
            out << "[LICAO] (" << lesson.domain << ") " << lesson.lessonRule << "\n";
            if (hits) hits->push_back(MemoryHitRef{lesson.id, "lesson"});
            any = true;
            if (++emitted >= maxResults) break;
        }
    }

    if (emitted < maxResults) {
        const char* sql =
            "SELECT e.id, s.canonical_name, e.relation, COALESCE(d.canonical_name, e.value_json, ''), "
            "e.valid_from, e.valid_to, e.confidence, e.metadata "
            "FROM memory_edge e "
            "JOIN memory_node s ON s.id=e.src_id "
            "LEFT JOIN memory_node d ON d.id=e.dst_id "
            "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
            "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES') AND ("
            "  e.src_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
            "  e.dst_id IN (SELECT node_id FROM memory_node_fts WHERE memory_node_fts MATCH ?) OR "
            "  e.value_json LIKE ?"
            ") AND (e.valid_from IS NULL OR e.valid_from <= ?) "
            "  AND (e.valid_to IS NULL OR e.valid_to > ?) "
            "ORDER BY e.recorded_at DESC LIMIT ?";

        Stmt st;
        std::string error;
        if (prepare(db_, sql, st, error)) {
            const std::string now = nowIso();
            const size_t remaining = maxResults - emitted;
            // Com filtro de dominio, o SQL devolve um superconjunto e o filtro fino
            // (metadata.domain) e aplicado abaixo. Sem filtro, o limite e exato.
            const size_t fetchLimit = domains.empty() ? remaining : std::max<size_t>(remaining * 4, 20);
            bindText(st.stmt, 1, scopeId);
            bindText(st.stmt, 2, match);
            bindText(st.stmt, 3, match);
            bindText(st.stmt, 4, "%" + text + "%");
            bindText(st.stmt, 5, now);
            bindText(st.stmt, 6, now);
            sqlite3_bind_int(st.stmt, 7, static_cast<int>(fetchLimit));
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                if (!domains.empty()) {
                    taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 7));
                    const std::string domain = metadata.is_object()
                        ? metadata.value("domain", std::string()) : std::string();
                    // Fatos sem dominio explicito ficam invisiveis para agentes restritos.
                    if (!domainAllowed(domain)) continue;
                }
                const std::string edgeId = columnText(st.stmt, 0);
                out << "[FATO] " << columnText(st.stmt, 1) << " --" << columnText(st.stmt, 2) << "--> "
                    << columnText(st.stmt, 3);
                const std::string from = columnText(st.stmt, 4);
                const std::string to = columnText(st.stmt, 5);
                if (!from.empty() || !to.empty()) out << " [" << from << " .. " << (to.empty() ? "agora" : to) << "]";
                out << " (confianca " << columnDouble(st.stmt, 6) << ")\n";
                if (hits) hits->push_back(MemoryHitRef{edgeId, "fact"});
                any = true;
                if (++emitted >= maxResults) break;
            }
        } else {
            lastError_ = error;
        }
    }

    return any ? sanitizeText(out.str()).text : std::string();
}

bool MemoryStore::recordHit(const std::string& scopeId, const std::string& runId,
                            const std::string& nodeId, const std::string& memoryId,
                            const std::string& kind, bool injected, bool used,
                            const std::string& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || memoryId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at) "
                       "VALUES (?,?,?,?,?,?,?,?,?,?) ON CONFLICT(scope_id,run_id,node_id,memory_id) DO UPDATE SET "
                       "kind=excluded.kind, "
                       "injected=MAX(memory_hit.injected, excluded.injected), "
                       "used=MAX(memory_hit.used, excluded.used), "
                       "outcome=CASE WHEN excluded.outcome<>'' THEN excluded.outcome ELSE memory_hit.outcome END",
                 st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, newId("hit"));
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, runId);
    bindText(st.stmt, 4, nodeId);
    bindText(st.stmt, 5, memoryId);
    bindText(st.stmt, 6, kind);
    sqlite3_bind_int(st.stmt, 7, injected ? 1 : 0);
    sqlite3_bind_int(st.stmt, 8, used ? 1 : 0);
    bindText(st.stmt, 9, outcome);
    bindText(st.stmt, 10, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

int MemoryStore::markHitsUsed(const std::string& scopeId, const std::string& runId,
                              const std::string& nodeId, const std::vector<std::string>& memoryIds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || memoryIds.empty()) return 0;
    std::string sql = "UPDATE memory_hit SET used=1 WHERE scope_id=? AND run_id=? AND node_id=? AND memory_id IN (";
    for (size_t i = 0; i < memoryIds.size(); ++i) sql += (i ? ",?" : "?");
    sql += ")";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql.c_str(), st, error)) {
        lastError_ = error;
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, runId);
    bindText(st.stmt, 3, nodeId);
    for (size_t i = 0; i < memoryIds.size(); ++i) bindText(st.stmt, static_cast<int>(i + 4), memoryIds[i]);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return 0;
    }
    return sqlite3_changes(db_);
}

int MemoryStore::recordConsultedHits(const std::string& scopeId, const std::string& runId,
                                     const std::string& nodeId,
                                     const std::vector<MemoryHitRef>& refs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || refs.empty()) return 0;

    int changed = 0;
    for (const auto& ref : refs) {
        if (ref.id.empty()) continue;

        // Se a memoria ja tinha sido injetada, so marcamos used=1 na linha existente.
        Stmt update;
        std::string error;
        if (prepare(db_, "UPDATE memory_hit SET used=1 WHERE scope_id=? AND run_id=? AND node_id=? AND memory_id=?",
                    update, error)) {
            bindText(update.stmt, 1, scopeId);
            bindText(update.stmt, 2, runId);
            bindText(update.stmt, 3, nodeId);
            bindText(update.stmt, 4, ref.id);
            if (stepRetry(update.stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0) {
                changed += sqlite3_changes(db_);
                continue;
            }
        } else {
            lastError_ = error;
            continue;
        }

        // Memoria descoberta por busca: conta como consultada, nao como injetada.
        Stmt insert;
        if (!prepare(db_, "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,created_at) "
                           "VALUES (?,?,?,?,?,?,0,1,'',?) "
                           "ON CONFLICT(scope_id,run_id,node_id,memory_id) DO UPDATE SET "
                           "used=1, kind=excluded.kind", insert, error)) {
            lastError_ = error;
            continue;
        }
        bindText(insert.stmt, 1, newId("hit"));
        bindText(insert.stmt, 2, scopeId);
        bindText(insert.stmt, 3, runId);
        bindText(insert.stmt, 4, nodeId);
        bindText(insert.stmt, 5, ref.id);
        bindText(insert.stmt, 6, ref.kind.empty() ? "memory" : ref.kind);
        bindText(insert.stmt, 7, nowIso());
        if (stepRetry(insert.stmt) == SQLITE_DONE) {
            ++changed;
        } else {
            lastError_ = sqlite3_errmsg(db_);
        }
    }
    return changed;
}

bool MemoryStore::finishHits(const std::string& scopeId, const std::string& runId,
                             const std::string& nodeId, const std::string& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_hit SET outcome=? WHERE scope_id=? AND run_id=? AND node_id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, outcome);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, runId);
    bindText(st.stmt, 4, nodeId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }

    // Reforco/decay: uso em sucesso reativa a memoria e soma evidencia; uso em
    // falha reduz a confianca. Nunca apaga a aresta.
    Stmt edgeSt;
    if (!prepare(db_, "UPDATE memory_edge SET "
                       "last_used=CASE WHEN EXISTS(SELECT 1 FROM memory_hit h WHERE h.scope_id=? "
                       "AND h.run_id=? AND h.node_id=? AND h.memory_id=memory_edge.id AND h.used=1) "
                       "THEN ? ELSE last_used END, "
                       "success_count=success_count+(SELECT COUNT(*) FROM memory_hit h WHERE h.scope_id=? "
                       "AND h.run_id=? AND h.node_id=? AND h.memory_id=memory_edge.id AND h.used=1 AND h.outcome='success'), "
                       "failure_count=failure_count+(SELECT COUNT(*) FROM memory_hit h WHERE h.scope_id=? "
                       "AND h.run_id=? AND h.node_id=? AND h.memory_id=memory_edge.id AND h.used=1 AND h.outcome='failure'), "
                       "dormant_at=CASE WHEN ?='success' AND EXISTS(SELECT 1 FROM memory_hit h WHERE h.scope_id=? "
                       "AND h.run_id=? AND h.node_id=? AND h.memory_id=memory_edge.id AND h.used=1) "
                       "THEN NULL ELSE dormant_at END, "
                       "utility=CASE WHEN ?='success' THEN MIN(1.0, utility+0.02) "
                       "ELSE MAX(0.0, utility-0.05) END "
                       "WHERE scope_id=? AND id IN (SELECT memory_id FROM memory_hit WHERE scope_id=? "
                       "AND run_id=? AND node_id=? AND used=1 AND kind IN ('lesson','fact'))",
                 edgeSt, error)) {
        return false;
    }
    const std::string now = nowIso();
    bindText(edgeSt.stmt, 1, scopeId);
    bindText(edgeSt.stmt, 2, runId);
    bindText(edgeSt.stmt, 3, nodeId);
    bindText(edgeSt.stmt, 4, now);
    bindText(edgeSt.stmt, 5, scopeId);
    bindText(edgeSt.stmt, 6, runId);
    bindText(edgeSt.stmt, 7, nodeId);
    bindText(edgeSt.stmt, 8, scopeId);
    bindText(edgeSt.stmt, 9, runId);
    bindText(edgeSt.stmt, 10, nodeId);
    bindText(edgeSt.stmt, 11, outcome);
    bindText(edgeSt.stmt, 12, scopeId);
    bindText(edgeSt.stmt, 13, runId);
    bindText(edgeSt.stmt, 14, nodeId);
    bindText(edgeSt.stmt, 15, outcome);
    bindText(edgeSt.stmt, 16, scopeId);
    bindText(edgeSt.stmt, 17, scopeId);
    bindText(edgeSt.stmt, 18, runId);
    bindText(edgeSt.stmt, 19, nodeId);
    if (stepRetry(edgeSt.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

std::vector<MemoryHitRef> MemoryStore::usedHitRefs(const std::string& scopeId, const std::string& runId,
                                                   const std::string& nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryHitRef> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT memory_id, kind FROM memory_hit WHERE scope_id=? AND run_id=? AND node_id=? "
                       "AND used=1 AND kind IN ('lesson','fact')", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, runId);
    bindText(st.stmt, 3, nodeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back(MemoryHitRef{columnText(st.stmt, 0), columnText(st.stmt, 1)});
    }
    return out;
}

bool MemoryStore::reinforceFromHits(const std::string& scopeId,
                                    const std::vector<std::string>& memoryIds,
                                    const std::string& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || memoryIds.empty()) return false;
    if (outcome != "success" && outcome != "failure") return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_edge SET "
                       "last_used=?, "
                       "success_count=success_count+?, "
                       "failure_count=failure_count+?, "
                       "dormant_at=CASE WHEN ?='success' THEN NULL ELSE dormant_at END, "
                       "utility=CASE WHEN ?='success' THEN MIN(1.0, utility+0.02) "
                       "ELSE MAX(0.0, utility-0.05) END "
                       "WHERE scope_id=? AND id=? AND status='active'", st, error)) {
        lastError_ = error;
        return false;
    }
    const std::string now = nowIso();
    bool ok = true;
    for (const auto& id : memoryIds) {
        if (id.empty()) continue;
        sqlite3_reset(st.stmt);
        sqlite3_clear_bindings(st.stmt);
        bindText(st.stmt, 1, now);
        sqlite3_bind_int(st.stmt, 2, outcome == "success" ? 1 : 0);
        sqlite3_bind_int(st.stmt, 3, outcome == "failure" ? 1 : 0);
        bindText(st.stmt, 4, outcome);
        bindText(st.stmt, 5, outcome);
        bindText(st.stmt, 6, scopeId);
        bindText(st.stmt, 7, id);
        if (stepRetry(st.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            ok = false;
        }
    }
    return ok;
}

taskforge::Json MemoryStore::hitSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COUNT(*), COALESCE(SUM(injected),0), COALESCE(SUM(used),0), "
                       "COALESCE(SUM(CASE WHEN used=1 AND injected=0 THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN used=1 AND injected=1 THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
                       "FROM memory_hit WHERE scope_id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (stepRetry(st.stmt) == SQLITE_ROW) {
        out["hits"] = columnInt(st.stmt, 0);
        out["injected"] = columnInt(st.stmt, 1);
        out["used"] = columnInt(st.stmt, 2);
        out["consulted"] = columnInt(st.stmt, 3);
        out["usedInjected"] = columnInt(st.stmt, 4);
        out["successHits"] = columnInt(st.stmt, 5);
        out["failureHits"] = columnInt(st.stmt, 6);
    }
    return out;
}

taskforge::Json MemoryStore::consolidationSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json{{"active", 0LL}, {"candidate", 0LL},
                                         {"superseded", 0LL}, {"dormant", 0LL},
                                         {"conflicts", 0LL}, {"aliases", 0LL}};
    if (!db_) return out;
    auto scalar = [&](const char* sql, const std::string& value) -> long long {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) return 0;
        bindText(st.stmt, 1, scopeId);
        if (!value.empty()) bindText(st.stmt, 2, value);
        return stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : 0;
    };
    out["active"] = scalar("SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND status='active' "
                           "AND dormant_at IS NULL AND relation NOT IN "
                           "('CONTRADICTS','SUPERSEDES','GENERALIZES')", "");
    out["candidate"] = scalar("SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND status='candidate'", "");
    out["superseded"] = scalar("SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND status='superseded'", "");
    out["dormant"] = scalar("SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND dormant_at IS NOT NULL", "");
    out["conflicts"] = scalar("SELECT COUNT(*) FROM memory_edge WHERE scope_id=? AND relation='CONTRADICTS' AND status='active'", "");
    out["aliases"] = scalar("SELECT COUNT(*) FROM memory_alias WHERE scope_id=?", "");
    return out;
}

bool MemoryStore::setPolicy(const std::string& scopeId, const std::string& key,
                            const taskforge::Json& value, bool immutable,
                            const std::string& actor, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || key.empty()) {
        error = "politica invalida";
        return false;
    }
    Stmt inherited;
    if (prepare(db_, "SELECT p.scope_id FROM memory_policy p "
                       "JOIN memory_scope_closure c ON c.ancestor_id=p.scope_id "
                       "WHERE c.descendant_id=? AND c.depth>0 AND p.key=? AND p.immutable=1 LIMIT 1",
                inherited, error)) {
        bindText(inherited.stmt, 1, scopeId);
        bindText(inherited.stmt, 2, key);
        if (stepRetry(inherited.stmt) == SQLITE_ROW) {
            error = "politica imutavel no escopo ancestral: " + columnText(inherited.stmt, 0);
            return false;
        }
    }
    taskforge::Json clean = value;
    sanitizeJsonInPlace(clean);
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_policy(scope_id,key,value_json,immutable,updated_at,updated_by) "
                       "VALUES(?,?,?,?,?,?) ON CONFLICT(scope_id,key) DO UPDATE SET "
                       "value_json=excluded.value_json,immutable=excluded.immutable,"
                       "updated_at=excluded.updated_at,updated_by=excluded.updated_by", st, error)) {
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, key);
    bindText(st.stmt, 3, clean.dump());
    sqlite3_bind_int(st.stmt, 4, immutable ? 1 : 0);
    bindText(st.stmt, 5, nowIso());
    bindText(st.stmt, 6, actor.empty() ? "human" : actor);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    audit(actor.empty() ? "human" : actor, "set_policy", key, "", scopeId);
    return true;
}

taskforge::Json MemoryStore::effectivePolicy(const std::string& scopeId,
                                             const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || key.empty()) return taskforge::Json();
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT p.scope_id,p.value_json,p.immutable FROM memory_policy p "
                       "JOIN memory_scope_closure c ON c.ancestor_id=p.scope_id "
                       "WHERE c.descendant_id=? AND p.key=? ORDER BY c.depth ASC LIMIT 1", st, error)) {
        lastError_ = error;
        return taskforge::Json();
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, key);
    if (stepRetry(st.stmt) != SQLITE_ROW) return taskforge::Json();
    auto valueOpt = taskforge::jsonutil::tryParse(columnText(st.stmt, 1));
    return taskforge::Json{{"scopeId", columnText(st.stmt, 0)},
                           {"key", key},
                           {"value", valueOpt ? *valueOpt : taskforge::Json(columnText(st.stmt, 1))},
                           {"immutable", columnInt(st.stmt, 2) != 0}};
}

bool MemoryStore::recordAudit(const std::string& actor, const std::string& action,
                              const std::string& target, const std::string& before,
                              const std::string& after) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    audit(actor, action, target, before, after);
    return true;
}

bool MemoryStore::recordUsage(const MemoryUsageRecord& usage) {    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_usage(id,scope_id,run_id,node_id,provider,model,prompt_tokens,completion_tokens,total_tokens,calls,latency_ms,memory_context_chars,memory_enabled,retries,outcome,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
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
    sqlite3_bind_int64(st.stmt, 14, usage.retries);
    bindText(st.stmt, 15, usage.outcome);
    bindText(st.stmt, 16, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
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
                       "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(memory_enabled),0), "
                       "COALESCE(SUM(retries),0), "
                       "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
                       "FROM memory_usage WHERE scope_id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (stepRetry(st.stmt) == SQLITE_ROW) {
        out["runs"] = columnInt(st.stmt, 0);
        out["promptTokens"] = columnInt(st.stmt, 1);
        out["completionTokens"] = columnInt(st.stmt, 2);
        out["totalTokens"] = columnInt(st.stmt, 3);
        out["calls"] = columnInt(st.stmt, 4);
        out["avgLatencyMs"] = columnDouble(st.stmt, 5);
        out["memoryContextChars"] = columnInt(st.stmt, 6);
        out["memoryEnabledRuns"] = columnInt(st.stmt, 7);
        out["retries"] = columnInt(st.stmt, 8);
        out["successRuns"] = columnInt(st.stmt, 9);
        out["failureRuns"] = columnInt(st.stmt, 10);
    }
    return out;
}

taskforge::Json MemoryStore::toCognitiveJson(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;

    out["scopeId"] = scopeId;

    taskforge::Json scopes = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT s.id, s.kind, COALESCE(s.parent_id,''), s.name, s.created_at, s.metadata "
                           "FROM memory_scope s JOIN memory_scope_closure c ON c.ancestor_id=s.id "
                           "WHERE c.descendant_id=? ORDER BY c.depth DESC", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                taskforge::Json item = taskforge::Json::object();
                item["id"] = columnText(st.stmt, 0);
                item["kind"] = columnText(st.stmt, 1);
                item["parentId"] = columnText(st.stmt, 2);
                item["name"] = sanitizeText(columnText(st.stmt, 3), 512).text;
                item["createdAt"] = columnText(st.stmt, 4);
                taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 5));
                item["metadata"] = metadata.is_object() ? metadata : taskforge::Json::object();
                scopes.push_back(std::move(item));
            }
        } else {
            lastError_ = error;
        }
    }
    out["scopes"] = scopes;

    taskforge::Json closure = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT ancestor_id, descendant_id, depth FROM memory_scope_closure "
                           "WHERE descendant_id=? ORDER BY depth", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                closure.push_back(taskforge::Json{{"ancestorId", columnText(st.stmt, 0)},
                                                  {"descendantId", columnText(st.stmt, 1)},
                                                  {"depth", columnInt(st.stmt, 2)}});
            }
        } else {
            lastError_ = error;
        }
    }
    out["closure"] = closure;

    taskforge::Json nodes = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT id, scope_id, kind, canonical_name, aliases_json, created_at, updated_at, metadata "
                           "FROM memory_node WHERE scope_id=? ORDER BY id", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                taskforge::Json item = taskforge::Json::object();
                item["id"] = columnText(st.stmt, 0);
                item["scopeId"] = columnText(st.stmt, 1);
                item["kind"] = columnText(st.stmt, 2);
                item["name"] = sanitizeText(columnText(st.stmt, 3), 4000).text;
                taskforge::Json aliases = parseJsonOrNull(columnText(st.stmt, 4));
                item["aliases"] = aliases.is_array() ? aliases : taskforge::Json::array();
                item["createdAt"] = columnText(st.stmt, 5);
                item["updatedAt"] = columnText(st.stmt, 6);
                taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 7));
                item["metadata"] = metadata.is_object() ? metadata : taskforge::Json::object();
                nodes.push_back(std::move(item));
            }
        } else {
            lastError_ = error;
        }
    }
    out["nodes"] = nodes;

    taskforge::Json aliases = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT scope_id,alias,node_id,source FROM memory_alias "
                           "WHERE scope_id=? ORDER BY alias", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                aliases.push_back(taskforge::Json{{"scopeId", columnText(st.stmt, 0)},
                                                  {"alias", columnText(st.stmt, 1)},
                                                  {"nodeId", columnText(st.stmt, 2)},
                                                  {"source", columnText(st.stmt, 3)}});
            }
        } else {
            lastError_ = error;
        }
    }
    out["aliases"] = aliases;

    taskforge::Json playbooks = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT id,scope_id,task_type,title,steps_json,preconditions_json,"
                           "success_count,failure_count,confidence,status,COALESCE(source_ref,''),"
                           "evidence_json,created_at,updated_at FROM memory_playbook "
                           "WHERE scope_id=? ORDER BY id", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                playbooks.push_back(taskforge::Json{
                    {"id", columnText(st.stmt, 0)},
                    {"scopeId", columnText(st.stmt, 1)},
                    {"taskType", columnText(st.stmt, 2)},
                    {"title", columnText(st.stmt, 3)},
                    {"steps", parseJsonOrNull(columnText(st.stmt, 4))},
                    {"preconditions", parseJsonOrNull(columnText(st.stmt, 5))},
                    {"successCount", columnInt(st.stmt, 6)},
                    {"failureCount", columnInt(st.stmt, 7)},
                    {"confidence", columnDouble(st.stmt, 8)},
                    {"status", columnText(st.stmt, 9)},
                    {"sourceRef", columnText(st.stmt, 10)},
                    {"evidence", parseJsonOrNull(columnText(st.stmt, 11))},
                    {"createdAt", columnText(st.stmt, 12)},
                    {"updatedAt", columnText(st.stmt, 13)}});
            }
        } else {
            lastError_ = error;
        }
    }
    out["playbooks"] = playbooks;

    taskforge::Json edges = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT id, scope_id, src_id, COALESCE(dst_id,''), relation, COALESCE(value_json,''), "
                           "COALESCE(valid_from,''), COALESCE(valid_to,''), recorded_at, confidence, status, "
                           "COALESCE(superseded_by,''), source_kind, COALESCE(source_ref,''), "
                           "COALESCE(evidence_hash,''), edge_hash, metadata, "
                           "COALESCE(last_used,''), success_count, failure_count, COALESCE(dormant_at,'') "
                           "FROM memory_edge WHERE scope_id=? ORDER BY id", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                taskforge::Json item = taskforge::Json::object();
                item["id"] = columnText(st.stmt, 0);
                item["scopeId"] = columnText(st.stmt, 1);
                item["srcId"] = columnText(st.stmt, 2);
                item["dstId"] = columnText(st.stmt, 3);
                item["relation"] = columnText(st.stmt, 4);
                const std::string rawValue = columnText(st.stmt, 5);
                taskforge::Json value = parseJsonOrNull(rawValue);
                item["value"] = value.is_null() && !rawValue.empty() ? taskforge::Json(rawValue) : value;
                item["validFrom"] = columnText(st.stmt, 6);
                item["validTo"] = columnText(st.stmt, 7);
                item["recordedAt"] = columnText(st.stmt, 8);
                item["confidence"] = columnDouble(st.stmt, 9);
                item["status"] = columnText(st.stmt, 10);
                item["supersededBy"] = columnText(st.stmt, 11);
                item["sourceKind"] = columnText(st.stmt, 12);
                item["sourceRef"] = columnText(st.stmt, 13);
                item["evidenceHash"] = columnText(st.stmt, 14);
                item["edgeHash"] = columnText(st.stmt, 15);
                taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 16));
                item["metadata"] = metadata.is_object() ? metadata : taskforge::Json::object();
                item["lastUsed"] = columnText(st.stmt, 17);
                item["successCount"] = columnInt(st.stmt, 18);
                item["failureCount"] = columnInt(st.stmt, 19);
                item["dormantAt"] = columnText(st.stmt, 20);
                edges.push_back(std::move(item));
            }
        } else {
            lastError_ = error;
        }
    }
    out["edges"] = edges;

    taskforge::Json workingRows = taskforge::Json::array();
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT scope_id, item_id, key, value_json, updated_at FROM memory_working "
                           "WHERE scope_id=? ORDER BY item_id, key", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                taskforge::Json item = taskforge::Json::object();
                item["scopeId"] = columnText(st.stmt, 0);
                item["itemId"] = columnText(st.stmt, 1);
                item["key"] = columnText(st.stmt, 2);
                item["value"] = parseJsonOrNull(columnText(st.stmt, 3));
                item["updatedAt"] = columnText(st.stmt, 4);
                workingRows.push_back(std::move(item));
            }
        } else {
            lastError_ = error;
        }
    }
    out["workingRows"] = workingRows;

    // Compatibilidade com leitores do snapshot v1.
    taskforge::Json lessons = taskforge::Json::array();
    for (const auto& lesson : getLessons(scopeId)) {
        lessons.push_back(taskforge::Json{{"id", lesson.id},
                                          {"domain", lesson.domain},
                                          {"triggerError", lesson.triggerError},
                                          {"lessonRule", lesson.lessonRule},
                                          {"sourceItem", lesson.sourceItem},
                                          {"createdAt", lesson.createdAt},
                                          {"confidence", lesson.confidence}});
    }
    out["lessons"] = lessons;
    out["working"] = workingToJson(scopeId);
    out["counts"] = taskforge::Json{{"scopes", scopes.size()},
                                    {"nodes", nodes.size()},
                                    {"edges", edges.size()},
                                    {"working", workingRows.size()},
                                    {"lessons", lessons.size()}};
    return out;
}

bool MemoryStore::importCognitiveJson(const std::string& scopeId, const taskforge::Json& data,
                                      std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco de memoria fechado";
        return false;
    }
    if (!data.is_object()) {
        error = "snapshot cognitivo invalido";
        return false;
    }

    auto fail = [&](const std::string& message) {
        std::string rollbackError;
        exec("ROLLBACK;", &rollbackError);
        error = message;
        if (!rollbackError.empty()) error += " (rollback: " + rollbackError + ")";
        return false;
    };
    auto readText = [](const taskforge::Json& object, const char* key, const std::string& fallback = "") {
        if (!object.is_object() || !object.contains(key)) return fallback;
        const auto& value = object[key];
        return value.is_string() ? value.str() : fallback;
    };
    auto readDouble = [](const taskforge::Json& object, const char* key, double fallback) {
        if (!object.is_object() || !object.contains(key) || !object[key].is_number()) return fallback;
        return object[key].get<double>();
    };
    auto readInt = [](const taskforge::Json& object, const char* key, long long fallback) {
        if (!object.is_object() || !object.contains(key) || !object[key].is_number()) return fallback;
        return object[key].get<long long>();
    };
    auto metadataText = [](const taskforge::Json& value) {
        if (value.is_null()) return std::string("{}");
        if (value.is_string()) return sanitizeText(value.str(), 4000).text;
        taskforge::Json copy = value;
        sanitizeJsonInPlace(copy);
        return copy.dump();
    };
    auto valueText = [](const taskforge::Json& value) {
        if (value.is_null()) return std::string();
        taskforge::Json copy = value;
        sanitizeJsonInPlace(copy);
        return copy.dump();
    };
    auto aliasesText = [](const taskforge::Json& value) {
        if (value.is_array()) return value.dump();
        if (value.is_string()) return sanitizeText(value.str(), 4000).text;
        return std::string("[]");
    };

    if (!exec("BEGIN IMMEDIATE;", &error)) return false;

    const taskforge::Json scopes = data.contains("scopes") && data["scopes"].is_array()
        ? data["scopes"] : taskforge::Json::array();
    const taskforge::Json nodes = data.contains("nodes") && data["nodes"].is_array()
        ? data["nodes"] : taskforge::Json::array();
    const taskforge::Json aliases = data.contains("aliases") && data["aliases"].is_array()
        ? data["aliases"] : taskforge::Json::array();
    const taskforge::Json playbooks = data.contains("playbooks") && data["playbooks"].is_array()
        ? data["playbooks"] : taskforge::Json::array();
    const taskforge::Json edges = data.contains("edges") && data["edges"].is_array()
        ? data["edges"] : taskforge::Json::array();
    const taskforge::Json closure = data.contains("closure") && data["closure"].is_array()
        ? data["closure"] : taskforge::Json::array();

    // 1. Escopos. Insere primeiro sem parent para nao depender da ordem do arquivo.
    if (scopes.empty()) {
        if (!ensureScope(scopeId, "project", "", scopeId)) {
            return fail("nao foi possivel criar o escopo do projeto");
        }
    } else {
        for (const auto& scope : scopes) {
            if (!scope.is_object()) continue;
            const std::string id = readText(scope, "id");
            if (id.empty()) continue;
            Stmt st;
            if (!prepare(db_, "INSERT INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
                               "VALUES(?,?,NULL,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                               "kind=excluded.kind,name=excluded.name,created_at=excluded.created_at,metadata=excluded.metadata",
                         st, error)) {
                return fail(error);
            }
            bindText(st.stmt, 1, id);
            bindText(st.stmt, 2, readText(scope, "kind", "project"));
            bindText(st.stmt, 3, sanitizeText(readText(scope, "name", id), 512).text);
            bindText(st.stmt, 4, readText(scope, "createdAt", nowIso()));
            bindText(st.stmt, 5, metadataText(scope.contains("metadata") ? scope["metadata"] : taskforge::Json::object()));
            if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        }
        for (const auto& scope : scopes) {
            if (!scope.is_object()) continue;
            const std::string id = readText(scope, "id");
            if (id.empty()) continue;
            Stmt st;
            if (!prepare(db_, "UPDATE memory_scope SET parent_id=? WHERE id=?", st, error)) {
                return fail(error);
            }
            bindNullableText(st.stmt, 1, readText(scope, "parentId"));
            bindText(st.stmt, 2, id);
            if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        }
    }

    // 2. Closure do escopo.
    {
        Stmt st;
        if (!prepare(db_, "DELETE FROM memory_scope_closure WHERE descendant_id=?", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }
    if (closure.empty()) {
        Stmt st;
        if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) VALUES(?,?,0)", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    } else {
        for (const auto& row : closure) {
            if (!row.is_object()) continue;
            Stmt st;
            if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) VALUES(?,?,?)", st, error)) {
                return fail(error);
            }
            bindText(st.stmt, 1, readText(row, "ancestorId", scopeId));
            bindText(st.stmt, 2, readText(row, "descendantId", scopeId));
            sqlite3_bind_int(st.stmt, 3, static_cast<int>(readDouble(row, "depth", 0)));
            if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        }
    }
    {
        // Garante a auto-referencia mesmo em snapshots antigos/incompletos.
        Stmt st;
        if (!prepare(db_, "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) VALUES(?,?,0)", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 3. Substitui apenas o grafo cognitivo do escopo; telemetria/auditoria permanecem.
    const char* deletes[] = {
        "DELETE FROM memory_evidence WHERE edge_id IN (SELECT id FROM memory_edge WHERE scope_id=?) "
        "OR episode_id IN (SELECT id FROM memory_episode WHERE scope_id=?)",
        "DELETE FROM memory_working WHERE scope_id=?",
        "DELETE FROM memory_alias WHERE scope_id=?",
        "DELETE FROM memory_playbook WHERE scope_id=?",
        "DELETE FROM memory_node_fts WHERE node_id IN (SELECT id FROM memory_node WHERE scope_id=?)",
        "DELETE FROM memory_edge WHERE scope_id=?",
        "DELETE FROM memory_node WHERE scope_id=?"
    };
    for (const char* sql : deletes) {
        Stmt st;
        if (!prepare(db_, sql, st, error)) return fail(error);
        bindText(st.stmt, 1, scopeId);
        if (std::string(sql).find("memory_evidence") != std::string::npos) bindText(st.stmt, 2, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 4. Nos + FTS.
    for (const auto& node : nodes) {
        if (!node.is_object()) continue;
        const std::string id = readText(node, "id");
        if (id.empty()) return fail("no do snapshot sem id");
        Stmt st;
        if (!prepare(db_, "INSERT INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,created_at,updated_at,metadata) "
                           "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                           "scope_id=excluded.scope_id,kind=excluded.kind,canonical_name=excluded.canonical_name,"
                           "aliases_json=excluded.aliases_json,created_at=excluded.created_at,"
                           "updated_at=excluded.updated_at,metadata=excluded.metadata", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, id);
        bindText(st.stmt, 2, readText(node, "scopeId", scopeId));
        bindText(st.stmt, 3, readText(node, "kind", "unknown"));
        bindText(st.stmt, 4, sanitizeText(readText(node, "name", id), 4000).text);
        bindText(st.stmt, 5, aliasesText(node.contains("aliases") ? node["aliases"] : taskforge::Json::array()));
        bindText(st.stmt, 6, readText(node, "createdAt", nowIso()));
        bindText(st.stmt, 7, readText(node, "updatedAt", nowIso()));
        bindText(st.stmt, 8, metadataText(node.contains("metadata") ? node["metadata"] : taskforge::Json::object()));
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));

        Stmt delFts;
        if (!prepare(db_, "DELETE FROM memory_node_fts WHERE node_id=?", delFts, error)) {
            return fail(error);
        }
        bindText(delFts.stmt, 1, id);
        if (stepRetry(delFts.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));

        Stmt fts;
        if (!prepare(db_, "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)", fts, error)) {
            return fail(error);
        }
        bindText(fts.stmt, 1, id);
        bindText(fts.stmt, 2, sanitizeText(readText(node, "name", id), 4000).text);
        bindText(fts.stmt, 3, aliasesText(node.contains("aliases") ? node["aliases"] : taskforge::Json::array()));
        bindText(fts.stmt, 4, metadataText(node.contains("metadata") ? node["metadata"] : taskforge::Json::object()));
        if (stepRetry(fts.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 4b. Aliases de entidades (M3).
    for (const auto& alias : aliases) {
        if (!alias.is_object()) continue;
        const std::string name = readText(alias, "alias");
        const std::string nodeId = readText(alias, "nodeId");
        if (name.empty() || nodeId.empty()) continue;
        Stmt st;
        if (!prepare(db_, "INSERT OR REPLACE INTO memory_alias(scope_id,alias,node_id,source) "
                           "VALUES(?,?,?,?)", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, readText(alias, "scopeId", scopeId));
        bindText(st.stmt, 2, normalizeComparable(name));
        bindText(st.stmt, 3, nodeId);
        bindText(st.stmt, 4, readText(alias, "source", "snapshot_restore"));
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 4c. Playbooks procedurais (M4).
    for (const auto& playbook : playbooks) {
        if (!playbook.is_object()) continue;
        const std::string id = readText(playbook, "id");
        const std::string taskType = readText(playbook, "taskType");
        if (id.empty() || taskType.empty()) continue;
        Stmt st;
        if (!prepare(db_, "INSERT OR REPLACE INTO memory_playbook(id,scope_id,task_type,title,"
                           "steps_json,preconditions_json,success_count,failure_count,confidence,"
                           "status,source_ref,evidence_json,created_at,updated_at) "
                           "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, id);
        bindText(st.stmt, 2, readText(playbook, "scopeId", scopeId));
        bindText(st.stmt, 3, taskType);
        bindText(st.stmt, 4, sanitizeText(readText(playbook, "title", taskType), 512).text);
        bindText(st.stmt, 5, metadataText(playbook.contains("steps") ? playbook["steps"]
                                                                     : taskforge::Json::array()));
        bindText(st.stmt, 6, metadataText(playbook.contains("preconditions")
                                            ? playbook["preconditions"] : taskforge::Json::object()));
        sqlite3_bind_int64(st.stmt, 7, readInt(playbook, "successCount", 0));
        sqlite3_bind_int64(st.stmt, 8, readInt(playbook, "failureCount", 0));
        sqlite3_bind_double(st.stmt, 9, readDouble(playbook, "confidence", 0.5));
        bindText(st.stmt, 10, readText(playbook, "status", "active"));
        bindNullableText(st.stmt, 11, readText(playbook, "sourceRef"));
        bindText(st.stmt, 12, metadataText(playbook.contains("evidence")
                                             ? playbook["evidence"] : taskforge::Json::array()));
        bindText(st.stmt, 13, readText(playbook, "createdAt", nowIso()));
        bindText(st.stmt, 14, readText(playbook, "updatedAt", nowIso()));
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 5. Arestas. superseded_by e resolvido em segundo passo.
    std::vector<std::pair<std::string, std::string>> superseded;
    for (const auto& edge : edges) {
        if (!edge.is_object()) continue;
        std::string id = readText(edge, "id");
        if (id.empty()) id = newId("edge");
        const std::string srcId = readText(edge, "srcId");
        const std::string dstId = readText(edge, "dstId");
        const std::string relation = readText(edge, "relation", "RELATED_TO");
        const std::string value = valueText(edge.contains("value") ? edge["value"] : taskforge::Json());
        const std::string validFrom = readText(edge, "validFrom");
        const std::string validTo = readText(edge, "validTo");
        std::string hash = readText(edge, "edgeHash");
        if (hash.empty()) hash = edgeHash(scopeId, srcId, dstId, relation, value, validFrom, validTo);

        Stmt st;
        if (!prepare(db_, "INSERT INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,valid_from,valid_to,"
                           "recorded_at,confidence,status,superseded_by,source_kind,source_ref,evidence_hash,edge_hash,metadata,"
                           "last_used,success_count,failure_count,dormant_at) "
                           "VALUES(?,?,?,?,?,?,?,?,?,?,?,NULL,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                           "scope_id=excluded.scope_id,src_id=excluded.src_id,dst_id=excluded.dst_id,relation=excluded.relation,"
                           "value_json=excluded.value_json,valid_from=excluded.valid_from,valid_to=excluded.valid_to,"
                           "recorded_at=excluded.recorded_at,confidence=excluded.confidence,status=excluded.status,"
                           "superseded_by=NULL,source_kind=excluded.source_kind,source_ref=excluded.source_ref,"
                           "evidence_hash=excluded.evidence_hash,edge_hash=excluded.edge_hash,metadata=excluded.metadata,"
                           "last_used=excluded.last_used,success_count=excluded.success_count,"
                           "failure_count=excluded.failure_count,dormant_at=excluded.dormant_at",
                     st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, id);
        bindText(st.stmt, 2, readText(edge, "scopeId", scopeId));
        bindText(st.stmt, 3, srcId);
        bindNullableText(st.stmt, 4, dstId);
        bindText(st.stmt, 5, relation);
        bindNullableText(st.stmt, 6, value);
        bindNullableText(st.stmt, 7, validFrom);
        bindNullableText(st.stmt, 8, validTo);
        bindText(st.stmt, 9, readText(edge, "recordedAt", nowIso()));
        sqlite3_bind_double(st.stmt, 10, readDouble(edge, "confidence", 0.5));
        bindText(st.stmt, 11, readText(edge, "status", "active"));
        bindText(st.stmt, 12, readText(edge, "sourceKind", "snapshot_restore"));
        bindNullableText(st.stmt, 13, readText(edge, "sourceRef"));
        bindNullableText(st.stmt, 14, readText(edge, "evidenceHash"));
        bindText(st.stmt, 15, hash);
        bindText(st.stmt, 16, metadataText(edge.contains("metadata") ? edge["metadata"] : taskforge::Json::object()));
        bindNullableText(st.stmt, 17, readText(edge, "lastUsed"));
        sqlite3_bind_int64(st.stmt, 18, readInt(edge, "successCount", 0));
        sqlite3_bind_int64(st.stmt, 19, readInt(edge, "failureCount", 0));
        bindNullableText(st.stmt, 20, readText(edge, "dormantAt"));
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));

        const std::string supersededBy = readText(edge, "supersededBy");
        if (!supersededBy.empty()) superseded.emplace_back(id, supersededBy);
    }
    for (const auto& [id, supersededBy] : superseded) {
        Stmt st;
        if (!prepare(db_, "UPDATE memory_edge SET superseded_by=? WHERE id=? AND scope_id=?", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, supersededBy);
        bindText(st.stmt, 2, id);
        bindText(st.stmt, 3, scopeId);
        if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
    }

    // 6. Working rows. workingRows e o formato v2; working e o fallback v1.
    const taskforge::Json workingRows = data.contains("workingRows") && data["workingRows"].is_array()
        ? data["workingRows"] : taskforge::Json::array();
    if (!workingRows.empty()) {
        for (const auto& row : workingRows) {
            if (!row.is_object()) continue;
            Stmt st;
            if (!prepare(db_, "INSERT INTO memory_working(scope_id,item_id,key,value_json,updated_at) VALUES(?,?,?,?,?) "
                               "ON CONFLICT(scope_id,item_id,key) DO UPDATE SET "
                               "value_json=excluded.value_json,updated_at=excluded.updated_at", st, error)) {
                return fail(error);
            }
            bindText(st.stmt, 1, readText(row, "scopeId", scopeId));
            bindText(st.stmt, 2, readText(row, "itemId", "pipeline"));
            bindText(st.stmt, 3, readText(row, "key"));
            bindText(st.stmt, 4, valueText(row.contains("value") ? row["value"] : taskforge::Json()));
            bindText(st.stmt, 5, readText(row, "updatedAt", nowIso()));
            if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        }
    } else if (data.contains("working") && data["working"].is_object()) {
        for (auto item = data["working"].begin(); item != data["working"].end(); ++item) {
            if (!item.value().is_object()) continue;
            for (auto key = item.value().begin(); key != item.value().end(); ++key) {
                Stmt st;
                if (!prepare(db_, "INSERT INTO memory_working(scope_id,item_id,key,value_json,updated_at) VALUES(?,?,?,?,?) "
                                   "ON CONFLICT(scope_id,item_id,key) DO UPDATE SET "
                                   "value_json=excluded.value_json,updated_at=excluded.updated_at", st, error)) {
                    return fail(error);
                }
                bindText(st.stmt, 1, scopeId);
                bindText(st.stmt, 2, item.key());
                bindText(st.stmt, 3, key.key());
                bindText(st.stmt, 4, valueText(key.value()));
                bindText(st.stmt, 5, nowIso());
                if (stepRetry(st.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
            }
        }
    }

    // 7. Verificacao de contagens antes do commit.
    auto countScoped = [&](const char* sql, const std::string& expected, const char* label) {
        Stmt st;
        std::string countError;
        if (!prepare(db_, sql, st, countError)) {
            error = countError;
            return false;
        }
        bindText(st.stmt, 1, scopeId);
        const long long actual = stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : -1;
        if (actual != static_cast<long long>(std::stoll(expected))) {
            error = std::string("contagem de ") + label + " divergente no snapshot";
            return false;
        }
        return true;
    };
    if (!countScoped("SELECT COUNT(*) FROM memory_node WHERE scope_id=?", std::to_string(nodes.size()), "nos") ||
        !countScoped("SELECT COUNT(*) FROM memory_edge WHERE scope_id=?", std::to_string(edges.size()), "arestas")) {
        return fail(error);
    }

    std::string commitError;
    if (!exec("COMMIT;", &commitError)) {
        return fail(commitError);
    }
    audit("runtime", "snapshot_import", scopeId, "", "v2");
    return true;
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
    out["hits"] = hitSummary(scopeId);
    return out;
}

bool MemoryStore::hasMeta(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT 1 FROM memory_meta WHERE key=? LIMIT 1", st, error)) return false;
    bindText(st.stmt, 1, key);
    return stepRetry(st.stmt) == SQLITE_ROW;
}

std::string MemoryStore::getMeta(const std::string& key) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT value FROM memory_meta WHERE key=?", st, error)) return "";
    bindText(st.stmt, 1, key);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnText(st.stmt, 0) : "";
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
    return stepRetry(st.stmt) == SQLITE_DONE;
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
    stepRetry(st.stmt);
}

} // namespace memory
} // namespace taskforge
