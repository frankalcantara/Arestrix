#include "MemoryStore.hpp"
#include "MemoryCatalog.hpp"
#include "MemoryPredicates.hpp"
#include "MemorySanitizer.hpp"
#include "MemorySources.hpp"
#include "project/RunnerSecurity.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#include "sqlite3.h"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace {

// Versao do esquema de conhecimento/telemetria. E2=2 (consulted/
// invalid_citations); E4=3 (memory_evidence autocontida); E11=4
// (knowledge_version e memory_trace); fix5=5 (WriteTxn, unique edge_hash e
// memory_operation); fix5=6 (catalogo, context_key e claim_key); fix5=7
// (linhagem, lapides, fila de exclusao e retracao); fix5=8 (epocas de prefixo).
constexpr int kMemorySchemaVersion = 9;

// Fase 1: tabelas de conhecimento/telemetria com scope_id que o purge precisa
// cobrir. memory_tombstone e a unica excecao explicita (Fase 3).
const char* const kScopedKnowledgeTables[] = {
    "memory_alias", "memory_claim_extraction", "memory_edge", "memory_embedding",
    "memory_episode", "memory_evidence", "memory_facet", "memory_hit", "memory_lens",
    "memory_node", "memory_operation", "memory_passage", "memory_path",
    "memory_path_schema", "memory_playbook", "memory_policy", "memory_proposal",
    "memory_source", "memory_state", "memory_state_history", "memory_state_key",
    "memory_trace", "memory_usage", "memory_validation", "memory_working",
    "memory_lineage", "memory_job", "memory_removal_log", "memory_prefix_epoch",
    "memory_credit_event", "memory_item_utility", "memory_credit_audit",
    "memory_validator_audit", "memory_task_snapshot"};

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

std::vector<std::string> textWindowHashes(const std::string& text, size_t step = 32) {
    std::vector<std::string> out;
    const size_t window = 64;
    step = std::max<size_t>(1, step);
    if (text.size() < window) {
        if (!text.empty()) out.push_back(project::RunnerSecurity::sha256(text));
        return out;
    }
    for (size_t offset = 0; offset + window <= text.size(); offset += step) {
        out.push_back(project::RunnerSecurity::sha256(text.substr(offset, window)));
    }
    return out;
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

MemoryStore::MemoryStore(std::string path, size_t maxActiveLessons, bool readOnly,
                         bool durableSync)
    : path_(std::move(path)),
      maxActiveLessons_(maxActiveLessons == 0 ? kMaxActiveLessons : maxActiveLessons),
      readOnly_(readOnly),
      durableSync_(durableSync) {}

MemoryStore::WriteTxn::WriteTxn(MemoryStore& store, std::string& error)
    : store_(store), lock_(store.mutex_) {
    if (!store_.db_) {
        error = "banco de memoria fechado";
        return;
    }
    if (sqlite3_get_autocommit(store_.db_) == 0) {
        depth_ = ++store_.writeTxnDepth_;
        savepoint_ = true;
        if (!store_.exec("SAVEPOINT sp_" + std::to_string(depth_) + ";", &error)) {
            --store_.writeTxnDepth_;
            return;
        }
        active_ = true;
        return;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (store_.exec("BEGIN IMMEDIATE;", &error)) {
            depth_ = ++store_.writeTxnDepth_;
            active_ = true;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10 * (1 << attempt)));
        error.clear();
    }
    error = "nao foi possivel iniciar transacao de escrita (SQLITE_BUSY)";
}

MemoryStore::WriteTxn::~WriteTxn() {
    if (active_ && !finished_) rollback();
    if (depth_ > 0) {
        --store_.writeTxnDepth_;
        depth_ = 0;
    }
}

bool MemoryStore::WriteTxn::commit(std::string* error) {
    if (!active_) {
        if (error) *error = "transacao de escrita inativa";
        return false;
    }
    std::string localError;
    const std::string sql = savepoint_
        ? "RELEASE sp_" + std::to_string(depth_) + ";"
        : "COMMIT;";
    if (!store_.exec(sql, &localError)) {
        if (error) *error = localError;
        return false;
    }
    active_ = false;
    finished_ = true;
    return true;
}

void MemoryStore::WriteTxn::rollback() {
    if (!active_ || finished_) return;
    std::string ignored;
    if (savepoint_) {
        store_.exec("ROLLBACK TO sp_" + std::to_string(depth_) + ";", &ignored);
        store_.exec("RELEASE sp_" + std::to_string(depth_) + ";", &ignored);
    } else {
        store_.exec("ROLLBACK;", &ignored);
    }
    active_ = false;
    finished_ = true;
}

void MemoryStore::setTestInterleaveHook(TestInterleaveHook hook) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    testInterleaveHook_ = std::move(hook);
}

MemoryStore::~MemoryStore() {
    close();
}

bool MemoryStore::open(std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (db_) return true;

    if (readOnly_ && path_ == ":memory:") {
        error = "banco em memoria nao pode ser somente leitura";
        return false;
    }
    if (!readOnly_ && path_ != ":memory:") {
        std::error_code ec;
        std::filesystem::path parent = std::filesystem::path(path_).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }

    const int flags = readOnly_
        ? (SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX)
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX);
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

    if (readOnly_) {
        // O medico A/B abre o conhecimento congelado sem chance de escrita.
        if (!exec("PRAGMA query_only=ON; PRAGMA foreign_keys=ON;", &error)) {
            lastError_ = error;
            close();
            return false;
        }
        return true;
    }
    const char* sync = durableSync_ ? "FULL" : "NORMAL";
    if (!exec("PRAGMA foreign_keys=ON; PRAGMA secure_delete=ON;", &error) ||
        !exec(path_ == ":memory:"
                  ? std::string("PRAGMA synchronous=") + sync + ";"
                  : std::string("PRAGMA journal_mode=WAL; PRAGMA synchronous=") + sync + ";",
              &error) ||
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

bool MemoryStore::vacuum(std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco indisponivel";
        return false;
    }
    return exec("PRAGMA wal_checkpoint(TRUNCATE); VACUUM;", &error);
}

bool MemoryStore::sealForReadOnly(const std::string& path, std::string& error) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db) : "sqlite3_open_v2 falhou";
        if (db) sqlite3_close(db);
        return false;
    }
    char* message = nullptr;
    const int rc = sqlite3_exec(db,
        "PRAGMA wal_checkpoint(TRUNCATE); PRAGMA journal_mode=DELETE;",
        nullptr, nullptr, &message);
    if (rc != SQLITE_OK) {
        error = message ? message : sqlite3_errmsg(db);
        if (message) sqlite3_free(message);
        sqlite3_close(db);
        return false;
    }
    if (message) sqlite3_free(message);
    sqlite3_close(db);
    return true;
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
  dormant_at TEXT,
  retracted_at TEXT
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
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  metadata TEXT NOT NULL DEFAULT '{}',
  intent_template_hash TEXT NOT NULL DEFAULT ''
);

-- E4: instantaneo autocontido da evidencia. Sem FK para episodio/edge: a
-- retencao da telemetria pode apagar o episodio sem apagar a prova.
CREATE TABLE IF NOT EXISTS memory_evidence (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  target_kind TEXT NOT NULL,
  target_id TEXT NOT NULL,
  action TEXT NOT NULL DEFAULT 'promote',
  episode_id TEXT,
  run_id TEXT,
  node_id TEXT,
  input_hash TEXT,
  validator_id TEXT,
  validator_version TEXT,
  ok INTEGER NOT NULL DEFAULT 0,
  artifact_hash TEXT,
  excerpt_hash TEXT,
  summary TEXT NOT NULL DEFAULT '',
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
  cached_tokens INTEGER NOT NULL DEFAULT 0,
  cache_write_tokens INTEGER NOT NULL DEFAULT 0,
  calls INTEGER NOT NULL DEFAULT 0,
  latency_ms INTEGER NOT NULL DEFAULT 0,
  memory_context_chars INTEGER NOT NULL DEFAULT 0,
  memory_enabled INTEGER NOT NULL DEFAULT 0,
  retries INTEGER NOT NULL DEFAULT 0,
  invalid_citations INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
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
  consulted INTEGER NOT NULL DEFAULT 0,
  used INTEGER NOT NULL DEFAULT 0,
  outcome TEXT NOT NULL DEFAULT '',
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
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
  ab_arm TEXT NOT NULL DEFAULT '',
  ab_repetition INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, episode_id, validator_id, validator_version)
);

-- L2/M0: propostas, embeddings, playbooks e aliases. As tabelas nascem vazias e
-- nao alteram o comportamento da recuperacao atual.
CREATE TABLE IF NOT EXISTS memory_trace (
  trace_id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  run_id TEXT NOT NULL,
  node_id TEXT NOT NULL,
  item_id TEXT NOT NULL DEFAULT '',
  task_type TEXT NOT NULL DEFAULT '',
  intent_template_hash TEXT NOT NULL DEFAULT '',
  input_hash TEXT NOT NULL DEFAULT '',
  knowledge_version INTEGER NOT NULL DEFAULT 0,
  model TEXT NOT NULL DEFAULT '',
  tier TEXT NOT NULL DEFAULT '',
  outcome TEXT NOT NULL DEFAULT '',
  anchors_json TEXT NOT NULL DEFAULT '[]',
  steps_json TEXT NOT NULL DEFAULT '[]',
  delivered_json TEXT NOT NULL DEFAULT '[]',
  used_json TEXT NOT NULL DEFAULT '[]',
  truncated_steps INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, run_id, node_id)
);

CREATE TABLE IF NOT EXISTS memory_source (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  uri TEXT NOT NULL,
  content_hash TEXT NOT NULL,
  kind TEXT NOT NULL DEFAULT '',
  size INTEGER NOT NULL DEFAULT 0,
  first_seen TEXT NOT NULL,
  last_seen TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active'
);

CREATE TABLE IF NOT EXISTS memory_passage (
  id TEXT PRIMARY KEY,
  source_id TEXT NOT NULL REFERENCES memory_source(id),
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  ordinal INTEGER NOT NULL,
  start INTEGER NOT NULL DEFAULT 0,
  end INTEGER NOT NULL DEFAULT 0,
  text TEXT NOT NULL,
  text_hash TEXT NOT NULL,
  tokens INTEGER NOT NULL DEFAULT 0,
  status TEXT NOT NULL DEFAULT 'active',
  created_at TEXT NOT NULL,
  UNIQUE(source_id, ordinal)
);

CREATE TABLE IF NOT EXISTS memory_lens (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  name TEXT NOT NULL,
  description TEXT NOT NULL DEFAULT '',
  version INTEGER NOT NULL DEFAULT 1,
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, name)
);

CREATE TABLE IF NOT EXISTS memory_edge_lens (
  edge_id TEXT NOT NULL REFERENCES memory_edge(id),
  lens_id TEXT NOT NULL REFERENCES memory_lens(id),
  PRIMARY KEY (edge_id, lens_id)
);

CREATE TABLE IF NOT EXISTS memory_facet (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  entity_id TEXT NOT NULL REFERENCES memory_node(id),
  lens_id TEXT NOT NULL REFERENCES memory_lens(id),
  revision INTEGER NOT NULL DEFAULT 1,
  description TEXT NOT NULL,
  supporting_claims_json TEXT NOT NULL DEFAULT '[]',
  claims_hash TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'active',
  embedding_fingerprint TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE(scope_id, entity_id, lens_id)
);

CREATE TABLE IF NOT EXISTS memory_path_schema (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  signature TEXT NOT NULL,
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, signature)
);

CREATE TABLE IF NOT EXISTS memory_path (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  revision INTEGER NOT NULL DEFAULT 1,
  schema_id TEXT NOT NULL REFERENCES memory_path_schema(id),
  task_type TEXT NOT NULL DEFAULT '',
  state TEXT NOT NULL DEFAULT 'candidate',
  intent_sample TEXT NOT NULL DEFAULT '',
  access_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_path_claim (
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  ordinal INTEGER NOT NULL,
  claim_id TEXT NOT NULL,
  claim_revision TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (path_id, ordinal)
);

CREATE TABLE IF NOT EXISTS memory_path_stats (
  path_id TEXT PRIMARY KEY REFERENCES memory_path(id),
  success INTEGER NOT NULL DEFAULT 0,
  failure INTEGER NOT NULL DEFAULT 0,
  last_tau INTEGER NOT NULL DEFAULT 0,
  uses INTEGER NOT NULL DEFAULT 0,
  distinct_inputs INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS memory_path_input (
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  input_hash TEXT NOT NULL,
  PRIMARY KEY (path_id, input_hash)
);

CREATE TABLE IF NOT EXISTS memory_path_event (
  trace_id TEXT PRIMARY KEY,
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  input_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS memory_claim_path (
  claim_id TEXT NOT NULL,
  path_id TEXT NOT NULL REFERENCES memory_path(id),
  PRIMARY KEY (claim_id, path_id)
);

CREATE TABLE IF NOT EXISTS memory_claim_extraction (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  passage_id TEXT NOT NULL,
  passage_hash TEXT NOT NULL,
  extractor_fingerprint TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'proposed',
  error TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  UNIQUE(scope_id, passage_hash, extractor_fingerprint)
);

CREATE VIRTUAL TABLE IF NOT EXISTS memory_passage_fts USING fts5(
  passage_id UNINDEXED,
  text,
  uri UNINDEXED,
  source_id UNINDEXED
);

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

-- Fase 1: chaves de operacao idempotentes (proposta, promocao e ingestao).
CREATE TABLE IF NOT EXISTS memory_operation (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  op_key TEXT NOT NULL,
  payload_hash TEXT NOT NULL,
  result_json TEXT NOT NULL DEFAULT '{}',
  created_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, op_key)
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

-- Fase 3: linhagem de derivacao. A identidade da entrada e o id fisico
-- (versao de fonte, trecho, afirmacao, proposta etc.), nunca apenas a URI.
CREATE TABLE IF NOT EXISTS memory_lineage (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  derived_kind TEXT NOT NULL,
  derived_id TEXT NOT NULL,
  input_kind TEXT NOT NULL,
  input_id TEXT NOT NULL,
  created_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, derived_kind, derived_id, input_kind, input_id)
);
CREATE INDEX IF NOT EXISTS idx_memory_lineage_input
  ON memory_lineage(scope_id, input_kind, input_id);
CREATE INDEX IF NOT EXISTS idx_memory_lineage_derived
  ON memory_lineage(scope_id, derived_kind, derived_id);

-- Fase 3: lapides guardam identidades e hashes, nunca conteudo.
CREATE TABLE IF NOT EXISTS memory_tombstone (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  object_kind TEXT NOT NULL,
  object_id TEXT NOT NULL,
  source_uri TEXT NOT NULL DEFAULT '',
  content_hash TEXT NOT NULL DEFAULT '',
  reason TEXT NOT NULL DEFAULT '',
  requested_by TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, object_kind, object_id)
);

-- Fase 3: fila idempotente de reconstrucao/limpeza.
CREATE TABLE IF NOT EXISTS memory_job (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  kind TEXT NOT NULL,
  payload_json TEXT NOT NULL DEFAULT '{}',
  status TEXT NOT NULL DEFAULT 'pending'
    CHECK (status IN ('pending','running','done','dead')),
  attempts INTEGER NOT NULL DEFAULT 0,
  lease_owner TEXT NOT NULL DEFAULT '',
  lease_until TEXT,
  last_error TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_memory_job_scope_status
  ON memory_job(scope_id, status, created_at);
CREATE INDEX IF NOT EXISTS idx_memory_job_lease
  ON memory_job(scope_id, status, lease_until);

-- Fase 3: sequencia dedicada de remocoes, independente de knowledge_version.
CREATE TABLE IF NOT EXISTS memory_removal_log (
  removal_seq INTEGER PRIMARY KEY AUTOINCREMENT,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  item_kind TEXT NOT NULL,
  item_id TEXT NOT NULL,
  reason TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_memory_removal_log_scope
  ON memory_removal_log(scope_id, removal_seq);

-- Fase 4: uma epoca e um conjunto publicado de itens; os bytes do prefixo
-- dependem tambem da skill e do molde, nao so desta tabela.
CREATE TABLE IF NOT EXISTS memory_prefix_epoch (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  epoch INTEGER NOT NULL,
  prefix_key TEXT NOT NULL,
  content_hash TEXT NOT NULL,
  item_ids_json TEXT NOT NULL DEFAULT '[]',
  published_at TEXT NOT NULL,
  PRIMARY KEY (scope_id, prefix_key, epoch)
);
CREATE INDEX IF NOT EXISTS idx_memory_prefix_epoch_latest
  ON memory_prefix_epoch(scope_id, prefix_key, epoch DESC);

-- Fase 5: livro de credito fracionario no banco da telemetria. As tabelas
-- nascem no mesmo esquema para manter o purge e o backfill uniformes.
CREATE TABLE IF NOT EXISTS memory_credit_event (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  trace_id TEXT NOT NULL,
  item_key TEXT NOT NULL,
  task_type TEXT NOT NULL DEFAULT '',
  model_tier TEXT NOT NULL DEFAULT '',
  delta_success REAL NOT NULL DEFAULT 0,
  delta_failure REAL NOT NULL DEFAULT 0,
  kind TEXT NOT NULL,
  origin TEXT NOT NULL DEFAULT '',
  capped INTEGER NOT NULL DEFAULT 0,
  audit_id TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL,
  UNIQUE(trace_id, item_key, kind, audit_id)
);
CREATE INDEX IF NOT EXISTS idx_memory_credit_event_scope
  ON memory_credit_event(scope_id, item_key, task_type);
CREATE INDEX IF NOT EXISTS idx_memory_credit_event_trace
  ON memory_credit_event(scope_id, trace_id);

CREATE TABLE IF NOT EXISTS memory_item_utility (
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  item_key TEXT NOT NULL,
  task_type TEXT NOT NULL DEFAULT '',
  model_tier TEXT NOT NULL DEFAULT '',
  s REAL NOT NULL DEFAULT 0,
  f REAL NOT NULL DEFAULT 0,
  low_res REAL NOT NULL DEFAULT 0,
  n_events INTEGER NOT NULL DEFAULT 0,
  updated_at TEXT NOT NULL,
  PRIMARY KEY(scope_id, item_key, task_type, model_tier)
);

CREATE TABLE IF NOT EXISTS memory_credit_audit (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  trace_id TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',
  arms_json TEXT NOT NULL DEFAULT '[]',
  reason TEXT NOT NULL DEFAULT '',
  tokens INTEGER NOT NULL DEFAULT 0,
  snapshot_ref TEXT NOT NULL DEFAULT '',
  input_hash TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_memory_credit_audit_scope
  ON memory_credit_audit(scope_id, status, created_at);

CREATE TABLE IF NOT EXISTS memory_validator_audit (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  validator TEXT NOT NULL,
  trace_id TEXT NOT NULL,
  reviewer TEXT NOT NULL DEFAULT '',
  verdict TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_memory_validator_audit_validator
  ON memory_validator_audit(scope_id, validator, created_at);

CREATE TABLE IF NOT EXISTS memory_task_snapshot (
  trace_id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  input_hash TEXT NOT NULL DEFAULT '',
  input_json TEXT NOT NULL DEFAULT '{}',
  workspace_kind TEXT NOT NULL DEFAULT '',
  workspace_ref TEXT NOT NULL DEFAULT '',
  bytes INTEGER NOT NULL DEFAULT 0,
  created_at TEXT NOT NULL,
  expires_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_memory_task_snapshot_scope
  ON memory_task_snapshot(scope_id, expires_at);

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
CREATE INDEX IF NOT EXISTS idx_memory_edge_functional
  ON memory_edge(scope_id, src_id, relation, status, valid_from);
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
CREATE INDEX IF NOT EXISTS idx_memory_source_uri ON memory_source(scope_id, uri, status);
CREATE INDEX IF NOT EXISTS idx_memory_passage_source ON memory_passage(source_id, status);
CREATE INDEX IF NOT EXISTS idx_memory_edge_lens_lens ON memory_edge_lens(lens_id, edge_id);
CREATE INDEX IF NOT EXISTS idx_memory_facet_entity ON memory_facet(scope_id, entity_id, lens_id);
CREATE INDEX IF NOT EXISTS idx_memory_path_state ON memory_path(scope_id, state, task_type);
CREATE INDEX IF NOT EXISTS idx_memory_claim_path_path ON memory_claim_path(path_id);
)SQL", &error)) {
        return false;
    }
    if (!exec("INSERT OR IGNORE INTO memory_meta(key,value) VALUES('knowledge_version','0');", &error)) {
        return false;
    }
    addColumnIfMissing(db_, "memory_usage", "retries", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_usage", "outcome", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_edge", "utility", "REAL NOT NULL DEFAULT 0.5");
    addColumnIfMissing(db_, "memory_usage", "cached_tokens", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_usage", "cache_write_tokens", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_usage", "ab_arm", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_usage", "ab_repetition", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_episode", "ab_arm", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "ab_repetition", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_validation", "ab_arm", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_validation", "ab_repetition", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_hit", "ab_arm", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_hit", "ab_repetition", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_episode", "task_type", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "outcome", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "error_class", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "input_hash", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_episode", "intent_template_hash", "TEXT NOT NULL DEFAULT ''");
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
    // Fase 2: identidade da proposicao e chave de contexto canonica.
    addColumnIfMissing(db_, "memory_edge", "context_key", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_edge", "claim_key", "TEXT NOT NULL DEFAULT ''");
    addColumnIfMissing(db_, "memory_edge", "retracted_at", "TEXT");
    addColumnIfMissing(db_, "memory_playbook", "source_ref", "TEXT");
    addColumnIfMissing(db_, "memory_playbook", "evidence_json", "TEXT NOT NULL DEFAULT '[]'");
    // E2: separa "consultado por busca" de "citado pelo agente" e conta
    // citacoes de ids que nao foram entregues na execucao.
    addColumnIfMissing(db_, "memory_hit", "consulted", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_usage", "invalid_citations", "INTEGER NOT NULL DEFAULT 0");
    addColumnIfMissing(db_, "memory_credit_audit", "reason", "TEXT NOT NULL DEFAULT ''");

    // E4: reconstroi memory_evidence quando ela ainda tem o esquema antigo
    // (edge_id/episode_id com FK). O instantaneo novo nao referencia episodio,
    // entao a retencao da telemetria nao o apaga.
    auto tableHasColumn = [&](const char* table, const char* column) {
        sqlite3_stmt* probe = nullptr;
        bool found = false;
        const std::string sql = std::string("PRAGMA table_info(") + table + ")";
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &probe, nullptr) == SQLITE_OK) {
            while (sqlite3_step(probe) == SQLITE_ROW) {
                const unsigned char* name = sqlite3_column_text(probe, 1);
                if (name && std::string(reinterpret_cast<const char*>(name)) == column) {
                    found = true;
                    break;
                }
            }
            sqlite3_finalize(probe);
        }
        return found;
    };
    if (!tableHasColumn("memory_evidence", "target_kind")) {
        std::string evidenceError;
        if (!exec("ALTER TABLE memory_evidence RENAME TO memory_evidence_legacy;", &evidenceError) ||
            !exec(R"SQL(
CREATE TABLE memory_evidence (
  id TEXT PRIMARY KEY,
  scope_id TEXT NOT NULL REFERENCES memory_scope(id),
  target_kind TEXT NOT NULL,
  target_id TEXT NOT NULL,
  action TEXT NOT NULL DEFAULT 'promote',
  episode_id TEXT,
  run_id TEXT,
  node_id TEXT,
  input_hash TEXT,
  validator_id TEXT,
  validator_version TEXT,
  ok INTEGER NOT NULL DEFAULT 0,
  artifact_hash TEXT,
  excerpt_hash TEXT,
  summary TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL
);
)SQL", &evidenceError) ||
            !exec(R"SQL(
INSERT INTO memory_evidence(id,scope_id,target_kind,target_id,action,episode_id,
                            summary,created_at)
SELECT o.id,
       COALESCE((SELECT e.scope_id FROM memory_edge e WHERE e.id=o.edge_id),
                (SELECT id FROM memory_scope LIMIT 1), 'project:default'),
       'edge', o.edge_id, 'promote', o.episode_id,
       COALESCE(o.source_ref, 'legacy evidence'), o.created_at
FROM memory_evidence_legacy o;
)SQL", &evidenceError) ||
            !exec("DROP TABLE memory_evidence_legacy;", &evidenceError)) {
            lastError_ = "migracao de memory_evidence falhou: " + evidenceError;
            return false;
        }
    }
    if (!exec("CREATE INDEX IF NOT EXISTS idx_memory_evidence_target "
              "ON memory_evidence(scope_id, target_kind, target_id);", &error)) {
        return false;
    }

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

    // Backfill idempotente: linhas antigas de busca foram gravadas com used=1.
    // consulted=1 preserva o sinal sem inventar citacao (used fica como estava).
    std::string backfillError;
    if (!exec("UPDATE memory_hit SET consulted=1 WHERE injected=0 AND used=1 AND consulted=0;",
              &backfillError)) {
        lastError_ = backfillError;
    }

    // Fase 2, migracao 6: a posicao funcional inclui o contexto canonico.
    int versionForIndex = 0;
    {
        const std::string versionText = getMeta("schema_version");
        if (!versionText.empty()) {
            try { versionForIndex = std::stoi(versionText); } catch (...) { versionForIndex = 0; }
        }
    }
    if (versionForIndex < 6) {
        if (!exec("DROP INDEX IF EXISTS idx_memory_edge_functional; "
                  "CREATE INDEX IF NOT EXISTS idx_memory_edge_functional "
                  "ON memory_edge(scope_id, src_id, relation, context_key, status, valid_from); "
                  "CREATE INDEX IF NOT EXISTS idx_memory_edge_claim_key "
                  "ON memory_edge(scope_id, claim_key);", &error)) {
            return false;
        }
    }

    // Fase 2: backfill de context_key/claim_key nas arestas existentes. A
    // identidade nova nao reabre nem encerra nada por si so. Roda so na
    // migracao para nao alterar um banco v6 congelado (A/B).
    int existingVersion = 0;
    {
        const std::string versionText = getMeta("schema_version");
        if (!versionText.empty()) {
            try { existingVersion = std::stoi(versionText); } catch (...) { existingVersion = 0; }
        }
    }
    if (existingVersion < 7) {
        Stmt scopes;
        std::string scopeError;
        if (prepare(db_, "SELECT id FROM memory_scope", scopes, scopeError)) {
            std::vector<std::string> scopeIds;
            while (stepRetry(scopes.stmt) == SQLITE_ROW) scopeIds.push_back(columnText(scopes.stmt, 0));
            for (const auto& scopeId : scopeIds) {
                taskforge::Json lineageReport;
                backfillLineage(scopeId, &lineageReport);
            }
        }
    }
    if (existingVersion < 6) {
        MemoryCatalog& catalog = MemoryCatalog::instance();
        Stmt pending;
        std::string backfillError;
        if (prepare(db_, "SELECT id,scope_id,src_id,COALESCE(dst_id,''),relation,"
                           "COALESCE(value_json,''),metadata,context_key,claim_key "
                           "FROM memory_edge WHERE context_key='' OR claim_key=''",
                     pending, backfillError)) {
            struct PendingEdge {
                std::string id, scopeId, srcId, dstId, relation, valueJson, metadata;
            };
            std::vector<PendingEdge> rows;
            while (stepRetry(pending.stmt) == SQLITE_ROW) {
                rows.push_back({columnText(pending.stmt, 0), columnText(pending.stmt, 1),
                                columnText(pending.stmt, 2), columnText(pending.stmt, 3),
                                columnText(pending.stmt, 4), columnText(pending.stmt, 5),
                                columnText(pending.stmt, 6)});
            }
            for (const auto& row : rows) {
                if (isStructuralPredicate(row.relation)) continue;
                auto metadataOpt = taskforge::jsonutil::tryParse(row.metadata);
                taskforge::Json metadata = metadataOpt && metadataOpt->is_object()
                    ? *metadataOpt : taskforge::Json::object();
                auto valueOpt = taskforge::jsonutil::tryParse(row.valueJson);
                taskforge::Json value = valueOpt ? *valueOpt : taskforge::Json();
                taskforge::Json qualifiers = metadata.contains("qualifiers") &&
                    metadata["qualifiers"].is_object() ? metadata["qualifiers"] : taskforge::Json::object();
                std::string contextKey;
                std::string contextError;
                catalog.canonicalContext(row.relation, qualifiers, contextKey, contextError);
                std::string claimKey;
                std::string keyError;
                std::string valueText;
                std::string unit;
                std::string polarity = "positive";
                if (value.is_object()) {
                    valueText = value.value("value", std::string());
                    unit = value.value("unit", std::string());
                    polarity = value.value("polarity", std::string("positive"));
                } else if (value.is_string()) {
                    valueText = value.str();
                }
                if (!catalog.canonicalClaimKey(row.scopeId, row.srcId, row.relation, row.dstId,
                                               valueText, valueText, unit, contextKey, polarity,
                                               claimKey, keyError)) {
                    metadata["claimKeyStatus"] = "uncomputable";
                }
                if (!catalog.isKnown(row.relation)) metadata["catalogStatus"] = "unknown";
                else if (contextError == "missing_context") metadata["catalogStatus"] = "missing_context";
                if (!contextKey.empty()) metadata["contextKey"] = contextKey;
                if (!claimKey.empty()) metadata["claimKey"] = claimKey;
                metadata["catalogFingerprint"] = catalog.fingerprint();
                Stmt update;
                std::string updateError;
                if (prepare(db_, "UPDATE memory_edge SET context_key=?,claim_key=?,metadata=? "
                                   "WHERE id=?", update, updateError)) {
                    bindText(update.stmt, 1, contextKey);
                    bindText(update.stmt, 2, claimKey);
                    bindText(update.stmt, 3, metadata.dump());
                    bindText(update.stmt, 4, row.id);
                    stepRetry(update.stmt);
                }
            }
        }
    }

    // Fase 1, migracao 5: resolve duplicatas ativas antes de impor a unicidade.
    // A mais antiga permanece ativa; as demais viram historia com superseded_by.
    {
        bool hasUniqueEdgeHash = false;
        sqlite3_stmt* probe = nullptr;
        if (sqlite3_prepare_v2(db_,
                "SELECT 1 FROM sqlite_master WHERE type='index' AND name='idx_memory_edge_active_hash';",
                -1, &probe, nullptr) == SQLITE_OK) {
            hasUniqueEdgeHash = sqlite3_step(probe) == SQLITE_ROW;
            sqlite3_finalize(probe);
        }
        if (!hasUniqueEdgeHash) {
            std::vector<std::pair<std::string, std::string>> duplicates;
            Stmt groups;
            std::string groupError;
            if (prepare(db_,
                    "SELECT scope_id, edge_hash FROM memory_edge WHERE status='active' "
                    "GROUP BY scope_id, edge_hash HAVING COUNT(*)>1",
                    groups, groupError)) {
                while (stepRetry(groups.stmt) == SQLITE_ROW) {
                    duplicates.emplace_back(columnText(groups.stmt, 0),
                                            columnText(groups.stmt, 1));
                }
            }
            for (const auto& group : duplicates) {
                Stmt rows;
                std::string rowError;
                std::vector<std::string> ids;
                if (!prepare(db_,
                        "SELECT id FROM memory_edge WHERE scope_id=? AND edge_hash=? "
                        "AND status='active' ORDER BY recorded_at, id",
                        rows, rowError)) {
                    lastError_ = rowError;
                    return false;
                }
                bindText(rows.stmt, 1, group.first);
                bindText(rows.stmt, 2, group.second);
                while (stepRetry(rows.stmt) == SQLITE_ROW) {
                    ids.push_back(columnText(rows.stmt, 0));
                }
                if (ids.size() < 2) continue;
                const std::string keep = ids.front();
                for (size_t i = 1; i < ids.size(); ++i) {
                    Stmt update;
                    std::string updateError;
                    if (!prepare(db_,
                            "UPDATE memory_edge SET status='superseded', "
                            "valid_to=COALESCE(valid_to,?), superseded_by=?, "
                            "metadata=json_set(metadata,'$.invalidationReason','duplicate_edge_hash') "
                            "WHERE id=? AND status='active'",
                            update, updateError)) {
                        lastError_ = updateError;
                        return false;
                    }
                    bindText(update.stmt, 1, nowIso());
                    bindText(update.stmt, 2, keep);
                    bindText(update.stmt, 3, ids[i]);
                    if (stepRetry(update.stmt) != SQLITE_DONE) {
                        lastError_ = sqlite3_errmsg(db_);
                        return false;
                    }
                    audit("migration", "dedupe_edge_hash", ids[i], "", keep);
                }
            }
            if (!exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_memory_edge_active_hash "
                      "ON memory_edge(scope_id, edge_hash) WHERE status='active';",
                      &error)) {
                lastError_ = error;
                return false;
            }
        }
    }

    int storedVersion = 0;
    const std::string versionText = getMeta("schema_version");
    if (!versionText.empty()) {
        try { storedVersion = std::stoi(versionText); } catch (...) { storedVersion = 0; }
    }
    if (storedVersion < kMemorySchemaVersion) {
        WriteTxn txn(*this, error);
        if (!txn.ok()) return false;
        int currentVersion = 0;
        const std::string currentText = getMeta("schema_version");
        if (!currentText.empty()) {
            try { currentVersion = std::stoi(currentText); } catch (...) { currentVersion = 0; }
        }
        if (currentVersion < kMemorySchemaVersion) {
        // Fase 5: knowledge_version so muda quando uma coluna de conhecimento
        // muda. Contadores de uso, utilidade, last_used e estatisticas de
        // caminho ficam no banco de conhecimento por compatibilidade, mas nao
        // invalidam o prefixo nem o cache de brief.
        const char* allTables[] = {
            "memory_scope", "memory_node", "memory_alias", "memory_edge",
            "memory_working", "memory_state", "memory_state_history",
            "memory_playbook", "memory_evidence", "memory_source",
            "memory_passage", "memory_lens", "memory_edge_lens", "memory_facet",
            "memory_path_schema", "memory_path", "memory_path_claim",
            "memory_path_stats", "memory_path_input", "memory_path_event",
            "memory_claim_path", "memory_lineage", "memory_tombstone",
            "memory_removal_log"};
        std::string drops;
        for (const char* table : allTables) {
            for (const char* event : {"INSERT", "UPDATE", "DELETE"}) {
                drops += "DROP TRIGGER IF EXISTS trg_kv_" + std::string(table) + "_" +
                         event + ";\n";
            }
        }
        if (!exec(drops, &error)) return false;

        const char* tables[] = {
            "memory_scope", "memory_node", "memory_alias", "memory_edge",
            "memory_working", "memory_state", "memory_state_history",
            "memory_playbook", "memory_evidence", "memory_source",
            "memory_passage", "memory_lens", "memory_edge_lens", "memory_facet",
            "memory_path_schema", "memory_path", "memory_path_claim",
            "memory_path_input", "memory_claim_path", "memory_lineage",
            "memory_tombstone", "memory_removal_log"};
        const std::string edgeKnowledgeColumns =
            "scope_id,src_id,dst_id,relation,value_json,valid_from,valid_to,"
            "recorded_at,confidence,status,superseded_by,source_kind,source_ref,"
            "evidence_hash,edge_hash,metadata,context_key,claim_key,dormant_at,retracted_at";
        const std::string playbookKnowledgeColumns =
            "scope_id,task_type,title,steps_json,preconditions_json,status,"
            "source_ref,evidence_json,created_at,updated_at";
        std::string triggers;
        auto bump = [](const std::string& name, const std::string& event,
                       const std::string& table, const std::string& columns) {
            std::string sql = "CREATE TRIGGER " + name + " AFTER " + event;
            if (!columns.empty()) sql += " OF " + columns;
            sql += " ON " + table +
                   " BEGIN UPDATE memory_meta SET value=CAST(value AS INTEGER)+1 "
                   "WHERE key='knowledge_version'; END;\n";
            return sql;
        };
        for (const char* table : tables) {
            const std::string name = std::string("trg_kv_") + table;
            triggers += bump(name + "_INSERT", "INSERT", table, "");
            if (std::string(table) == "memory_edge") {
                triggers += bump(name + "_UPDATE", "UPDATE", table, edgeKnowledgeColumns);
            } else if (std::string(table) == "memory_playbook") {
                triggers += bump(name + "_UPDATE", "UPDATE", table, playbookKnowledgeColumns);
            } else {
                triggers += bump(name + "_UPDATE", "UPDATE", table, "");
            }
            triggers += bump(name + "_DELETE", "DELETE", table, "");
        }
        if (!exec(triggers, &error)) return false;
        setMeta("schema_version", std::to_string(kMemorySchemaVersion));
        }
        if (!txn.commit(&error)) return false;
    }
    if (durableSync_ && getMeta("catalog_fingerprint").empty()) {
        setMeta("catalog_fingerprint", MemoryCatalog::instance().fingerprint());
        setMeta("catalog_mode", MemoryCatalog::instance().mode());
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
    WriteTxn txn(*this, error);
    if (!txn.ok()) {
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
    if (!txn.commit(&error)) {
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
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
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return false;
    }

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

    // E4: evidencia e autocontida e nao tem FK para episodio; o expurgo
    // explicito do escopo remove os instantaneos junto com o conhecimento.
    execScoped("DELETE FROM memory_evidence WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_job WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_lineage WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_removal_log WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_prefix_epoch WHERE scope_id=?", 1);
    if (options.dropTombstones) {
        execScoped("DELETE FROM memory_tombstone WHERE scope_id=?", 1);
    }
    execScoped("DELETE FROM memory_hit WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_claim_extraction WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_claim_path WHERE path_id IN "
               "(SELECT id FROM memory_path WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_path_event WHERE path_id IN "
               "(SELECT id FROM memory_path WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_path_input WHERE path_id IN "
               "(SELECT id FROM memory_path WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_path_stats WHERE path_id IN "
               "(SELECT id FROM memory_path WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_path_claim WHERE path_id IN "
               "(SELECT id FROM memory_path WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_path WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_path_schema WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_facet WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_edge_lens WHERE edge_id IN "
               "(SELECT id FROM memory_edge WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_lens WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_passage_fts WHERE source_id IN "
               "(SELECT id FROM memory_source WHERE scope_id=?)", 1);
    execScoped("DELETE FROM memory_passage WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_source WHERE scope_id=?", 1);
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
    execScoped("DELETE FROM memory_trace WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_operation WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_edge WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_episode WHERE scope_id=?", 1);
    execScoped("DELETE FROM memory_node WHERE scope_id=?", 1);

    if (!ok) {
        lastError_ = lastError_.empty() ? "falha no expurgo do escopo" : lastError_;
        return false;
    }
    {
        Stmt removal;
        std::string removalError;
        if (prepare(db_, "INSERT INTO memory_removal_log"
                           "(scope_id,item_kind,item_id,reason,created_at) VALUES(?,?,?,?,?)",
                     removal, removalError)) {
            bindText(removal.stmt, 1, scopeId);
            bindText(removal.stmt, 2, "scope");
            bindText(removal.stmt, 3, scopeId);
            bindText(removal.stmt, 4, "purge");
            bindText(removal.stmt, 5, nowIso());
            stepRetry(removal.stmt);
        }
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return false;
    }
    // Checkpoint e VACUUM ficam fora de qualquer transacao, como o SQLite exige.
    std::string maintenanceError;
    if (!exec("PRAGMA wal_checkpoint(TRUNCATE);", &maintenanceError)) {
        // Bancos :memory: e journal DELETE nao tem WAL; nao e falha do expurgo.
        if (maintenanceError.find("database is locked") != std::string::npos) {
            lastError_ = maintenanceError;
            return false;
        }
    }
    if (options.vacuum && !exec("VACUUM;", &maintenanceError)) {
        lastError_ = maintenanceError;
        return false;
    }
    return true;
}

std::vector<std::string> MemoryStore::purgeCoverageMissing() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> missing;
    if (!db_) return missing;
    std::set<std::string> covered;
    for (const char* table : kScopedKnowledgeTables) covered.insert(table);
    Stmt tables;
    std::string error;
    if (!prepare(db_, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name",
                 tables, error)) {
        return missing;
    }
    std::vector<std::string> names;
    while (stepRetry(tables.stmt) == SQLITE_ROW) names.push_back(columnText(tables.stmt, 0));
    for (const auto& name : names) {
        if (name.rfind("sqlite_", 0) == 0 || name == "memory_tombstone") continue;
        sqlite3_stmt* probe = nullptr;
        const std::string sql = "PRAGMA table_info(" + name + ")";
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &probe, nullptr) != SQLITE_OK) continue;
        bool hasScope = false;
        while (sqlite3_step(probe) == SQLITE_ROW) {
            const unsigned char* column = sqlite3_column_text(probe, 1);
            if (column && std::string(reinterpret_cast<const char*>(column)) == "scope_id") {
                hasScope = true;
                break;
            }
        }
        sqlite3_finalize(probe);
        if (hasScope && covered.count(name) == 0) missing.push_back(name);
    }
    return missing;
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
                                    const std::string& sourceRef, const taskforge::Json& metadata,
                                    const std::string& contextKeyArg,
                                    const std::string& claimKeyArg) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return "";
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return "";
    }
    taskforge::Json cleanValue = value;
    taskforge::Json cleanMetadata = metadata;
    size_t redactions = sanitizeJsonInPlace(cleanValue);
    redactions += sanitizeJsonInPlace(cleanMetadata);
    const std::string valueJson = cleanValue.is_null() ? "" : cleanValue.dump();
    const std::string from = validFrom.empty() ? nowIso() : validFrom;
    const std::string hash = edgeHash(scopeId, srcId, dstId, relation, valueJson, from, validTo);

    MemoryCatalog& catalog = MemoryCatalog::instance();
    std::string contextKey = contextKeyArg;
    std::string claimKey = claimKeyArg;
    std::string catalogStatus;
    if (!isStructuralPredicate(relation)) {
        if (!catalog.isKnown(relation)) {
            catalogStatus = "unknown";
        } else {
            taskforge::Json qualifiers = taskforge::Json::object();
            if (cleanMetadata.is_object() && cleanMetadata.contains("qualifiers") &&
                cleanMetadata["qualifiers"].is_object()) {
                qualifiers = cleanMetadata["qualifiers"];
            }
            std::string contextError;
            if (contextKey.empty()) {
                catalog.canonicalContext(relation, qualifiers, contextKey, contextError);
            }
            if (!contextError.empty()) {
                catalogStatus = contextError == "missing_context" ? "missing_context" : "unknown";
            }
        }
        if (claimKey.empty()) {
            std::string objectText;
            std::string valueText;
            std::string unit;
            std::string polarity = "positive";
            if (cleanValue.is_object()) {
                valueText = cleanValue.value("value", std::string());
                unit = cleanValue.value("unit", std::string());
                polarity = cleanValue.value("polarity", std::string("positive"));
                objectText = valueText;
            } else if (cleanValue.is_string()) {
                objectText = cleanValue.str();
                valueText = objectText;
            }
            std::string keyError;
            if (!catalog.canonicalClaimKey(scopeId, srcId, relation, dstId, objectText,
                                           valueText, unit, contextKey, polarity, claimKey,
                                           keyError)) {
                cleanMetadata["claimKeyStatus"] = "uncomputable";
            }
        }
        cleanMetadata["catalogFingerprint"] = catalog.fingerprint();
        if (!catalogStatus.empty()) cleanMetadata["catalogStatus"] = catalogStatus;
        if (!contextKey.empty()) cleanMetadata["contextKey"] = contextKey;
        if (!claimKey.empty()) {
            cleanMetadata["claimKey"] = claimKey;
            std::set<std::string> roots;
            const std::string currentUri = cleanMetadata.value("sourceUri", std::string());
            if (!currentUri.empty()) roots.insert(MemorySources::normalizeUri(currentUri));
            Stmt support;
            std::string supportError;
            if (prepare(db_, "SELECT metadata FROM memory_edge WHERE scope_id=? AND claim_key=? "
                               "AND status='active'", support, supportError)) {
                bindText(support.stmt, 1, scopeId);
                bindText(support.stmt, 2, claimKey);
                while (stepRetry(support.stmt) == SQLITE_ROW) {
                    auto other = taskforge::jsonutil::tryParse(columnText(support.stmt, 0));
                    if (other && other->is_object()) {
                        const std::string uri = other->value("sourceUri", std::string());
                        if (!uri.empty()) roots.insert(MemorySources::normalizeUri(uri));
                    }
                }
            }
            taskforge::Json rootsArray = taskforge::Json::array();
            for (const auto& root : roots) rootsArray.push_back(root);
            cleanMetadata["supportRoots"] = rootsArray;
            cleanMetadata["supportRootCount"] = static_cast<long long>(roots.size());
        }
    }
    auto attachLens = [&](const std::string& edgeId) {
        std::string lensName = relation;
        if (cleanMetadata.is_object()) {
            lensName = cleanMetadata.value("lens",
                cleanMetadata.value("domain", relation));
        }
        if (lensName.empty()) lensName = relation;
        const std::string lensId = ensureLens(scopeId, lensName);
        if (!lensId.empty()) linkEdgeLens(edgeId, lensId);
    };

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND edge_hash=? AND status='active' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, hash);
        if (stepRetry(existing.stmt) == SQLITE_ROW) {
            const std::string existingId = columnText(existing.stmt, 0);
            if (!claimKey.empty()) {
                Stmt metadataStmt;
                std::string metadataError;
                if (prepare(db_, "SELECT metadata FROM memory_edge WHERE id=?", metadataStmt,
                            metadataError)) {
                    bindText(metadataStmt.stmt, 1, existingId);
                    if (stepRetry(metadataStmt.stmt) == SQLITE_ROW) {
                        auto parsed = taskforge::jsonutil::tryParse(columnText(metadataStmt.stmt, 0));
                        taskforge::Json merged = parsed && parsed->is_object()
                            ? *parsed : taskforge::Json::object();
                        std::set<std::string> roots;
                        if (merged.contains("supportRoots") && merged["supportRoots"].is_array()) {
                            for (const auto& root : merged["supportRoots"]) {
                                if (root.is_string()) roots.insert(root.str());
                            }
                        }
                        const std::string oldUri = merged.value("sourceUri", std::string());
                        if (!oldUri.empty()) roots.insert(MemorySources::normalizeUri(oldUri));
                        const std::string newUri = cleanMetadata.value("sourceUri", std::string());
                        if (!newUri.empty()) roots.insert(MemorySources::normalizeUri(newUri));
                        taskforge::Json array = taskforge::Json::array();
                        for (const auto& root : roots) array.push_back(root);
                        merged["supportRoots"] = array;
                        merged["supportRootCount"] = static_cast<long long>(roots.size());
                        merged["claimKey"] = claimKey;
                        merged.erase("claimKeyStatus");
                        Stmt update;
                        std::string updateError;
                        if (prepare(db_, "UPDATE memory_edge SET metadata=?,claim_key=? WHERE id=?",
                                    update, updateError)) {
                            bindText(update.stmt, 1, merged.dump());
                            bindText(update.stmt, 2, claimKey);
                            bindText(update.stmt, 3, existingId);
                            stepRetry(update.stmt);
                        }
                    }
                }
            }
            attachLens(existingId);
            if (!txn.commit(&txnError)) {
                lastError_ = txnError;
                return "";
            }
            return existingId;
        }
    }

    const std::string id = newId("edge");
    if (testInterleaveHook_) testInterleaveHook_("recordEdge:before_insert");
    Stmt st;
    if (!prepare(db_, "INSERT INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,valid_from,valid_to,recorded_at,confidence,status,source_kind,source_ref,edge_hash,metadata,context_key,claim_key) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,edge_hash) WHERE status='active' DO NOTHING",
                    st, error)) {
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
    bindText(st.stmt, 16, contextKey);
    bindText(st.stmt, 17, claimKey);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    std::string insertedId = id;
    if (sqlite3_changes(db_) == 0) {
        Stmt winner;
        std::string winnerError;
        if (!prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND edge_hash=? "
                          "AND status='active' LIMIT 1", winner, winnerError)) {
            lastError_ = winnerError;
            return "";
        }
        bindText(winner.stmt, 1, scopeId);
        bindText(winner.stmt, 2, hash);
        if (stepRetry(winner.stmt) != SQLITE_ROW) {
            lastError_ = "conflito de edge_hash sem vencedor ativo";
            return "";
        }
        insertedId = columnText(winner.stmt, 0);
        attachLens(insertedId);
        if (!txn.commit(&txnError)) {
            lastError_ = txnError;
            return "";
        }
        return insertedId;
    }
    if (redactions > 0) audit("runtime", "redact", scopeId, "", "edge:" + insertedId);
    const bool structural = isStructuralPredicate(relation);
    if (status == "active" && !structural) {
        MemoryEvidenceRecord evidence;
        evidence.scopeId = scopeId;
        evidence.targetKind = "fact";
        evidence.targetId = insertedId;
        evidence.action = "create";
        evidence.ok = true;
        evidence.summary = relation + " " + srcId + " -> " + (dstId.empty() ? valueJson : dstId);
        evidence.excerptHash = hashHex(valueJson.empty() ? relation : valueJson);
        recordEvidence(evidence);
    }
    attachLens(insertedId);
    if (cleanMetadata.is_object()) {
        const std::string passageId = cleanMetadata.value("passageId", std::string());
        if (!passageId.empty()) recordLineage(scopeId, "edge", insertedId, "passage", passageId);
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return "";
    }
    return insertedId;
}

std::string MemoryStore::recordLesson(const std::string& scopeId, const std::string& domain,
                                      const std::string& triggerError, const std::string& lessonRule,
                                      const std::string& sourceItem, const std::string& sourceKind) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || lessonRule.empty()) return "";
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return "";
    }

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
        if (stepRetry(existing.stmt) == SQLITE_ROW) {
            const std::string existingId = columnText(existing.stmt, 0);
            if (!txn.commit(&txnError)) {
                lastError_ = txnError;
                return "";
            }
            return existingId;
        }
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
    if (!id.empty()) {
        MemoryEvidenceRecord evidence;
        evidence.scopeId = scopeId;
        evidence.targetKind = "lesson";
        evidence.targetId = id;
        evidence.action = "create";
        evidence.ok = true;
        evidence.summary = "Licao (" + dom + "): " + cleanRule.text;
        recordEvidence(evidence);
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return "";
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

std::vector<MemoryLesson> MemoryStore::getLessonsForDomains(
    const std::string& scopeId, const std::vector<std::string>& domains) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryLesson> out;
    if (!db_ || domains.empty()) return out;
    std::string sql =
        "SELECT e.id, e.scope_id, d.canonical_name, r.canonical_name, e.recorded_at, e.metadata, e.source_ref, e.confidence "
        "FROM memory_edge e "
        "JOIN memory_node d ON d.id=e.src_id "
        "JOIN memory_node r ON r.id=e.dst_id "
        "WHERE e.scope_id=? AND e.relation='HAS_LESSON' AND e.status='active' AND e.dormant_at IS NULL "
        "AND d.canonical_name IN (";
    for (size_t i = 0; i < domains.size(); ++i) sql += (i ? ",?" : "?");
    sql += ") ORDER BY e.confidence DESC, e.recorded_at DESC, e.id DESC";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql.c_str(), st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    for (size_t i = 0; i < domains.size(); ++i) {
        bindText(st.stmt, static_cast<int>(i + 2), domains[i]);
    }
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
                       "artifacts_json,evidence_json,success_signals_json,redactions,ab_arm,ab_repetition,"
                       "created_at,metadata,intent_template_hash) "
                       "VALUES(?,?,?,?,?,?,?,1,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
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
    const char* abArm = std::getenv("ARESTRIX_AB_ARM");
    bindText(st.stmt, 18, abArm ? abArm : "");
    const char* abRep = std::getenv("ARESTRIX_AB_REPETITION");
    sqlite3_bind_int64(st.stmt, 19, abRep ? std::atoll(abRep) : 0);
    bindText(st.stmt, 20, nowIso());
    bindText(st.stmt, 21, cleanMetadata.dump());
    bindText(st.stmt, 22, episode.intentTemplateHash);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    if (redactions > 0) audit("runtime", "redact", episode.scopeId, "", "episode:" + id);
    return id;
}

std::string MemoryStore::episodeIdFor(const std::string& scopeId, const std::string& runId,
                                      const std::string& nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || runId.empty()) return "";
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id FROM memory_episode WHERE scope_id=? AND run_id=? "
                       "AND COALESCE(node_id,'')=? ORDER BY created_at DESC, id DESC LIMIT 1",
                 st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, runId);
    bindText(st.stmt, 3, nodeId);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnText(st.stmt, 0) : std::string();
}

taskforge::Json MemoryStore::getEpisode(const std::string& scopeId, const std::string& episodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_ || episodeId.empty()) return out;

    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,run_id,COALESCE(node_id,''),COALESCE(item_id,''),kind,content,sanitized,"
                       "task_type,outcome,error_class,input_hash,tool_calls_json,files_touched_json,artifacts_json,"
                       "evidence_json,success_signals_json,redactions,created_at,metadata,intent_template_hash "
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
    out["intentTemplateHash"] = sanitizeText(columnText(st.stmt, 20), 128).text;
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
                       "validator_id,validator_version,ok,score,reasons_json,artifact_hash,"
                       "ab_arm,ab_repetition,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,episode_id,validator_id,validator_version) DO UPDATE SET "
                       "run_id=excluded.run_id,node_id=excluded.node_id,ok=excluded.ok,"
                       "score=excluded.score,reasons_json=excluded.reasons_json,"
                       "artifact_hash=excluded.artifact_hash,ab_arm=excluded.ab_arm,"
                       "ab_repetition=excluded.ab_repetition,created_at=excluded.created_at", st, error)) {
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
    const char* abArm = std::getenv("ARESTRIX_AB_ARM");
    bindText(st.stmt, 12, abArm ? abArm : "");
    const char* abRep = std::getenv("ARESTRIX_AB_REPETITION");
    sqlite3_bind_int64(st.stmt, 13, abRep ? std::atoll(abRep) : 0);
    bindText(st.stmt, 14, now);
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

std::string MemoryStore::recordEvidence(const MemoryEvidenceRecord& evidence) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || evidence.scopeId.empty() || evidence.targetKind.empty() ||
        evidence.targetId.empty()) {
        return "";
    }
    MemoryEvidenceRecord copy = evidence;
    copy.summary = sanitizeText(copy.summary, 4000).text;
    const std::string action = copy.action.empty() ? "promote" : copy.action;
    if (copy.excerptHash.empty()) {
        copy.excerptHash = hashHex(copy.targetKind + "|" + copy.targetId + "|" +
                                   action + "|" + copy.summary);
    }
    const std::string id = copy.id.empty() ? newId("ev") : copy.id;
    const std::string now = copy.createdAt.empty() ? nowIso() : copy.createdAt;

    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_evidence(id,scope_id,target_kind,target_id,action,"
                       "episode_id,run_id,node_id,input_hash,validator_id,validator_version,"
                       "ok,artifact_hash,excerpt_hash,summary,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, copy.scopeId);
    bindText(st.stmt, 3, copy.targetKind);
    bindText(st.stmt, 4, copy.targetId);
    bindText(st.stmt, 5, action);
    bindNullableText(st.stmt, 6, copy.episodeId);
    bindNullableText(st.stmt, 7, copy.runId);
    bindNullableText(st.stmt, 8, copy.nodeId);
    bindNullableText(st.stmt, 9, copy.inputHash);
    bindNullableText(st.stmt, 10, copy.validatorId);
    bindNullableText(st.stmt, 11, copy.validatorVersion);
    sqlite3_bind_int(st.stmt, 12, copy.ok ? 1 : 0);
    bindNullableText(st.stmt, 13, copy.artifactHash);
    bindText(st.stmt, 14, copy.excerptHash);
    bindText(st.stmt, 15, copy.summary);
    bindText(st.stmt, 16, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    return id;
}

std::vector<MemoryEvidenceRecord> MemoryStore::listEvidence(const std::string& scopeId,
                                                            const std::string& targetKind,
                                                            const std::string& targetId,
                                                            size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEvidenceRecord> out;
    if (!db_ || scopeId.empty()) return out;
    std::string sql = "SELECT id,scope_id,target_kind,target_id,action,COALESCE(episode_id,''),"
                      "COALESCE(run_id,''),COALESCE(node_id,''),COALESCE(input_hash,''),"
                      "COALESCE(validator_id,''),COALESCE(validator_version,''),ok,"
                      "COALESCE(artifact_hash,''),COALESCE(excerpt_hash,''),summary,created_at "
                      "FROM memory_evidence WHERE scope_id=?";
    int index = 2;
    if (!targetKind.empty()) { sql += " AND target_kind=?"; ++index; }
    if (!targetId.empty()) { sql += " AND target_id=?"; ++index; }
    sql += " ORDER BY created_at DESC, id DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (!targetKind.empty()) bindText(st.stmt, 2, targetKind);
    if (!targetId.empty()) bindText(st.stmt, targetKind.empty() ? 2 : 3, targetId);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(std::min<size_t>(limit == 0 ? 100 : limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryEvidenceRecord record;
        record.id = columnText(st.stmt, 0);
        record.scopeId = columnText(st.stmt, 1);
        record.targetKind = columnText(st.stmt, 2);
        record.targetId = columnText(st.stmt, 3);
        record.action = columnText(st.stmt, 4);
        record.episodeId = columnText(st.stmt, 5);
        record.runId = columnText(st.stmt, 6);
        record.nodeId = columnText(st.stmt, 7);
        record.inputHash = columnText(st.stmt, 8);
        record.validatorId = columnText(st.stmt, 9);
        record.validatorVersion = columnText(st.stmt, 10);
        record.ok = columnInt(st.stmt, 11) != 0;
        record.artifactHash = columnText(st.stmt, 12);
        record.excerptHash = columnText(st.stmt, 13);
        record.summary = sanitizeText(columnText(st.stmt, 14), 4000).text;
        record.createdAt = columnText(st.stmt, 15);
        out.push_back(std::move(record));
    }
    return out;
}

taskforge::Json MemoryStore::evidenceSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json{{"snapshots", 0LL}, {"targets", 0LL},
                                          {"byKind", taskforge::Json::object()}};
    if (!db_ || scopeId.empty()) return out;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT COUNT(*), COUNT(DISTINCT target_kind || ':' || target_id) "
                       "FROM memory_evidence WHERE scope_id=?", st, error)) {
        bindText(st.stmt, 1, scopeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            out["snapshots"] = columnInt(st.stmt, 0);
            out["targets"] = columnInt(st.stmt, 1);
        }
    }
    if (prepare(db_, "SELECT target_kind, COUNT(*) FROM memory_evidence WHERE scope_id=? "
                       "GROUP BY target_kind ORDER BY target_kind", st, error)) {
        bindText(st.stmt, 1, scopeId);
        taskforge::Json byKind = taskforge::Json::object();
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            byKind[columnText(st.stmt, 0)] = columnInt(st.stmt, 1);
        }
        out["byKind"] = byKind;
    }
    return out;
}

size_t MemoryStore::evidenceCount(const std::string& scopeId, const std::string& targetKind,
                                  const std::string& targetId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || targetKind.empty() || targetId.empty()) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COUNT(*) FROM memory_evidence WHERE scope_id=? AND target_kind=? "
                       "AND target_id=?", st, error)) {
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, targetKind);
    bindText(st.stmt, 3, targetId);
    return stepRetry(st.stmt) == SQLITE_ROW ? static_cast<size_t>(columnInt(st.stmt, 0)) : 0;
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

std::vector<bool> MemoryStore::validationOutcomesAdjusted(
    const std::string& scopeId, const std::string& taskType, size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<bool> out;
    if (!db_ || taskType.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT v.ok FROM memory_validation v "
                 "JOIN memory_episode e ON e.id=v.episode_id "
                 "LEFT JOIN memory_trace t ON t.scope_id=v.scope_id "
                 "AND t.run_id=v.run_id AND t.node_id=v.node_id "
                 "LEFT JOIN memory_credit_audit a ON a.scope_id=v.scope_id "
                 "AND a.trace_id=t.trace_id AND a.status='applied' "
                 "WHERE v.scope_id=? AND e.scope_id=? AND e.task_type=? "
                 "AND (v.ok=1 OR a.id IS NULL) "
                 "ORDER BY v.created_at DESC LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, taskType);
    sqlite3_bind_int(st.stmt, 4, static_cast<int>(std::min<size_t>(limit, 1000)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back(columnInt(st.stmt, 0) != 0);
    }
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
                      "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES','via_caminho') "
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

    Stmt passages;
    if (prepare(db_, "SELECT p.id,p.text,s.uri,p.created_at FROM memory_passage p "
                       "JOIN memory_source s ON s.id=p.source_id "
                       "WHERE p.scope_id=? AND p.status='active' AND s.status='active' LIMIT 2000",
                 passages, error)) {
        bindText(passages.stmt, 1, scopeId);
        while (stepRetry(passages.stmt) == SQLITE_ROW) {
            MemoryEmbeddingObject object;
            object.objectType = "passage";
            object.objectId = columnText(passages.stmt, 0);
            object.text = sanitizeText(columnText(passages.stmt, 2) + " | " +
                                       columnText(passages.stmt, 1), 8000).text;
            object.createdAt = columnText(passages.stmt, 3);
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
                      "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES','via_caminho') AND ("
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
    for (const auto& item : cleanEvidence) {
        if (!item.is_object()) continue;
        const std::string kind = item.value("kind", std::string("edge"));
        const std::string inputId = item.value("id", item.value("edgeId",
            item.value("lessonId", std::string())));
        if (!inputId.empty()) recordLineage(scopeId, "playbook", id, kind, inputId);
    }
    audit("playbook", "record_playbook", id, "", cleanTask.text);
    if (status == "active") {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = scopeId;
        snapshot.targetKind = "playbook";
        snapshot.targetId = id;
        snapshot.action = "create";
        snapshot.ok = true;
        snapshot.summary = "Playbook (" + cleanTask.text + "): " + cleanTitle.text;
        recordEvidence(snapshot);
    }
    return id;
}

taskforge::Json MemoryStore::recompilePlaybook(const std::string& scopeId,
                                               const std::string& taskType) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"ok", true}, {"created", false}, {"playbookId", ""},
                        {"reason", ""}};
    if (!db_ || scopeId.empty()) {
        out["ok"] = false;
        out["reason"] = "memoria indisponivel";
        return out;
    }
    const auto lessons = getLessons(scopeId);
    std::vector<MemoryLesson> selected;
    for (const auto& lesson : lessons) {
        if (taskType.empty() || lesson.domain == taskType) selected.push_back(lesson);
    }
    if (selected.empty()) selected = lessons;
    if (selected.size() < 3) {
        out["reason"] = "insufficient_lessons";
        return out;
    }
    taskforge::Json steps = taskforge::Json::array();
    taskforge::Json evidence = taskforge::Json::array();
    for (const auto& lesson : selected) {
        steps.push_back(lesson.lessonRule);
        evidence.push_back(taskforge::Json{{"kind", "lesson"}, {"id", lesson.id}});
    }
    const std::string id = recordPlaybook(
        scopeId, taskType, taskType.empty() ? "memoria playbook" : taskType + " playbook",
        steps, taskforge::Json::object(), 0.6, "candidate", "", evidence);
    if (id.empty()) {
        out["ok"] = false;
        out["reason"] = "record_playbook_failed";
        return out;
    }
    out["created"] = true;
    out["playbookId"] = id;
    return out;
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
    {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = scopeId;
        snapshot.targetKind = "playbook";
        snapshot.targetId = playbookId;
        snapshot.action = "promote";
        snapshot.episodeId = episodeId;
        snapshot.validatorId = validatorId;
        snapshot.validatorVersion = validatorVersion;
        snapshot.ok = true;
        snapshot.summary = "Gate promoveu playbook de " + taskType +
                           " com " + std::to_string(inputs.size()) + " entradas distintas";
        recordEvidence(snapshot);
    }
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

    std::vector<std::string> activeIds;
    {
        Stmt probe;
        std::string probeError;
        if (prepare(db_, "SELECT id FROM memory_playbook WHERE scope_id=? AND task_type=? "
                           "AND status='active'", probe, probeError)) {
            bindText(probe.stmt, 1, scopeId);
            bindText(probe.stmt, 2, taskType);
            while (stepRetry(probe.stmt) == SQLITE_ROW) activeIds.push_back(columnText(probe.stmt, 0));
        }
    }

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
    for (const auto& id : activeIds) {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = scopeId;
        snapshot.targetKind = "playbook";
        snapshot.targetId = id;
        snapshot.action = "demote";
        snapshot.ok = false;
        snapshot.summary = "Gate despromoveu playbook de " + taskType + ": " + reason;
        recordEvidence(snapshot);
    }
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
    {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = scopeId;
        snapshot.targetKind = "playbook";
        snapshot.targetId = playbookId;
        snapshot.action = "unpromote";
        snapshot.ok = false;
        snapshot.summary = "Playbook voltou a candidato: " + reason;
        recordEvidence(snapshot);
    }
    return true;
}

std::string MemoryStore::recordProposal(const std::string& scopeId, const std::string& kind,
                                        const taskforge::Json& payload,
                                        const std::string& sourceEpisodeId,
                                        const taskforge::Json& evidence,
                                        double confidence, const std::string& operationKey) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || kind.empty()) return "";
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return "";
    }

    taskforge::Json cleanPayload = payload.is_object() ? payload : taskforge::Json::object();
    taskforge::Json cleanEvidence = evidence.is_array() ? evidence : taskforge::Json::array();
    sanitizeJsonInPlace(cleanPayload);
    sanitizeJsonInPlace(cleanEvidence);
    const std::string payloadDump = cleanPayload.dump();
    const std::string evidenceDump = cleanEvidence.dump();
    const double clamped = std::max(0.0, std::min(1.0, confidence));
    const std::string operationHash = project::RunnerSecurity::sha256(
        kind + "\n" + payloadDump + "\n" + evidenceDump);

    if (!operationKey.empty()) {
        Stmt prior;
        std::string priorError;
        if (!prepare(db_, "SELECT payload_hash,result_json FROM memory_operation "
                          "WHERE scope_id=? AND op_key=? LIMIT 1", prior, priorError)) {
            lastError_ = priorError;
            return "";
        }
        bindText(prior.stmt, 1, scopeId);
        bindText(prior.stmt, 2, operationKey);
        if (stepRetry(prior.stmt) == SQLITE_ROW) {
            if (columnText(prior.stmt, 0) != operationHash) {
                lastError_ = "chave de operacao reutilizada com conteudo diferente";
                return "";
            }
            const auto stored = parseJsonOrNull(columnText(prior.stmt, 1));
            return stored.is_object() ? stored.value("proposalId", std::string()) : std::string();
        }
    }

    Stmt existing;
    std::string error;
    if (prepare(db_, "SELECT id FROM memory_proposal WHERE scope_id=? AND kind=? AND payload_json=? "
                       "AND status='pending' LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, kind);
        bindText(existing.stmt, 3, payloadDump);
        if (stepRetry(existing.stmt) == SQLITE_ROW) {
            const std::string existingId = columnText(existing.stmt, 0);
            if (!txn.commit(&txnError)) {
                lastError_ = txnError;
                return "";
            }
            return existingId;
        }
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
    bindText(st.stmt, 6, evidenceDump);
    sqlite3_bind_double(st.stmt, 7, clamped);
    bindText(st.stmt, 8, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    if (!operationKey.empty()) {
        Stmt operation;
        if (!prepare(db_, "INSERT INTO memory_operation(scope_id,op_key,payload_hash,result_json,created_at) "
                          "VALUES(?,?,?,?,?)", operation, error)) {
            lastError_ = error;
            return "";
        }
        bindText(operation.stmt, 1, scopeId);
        bindText(operation.stmt, 2, operationKey);
        bindText(operation.stmt, 3, operationHash);
        bindText(operation.stmt, 4, taskforge::Json{{"proposalId", id}}.dump());
        bindText(operation.stmt, 5, nowIso());
        if (stepRetry(operation.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return "";
        }
    }
    if (cleanPayload.is_object()) {
        const std::string passageId = cleanPayload.value("passageId", std::string());
        if (!passageId.empty()) recordLineage(scopeId, "proposal", id, "passage", passageId);
    }
    if (cleanEvidence.is_array()) {
        for (const auto& item : cleanEvidence) {
            if (!item.is_object()) continue;
            const std::string passageId = item.value("passageId", std::string());
            if (!passageId.empty()) recordLineage(scopeId, "proposal", id, "passage", passageId);
        }
    }
    audit("extractor", "proposal", scopeId, "", kind + ":" + id);
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return "";
    }
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
                                  std::string* promotedId, const std::string& operationKey) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco de memoria fechado";
        return false;
    }
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        error = txnError;
        return false;
    }
    MemoryProposalRecord proposal;
    if (!loadProposal(scopeId, proposalId, proposal)) {
        error = "proposta nao encontrada";
        return false;
    }
    const std::string operationHash = project::RunnerSecurity::sha256(
        proposal.payloadJson + "\n" + proposal.evidenceJson);
    if (!operationKey.empty()) {
        Stmt prior;
        std::string priorError;
        if (!prepare(db_, "SELECT payload_hash,result_json FROM memory_operation "
                          "WHERE scope_id=? AND op_key=? LIMIT 1", prior, priorError)) {
            error = priorError;
            return false;
        }
        bindText(prior.stmt, 1, scopeId);
        bindText(prior.stmt, 2, operationKey);
        if (stepRetry(prior.stmt) == SQLITE_ROW) {
            const std::string storedHash = columnText(prior.stmt, 0);
            if (storedHash != operationHash) {
                error = "chave de operacao reutilizada com conteudo diferente";
                return false;
            }
            if (promotedId) {
                const auto stored = parseJsonOrNull(columnText(prior.stmt, 1));
                if (stored.is_object()) {
                    *promotedId = stored.value("promotedId", std::string());
                }
            }
            return true;
        }
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
    if (testInterleaveHook_) testInterleaveHook_("promoteProposal:before_write");

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
    } else if (proposal.kind == "claim") {
        const std::string subject = payload.value("subject", std::string());
        const std::string predicate = canonicalPredicateName(payload.value("predicate", std::string()));
        const std::string object = payload.value("object", std::string());
        const std::string value = payload.value("value", std::string());
        if (subject.empty() || predicate.empty() || (object.empty() && value.empty())) {
            error = "claim promovida sem subject/predicate/object ou value";
            return false;
        }
        auto markUnresolved = [&](const std::string& reason) {
            Stmt update;
            std::string updateError;
            if (prepare(db_, "UPDATE memory_proposal SET status='unresolved',reviewed_at=?,"
                               "reviewed_by=? WHERE scope_id=? AND id=? AND status='pending'",
                         update, updateError)) {
                bindText(update.stmt, 1, nowIso());
                bindText(update.stmt, 2, "catalog:" + reason);
                bindText(update.stmt, 3, scopeId);
                bindText(update.stmt, 4, proposal.id);
                stepRetry(update.stmt);
            }
            audit("catalog", "unresolved", proposal.id, "", reason);
            if (!txn.commit(&txnError)) {
                error = txnError;
                return false;
            }
            error = reason;
            return false;
        };
        if (isStructuralPredicate(predicate)) {
            return markUnresolved("structural_predicate");
        }
        MemoryCatalog& catalog = MemoryCatalog::instance();
        if (!catalog.isKnown(predicate) && catalog.mode() == "strict") {
            return markUnresolved("unknown_predicate");
        }
        std::string subjectId = payload.value("subjectId", std::string());
        if (subjectId.empty()) {
            bool ambiguous = false;
            subjectId = findEntity(scopeId, subject, &ambiguous);
            if (subjectId.empty() || ambiguous) {
                return markUnresolved(ambiguous ? "ambiguous_subject" : "unknown_subject");
            }
        }
        std::string objectId = payload.value("objectId", std::string());
        if (!object.empty() && objectId.empty()) {
            bool ambiguous = false;
            objectId = findEntity(scopeId, object, &ambiguous);
            if (objectId.empty() || ambiguous) {
                return markUnresolved(ambiguous ? "ambiguous_object" : "unknown_object");
            }
        }
        const std::string validFrom = payload.value("validFrom", proposal.createdAt);
        const std::string validTo = payload.value("validTo", std::string());
        taskforge::Json claimValue{
            {"value", value.empty() ? taskforge::Json(object) : taskforge::Json(value)},
            {"unit", payload.value("unit", std::string())},
            {"polarity", payload.value("polarity", std::string("positive"))},
            {"qualifiers", payload.contains("qualifiers") ? payload["qualifiers"]
                                                            : taskforge::Json::object()},
            {"quote", payload.value("quote", std::string())},
            {"passageId", payload.value("passageId", std::string())},
            {"sourceHash", payload.value("passageHash", std::string())}};
        taskforge::Json qualifiers = payload.contains("qualifiers") && payload["qualifiers"].is_object()
            ? payload["qualifiers"] : taskforge::Json::object();
        std::string contextKey;
        std::string contextError;
        const bool contextOk = catalog.canonicalContext(predicate, qualifiers, contextKey,
                                                        contextError);
        if (!contextOk && contextError == "missing_context" && catalog.mode() == "strict") {
            return markUnresolved("missing_context");
        }
        std::string claimKey;
        std::string claimKeyError;
        const std::string unit = payload.value("unit", std::string());
        const std::string polarity = payload.value("polarity", std::string("positive"));
        if (!catalog.canonicalClaimKey(scopeId, subjectId, predicate, objectId, object,
                                       value, unit, contextKey, polarity, claimKey,
                                       claimKeyError)) {
            claimKey.clear();
        }
        taskforge::Json metadata{
            {"lens", payload.value("lens", std::string())},
            {"claim", true},
            {"knownPredicate", catalog.isKnown(predicate)},
            {"canonicalPredicate", predicate},
            {"catalogFingerprint", catalog.fingerprint()},
            {"extractorFingerprint", payload.value("extractorFingerprint", std::string())},
            {"extractorModel", payload.value("extractorModel", std::string())},
            {"extractorRevision", payload.value("extractorRevision", std::string())},
            {"passageId", payload.value("passageId", std::string())},
            {"sourceUri", payload.value("sourceUri", std::string())},
            {"quote", payload.value("quote", std::string())}};
        if (!catalog.isKnown(predicate)) metadata["catalogStatus"] = "unknown";
        else if (!contextOk && contextError == "missing_context") {
            metadata["catalogStatus"] = "missing_context";
        } else {
            metadata["catalogStatus"] = "ok";
        }
        if (!contextKey.empty()) metadata["contextKey"] = contextKey;
        if (!claimKey.empty()) metadata["claimKey"] = claimKey;
        else metadata["claimKeyStatus"] = "uncomputable";
        const bool canClose = contextOk && isSingleValuedPredicate(predicate) &&
                              payload.value("claim", true);
        if (canClose) {
            // E13: so encerra valores anteriores quando as validades se
            // sobrepoem; intervalos disjuntos contam historia, nao conflito.
            auto lower = [](const std::string& v) {
                return v.empty() ? std::string("0000-00-00T00:00:00Z") : v;
            };
            auto upper = [](const std::string& v) {
                return v.empty() ? std::string("9999-12-31T23:59:59Z") : v;
            };
            Stmt previous;
            std::vector<std::string> previousIds;
            if (prepare(db_, "SELECT id,valid_from,valid_to,dst_id FROM memory_edge "
                               "WHERE scope_id=? AND src_id=? AND relation=? AND context_key=? "
                               "AND status='active'",
                         previous, error)) {
                bindText(previous.stmt, 1, scopeId);
                bindText(previous.stmt, 2, subjectId);
                bindText(previous.stmt, 3, predicate);
                bindText(previous.stmt, 4, contextKey);
                while (stepRetry(previous.stmt) == SQLITE_ROW) {
                    const std::string otherDst = columnText(previous.stmt, 3);
                    if (!objectId.empty() && otherDst == objectId) continue;
                    if (lower(validFrom) < upper(columnText(previous.stmt, 2)) &&
                        lower(columnText(previous.stmt, 1)) < upper(validTo)) {
                        previousIds.push_back(columnText(previous.stmt, 0));
                    }
                }
            }
            for (const auto& previousId : previousIds) {
                invalidateEdge(previousId, proposal.createdAt, "claim_conflict", "promotion");
            }
        }
        promoted = recordEdge(scopeId, subjectId, objectId, predicate, claimValue,
                              validFrom, validTo, proposal.confidence, "active",
                              "claim", proposal.id, metadata, contextKey, claimKey);
        if (promoted.empty()) {
            error = "falha ao gravar claim promovida";
            return false;
        }
        MemoryEvidenceRecord claimEvidence;
        claimEvidence.scopeId = scopeId;
        claimEvidence.targetKind = "claim";
        claimEvidence.targetId = promoted;
        claimEvidence.action = "promote";
        claimEvidence.episodeId = proposal.sourceEpisodeId;
        claimEvidence.artifactHash = payload.value("passageHash", std::string());
        claimEvidence.excerptHash = project::RunnerSecurity::sha256(
            payload.value("quote", std::string()));
        claimEvidence.summary = payload.value("quote", std::string());
        claimEvidence.ok = true;
        recordEvidence(claimEvidence);
    } else if (proposal.kind == "fact") {
        const std::string subject = payload.value("subject", std::string());
        const std::string predicate = canonicalPredicateName(payload.value("predicate", std::string()));
        const std::string object = payload.value("object", std::string());
        if (subject.empty() || predicate.empty() || object.empty()) {
            error = "fato promovido sem subject/predicate/object";
            return false;
        }
        auto markFactUnresolved = [&](const std::string& reason) {
            Stmt update;
            std::string updateError;
            if (prepare(db_, "UPDATE memory_proposal SET status='unresolved',reviewed_at=?,"
                               "reviewed_by=? WHERE scope_id=? AND id=? AND status='pending'",
                         update, updateError)) {
                bindText(update.stmt, 1, nowIso());
                bindText(update.stmt, 2, "catalog:" + reason);
                bindText(update.stmt, 3, scopeId);
                bindText(update.stmt, 4, proposal.id);
                stepRetry(update.stmt);
            }
            audit("catalog", "unresolved", proposal.id, "", reason);
            if (!txn.commit(&txnError)) {
                error = txnError;
                return false;
            }
            error = reason;
            return false;
        };
        if (isStructuralPredicate(predicate)) return markFactUnresolved("structural_predicate");
        MemoryCatalog& catalog = MemoryCatalog::instance();
        if (!catalog.isKnown(predicate) && catalog.mode() == "strict") {
            return markFactUnresolved("unknown_predicate");
        }
        const std::string subjectId = resolveNode(scopeId, subject, "entity");
        const std::string objectId = resolveNode(scopeId, object, "entity");
        if (subjectId.empty() || objectId.empty()) {
            error = "falha ao criar entidades do fato promovido";
            return false;
        }
        std::string contextKey;
        std::string contextError;
        taskforge::Json factQualifiers = payload.contains("qualifiers") &&
            payload["qualifiers"].is_object() ? payload["qualifiers"] : taskforge::Json::object();
        const bool contextOk = catalog.canonicalContext(predicate, factQualifiers,
                                                        contextKey, contextError);
        if (!contextOk && contextError == "missing_context" && catalog.mode() == "strict") {
            return markFactUnresolved("missing_context");
        }
        std::string claimKey;
        std::string claimKeyError;
        catalog.canonicalClaimKey(scopeId, subjectId, predicate, objectId, object, "",
                                  "", contextKey, "positive", claimKey, claimKeyError);

        Stmt existing;
        if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND dst_id=? "
                           "AND relation=? AND context_key=? AND status='active' LIMIT 1",
                    existing, error)) {
            bindText(existing.stmt, 1, scopeId);
            bindText(existing.stmt, 2, subjectId);
            bindText(existing.stmt, 3, objectId);
            bindText(existing.stmt, 4, predicate);
            bindText(existing.stmt, 5, contextKey);
            if (stepRetry(existing.stmt) == SQLITE_ROW) {
                promoted = columnText(existing.stmt, 0);
            }
        }
        if (promoted.empty() && contextOk && isSingleValuedPredicate(predicate)) {
            // C5: um predicado funcional aceita um unico valor ativo. O valor
            // anterior e encerrado, nao apagado, e fica no historico.
            Stmt previous;
            std::vector<std::string> previousIds;
            if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND relation=? "
                               "AND context_key=? AND status='active' AND (dst_id IS NULL OR dst_id<>?)",
                        previous, error)) {
                bindText(previous.stmt, 1, scopeId);
                bindText(previous.stmt, 2, subjectId);
                bindText(previous.stmt, 3, predicate);
                bindText(previous.stmt, 4, contextKey);
                bindText(previous.stmt, 5, objectId);
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
            metadata["qualifiers"] = factQualifiers;
            metadata["canonicalPredicate"] = predicate;
            metadata["catalogFingerprint"] = catalog.fingerprint();
            metadata["catalogStatus"] = catalog.isKnown(predicate)
                ? (contextOk ? "ok" : "missing_context") : "unknown";
            if (!contextKey.empty()) metadata["contextKey"] = contextKey;
            if (!claimKey.empty()) metadata["claimKey"] = claimKey;
            promoted = recordEdge(scopeId, subjectId, objectId, predicate, taskforge::Json(),
                                  proposal.createdAt, "", proposal.confidence, "active",
                                  "promotion", proposal.id, metadata, contextKey, claimKey);
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
    {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = scopeId;
        snapshot.targetKind = (proposal.kind == "fact") ? "fact"
                            : (proposal.kind == "lesson") ? "lesson"
                            : (proposal.kind == "playbook") ? "playbook"
                            : (proposal.kind == "claim") ? "claim" : "entity";
        snapshot.targetId = promoted;
        snapshot.action = "promote";
        snapshot.episodeId = proposal.sourceEpisodeId;
        snapshot.validatorId = "proposal-review";
        snapshot.ok = true;
        snapshot.summary = "Proposta " + proposal.kind + " promovida por " +
                           (reviewer.empty() ? std::string("human") : reviewer);
        snapshot.excerptHash = hashHex(evidence.dump());
        recordEvidence(snapshot);
    }
    if (!operationKey.empty()) {
        Stmt operation;
        if (!prepare(db_, "INSERT INTO memory_operation(scope_id,op_key,payload_hash,result_json,created_at) "
                          "VALUES(?,?,?,?,?)", operation, error)) {
            return false;
        }
        bindText(operation.stmt, 1, scopeId);
        bindText(operation.stmt, 2, operationKey);
        bindText(operation.stmt, 3, operationHash);
        bindText(operation.stmt, 4, taskforge::Json{{"promotedId", promoted}}.dump());
        bindText(operation.stmt, 5, nowIso());
        if (stepRetry(operation.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return false;
        }
    }
    if (!txn.commit(&txnError)) {
        error = txnError;
        return false;
    }
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
    std::string edgeScope;
    std::string edgeRelation;
    {
        Stmt probe;
        std::string probeError;
        if (prepare(db_, "SELECT scope_id,relation FROM memory_edge WHERE id=?", probe, probeError)) {
            bindText(probe.stmt, 1, edgeId);
            if (stepRetry(probe.stmt) == SQLITE_ROW) {
                edgeScope = columnText(probe.stmt, 0);
                edgeRelation = columnText(probe.stmt, 1);
            }
        }
    }
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
    // E15: a suspensao acontece no mesmo passo da invalidacao; a revalidacao
    // posterior pode remapear a dependencia para uma substituicao compativel.
    {
        Stmt suspend;
        std::string suspendError;
        if (prepare(db_, "UPDATE memory_path SET state='suspended',updated_at=? "
                           "WHERE state='active' AND id IN "
                           "(SELECT path_id FROM memory_claim_path WHERE claim_id=?)",
                     suspend, suspendError)) {
            bindText(suspend.stmt, 1, nowIso());
            bindText(suspend.stmt, 2, edgeId);
            stepRetry(suspend.stmt);
        }
        Stmt hideShortcuts;
        if (prepare(db_, "UPDATE memory_edge SET status='superseded',valid_to=? "
                           "WHERE relation='via_caminho' AND json_extract(metadata,'$.pathId') IN "
                           "(SELECT path_id FROM memory_claim_path WHERE claim_id=?)",
                     hideShortcuts, suspendError)) {
            bindText(hideShortcuts.stmt, 1, nowIso());
            bindText(hideShortcuts.stmt, 2, edgeId);
            stepRetry(hideShortcuts.stmt);
        }
    }
    audit(actor, "invalidate_edge", edgeId, "", reason);
    if (!edgeScope.empty()) {
        Stmt removal;
        std::string removalError;
        if (prepare(db_, "INSERT INTO memory_removal_log"
                           "(scope_id,item_kind,item_id,reason,created_at) VALUES(?,?,?,?,?)",
                     removal, removalError)) {
            bindText(removal.stmt, 1, edgeScope);
            bindText(removal.stmt, 2, "edge");
            bindText(removal.stmt, 3, edgeId);
            bindText(removal.stmt, 4, reason);
            bindText(removal.stmt, 5, nowIso());
            stepRetry(removal.stmt);
        }
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = edgeScope;
        snapshot.targetKind = edgeRelation == "HAS_LESSON" ? "lesson" : "fact";
        snapshot.targetId = edgeId;
        snapshot.action = "invalidate";
        snapshot.ok = false;
        snapshot.summary = "Invalidacao: " + reason;
        recordEvidence(snapshot);
    }
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
        "    AND e.relation<>'via_caminho' "
        "    AND (e.valid_from IS NULL OR e.valid_from <= ?) "
        "    AND (e.valid_to IS NULL OR e.valid_to > ?) "
        ") "
        "SELECT e.id,e.scope_id,e.src_id,e.dst_id,e.relation,e.value_json,e.valid_from,e.valid_to,"
        "e.recorded_at,e.confidence,e.status,e.superseded_by,e.source_kind,e.source_ref,e.metadata "
        "FROM memory_edge e JOIN walk w ON e.src_id=w.node_id "
        "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
        "  AND e.relation<>'via_caminho' "
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

std::vector<MemoryEdgeRecord> MemoryStore::incidentEdges(const std::string& scopeId,
                                                        const std::string& nodeId,
                                                        size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryEdgeRecord> out;
    if (!db_ || nodeId.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,src_id,COALESCE(dst_id,''),relation,"
                       "COALESCE(value_json,''),COALESCE(valid_from,''),COALESCE(valid_to,''),"
                       "recorded_at,confidence,status,COALESCE(superseded_by,''),source_kind,"
                       "COALESCE(source_ref,''),metadata,COALESCE(last_used,''),success_count,"
                       "failure_count,utility,COALESCE(dormant_at,'') "
                       "FROM memory_edge WHERE scope_id=? AND status='active' "
                       "AND dormant_at IS NULL AND relation<>'via_caminho' "
                       "AND (src_id=? OR dst_id=?) "
                       "AND (valid_from IS NULL OR valid_from<=?) "
                       "AND (valid_to IS NULL OR valid_to>?) "
                       "ORDER BY recorded_at DESC LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, nodeId);
    bindText(st.stmt, 3, nodeId);
    bindText(st.stmt, 4, now);
    bindText(st.stmt, 5, now);
    sqlite3_bind_int(st.stmt, 6, static_cast<int>(std::max<size_t>(1, limit)));
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
        edge.claimKey = columnText(st.stmt, 20);
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
        "WHERE e.scope_id=? AND e.status='active' AND e.dormant_at IS NULL "
        "AND e.relation<>'via_caminho' AND ("
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
                      "COALESCE(last_used,''),success_count,failure_count,utility,"
                      "COALESCE(dormant_at,''),COALESCE(claim_key,'') "
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
        edge.claimKey = columnText(st.stmt, 20);
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
    if ((!status.empty() && status != "active") || (!validTo.empty() && validTo <= nowIso())) {
        Stmt suspend;
        std::string suspendError;
        if (prepare(db_, "UPDATE memory_path SET state='suspended',updated_at=? "
                           "WHERE state='active' AND id IN "
                           "(SELECT path_id FROM memory_claim_path WHERE claim_id=?)",
                     suspend, suspendError)) {
            bindText(suspend.stmt, 1, nowIso());
            bindText(suspend.stmt, 2, edgeId);
            stepRetry(suspend.stmt);
        }
        Stmt removal;
        std::string removalError;
        if (prepare(db_, "INSERT INTO memory_removal_log"
                           "(scope_id,item_kind,item_id,reason,created_at) VALUES(?,?,?,?,?)",
                     removal, removalError)) {
            bindText(removal.stmt, 1, scopeId);
            bindText(removal.stmt, 2, "edge");
            bindText(removal.stmt, 3, edgeId);
            bindText(removal.stmt, 4, status.empty() ? "validity_closed" : status);
            bindText(removal.stmt, 5, nowIso());
            stepRetry(removal.stmt);
        }
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

std::string MemoryStore::nodeKind(const std::string& scopeId, const std::string& nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || nodeId.empty()) return "";
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT kind FROM memory_node WHERE scope_id=? AND id=?", st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, nodeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) return columnText(st.stmt, 0);
    }
    return "";
}

std::string MemoryStore::findEntity(const std::string& scopeId, const std::string& mention,
                                    bool* ambiguous) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (ambiguous) *ambiguous = false;
    if (!db_ || mention.empty()) return "";
    const std::string key = normalizeMemoryKey(mention);
    if (key.empty()) return "";
    Stmt st;
    std::string error;
    std::vector<std::string> ids;
    if (prepare(db_, "SELECT DISTINCT node_id FROM memory_alias WHERE scope_id=? AND alias=?",
                 st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, key);
        while (stepRetry(st.stmt) == SQLITE_ROW) ids.push_back(columnText(st.stmt, 0));
    }
    if (ids.empty()) {
        Stmt nodes;
        if (prepare(db_, "SELECT id,canonical_name FROM memory_node WHERE scope_id=? AND kind<>'conflict'",
                     nodes, error)) {
            bindText(nodes.stmt, 1, scopeId);
            while (stepRetry(nodes.stmt) == SQLITE_ROW) {
                if (normalizeMemoryKey(columnText(nodes.stmt, 1)) == key) {
                    ids.push_back(columnText(nodes.stmt, 0));
                }
            }
        }
    }
    if (ids.size() > 1) {
        if (ambiguous) *ambiguous = true;
        return "";
    }
    return ids.empty() ? std::string() : ids.front();
}

std::string MemoryStore::searchContext(const std::string& scopeId, const std::string& text,
                                       size_t limit, const std::vector<std::string>& domains,
                                       std::vector<MemoryHitRef>* hits,
                                       const std::vector<std::string>& lenses) const {
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
            out << "[LICAO] [MEMO:" << lesson.id << "] (" << lesson.domain << ") "
                << lesson.lessonRule << "\n";
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
            "AND e.relation NOT IN ('HAS_LESSON','CONTRADICTS','SUPERSEDES','GENERALIZES','via_caminho') AND ("
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
                if (!lenses.empty()) {
                    auto names = edgeLensNames(edgeId);
                    if (names.empty()) {
                        // Arestas antigas/importadas ainda sem backfill usam o
                        // tema/dominio/relacao como lente virtual, sem escrita.
                        const taskforge::Json metadata = parseJsonOrNull(columnText(st.stmt, 7));
                        std::string virtualLens = metadata.is_object()
                            ? metadata.value("lens", metadata.value("domain", columnText(st.stmt, 2)))
                            : columnText(st.stmt, 2);
                        if (virtualLens.empty()) virtualLens = columnText(st.stmt, 2);
                        if (!virtualLens.empty()) names.push_back(virtualLens);
                    }
                    bool matchesLens = false;
                    for (const auto& name : names) {
                        if (std::find(lenses.begin(), lenses.end(), name) != lenses.end()) {
                            matchesLens = true;
                            break;
                        }
                    }
                    if (!matchesLens) continue;
                }
                out << "[FATO] [MEMO:" << edgeId << "] " << columnText(st.stmt, 1) << " --"
                    << columnText(st.stmt, 2) << "--> " << columnText(st.stmt, 3);
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

    if (!any) return std::string();
    const std::string header =
        "[ARESTRIX MEMORIA: RESULTADOS DA BUSCA]\n"
        "Se usar um item, cite o ID [MEMO:<id>] na resposta final.\n";
    return sanitizeText(header + out.str()).text;
}

bool MemoryStore::recordHit(const std::string& scopeId, const std::string& runId,
                            const std::string& nodeId, const std::string& memoryId,
                            const std::string& kind, bool injected, bool used,
                            const std::string& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || memoryId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,used,outcome,ab_arm,ab_repetition,created_at) "
                       "VALUES (?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(scope_id,run_id,node_id,memory_id) DO UPDATE SET "
                       "kind=excluded.kind, "
                       "injected=MAX(memory_hit.injected, excluded.injected), "
                       "used=MAX(memory_hit.used, excluded.used), "
                       "outcome=CASE WHEN excluded.outcome<>'' THEN excluded.outcome ELSE memory_hit.outcome END, "
                       "ab_arm=excluded.ab_arm, ab_repetition=excluded.ab_repetition",
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
    const char* abArm = std::getenv("ARESTRIX_AB_ARM");
    bindText(st.stmt, 10, abArm ? abArm : "");
    const char* abRep = std::getenv("ARESTRIX_AB_REPETITION");
    sqlite3_bind_int64(st.stmt, 11, abRep ? std::atoll(abRep) : 0);
    bindText(st.stmt, 12, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

int MemoryStore::markHitsUsed(const std::string& scopeId, const std::string& runId,
                              const std::string& nodeId, const std::vector<std::string>& memoryIds,
                              int* invalid) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (invalid) *invalid = 0;
    if (!db_ || memoryIds.empty()) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_hit SET used=1 WHERE scope_id=? AND run_id=? AND node_id=? AND memory_id=?",
                 st, error)) {
        lastError_ = error;
        return 0;
    }
    int marked = 0;
    int ignored = 0;
    for (const auto& id : memoryIds) {
        if (id.empty()) continue;
        sqlite3_reset(st.stmt);
        sqlite3_clear_bindings(st.stmt);
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, runId);
        bindText(st.stmt, 3, nodeId);
        bindText(st.stmt, 4, id);
        if (stepRetry(st.stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0) {
            ++marked;
        } else {
            ++ignored;
        }
    }
    if (ignored > 0) {
        Stmt usage;
        if (prepare(db_, "UPDATE memory_usage SET invalid_citations=invalid_citations+? "
                           "WHERE scope_id=? AND run_id=? AND node_id=?", usage, error)) {
            sqlite3_bind_int(usage.stmt, 1, ignored);
            bindText(usage.stmt, 2, scopeId);
            bindText(usage.stmt, 3, runId);
            bindText(usage.stmt, 4, nodeId);
            stepRetry(usage.stmt);
        }
    }
    if (invalid) *invalid = ignored;
    return marked;
}

int MemoryStore::recordConsultedHits(const std::string& scopeId, const std::string& runId,
                                     const std::string& nodeId,
                                     const std::vector<MemoryHitRef>& refs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || refs.empty()) return 0;

    int changed = 0;
    for (const auto& ref : refs) {
        if (ref.id.empty()) continue;

        // Se a memoria ja tinha sido injetada (ou consultada antes), apenas
        // marcamos consulted=1. E2: busca nunca grava used=1; so a citacao.
        Stmt update;
        std::string error;
        if (prepare(db_, "UPDATE memory_hit SET consulted=1 WHERE scope_id=? AND run_id=? AND node_id=? AND memory_id=?",
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

        // Memoria descoberta por busca: conta como consultada, nao como usada.
        Stmt insert;
        if (!prepare(db_, "INSERT INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,injected,consulted,used,outcome,ab_arm,ab_repetition,created_at) "
                           "VALUES (?,?,?,?,?,?,0,1,0,'',?,?,?) "
                           "ON CONFLICT(scope_id,run_id,node_id,memory_id) DO UPDATE SET "
                           "consulted=1, kind=excluded.kind, ab_arm=excluded.ab_arm, ab_repetition=excluded.ab_repetition", insert, error)) {
            lastError_ = error;
            continue;
        }
        bindText(insert.stmt, 1, newId("hit"));
        bindText(insert.stmt, 2, scopeId);
        bindText(insert.stmt, 3, runId);
        bindText(insert.stmt, 4, nodeId);
        bindText(insert.stmt, 5, ref.id);
        bindText(insert.stmt, 6, ref.kind.empty() ? "memory" : ref.kind);
        const char* abArm = std::getenv("ARESTRIX_AB_ARM");
        bindText(insert.stmt, 7, abArm ? abArm : "");
        const char* abRep = std::getenv("ARESTRIX_AB_REPETITION");
        sqlite3_bind_int64(insert.stmt, 8, abRep ? std::atoll(abRep) : 0);
        bindText(insert.stmt, 9, nowIso());
        if (stepRetry(insert.stmt) == SQLITE_DONE) {
            ++changed;
        } else {
            lastError_ = sqlite3_errmsg(db_);
        }
    }
    return changed;
}

bool MemoryStore::finishHits(const std::string& scopeId, const std::string& runId,
                             const std::string& nodeId, const std::string& outcome,
                             bool reinforce) {
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

    // E1: com o veredito independente ligado, o resultado do agente nao altera
    // utilidade nem contagens; so o veredito de recordValidation reforca.
    if (!reinforce) {
        Stmt touch;
        if (!prepare(db_, "UPDATE memory_edge SET last_used=? WHERE scope_id=? AND id IN "
                           "(SELECT memory_id FROM memory_hit WHERE scope_id=? AND run_id=? "
                           "AND node_id=? AND used=1 AND kind IN ('lesson','fact'))",
                     touch, error)) {
            return false;
        }
        bindText(touch.stmt, 1, nowIso());
        bindText(touch.stmt, 2, scopeId);
        bindText(touch.stmt, 3, scopeId);
        bindText(touch.stmt, 4, runId);
        bindText(touch.stmt, 5, nodeId);
        if (stepRetry(touch.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        return true;
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

std::vector<MemoryHitRecord> MemoryStore::listHits(const std::string& scopeId,
                                                   const std::string& runId,
                                                   const std::string& nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryHitRecord> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,run_id,node_id,memory_id,kind,injected,consulted,used,"
                       "outcome,created_at FROM memory_hit WHERE scope_id=? AND run_id=? "
                       "AND node_id=? ORDER BY created_at,id", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, runId);
    bindText(st.stmt, 3, nodeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryHitRecord hit;
        hit.id = columnText(st.stmt, 0);
        hit.runId = columnText(st.stmt, 1);
        hit.nodeId = columnText(st.stmt, 2);
        hit.memoryId = columnText(st.stmt, 3);
        hit.kind = columnText(st.stmt, 4);
        hit.injected = columnInt(st.stmt, 5) != 0;
        hit.consulted = columnInt(st.stmt, 6) != 0;
        hit.used = columnInt(st.stmt, 7) != 0;
        hit.outcome = columnText(st.stmt, 8);
        hit.createdAt = columnText(st.stmt, 9);
        out.push_back(std::move(hit));
    }
    return out;
}

bool MemoryStore::recordCreditEvents(const std::string& scopeId,
                                     const std::vector<MemoryCreditEvent>& events) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty()) return false;
    if (events.empty()) return true;
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return false;
    }
    Stmt insert;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_credit_event"
                       "(id,scope_id,trace_id,item_key,task_type,model_tier,"
                       "delta_success,delta_failure,kind,origin,capped,audit_id,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)", insert, error)) {
        lastError_ = error;
        return false;
    }
    Stmt aggregate;
    if (!prepare(db_, "INSERT INTO memory_item_utility"
                       "(scope_id,item_key,task_type,model_tier,s,f,low_res,n_events,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,1,?) "
                       "ON CONFLICT(scope_id,item_key,task_type,model_tier) DO UPDATE SET "
                       "s=s+excluded.s,f=f+excluded.f,low_res=low_res+excluded.low_res,"
                       "n_events=n_events+1,updated_at=excluded.updated_at", aggregate, error)) {
        lastError_ = error;
        return false;
    }
    const std::string now = nowIso();
    bool changed = false;
    for (const auto& event : events) {
        if (event.traceId.empty() || event.itemKey.empty()) continue;
        sqlite3_reset(insert.stmt);
        sqlite3_clear_bindings(insert.stmt);
        bindText(insert.stmt, 1, newId("credit"));
        bindText(insert.stmt, 2, scopeId);
        bindText(insert.stmt, 3, event.traceId);
        bindText(insert.stmt, 4, event.itemKey);
        bindText(insert.stmt, 5, event.taskType);
        bindText(insert.stmt, 6, event.modelTier);
        sqlite3_bind_double(insert.stmt, 7, event.deltaSuccess);
        sqlite3_bind_double(insert.stmt, 8, event.deltaFailure);
        bindText(insert.stmt, 9, event.kind);
        bindText(insert.stmt, 10, event.origin);
        sqlite3_bind_int(insert.stmt, 11, event.capped ? 1 : 0);
        bindText(insert.stmt, 12, event.auditId);
        bindText(insert.stmt, 13, now);
        if (stepRetry(insert.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        if (sqlite3_changes(db_) == 0) continue;   // evento ja existia
        const double lowRes = event.kind == "presence_low_res"
            ? event.deltaSuccess + event.deltaFailure : 0.0;
        sqlite3_reset(aggregate.stmt);
        sqlite3_clear_bindings(aggregate.stmt);
        bindText(aggregate.stmt, 1, scopeId);
        bindText(aggregate.stmt, 2, event.itemKey);
        bindText(aggregate.stmt, 3, event.taskType.empty() ? "*" : event.taskType);
        bindText(aggregate.stmt, 4, event.modelTier);
        sqlite3_bind_double(aggregate.stmt, 5, event.deltaSuccess);
        sqlite3_bind_double(aggregate.stmt, 6, event.deltaFailure);
        sqlite3_bind_double(aggregate.stmt, 7, lowRes);
        bindText(aggregate.stmt, 8, now);
        if (stepRetry(aggregate.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        changed = true;
    }
    if (changed && !exec("INSERT INTO memory_meta(key,value) VALUES('usage_version','1') "
                         "ON CONFLICT(key) DO UPDATE SET "
                         "value=CAST(value AS INTEGER)+1;", &error)) {
        lastError_ = error;
        return false;
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::utilityFor(const std::string& scopeId,
                                        const std::string& itemKey,
                                        const std::string& taskType) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"source", "prior"}, {"utility", 0.5},
                        {"s", 0.0}, {"f", 0.0}, {"lowRes", 0.0}, {"events", 0LL}};
    if (!db_ || scopeId.empty() || itemKey.empty()) return out;
    for (const std::string& type : {taskType, std::string("*")}) {
        Stmt st;
        std::string error;
        if (!prepare(db_, "SELECT s,f,low_res,n_events FROM memory_item_utility "
                           "WHERE scope_id=? AND item_key=? AND task_type=? "
                           "AND model_tier='' LIMIT 1", st, error)) continue;
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, itemKey);
        bindText(st.stmt, 3, type);
        if (stepRetry(st.stmt) != SQLITE_ROW) continue;
        const double s = sqlite3_column_double(st.stmt, 0);
        const double f = sqlite3_column_double(st.stmt, 1);
        out["source"] = type == taskType ? "ledger" : "legacy_seed";
        out["s"] = s;
        out["f"] = f;
        out["lowRes"] = sqlite3_column_double(st.stmt, 2);
        out["events"] = columnInt(st.stmt, 3);
        out["utility"] = (s + 1.0) / (s + f + 2.0);
        return out;
    }
    return out;
}

taskforge::Json MemoryStore::creditSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"events", 0LL}, {"items", 0LL}, {"capped", 0LL},
                        {"success", 0.0}, {"failure", 0.0}, {"lowRes", 0.0}};
    if (!db_) return out;
    auto scalar = [&](const char* sql) -> taskforge::Json {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) return taskforge::Json();
        bindText(st.stmt, 1, scopeId);
        if (stepRetry(st.stmt) != SQLITE_ROW) return taskforge::Json();
        return taskforge::Json{{"value", sqlite3_column_double(st.stmt, 0)}};
    };
    out["events"] = scalar("SELECT COUNT(*) FROM memory_credit_event WHERE scope_id=?")
        .value("value", 0.0);
    out["capped"] = scalar("SELECT COUNT(*) FROM memory_credit_event "
                           "WHERE scope_id=? AND capped=1").value("value", 0.0);
    out["items"] = scalar("SELECT COUNT(*) FROM memory_item_utility WHERE scope_id=?")
        .value("value", 0.0);
    out["success"] = scalar("SELECT COALESCE(SUM(s),0) FROM memory_item_utility WHERE scope_id=?")
        .value("value", 0.0);
    out["failure"] = scalar("SELECT COALESCE(SUM(f),0) FROM memory_item_utility WHERE scope_id=?")
        .value("value", 0.0);
    out["lowRes"] = scalar("SELECT COALESCE(SUM(low_res),0) FROM memory_item_utility WHERE scope_id=?")
        .value("value", 0.0);
    return out;
}

double MemoryStore::creditTotalForOrigin(const std::string& scopeId,
                                         const std::string& itemKey,
                                         const std::string& taskType,
                                         const std::string& origin,
                                         const std::string& since) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || itemKey.empty()) return 0.0;
    std::string sql = "SELECT COALESCE(SUM(ABS(delta_success)+ABS(delta_failure)),0) "
                      "FROM memory_credit_event WHERE scope_id=? AND item_key=? "
                      "AND task_type=? AND origin=?";
    if (!since.empty()) sql += " AND created_at>=?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) return 0.0;
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, itemKey);
    bindText(st.stmt, 3, taskType.empty() ? "*" : taskType);
    bindText(st.stmt, 4, origin);
    if (!since.empty()) bindText(st.stmt, 5, since);
    return stepRetry(st.stmt) == SQLITE_ROW ? sqlite3_column_double(st.stmt, 0) : 0.0;
}

bool MemoryStore::rebuildItemUtility(const std::string& scopeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty()) return false;
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return false;
    }
    Stmt clear;
    std::string error;
    if (!prepare(db_, "DELETE FROM memory_item_utility WHERE scope_id=?", clear, error)) {
        lastError_ = error;
        return false;
    }
    bindText(clear.stmt, 1, scopeId);
    if (stepRetry(clear.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    Stmt rebuild;
    if (!prepare(db_, "INSERT INTO memory_item_utility"
                       "(scope_id,item_key,task_type,model_tier,s,f,low_res,n_events,updated_at) "
                       "SELECT scope_id,item_key,task_type,model_tier,"
                       "SUM(delta_success),SUM(delta_failure),"
                       "SUM(CASE WHEN kind='presence_low_res' "
                       "THEN delta_success+delta_failure ELSE 0 END),"
                       "COUNT(*),MAX(created_at) FROM memory_credit_event "
                       "WHERE scope_id=? GROUP BY item_key,task_type,model_tier",
                 rebuild, error)) {
        lastError_ = error;
        return false;
    }
    bindText(rebuild.stmt, 1, scopeId);
    if (stepRetry(rebuild.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (!exec("INSERT INTO memory_meta(key,value) VALUES('usage_version','1') "
              "ON CONFLICT(key) DO UPDATE SET value=CAST(value AS INTEGER)+1;", &error)) {
        lastError_ = error;
        return false;
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return false;
    }
    return true;
}

size_t MemoryStore::rekeyCreditItem(const std::string& scopeId,
                                    const std::string& fromKey,
                                    const std::string& toKey) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || fromKey.empty() || toKey.empty() ||
        fromKey == toKey) {
        return 0;
    }
    struct Row {
        std::string id;
        std::string traceId;
        std::string taskType;
        std::string modelTier;
        double deltaSuccess = 0.0;
        double deltaFailure = 0.0;
        std::string kind;
        std::string origin;
        bool capped = false;
        std::string auditId;
        std::string createdAt;
    };
    std::vector<Row> rows;
    {
        Stmt st;
        std::string error;
        if (!prepare(db_,
                     "SELECT id,trace_id,task_type,model_tier,delta_success,"
                     "delta_failure,kind,origin,capped,audit_id,created_at "
                     "FROM memory_credit_event WHERE scope_id=? AND item_key=?",
                     st, error)) {
            lastError_ = error;
            return 0;
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, fromKey);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            Row row;
            row.id = columnText(st.stmt, 0);
            row.traceId = columnText(st.stmt, 1);
            row.taskType = columnText(st.stmt, 2);
            row.modelTier = columnText(st.stmt, 3);
            row.deltaSuccess = sqlite3_column_double(st.stmt, 4);
            row.deltaFailure = sqlite3_column_double(st.stmt, 5);
            row.kind = columnText(st.stmt, 6);
            row.origin = columnText(st.stmt, 7);
            row.capped = sqlite3_column_int(st.stmt, 8) != 0;
            row.auditId = columnText(st.stmt, 9);
            row.createdAt = columnText(st.stmt, 10);
            rows.push_back(std::move(row));
        }
    }
    if (rows.empty()) return 0;

    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return 0;
    }
    Stmt move;
    Stmt merge;
    Stmt remove;
    std::string error;
    if (!prepare(db_, "UPDATE OR IGNORE memory_credit_event SET item_key=? WHERE id=?", move, error) ||
        !prepare(db_,
                 "UPDATE memory_credit_event SET delta_success=delta_success+?,"
                 "delta_failure=delta_failure+?,capped=CASE WHEN ? THEN 1 ELSE capped END "
                 "WHERE scope_id=? AND trace_id=? AND item_key=? AND kind=? AND audit_id=?",
                 merge, error) ||
        !prepare(db_, "DELETE FROM memory_credit_event WHERE id=?", remove, error)) {
        lastError_ = error;
        return 0;
    }
    size_t moved = 0;
    for (const auto& row : rows) {
        sqlite3_reset(move.stmt);
        sqlite3_clear_bindings(move.stmt);
        bindText(move.stmt, 1, toKey);
        bindText(move.stmt, 2, row.id);
        if (stepRetry(move.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return 0;
        }
        if (sqlite3_changes(db_) == 1) {
            ++moved;
            continue;
        }
        sqlite3_reset(merge.stmt);
        sqlite3_clear_bindings(merge.stmt);
        sqlite3_bind_double(merge.stmt, 1, row.deltaSuccess);
        sqlite3_bind_double(merge.stmt, 2, row.deltaFailure);
        sqlite3_bind_int(merge.stmt, 3, row.capped ? 1 : 0);
        bindText(merge.stmt, 4, scopeId);
        bindText(merge.stmt, 5, row.traceId);
        bindText(merge.stmt, 6, toKey);
        bindText(merge.stmt, 7, row.kind);
        bindText(merge.stmt, 8, row.auditId);
        if (stepRetry(merge.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return 0;
        }
        sqlite3_reset(remove.stmt);
        sqlite3_clear_bindings(remove.stmt);
        bindText(remove.stmt, 1, row.id);
        if (stepRetry(remove.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return 0;
        }
        ++moved;
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return 0;
    }
    if (moved > 0) rebuildItemUtility(scopeId);
    return moved;
}

bool MemoryStore::saveCreditAudit(const MemoryCreditAuditRecord& audit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || audit.scopeId.empty() || audit.id.empty()) return false;
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return false;
    }
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "INSERT INTO memory_credit_audit"
                 "(id,scope_id,trace_id,status,arms_json,reason,tokens,snapshot_ref,input_hash,created_at) "
                 "VALUES(?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                 "status=excluded.status,arms_json=excluded.arms_json,reason=excluded.reason,"
                 "tokens=excluded.tokens,snapshot_ref=excluded.snapshot_ref,"
                 "input_hash=excluded.input_hash",
                 st, error)) {
        lastError_ = error;
        return false;
    }
    const std::string created = audit.createdAt.empty() ? nowIso() : audit.createdAt;
    bindText(st.stmt, 1, audit.id);
    bindText(st.stmt, 2, audit.scopeId);
    bindText(st.stmt, 3, audit.traceId);
    bindText(st.stmt, 4, audit.status);
    bindText(st.stmt, 5, audit.armsJson);
    bindText(st.stmt, 6, audit.reason);
    sqlite3_bind_int64(st.stmt, 7, audit.tokens);
    bindText(st.stmt, 8, audit.snapshotRef);
    bindText(st.stmt, 9, audit.inputHash);
    bindText(st.stmt, 10, created);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::getCreditAudit(const std::string& scopeId,
                                            const std::string& traceId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || traceId.empty()) return taskforge::Json::object();
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT id,trace_id,status,arms_json,reason,tokens,snapshot_ref,input_hash,created_at "
                 "FROM memory_credit_audit WHERE scope_id=? AND trace_id=? "
                 "ORDER BY created_at DESC LIMIT 1", st, error)) {
        lastError_ = error;
        return taskforge::Json::object();
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, traceId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return taskforge::Json::object();
    taskforge::Json out{
        {"id", columnText(st.stmt, 0)},
        {"traceId", columnText(st.stmt, 1)},
        {"status", columnText(st.stmt, 2)},
        {"arms", taskforge::jsonutil::tryParse(columnText(st.stmt, 3))
                     .value_or(taskforge::Json::array())},
        {"reason", columnText(st.stmt, 4)},
        {"tokens", sqlite3_column_int64(st.stmt, 5)},
        {"snapshotRef", columnText(st.stmt, 6)},
        {"inputHash", columnText(st.stmt, 7)},
        {"createdAt", columnText(st.stmt, 8)}};
    return out;
}

taskforge::Json MemoryStore::listCreditAudits(const std::string& scopeId,
                                              const std::string& status,
                                              size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::array();
    if (!db_ || scopeId.empty()) return out;
    Stmt st;
    std::string error;
    const std::string sql = status.empty()
        ? "SELECT id,trace_id,status,arms_json,reason,tokens,snapshot_ref,input_hash,created_at "
          "FROM memory_credit_audit WHERE scope_id=? ORDER BY created_at DESC LIMIT ?"
        : "SELECT id,trace_id,status,arms_json,reason,tokens,snapshot_ref,input_hash,created_at "
          "FROM memory_credit_audit WHERE scope_id=? AND status=? "
          "ORDER BY created_at DESC LIMIT ?";
    if (!prepare(db_, sql, st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    if (status.empty()) {
        sqlite3_bind_int64(st.stmt, 2, static_cast<sqlite3_int64>(limit));
    } else {
        bindText(st.stmt, 2, status);
        sqlite3_bind_int64(st.stmt, 3, static_cast<sqlite3_int64>(limit));
    }
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back(taskforge::Json{
            {"id", columnText(st.stmt, 0)},
            {"traceId", columnText(st.stmt, 1)},
            {"status", columnText(st.stmt, 2)},
            {"arms", taskforge::jsonutil::tryParse(columnText(st.stmt, 3))
                         .value_or(taskforge::Json::array())},
            {"reason", columnText(st.stmt, 4)},
            {"tokens", sqlite3_column_int64(st.stmt, 5)},
            {"snapshotRef", columnText(st.stmt, 6)},
            {"inputHash", columnText(st.stmt, 7)},
            {"createdAt", columnText(st.stmt, 8)}});
    }
    return out;
}

taskforge::Json MemoryStore::creditAuditUsage(const std::string& scopeId,
                                              const std::string& since) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"audits", 0LL}, {"tokens", 0LL}};
    if (!db_ || scopeId.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT COUNT(*),COALESCE(SUM(tokens),0) FROM memory_credit_audit "
                 "WHERE scope_id=? AND created_at>=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, since);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    out["audits"] = sqlite3_column_int64(st.stmt, 0);
    out["tokens"] = sqlite3_column_int64(st.stmt, 1);
    return out;
}

std::string MemoryStore::recordValidatorAudit(const MemoryValidatorAuditRecord& audit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || audit.scopeId.empty() || audit.validator.empty()) return std::string();
    const std::string id = audit.id.empty() ? newId("vaudit") : audit.id;
    const std::string created = audit.createdAt.empty() ? nowIso() : audit.createdAt;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "INSERT OR IGNORE INTO memory_validator_audit"
                 "(id,scope_id,validator,trace_id,reviewer,verdict,created_at) "
                 "VALUES(?,?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return std::string();
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, audit.scopeId);
    bindText(st.stmt, 3, audit.validator);
    bindText(st.stmt, 4, audit.traceId);
    bindText(st.stmt, 5, audit.reviewer);
    bindText(st.stmt, 6, audit.verdict);
    bindText(st.stmt, 7, created);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return std::string();
    }
    return id;
}

taskforge::Json MemoryStore::validatorAuditStats(const std::string& scopeId,
                                                 const std::string& validator) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"validator", validator}, {"audits", 0LL}, {"rejections", 0LL},
                        {"divergence", 0.0}, {"wilsonLow", 0.0}, {"wilsonHigh", 1.0},
                        {"weight", 1.0}};
    if (!db_ || scopeId.empty() || validator.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT COUNT(*),SUM(CASE WHEN verdict='rejected' THEN 1 ELSE 0 END) "
                 "FROM memory_validator_audit WHERE scope_id=? AND validator=?",
                 st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, validator);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    const long long audits = sqlite3_column_int64(st.stmt, 0);
    const long long rejections = sqlite3_column_type(st.stmt, 1) == SQLITE_NULL
        ? 0 : sqlite3_column_int64(st.stmt, 1);
    const double p = audits > 0 ? static_cast<double>(rejections) /
        static_cast<double>(audits) : 0.0;
    double lo = 0.0;
    double hi = 1.0;
    if (audits > 0) {
        const double n = static_cast<double>(audits);
        const double z = 1.959963984540054;
        const double denom = 1.0 + z * z / n;
        const double center = (p + z * z / (2.0 * n)) / denom;
        const double margin = z * std::sqrt(
            (p * (1.0 - p) + z * z / (4.0 * n)) / n) / denom;
        lo = std::max(0.0, center - margin);
        hi = std::min(1.0, center + margin);
    }
    out["audits"] = audits;
    out["rejections"] = rejections;
    out["divergence"] = p;
    out["wilsonLow"] = lo;
    out["wilsonHigh"] = hi;
    out["weight"] = 1.0 - p;
    return out;
}

taskforge::Json MemoryStore::validatorAuditReport(const std::string& scopeId,
                                                  size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::array();
    if (!db_ || scopeId.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT validator,COUNT(*),SUM(CASE WHEN verdict='rejected' THEN 1 ELSE 0 END),"
                 "MAX(created_at) FROM memory_validator_audit WHERE scope_id=? "
                 "GROUP BY validator ORDER BY MAX(created_at) DESC LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int64(st.stmt, 2, static_cast<sqlite3_int64>(limit));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        const std::string validator = columnText(st.stmt, 0);
        const auto stats = validatorAuditStats(scopeId, validator);
        out.push_back(taskforge::Json{
            {"validator", validator},
            {"audits", sqlite3_column_int64(st.stmt, 1)},
            {"rejections", sqlite3_column_type(st.stmt, 2) == SQLITE_NULL
                ? 0LL : sqlite3_column_int64(st.stmt, 2)},
            {"divergence", stats.value("divergence", 0.0)},
            {"wilsonLow", stats.value("wilsonLow", 0.0)},
            {"wilsonHigh", stats.value("wilsonHigh", 1.0)},
            {"weight", stats.value("weight", 1.0)},
            {"lastAudit", columnText(st.stmt, 3)}});
    }
    return out;
}

bool MemoryStore::saveTaskSnapshot(const MemoryTaskSnapshotRecord& snapshot) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || snapshot.scopeId.empty() || snapshot.traceId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "INSERT INTO memory_task_snapshot"
                 "(trace_id,scope_id,input_hash,input_json,workspace_kind,workspace_ref,bytes,"
                 "created_at,expires_at) VALUES(?,?,?,?,?,?,?,?,?) "
                 "ON CONFLICT(trace_id) DO UPDATE SET input_hash=excluded.input_hash,"
                 "input_json=excluded.input_json,workspace_kind=excluded.workspace_kind,"
                 "workspace_ref=excluded.workspace_ref,bytes=excluded.bytes,"
                 "expires_at=excluded.expires_at", st, error)) {
        lastError_ = error;
        return false;
    }
    const std::string created = snapshot.createdAt.empty() ? nowIso() : snapshot.createdAt;
    bindText(st.stmt, 1, snapshot.traceId);
    bindText(st.stmt, 2, snapshot.scopeId);
    bindText(st.stmt, 3, snapshot.inputHash);
    bindText(st.stmt, 4, snapshot.inputJson);
    bindText(st.stmt, 5, snapshot.workspaceKind);
    bindText(st.stmt, 6, snapshot.workspaceRef);
    sqlite3_bind_int64(st.stmt, 7, snapshot.bytes);
    bindText(st.stmt, 8, created);
    bindText(st.stmt, 9, snapshot.expiresAt);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::getTaskSnapshot(const std::string& scopeId,
                                             const std::string& traceId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || traceId.empty()) return taskforge::Json::object();
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT trace_id,input_hash,input_json,workspace_kind,workspace_ref,bytes,"
                 "created_at,expires_at FROM memory_task_snapshot "
                 "WHERE scope_id=? AND trace_id=?", st, error)) {
        lastError_ = error;
        return taskforge::Json::object();
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, traceId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return taskforge::Json::object();
    return taskforge::Json{
        {"traceId", columnText(st.stmt, 0)},
        {"inputHash", columnText(st.stmt, 1)},
        {"input", taskforge::jsonutil::tryParse(columnText(st.stmt, 2))
                      .value_or(taskforge::Json::object())},
        {"workspaceKind", columnText(st.stmt, 3)},
        {"workspaceRef", columnText(st.stmt, 4)},
        {"bytes", sqlite3_column_int64(st.stmt, 5)},
        {"createdAt", columnText(st.stmt, 6)},
        {"expiresAt", columnText(st.stmt, 7)}};
}

long long MemoryStore::purgeExpiredSnapshots(const std::string& scopeId,
                                             const std::string& now) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty()) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "DELETE FROM memory_task_snapshot WHERE scope_id=? AND expires_at<?",
                 st, error)) {
        lastError_ = error;
        return 0;
    }
    const std::string cutoff = now.empty() ? nowIso() : now;
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, cutoff);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return 0;
    }
    return static_cast<long long>(sqlite3_changes(db_));
}

taskforge::Json MemoryStore::snapshotSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"count", 0LL}, {"bytes", 0LL}, {"expired", 0LL}};
    if (!db_ || scopeId.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_,
                 "SELECT COUNT(*),COALESCE(SUM(bytes),0),"
                 "COALESCE(SUM(CASE WHEN expires_at<? THEN 1 ELSE 0 END),0) "
                 "FROM memory_task_snapshot WHERE scope_id=?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, nowIso());
    bindText(st.stmt, 2, scopeId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    out["count"] = sqlite3_column_int64(st.stmt, 0);
    out["bytes"] = sqlite3_column_int64(st.stmt, 1);
    out["expired"] = sqlite3_column_int64(st.stmt, 2);
    return out;
}

bool MemoryStore::touchHitsLastUsed(const std::string& scopeId,
                                    const std::vector<std::string>& memoryIds,
                                    const std::string& at) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || memoryIds.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_edge SET last_used=? WHERE scope_id=? AND id=? AND status='active'",
                 st, error)) {
        lastError_ = error;
        return false;
    }
    const std::string when = at.empty() ? nowIso() : at;
    bool ok = true;
    for (const auto& id : memoryIds) {
        if (id.empty()) continue;
        sqlite3_reset(st.stmt);
        sqlite3_clear_bindings(st.stmt);
        bindText(st.stmt, 1, when);
        bindText(st.stmt, 2, scopeId);
        bindText(st.stmt, 3, id);
        if (stepRetry(st.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            ok = false;
        }
    }
    return ok;
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
    if (!prepare(db_, "SELECT COUNT(*), COALESCE(SUM(injected),0), COALESCE(SUM(consulted),0), "
                       "COALESCE(SUM(used),0), "
                       "COALESCE(SUM(CASE WHEN used=1 AND injected=1 THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN used=1 AND consulted=1 AND injected=0 THEN 1 ELSE 0 END),0), "
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
        out["consulted"] = columnInt(st.stmt, 2);
        out["used"] = columnInt(st.stmt, 3);
        out["citedFromBrief"] = columnInt(st.stmt, 4);
        out["citedFromSearch"] = columnInt(st.stmt, 5);
        out["usedInjected"] = columnInt(st.stmt, 4);  // compatibilidade
        out["successHits"] = columnInt(st.stmt, 6);
        out["failureHits"] = columnInt(st.stmt, 7);
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
    if (!prepare(db_, "INSERT INTO memory_usage(id,scope_id,run_id,node_id,provider,model,prompt_tokens,completion_tokens,total_tokens,cached_tokens,cache_write_tokens,calls,latency_ms,memory_context_chars,memory_enabled,retries,outcome,ab_arm,ab_repetition,created_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)", st, error)) {
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
    sqlite3_bind_int64(st.stmt, 10, usage.cachedTokens);
    sqlite3_bind_int64(st.stmt, 11, usage.cacheWriteTokens);
    sqlite3_bind_int64(st.stmt, 12, usage.calls);
    sqlite3_bind_int64(st.stmt, 13, usage.latencyMs);
    sqlite3_bind_int64(st.stmt, 14, usage.memoryContextChars);
    sqlite3_bind_int64(st.stmt, 15, usage.memoryEnabled);
    sqlite3_bind_int64(st.stmt, 16, usage.retries);
    bindText(st.stmt, 17, usage.outcome);
    const char* abArm = std::getenv("ARESTRIX_AB_ARM");
    bindText(st.stmt, 18, abArm ? abArm : "");
    const char* abRep = std::getenv("ARESTRIX_AB_REPETITION");
    sqlite3_bind_int64(st.stmt, 19, abRep ? std::atoll(abRep) : 0);
    bindText(st.stmt, 20, nowIso());
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
                       "COALESCE(SUM(total_tokens),0), COALESCE(SUM(cached_tokens),0), "
                       "COALESCE(SUM(cache_write_tokens),0), COALESCE(SUM(calls),0), COALESCE(AVG(latency_ms),0), "
                       "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(memory_enabled),0), "
                       "COALESCE(SUM(retries),0), "
                       "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0), "
                       "COALESCE(SUM(invalid_citations),0) "
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
        out["cachedTokens"] = columnInt(st.stmt, 4);
        out["cacheWriteTokens"] = columnInt(st.stmt, 5);
        out["calls"] = columnInt(st.stmt, 6);
        out["avgLatencyMs"] = columnDouble(st.stmt, 7);
        out["memoryContextChars"] = columnInt(st.stmt, 8);
        out["memoryEnabledRuns"] = columnInt(st.stmt, 9);
        out["retries"] = columnInt(st.stmt, 10);
        out["successRuns"] = columnInt(st.stmt, 11);
        out["failureRuns"] = columnInt(st.stmt, 12);
        out["invalidCitations"] = columnInt(st.stmt, 13);
    }
    return out;
}

taskforge::Json MemoryStore::recurrenceReport(const std::string& scopeId, const std::string& since,
                                              double minVerdictRate, double minCitationRate,
                                              long long minExecutions, long long minDistinctInputs,
                                              double minReuseRate) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;

    struct Verdict {
        bool found = false;
        bool approved = false;
    };
    std::map<std::string, Verdict> verdicts;
    std::map<std::string, long long> tokens;
    std::map<std::string, std::set<std::string>> citations;
    auto key = [](const std::string& runId, const std::string& nodeId) {
        return runId + "\x1f" + nodeId;
    };
    std::string error;
    Stmt st;

    if (prepare(db_, "SELECT run_id,COALESCE(node_id,''),ok FROM memory_validation "
                       "WHERE scope_id=? ORDER BY rowid", st, error)) {
        bindText(st.stmt, 1, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            Verdict& v = verdicts[key(columnText(st.stmt, 0), columnText(st.stmt, 1))];
            v.found = true;
            v.approved = columnInt(st.stmt, 2) != 0;
        }
    }

    if (prepare(db_, "SELECT run_id,COALESCE(node_id,''),COALESCE(SUM(total_tokens),0) "
                       "FROM memory_usage WHERE scope_id=? GROUP BY run_id,node_id", st, error)) {
        bindText(st.stmt, 1, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            tokens[key(columnText(st.stmt, 0), columnText(st.stmt, 1))] = columnInt(st.stmt, 2);
        }
    }

    if (prepare(db_, "SELECT run_id,COALESCE(node_id,''),memory_id FROM memory_hit "
                       "WHERE scope_id=? AND used=1 ORDER BY rowid", st, error)) {
        bindText(st.stmt, 1, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            citations[key(columnText(st.stmt, 0), columnText(st.stmt, 1))]
                .insert(columnText(st.stmt, 2));
        }
    }

    struct EpisodeRow {
        std::string runId;
        std::string nodeId;
        std::string taskType;
        std::string inputHash;
        std::string intentHash;
    };
    std::vector<EpisodeRow> episodes;
    const std::string episodeSql =
        "SELECT run_id,COALESCE(node_id,''),COALESCE(task_type,''),COALESCE(input_hash,''),"
        "COALESCE(intent_template_hash,'') FROM memory_episode "
        "WHERE scope_id=? AND kind LIKE 'agent_%'" +
        std::string(since.empty() ? "" : " AND created_at>=?") + " ORDER BY rowid";
    if (prepare(db_, episodeSql.c_str(), st, error)) {
        bindText(st.stmt, 1, scopeId);
        if (!since.empty()) bindText(st.stmt, 2, since);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            episodes.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                                columnText(st.stmt, 2), columnText(st.stmt, 3),
                                columnText(st.stmt, 4)});
        }
    }

    struct Stats {
        long long executions = 0;
        long long verdicts = 0;
        long long approved = 0;
        long long cited = 0;
        long long reused = 0;
        long long tokens = 0;
        std::set<std::string> inputs;
        std::map<std::string, long long> intents;
        std::set<std::string> seenCitations;
    };
    std::map<std::string, Stats> byTaskType;
    for (const auto& episode : episodes) {
        const std::string taskType = episode.taskType.empty() ? "(sem task_type)" : episode.taskType;
        Stats& stats = byTaskType[taskType];
        ++stats.executions;
        if (!episode.inputHash.empty()) stats.inputs.insert(episode.inputHash);
        ++stats.intents[episode.intentHash];
        stats.tokens += tokens[key(episode.runId, episode.nodeId)];
        auto foundVerdict = verdicts.find(key(episode.runId, episode.nodeId));
        if (foundVerdict != verdicts.end() && foundVerdict->second.found) {
            ++stats.verdicts;
            if (foundVerdict->second.approved) ++stats.approved;
        }
        auto foundCitations = citations.find(key(episode.runId, episode.nodeId));
        if (foundCitations != citations.end() && !foundCitations->second.empty()) {
            ++stats.cited;
            bool reused = false;
            for (const auto& id : foundCitations->second) {
                if (stats.seenCitations.count(id)) reused = true;
            }
            if (reused) ++stats.reused;
            stats.seenCitations.insert(foundCitations->second.begin(), foundCitations->second.end());
        }
    }

    taskforge::Json thresholdJson{{"minVerdictRate", minVerdictRate},
                                  {"minCitationRate", minCitationRate},
                                  {"minExecutions", minExecutions},
                                  {"minDistinctInputs", minDistinctInputs},
                                  {"minReuseRate", minReuseRate}};
    taskforge::Json taskTypes = taskforge::Json::array();
    bool allThresholds = !byTaskType.empty();
    bool verdictCitationOk = !byTaskType.empty();
    for (auto& [taskType, stats] : byTaskType) {
        const double verdictRate = stats.executions > 0
            ? static_cast<double>(stats.verdicts) / static_cast<double>(stats.executions) : 0.0;
        const double approvalRate = stats.verdicts > 0
            ? static_cast<double>(stats.approved) / static_cast<double>(stats.verdicts) : 0.0;
        const double citationRate = stats.executions > 0
            ? static_cast<double>(stats.cited) / static_cast<double>(stats.executions) : 0.0;
        const double reuseRate = stats.executions > 0
            ? static_cast<double>(stats.reused) / static_cast<double>(stats.executions) : 0.0;
        const bool verdictOk = verdictRate >= minVerdictRate;
        const bool citationOk = citationRate >= minCitationRate;
        const bool recurrenceOk = stats.executions >= minExecutions &&
                                  static_cast<long long>(stats.inputs.size()) >= minDistinctInputs;
        const bool reuseOk = reuseRate >= minReuseRate;

        taskforge::Json pairs = taskforge::Json::array();
        for (const auto& [intentHash, count] : stats.intents) {
            pairs.push_back(taskforge::Json{{"intentTemplateHash", intentHash},
                                            {"executions", count}});
        }
        taskforge::Json task{
            {"taskType", taskType},
            {"executions", stats.executions},
            {"executionsWithVerdict", stats.verdicts},
            {"verdictRate", verdictRate},
            {"approved", stats.approved},
            {"approvalRate", approvalRate},
            {"distinctInputs", static_cast<long long>(stats.inputs.size())},
            {"citedExecutions", stats.cited},
            {"citationRate", citationRate},
            {"reusedExecutions", stats.reused},
            {"reuseRate", reuseRate},
            {"tokens", stats.tokens},
            {"tokensPerExecution", stats.executions > 0
                ? static_cast<double>(stats.tokens) / static_cast<double>(stats.executions) : 0.0},
            {"intentPairs", pairs},
            {"checks", taskforge::Json{{"verdict", verdictOk},
                                       {"citation", citationOk},
                                       {"recurrence", recurrenceOk},
                                       {"reuse", reuseOk}}},
            {"ok", verdictOk && citationOk && recurrenceOk && reuseOk}};
        taskTypes.push_back(task);
        if (!(verdictOk && citationOk)) verdictCitationOk = false;
        if (!(verdictOk && citationOk && recurrenceOk && reuseOk)) allThresholds = false;
    }

    std::string decision = byTaskType.empty() ? "insufficient_data" : "fix_validators";
    std::string reason = byTaskType.empty()
        ? "sem episodios de agente no periodo"
        : "veredito ou citacao abaixo do limiar; corrija validadores e formato de saida antes de seguir";
    if (verdictCitationOk) {
        if (allThresholds) {
            decision = "continue_full";
            reason = "todos os limiares atingidos; o bloco C completo esta justificado";
        } else {
            decision = "continue_base_knowledge";
            reason = "veredito e citacao atingidos, mas recorrencia ou reuso abaixo; siga E11-E14 e adie caminhos";
        }
    }

    out["scopeId"] = scopeId;
    out["since"] = since;
    out["thresholds"] = thresholdJson;
    out["taskTypes"] = taskTypes;
    out["taskTypeCount"] = static_cast<long long>(taskTypes.size());
    out["decision"] = decision;
    out["decisionReason"] = reason;
    out["ok"] = true;
    return out;
}

long long MemoryStore::knowledgeVersion() const {
    const std::string value = getMeta("knowledge_version");
    if (value.empty()) return 0;
    try {
        return std::stoll(value);
    } catch (...) {
        return 0;
    }
}

bool MemoryStore::appendTrace(const MemoryTraceRecord& trace, const taskforge::Json& step,
                              size_t maxSteps, size_t maxCandidates) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || trace.scopeId.empty() || trace.runId.empty() || trace.nodeId.empty()) return false;

    taskforge::Json cleanStep = step.is_object() ? step : taskforge::Json::object();
    size_t candidatesDropped = 0;
    if (cleanStep.contains("candidates") && cleanStep["candidates"].is_array() &&
        cleanStep["candidates"].size() > maxCandidates) {
        taskforge::Json kept = taskforge::Json::array();
        for (size_t i = 0; i < maxCandidates; ++i) kept.push_back(cleanStep["candidates"][i]);
        candidatesDropped = cleanStep["candidates"].size() - maxCandidates;
        cleanStep["candidates"] = kept;
    }
    cleanStep["truncatedCandidates"] = static_cast<long long>(candidatesDropped);

    std::string traceId = trace.traceId;
    taskforge::Json steps = taskforge::Json::array();
    long long version = trace.knowledgeVersion;
    long long truncatedSteps = 0;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT trace_id,steps_json,truncated_steps,knowledge_version FROM memory_trace "
                       "WHERE scope_id=? AND run_id=? AND node_id=?", st, error)) {
        bindText(st.stmt, 1, trace.scopeId);
        bindText(st.stmt, 2, trace.runId);
        bindText(st.stmt, 3, trace.nodeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            traceId = columnText(st.stmt, 0);
            taskforge::Json existing = parseJsonOrNull(columnText(st.stmt, 1));
            if (existing.is_array()) steps = std::move(existing);
            truncatedSteps = columnInt(st.stmt, 2);
            version = columnInt(st.stmt, 3);
        }
    }
    if (steps.size() < maxSteps) {
        steps.push_back(cleanStep);
    } else {
        ++truncatedSteps;
    }
    if (traceId.empty()) traceId = newId("trace");

    const std::string now = nowIso();
    if (!prepare(db_, "INSERT INTO memory_trace(trace_id,scope_id,run_id,node_id,item_id,task_type,"
                       "intent_template_hash,input_hash,knowledge_version,model,tier,outcome,"
                       "anchors_json,steps_json,delivered_json,used_json,truncated_steps,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,run_id,node_id) DO UPDATE SET "
                       "item_id=CASE WHEN excluded.item_id<>'' THEN excluded.item_id ELSE memory_trace.item_id END,"
                       "task_type=CASE WHEN excluded.task_type<>'' THEN excluded.task_type ELSE memory_trace.task_type END,"
                       "intent_template_hash=CASE WHEN excluded.intent_template_hash<>'' "
                       "THEN excluded.intent_template_hash ELSE memory_trace.intent_template_hash END,"
                       "input_hash=CASE WHEN excluded.input_hash<>'' THEN excluded.input_hash "
                       "ELSE memory_trace.input_hash END,"
                       "knowledge_version=CASE WHEN memory_trace.knowledge_version=0 "
                       "THEN excluded.knowledge_version ELSE memory_trace.knowledge_version END,"
                       "model=CASE WHEN excluded.model<>'' THEN excluded.model ELSE memory_trace.model END,"
                       "tier=CASE WHEN excluded.tier<>'' THEN excluded.tier ELSE memory_trace.tier END,"
                       "outcome=CASE WHEN excluded.outcome<>'' THEN excluded.outcome ELSE memory_trace.outcome END,"
                       "anchors_json=excluded.anchors_json,"
                       "steps_json=excluded.steps_json,truncated_steps=excluded.truncated_steps,"
                       "updated_at=excluded.updated_at", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, traceId);
    bindText(st.stmt, 2, trace.scopeId);
    bindText(st.stmt, 3, trace.runId);
    bindText(st.stmt, 4, trace.nodeId);
    bindText(st.stmt, 5, trace.itemId);
    bindText(st.stmt, 6, trace.taskType);
    bindText(st.stmt, 7, trace.intentTemplateHash);
    bindText(st.stmt, 8, trace.inputHash);
    sqlite3_bind_int64(st.stmt, 9, version);
    bindText(st.stmt, 10, trace.model);
    bindText(st.stmt, 11, trace.tier);
    bindText(st.stmt, 12, trace.outcome);
    bindText(st.stmt, 13, trace.anchors.dump());
    bindText(st.stmt, 14, steps.dump());
    bindText(st.stmt, 15, trace.delivered.dump());
    bindText(st.stmt, 16, trace.used.dump());
    sqlite3_bind_int64(st.stmt, 17, truncatedSteps);
    bindText(st.stmt, 18, now);
    bindText(st.stmt, 19, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool MemoryStore::closeTrace(const std::string& scopeId, const std::string& runId,
                             const std::string& nodeId, const std::string& outcome,
                             const std::string& model, const std::string& tier,
                             bool memoryEnabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || runId.empty() || nodeId.empty()) return false;

    taskforge::Json delivered = taskforge::Json::array();
    taskforge::Json used = taskforge::Json::array();
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT memory_id,MAX(injected),MAX(used) FROM memory_hit "
                       "WHERE scope_id=? AND run_id=? AND node_id=? GROUP BY memory_id", st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, runId);
        bindText(st.stmt, 3, nodeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            if (columnInt(st.stmt, 1) != 0) delivered.push_back(columnText(st.stmt, 0));
            if (columnInt(st.stmt, 2) != 0) used.push_back(columnText(st.stmt, 0));
        }
        sqlite3_finalize(st.stmt);
        st.stmt = nullptr;
    }

    std::string itemId;
    std::string taskType;
    std::string intentHash;
    std::string inputHash;
    if (prepare(db_, "SELECT COALESCE(item_id,''),COALESCE(task_type,''),"
                       "COALESCE(intent_template_hash,''),COALESCE(input_hash,'') "
                       "FROM memory_episode WHERE scope_id=? AND run_id=? AND node_id=? "
                       "ORDER BY rowid DESC LIMIT 1", st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, runId);
        bindText(st.stmt, 3, nodeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            itemId = columnText(st.stmt, 0);
            taskType = columnText(st.stmt, 1);
            intentHash = columnText(st.stmt, 2);
            inputHash = columnText(st.stmt, 3);
        }
        sqlite3_finalize(st.stmt);
        st.stmt = nullptr;
    }

    const std::string now = nowIso();
    const long long version = knowledgeVersion();
    if (!prepare(db_, "INSERT INTO memory_trace(trace_id,scope_id,run_id,node_id,item_id,task_type,"
                       "intent_template_hash,input_hash,knowledge_version,model,tier,outcome,"
                       "anchors_json,steps_json,delivered_json,used_json,truncated_steps,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,'[]','[]',?,?,0,?,?) "
                       "ON CONFLICT(scope_id,run_id,node_id) DO UPDATE SET "
                       "item_id=CASE WHEN excluded.item_id<>'' THEN excluded.item_id ELSE memory_trace.item_id END,"
                       "task_type=CASE WHEN excluded.task_type<>'' THEN excluded.task_type ELSE memory_trace.task_type END,"
                       "intent_template_hash=CASE WHEN excluded.intent_template_hash<>'' "
                       "THEN excluded.intent_template_hash ELSE memory_trace.intent_template_hash END,"
                       "input_hash=CASE WHEN excluded.input_hash<>'' THEN excluded.input_hash ELSE memory_trace.input_hash END,"
                       "model=excluded.model,tier=excluded.tier,outcome=excluded.outcome,"
                       "delivered_json=excluded.delivered_json,used_json=excluded.used_json,"
                       "updated_at=excluded.updated_at", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, newId("trace"));
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, runId);
    bindText(st.stmt, 4, nodeId);
    bindText(st.stmt, 5, itemId);
    bindText(st.stmt, 6, taskType);
    bindText(st.stmt, 7, intentHash);
    bindText(st.stmt, 8, inputHash);
    sqlite3_bind_int64(st.stmt, 9, version);
    bindText(st.stmt, 10, model);
    bindText(st.stmt, 11, tier);
    bindText(st.stmt, 12, outcome);
    bindText(st.stmt, 13, delivered.dump());
    bindText(st.stmt, 14, used.dump());
    bindText(st.stmt, 15, now);
    bindText(st.stmt, 16, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (!memoryEnabled) {
        // Nao apaga um traco ja criado por consulta; apenas evita criar um
        // vazio quando o vertice realmente nao usou memoria.
        if (delivered.empty() && used.empty()) {
            sqlite3_stmt* del = nullptr;
            if (sqlite3_prepare_v2(db_, "DELETE FROM memory_trace WHERE scope_id=? AND run_id=? "
                                        "AND node_id=? AND steps_json='[]'", -1, &del, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(del, 1, scopeId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(del, 2, runId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(del, 3, nodeId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(del);
            }
            if (del) sqlite3_finalize(del);
        }
    }
    return true;
}

taskforge::Json MemoryStore::getTrace(const std::string& scopeId, const std::string& runId,
                                      const std::string& nodeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_ || scopeId.empty() || runId.empty() || nodeId.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT trace_id,run_id,node_id,item_id,task_type,intent_template_hash,"
                       "input_hash,knowledge_version,model,tier,outcome,anchors_json,steps_json,"
                       "delivered_json,used_json,truncated_steps,created_at,updated_at "
                       "FROM memory_trace WHERE scope_id=? AND run_id=? AND node_id=?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, runId);
    bindText(st.stmt, 3, nodeId);
    if (stepRetry(st.stmt) != SQLITE_ROW) return out;
    auto array = [&](int index) {
        taskforge::Json value = parseJsonOrNull(columnText(st.stmt, index));
        return value.is_array() ? value : taskforge::Json::array();
    };
    out["traceId"] = columnText(st.stmt, 0);
    out["runId"] = columnText(st.stmt, 1);
    out["nodeId"] = columnText(st.stmt, 2);
    out["itemId"] = columnText(st.stmt, 3);
    out["taskType"] = columnText(st.stmt, 4);
    out["intentTemplateHash"] = columnText(st.stmt, 5);
    out["inputHash"] = columnText(st.stmt, 6);
    out["knowledgeVersion"] = columnInt(st.stmt, 7);
    out["model"] = columnText(st.stmt, 8);
    out["tier"] = columnText(st.stmt, 9);
    out["outcome"] = columnText(st.stmt, 10);
    out["anchors"] = array(11);
    out["steps"] = array(12);
    out["delivered"] = array(13);
    out["used"] = array(14);
    out["truncatedSteps"] = columnInt(st.stmt, 15);
    out["createdAt"] = columnText(st.stmt, 16);
    out["updatedAt"] = columnText(st.stmt, 17);
    return out;
}

bool MemoryStore::recordLineage(const std::string& scopeId, const std::string& derivedKind,
                                const std::string& derivedId, const std::string& inputKind,
                                const std::string& inputId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || derivedKind.empty() || derivedId.empty() ||
        inputKind.empty() || inputId.empty()) {
        return false;
    }
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_lineage"
                       "(scope_id,derived_kind,derived_id,input_kind,input_id,created_at) "
                       "VALUES(?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, derivedKind);
    bindText(st.stmt, 3, derivedId);
    bindText(st.stmt, 4, inputKind);
    bindText(st.stmt, 5, inputId);
    bindText(st.stmt, 6, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

size_t MemoryStore::backfillLineage(const std::string& scopeId, taskforge::Json* report) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    size_t linked = 0;
    size_t orphans = 0;
    if (!db_) return 0;
    auto link = [&](const std::string& derivedKind, const std::string& derivedId,
                    const std::string& inputKind, const std::string& inputId) {
        if (derivedId.empty() || inputId.empty()) return false;
        if (recordLineage(scopeId, derivedKind, derivedId, inputKind, inputId)) {
            ++linked;
            return true;
        }
        return false;
    };
    Stmt edges;
    std::string error;
    if (prepare(db_, "SELECT id,metadata FROM memory_edge WHERE scope_id=?", edges, error)) {
        bindText(edges.stmt, 1, scopeId);
        while (stepRetry(edges.stmt) == SQLITE_ROW) {
            const auto metadata = parseJsonOrNull(columnText(edges.stmt, 1));
            const std::string passageId = metadata.is_object()
                ? metadata.value("passageId", std::string()) : std::string();
            if (passageId.empty() || !link("edge", columnText(edges.stmt, 0), "passage", passageId)) {
                ++orphans;
            }
        }
    }
    Stmt proposals;
    if (prepare(db_, "SELECT id,payload_json,evidence_json FROM memory_proposal WHERE scope_id=?",
                proposals, error)) {
        bindText(proposals.stmt, 1, scopeId);
        while (stepRetry(proposals.stmt) == SQLITE_ROW) {
            const auto payload = parseJsonOrNull(columnText(proposals.stmt, 1));
            const auto evidence = parseJsonOrNull(columnText(proposals.stmt, 2));
            std::set<std::string> passages;
            if (payload.is_object()) {
                const std::string id = payload.value("passageId", std::string());
                if (!id.empty()) passages.insert(id);
            }
            if (evidence.is_array()) {
                for (const auto& item : evidence) {
                    if (item.is_object()) {
                        const std::string id = item.value("passageId", std::string());
                        if (!id.empty()) passages.insert(id);
                    }
                }
            }
            bool any = false;
            for (const auto& id : passages) {
                any = link("proposal", columnText(proposals.stmt, 0), "passage", id) || any;
            }
            if (!any) ++orphans;
        }
    }
    Stmt facets;
    if (prepare(db_, "SELECT id,supporting_claims_json FROM memory_facet WHERE scope_id=?",
                facets, error)) {
        bindText(facets.stmt, 1, scopeId);
        while (stepRetry(facets.stmt) == SQLITE_ROW) {
            const auto claims = parseJsonOrNull(columnText(facets.stmt, 1));
            bool any = false;
            if (claims.is_array()) {
                for (const auto& claim : claims) {
                    if (claim.is_string()) {
                        any = link("facet", columnText(facets.stmt, 0), "edge", claim.str()) || any;
                    }
                }
            }
            if (!any) ++orphans;
        }
    }
    Stmt playbooks;
    if (prepare(db_, "SELECT id,evidence_json FROM memory_playbook WHERE scope_id=?",
                playbooks, error)) {
        bindText(playbooks.stmt, 1, scopeId);
        while (stepRetry(playbooks.stmt) == SQLITE_ROW) {
            const auto evidence = parseJsonOrNull(columnText(playbooks.stmt, 1));
            bool any = false;
            if (evidence.is_array()) {
                for (const auto& item : evidence) {
                    if (!item.is_object()) continue;
                    const std::string kind = item.value("kind", std::string("edge"));
                    const std::string id = item.value("id", item.value("edgeId",
                        item.value("lessonId", std::string())));
                    if (!id.empty()) any = link("playbook", columnText(playbooks.stmt, 0),
                                                kind, id) || any;
                }
            }
            if (!any) ++orphans;
        }
    }
    if (report) {
        *report = taskforge::Json{{"linked", static_cast<long long>(linked)},
                                  {"orphans", static_cast<long long>(orphans)}};
    }
    return linked;
}

std::string MemoryStore::enqueueJob(const std::string& scopeId, const std::string& kind,
                                    const taskforge::Json& payload) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || kind.empty()) return "";
    const std::string id = newId("job");
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_job(id,scope_id,kind,payload_json,status,created_at,updated_at) "
                       "VALUES(?,?,?,?,'pending',?,?)", st, error)) {
        lastError_ = error;
        return "";
    }
    const std::string now = nowIso();
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, kind);
    bindText(st.stmt, 4, payload.is_object() ? payload.dump() : taskforge::Json::object().dump());
    bindText(st.stmt, 5, now);
    bindText(st.stmt, 6, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    return id;
}

std::vector<MemoryJob> MemoryStore::claimJobs(const std::string& scopeId, size_t limit,
                                              const std::string& owner, int leaseSeconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryJob> out;
    if (!db_ || scopeId.empty() || limit == 0) return out;
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return out;
    }
    const std::string now = nowIso();
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,kind,payload_json,status,attempts,"
                       "COALESCE(lease_owner,''),COALESCE(lease_until,''),"
                       "COALESCE(last_error,''),created_at,updated_at FROM memory_job "
                       "WHERE scope_id=? AND (status='pending' OR "
                       "(status='running' AND COALESCE(lease_until,'')<=?)) "
                       "ORDER BY created_at,id LIMIT ?", st, error)) {
        lastError_ = error;
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, now);
    sqlite3_bind_int(st.stmt, 3, static_cast<int>(std::max<size_t>(1, limit)));
    std::vector<MemoryJob> candidates;
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryJob job;
        job.id = columnText(st.stmt, 0);
        job.scopeId = columnText(st.stmt, 1);
        job.kind = columnText(st.stmt, 2);
        job.payloadJson = columnText(st.stmt, 3);
        job.status = columnText(st.stmt, 4);
        job.attempts = static_cast<int>(columnInt(st.stmt, 5));
        job.leaseOwner = columnText(st.stmt, 6);
        job.leaseUntil = columnText(st.stmt, 7);
        job.lastError = columnText(st.stmt, 8);
        job.createdAt = columnText(st.stmt, 9);
        job.updatedAt = columnText(st.stmt, 10);
        candidates.push_back(std::move(job));
    }
    const std::string leaseUntil = isoAfterSecondsImpl(std::max(1, leaseSeconds));
    for (auto& job : candidates) {
        Stmt update;
        if (!prepare(db_, "UPDATE memory_job SET status='running',lease_owner=?,lease_until=?,"
                           "updated_at=? WHERE id=? AND (status='pending' OR "
                           "(status='running' AND COALESCE(lease_until,'')<=?))",
                     update, error)) {
            lastError_ = error;
            txn.rollback();
            return {};
        }
        bindText(update.stmt, 1, owner);
        bindText(update.stmt, 2, leaseUntil);
        bindText(update.stmt, 3, now);
        bindText(update.stmt, 4, job.id);
        bindText(update.stmt, 5, now);
        if (stepRetry(update.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            txn.rollback();
            return {};
        }
        if (sqlite3_changes(db_) == 0) continue;
        job.status = "running";
        job.leaseOwner = owner;
        job.leaseUntil = leaseUntil;
        out.push_back(std::move(job));
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return {};
    }
    return out;
}

bool MemoryStore::completeJob(const std::string& jobId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || jobId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_job SET status='done',payload_json='{}',lease_owner='',"
                       "lease_until=NULL,updated_at=? WHERE id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, nowIso());
    bindText(st.stmt, 2, jobId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

bool MemoryStore::failJob(const std::string& jobId, const std::string& error, int maxAttempts) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || jobId.empty()) return false;
    Stmt st;
    std::string prepareError;
    if (!prepare(db_, "UPDATE memory_job SET attempts=attempts+1,"
                       "status=CASE WHEN attempts+1>=? THEN 'dead' ELSE 'pending' END,"
                       "lease_owner='',lease_until=NULL,last_error=?,updated_at=? WHERE id=?",
                 st, prepareError)) {
        lastError_ = prepareError;
        return false;
    }
    sqlite3_bind_int(st.stmt, 1, std::max(1, maxAttempts));
    bindText(st.stmt, 2, error);
    bindText(st.stmt, 3, nowIso());
    bindText(st.stmt, 4, jobId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

taskforge::Json MemoryStore::jobSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"pending", 0LL}, {"running", 0LL}, {"done", 0LL}, {"dead", 0LL}};
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT status,COUNT(*) FROM memory_job WHERE scope_id=? GROUP BY status",
                 st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out[columnText(st.stmt, 0)] = columnInt(st.stmt, 1);
    }
    return out;
}

taskforge::Json MemoryStore::tombstoneSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"total", 0LL}, {"byKind", taskforge::Json::object()}};
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT object_kind,COUNT(*) FROM memory_tombstone WHERE scope_id=? "
                       "GROUP BY object_kind", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    long long total = 0;
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        const long long count = columnInt(st.stmt, 1);
        total += count;
        out["byKind"][columnText(st.stmt, 0)] = count;
    }
    out["total"] = total;
    return out;
}

taskforge::Json briefInfoToJson(const MemoryBriefInfo& info) {
    auto array = [](const std::vector<std::string>& values) {
        taskforge::Json out = taskforge::Json::array();
        for (const auto& value : values) out.push_back(value);
        return out;
    };
    return taskforge::Json{{"lessonIds", array(info.lessonIds)},
                           {"factIds", array(info.factIds)},
                           {"playbookIds", array(info.playbookIds)},
                           {"pathIds", array(info.pathIds)},
                           {"pathClaimIds", array(info.pathClaimIds)},
                           {"traversalIds", array(info.traversalIds)},
                           {"workingKeys", array(info.workingKeys)},
                           {"facetIds", array(info.facetIds)},
                           {"stableIds", array(info.stableIds)},
                           {"volatileIds", array(info.volatileIds)},
                           {"stableBlock", info.stableBlock},
                           {"prefixKey", info.prefixKey},
                           {"prefixIdentity", info.prefixIdentity},
                           {"prefixEpoch", info.prefixEpoch},
                           {"removalSeqStart", info.removalSeqStart}};
}

long long MemoryStore::currentRemovalSeq(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COALESCE(MAX(removal_seq),0) FROM memory_removal_log "
                       "WHERE scope_id=?", st, error)) return 0;
    bindText(st.stmt, 1, scopeId);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : 0;
}

long long MemoryStore::latestPrefixEpoch(const std::string& scopeId,
                                         const std::string& prefixKey) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return 0;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT COALESCE(MAX(epoch),0) FROM memory_prefix_epoch "
                       "WHERE scope_id=? AND prefix_key=?", st, error)) return 0;
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, prefixKey);
    return stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : 0;
}

bool MemoryStore::publishPrefixEpoch(const std::string& scopeId, const std::string& prefixKey,
                                     const std::string& contentHash,
                                     const std::string& itemIdsJson, long long* epoch) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || prefixKey.empty()) return false;
    const long long next = latestPrefixEpoch(scopeId, prefixKey) + 1;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_prefix_epoch"
                       "(scope_id,epoch,prefix_key,content_hash,item_ids_json,published_at) "
                       "VALUES(?,?,?,?,?,?)", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int64(st.stmt, 2, next);
    bindText(st.stmt, 3, prefixKey);
    bindText(st.stmt, 4, contentHash);
    bindText(st.stmt, 5, itemIdsJson.empty() ? "[]" : itemIdsJson);
    bindText(st.stmt, 6, nowIso());
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (epoch) *epoch = next;
    return true;
}

taskforge::Json MemoryStore::listPrefixEpochs(const std::string& scopeId,
                                              const std::string& prefixKey,
                                              size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::array();
    if (!db_) return out;
    std::string sql = "SELECT epoch,prefix_key,content_hash,item_ids_json,published_at "
                      "FROM memory_prefix_epoch WHERE scope_id=?";
    if (!prefixKey.empty()) sql += " AND prefix_key=?";
    sql += " ORDER BY epoch DESC LIMIT ?";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql, st, error)) return out;
    bindText(st.stmt, 1, scopeId);
    int index = 2;
    if (!prefixKey.empty()) bindText(st.stmt, index++, prefixKey);
    sqlite3_bind_int(st.stmt, index, static_cast<int>(std::max<size_t>(1, limit)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back(taskforge::Json{{"epoch", columnInt(st.stmt, 0)},
                                      {"prefixKey", columnText(st.stmt, 1)},
                                      {"contentHash", columnText(st.stmt, 2)},
                                      {"itemIds", parseJsonOrNull(columnText(st.stmt, 3))},
                                      {"publishedAt", columnText(st.stmt, 4)}});
    }
    return out;
}

taskforge::Json MemoryStore::removedSince(
    const std::string& scopeId, long long removalSeqStart,
    const std::vector<std::pair<std::string, std::string>>& delivered) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"maxSeq", removalSeqStart}, {"scopeRemoval", false},
                        {"removed", taskforge::Json::array()}};
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT removal_seq,item_kind,item_id FROM memory_removal_log "
                       "WHERE scope_id=? AND removal_seq>? ORDER BY removal_seq", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int64(st.stmt, 2, removalSeqStart);
    long long maxSeq = removalSeqStart;
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        const long long seq = columnInt(st.stmt, 0);
        maxSeq = std::max(maxSeq, seq);
        const std::string kind = columnText(st.stmt, 1);
        const std::string id = columnText(st.stmt, 2);
        bool hit = kind == "scope";
        if (hit) out["scopeRemoval"] = true;
        for (const auto& item : delivered) {
            if (item.first == kind && item.second == id) {
                hit = true;
                break;
            }
        }
        if (hit) {
            out["removed"].push_back(taskforge::Json{{"kind", kind}, {"id", id},
                                                     {"seq", seq}});
        }
    }
    out["maxSeq"] = maxSeq;
    return out;
}

taskforge::Json MemoryStore::lineageSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"links", 0LL}, {"orphanEdges", 0LL}, {"orphanProposals", 0LL},
                        {"orphanFacets", 0LL}, {"orphanPlaybooks", 0LL}};
    if (!db_) return out;
    auto scalar = [&](const char* sql) -> long long {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) return 0;
        bindText(st.stmt, 1, scopeId);
        return stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : 0;
    };
    out["links"] = scalar("SELECT COUNT(*) FROM memory_lineage WHERE scope_id=?");
    out["orphanEdges"] = scalar(
        "SELECT COUNT(*) FROM memory_edge e WHERE e.scope_id=? AND e.status='active' "
        "AND NOT EXISTS (SELECT 1 FROM memory_lineage l WHERE l.scope_id=e.scope_id "
        "AND l.derived_kind='edge' AND l.derived_id=e.id)");
    out["orphanProposals"] = scalar(
        "SELECT COUNT(*) FROM memory_proposal p WHERE p.scope_id=? AND p.status='pending' "
        "AND NOT EXISTS (SELECT 1 FROM memory_lineage l WHERE l.scope_id=p.scope_id "
        "AND l.derived_kind='proposal' AND l.derived_id=p.id)");
    out["orphanFacets"] = scalar(
        "SELECT COUNT(*) FROM memory_facet f WHERE f.scope_id=? AND f.status='active' "
        "AND NOT EXISTS (SELECT 1 FROM memory_lineage l WHERE l.scope_id=f.scope_id "
        "AND l.derived_kind='facet' AND l.derived_id=f.id)");
    out["orphanPlaybooks"] = scalar(
        "SELECT COUNT(*) FROM memory_playbook p WHERE p.scope_id=? AND p.status IN ('active','candidate') "
        "AND NOT EXISTS (SELECT 1 FROM memory_lineage l WHERE l.scope_id=p.scope_id "
        "AND l.derived_kind='playbook' AND l.derived_id=p.id)");
    return out;
}

bool MemoryStore::purgeTelemetry(const std::string& scopeId, const std::string& payloadJson,
                                 std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty()) {
        error = "telemetria indisponivel";
        return false;
    }
    const auto payloadOpt = taskforge::jsonutil::tryParse(payloadJson);
    const taskforge::Json payload = payloadOpt && payloadOpt->is_object()
        ? *payloadOpt : taskforge::Json::object();
    std::set<std::string> itemIds;
    std::set<std::string> passageIds;
    std::set<std::string> hashes;
    std::set<std::string> creditKeys;
    auto collect = [](const taskforge::Json& value, std::set<std::string>& target) {
        if (!value.is_array()) return;
        for (const auto& item : value) {
            if (item.is_string()) target.insert(item.str());
        }
    };
    collect(payload.contains("itemIds") ? payload["itemIds"] : taskforge::Json::array(), itemIds);
    collect(payload.contains("passageIds") ? payload["passageIds"] : taskforge::Json::array(),
            passageIds);
    collect(payload.contains("textHashes") ? payload["textHashes"] : taskforge::Json::array(),
            hashes);
    collect(payload.contains("creditKeys") ? payload["creditKeys"]
                                           : taskforge::Json::array(),
            creditKeys);
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        error = txnError;
        return false;
    }
    for (const auto& id : itemIds) {
        Stmt del;
        if (!prepare(db_, "DELETE FROM memory_hit WHERE scope_id=? AND memory_id=?", del, error)) {
            txn.rollback();
            return false;
        }
        bindText(del.stmt, 1, scopeId);
        bindText(del.stmt, 2, id);
        if (stepRetry(del.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            txn.rollback();
            return false;
        }
    }
    for (const auto& key : creditKeys) {
        Stmt delEvent;
        if (!prepare(db_, "DELETE FROM memory_credit_event WHERE scope_id=? AND item_key=?",
                     delEvent, error)) {
            txn.rollback();
            return false;
        }
        bindText(delEvent.stmt, 1, scopeId);
        bindText(delEvent.stmt, 2, key);
        if (stepRetry(delEvent.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            txn.rollback();
            return false;
        }
        Stmt delUtility;
        if (!prepare(db_, "DELETE FROM memory_item_utility WHERE scope_id=? AND item_key=?",
                     delUtility, error)) {
            txn.rollback();
            return false;
        }
        bindText(delUtility.stmt, 1, scopeId);
        bindText(delUtility.stmt, 2, key);
        if (stepRetry(delUtility.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            txn.rollback();
            return false;
        }
    }
    Stmt episodes;
    if (!prepare(db_, "SELECT id,content,metadata FROM memory_episode WHERE scope_id=?", episodes,
                 error)) {
        txn.rollback();
        return false;
    }
    bindText(episodes.stmt, 1, scopeId);
    std::vector<std::pair<std::string, std::string>> redact;
    while (stepRetry(episodes.stmt) == SQLITE_ROW) {
        const std::string id = columnText(episodes.stmt, 0);
        const std::string content = columnText(episodes.stmt, 1);
        const std::string metadata = columnText(episodes.stmt, 2);
        bool hit = false;
        for (const auto& item : itemIds) {
            if (metadata.find(item) != std::string::npos) hit = true;
        }
        for (const auto& passage : passageIds) {
            if (metadata.find(passage) != std::string::npos) hit = true;
        }
        if (!hit && !hashes.empty()) {
            for (const auto& hash : textWindowHashes(content, 1)) {
                if (hashes.count(hash)) {
                    hit = true;
                    break;
                }
            }
        }
        if (hit) redact.emplace_back(id, content);
    }
    for (const auto& item : redact) {
        Stmt update;
        if (!prepare(db_, "UPDATE memory_episode SET content='[apagado]',metadata=? WHERE id=?",
                     update, error)) {
            txn.rollback();
            return false;
        }
        bindText(update.stmt, 1, taskforge::Json{{"redacted", true}}.dump());
        bindText(update.stmt, 2, item.first);
        if (stepRetry(update.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            txn.rollback();
            return false;
        }
    }
    if (!txn.commit(&txnError)) {
        error = txnError;
        return false;
    }
    return true;
}

MemoryEraseResult MemoryStore::eraseSource(const std::string& scopeId,
                                           const MemorySourceSelector& selector,
                                           const std::string& reason,
                                           const std::string& requestedBy) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryEraseResult result;
    if (!db_ || scopeId.empty()) {
        result.error = "memoria indisponivel";
        return result;
    }
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        result.error = txnError;
        return result;
    }
    auto fail = [&](const std::string& message) {
        txn.rollback();
        result.error = message;
        return result;
    };
    struct SourceRow {
        std::string id, uri, hash, status;
    };
    std::vector<SourceRow> sources;
    std::string normalizedUri;
    if (!selector.sourceVersionId.empty()) {
        Stmt st;
        std::string error;
        if (!prepare(db_, "SELECT id,uri,content_hash,status FROM memory_source "
                           "WHERE scope_id=? AND id=?", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, selector.sourceVersionId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            sources.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                               columnText(st.stmt, 2), columnText(st.stmt, 3)});
        }
        if (sources.empty()) return fail("versao de fonte nao encontrada");
    } else {
        normalizedUri = MemorySources::normalizeUri(selector.uri);
        if (normalizedUri.empty()) return fail("uri da fonte vazia");
        Stmt st;
        std::string error;
        if (!prepare(db_, "SELECT id,uri,content_hash,status FROM memory_source "
                           "WHERE scope_id=? AND uri=? ORDER BY first_seen,id", st, error)) {
            return fail(error);
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, normalizedUri);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            sources.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                               columnText(st.stmt, 2), columnText(st.stmt, 3)});
        }
        if (sources.empty()) return fail("fonte nao encontrada");
        if (sources.size() > 1 && !selector.allVersions) {
            std::string list;
            for (const auto& source : sources) {
                if (!list.empty()) list += ", ";
                list += source.id;
            }
            return fail("mais de uma versao da fonte: " + list);
        }
    }
    bool allForgotten = true;
    for (const auto& source : sources) {
        if (source.status != "forgotten") allForgotten = false;
    }
    if (allForgotten) {
        result.ok = true;
        result.uri = normalizedUri.empty() ? sources.front().uri : normalizedUri;
        return result;
    }
    // A lapide de URI bloqueia nova ingestao, nao a exclusao explicita de outra
    // versao historica da mesma fonte.
    result.sourceVersionId = selector.sourceVersionId;
    result.uri = normalizedUri.empty() && !sources.empty() ? sources.front().uri : normalizedUri;
    result.sources = sources.size();

    std::set<std::string> sourceIds;
    std::set<std::string> passageIds;
    std::set<std::string> deletedEdges;
    std::set<std::string> deletedClaimKeys;
    std::set<std::string> deletedProposals;
    std::set<std::string> deletedFacets;
    std::set<std::string> deletedPlaybooks;
    std::set<std::string> deletedLessons;
    std::set<std::string> deletedPaths;
    std::vector<std::string> passageTexts;

    for (const auto& source : sources) {
        sourceIds.insert(source.id);
        Stmt passages;
        std::string error;
        if (!prepare(db_, "SELECT id,text FROM memory_passage WHERE scope_id=? AND source_id=?",
                     passages, error)) {
            return fail(error);
        }
        bindText(passages.stmt, 1, scopeId);
        bindText(passages.stmt, 2, source.id);
        while (stepRetry(passages.stmt) == SQLITE_ROW) {
            passageIds.insert(columnText(passages.stmt, 0));
            passageTexts.push_back(columnText(passages.stmt, 1));
        }
    }
    result.passages = passageIds.size();
    result.passageIds.assign(passageIds.begin(), passageIds.end());

    // BFS sobre a linhagem gravada.
    struct LineageEdge {
        std::string derivedKind, derivedId, inputKind, inputId;
    };
    std::vector<LineageEdge> lineage;
    {
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT derived_kind,derived_id,input_kind,input_id "
                           "FROM memory_lineage WHERE scope_id=?", st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                lineage.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                                   columnText(st.stmt, 2), columnText(st.stmt, 3)});
            }
        }
    }
    std::set<std::string> frontier = passageIds;
    std::set<std::string> seen = passageIds;
    while (!frontier.empty()) {
        std::set<std::string> next;
        for (const auto& edge : lineage) {
            if (!frontier.count(edge.inputId)) continue;
            if (!seen.insert(edge.derivedId).second) continue;
            if (edge.derivedKind == "edge") deletedEdges.insert(edge.derivedId);
            else if (edge.derivedKind == "proposal") deletedProposals.insert(edge.derivedId);
            else if (edge.derivedKind == "facet") deletedFacets.insert(edge.derivedId);
            else if (edge.derivedKind == "playbook") deletedPlaybooks.insert(edge.derivedId);
            else if (edge.derivedKind == "lesson") deletedLessons.insert(edge.derivedId);
            else if (edge.derivedKind == "path") deletedPaths.insert(edge.derivedId);
            next.insert(edge.derivedId);
        }
        frontier.swap(next);
    }

    // Banco antigo: a metadata da aresta e da proposta ainda e uma fonte de verdade.
    {
        Stmt edges;
        std::string error;
        if (prepare(db_, "SELECT id,metadata FROM memory_edge WHERE scope_id=?", edges, error)) {
            bindText(edges.stmt, 1, scopeId);
            while (stepRetry(edges.stmt) == SQLITE_ROW) {
                const auto metadata = parseJsonOrNull(columnText(edges.stmt, 1));
                if (!metadata.is_object()) continue;
                const std::string passageId = metadata.value("passageId", std::string());
                if (passageIds.count(passageId)) deletedEdges.insert(columnText(edges.stmt, 0));
            }
        }
    }
    {
        Stmt proposals;
        std::string error;
        if (prepare(db_, "SELECT id,payload_json,evidence_json FROM memory_proposal WHERE scope_id=?",
                    proposals, error)) {
            bindText(proposals.stmt, 1, scopeId);
            while (stepRetry(proposals.stmt) == SQLITE_ROW) {
                const auto payload = parseJsonOrNull(columnText(proposals.stmt, 1));
                const auto evidence = parseJsonOrNull(columnText(proposals.stmt, 2));
                bool hit = payload.is_object() &&
                           passageIds.count(payload.value("passageId", std::string()));
                if (evidence.is_array()) {
                    for (const auto& item : evidence) {
                        if (item.is_object() && passageIds.count(
                                item.value("passageId", std::string()))) hit = true;
                    }
                }
                if (hit) deletedProposals.insert(columnText(proposals.stmt, 0));
            }
        }
    }
    // Uma claim com outra evidencia sobrevivente continua ativa; a linhagem
    // registra a versao/trecho de cada apoio, nao apenas a raiz.
    auto inputAlive = [&](const std::string& kind, const std::string& id) {
        if (id.empty()) return false;
        if (kind == "passage") return !passageIds.count(id) &&
            [&]() {
                Stmt query;
                std::string error;
                if (!prepare(db_, "SELECT 1 FROM memory_passage WHERE id=? AND status='active'",
                             query, error)) return false;
                bindText(query.stmt, 1, id);
                return stepRetry(query.stmt) == SQLITE_ROW;
            }();
        if (kind == "source_version") return !sourceIds.count(id) &&
            [&]() {
                Stmt query;
                std::string error;
                if (!prepare(db_, "SELECT 1 FROM memory_source WHERE id=? AND status='active'",
                             query, error)) return false;
                bindText(query.stmt, 1, id);
                return stepRetry(query.stmt) == SQLITE_ROW;
            }();
        return false;
    };
    for (auto it = deletedEdges.begin(); it != deletedEdges.end();) {
        bool hasLineage = false;
        bool survives = false;
        for (const auto& entry : lineage) {
            if (entry.derivedKind != "edge" || entry.derivedId != *it) continue;
            hasLineage = true;
            if (inputAlive(entry.inputKind, entry.inputId)) survives = true;
        }
        if (!hasLineage) {
            Stmt query;
            std::string error;
            if (prepare(db_, "SELECT COALESCE(metadata,'{}') FROM memory_edge WHERE id=?",
                        query, error)) {
                bindText(query.stmt, 1, *it);
                if (stepRetry(query.stmt) == SQLITE_ROW) {
                    const auto metadata = parseJsonOrNull(columnText(query.stmt, 0));
                    if (metadata.is_object()) {
                        const std::string passageId = metadata.value("passageId", std::string());
                        if (inputAlive("passage", passageId)) survives = true;
                    }
                }
            }
        }
        if (survives) it = deletedEdges.erase(it);
        else ++it;
    }

    // Facetas que dependem de uma afirmacao retirada.
    {
        Stmt facets;
        std::string error;
        if (prepare(db_, "SELECT id,supporting_claims_json FROM memory_facet WHERE scope_id=?",
                    facets, error)) {
            bindText(facets.stmt, 1, scopeId);
            while (stepRetry(facets.stmt) == SQLITE_ROW) {
                const auto claims = parseJsonOrNull(columnText(facets.stmt, 1));
                if (!claims.is_array()) continue;
                for (const auto& claim : claims) {
                    if (claim.is_string() && deletedEdges.count(claim.str())) {
                        deletedFacets.insert(columnText(facets.stmt, 0));
                        break;
                    }
                }
            }
        }
    }

    auto insertTombstone = [&](const std::string& kind, const std::string& id,
                               const std::string& uri, const std::string& hash,
                               const std::string& why) -> bool {
        Stmt st;
        std::string error;
        if (!prepare(db_, "INSERT OR IGNORE INTO memory_tombstone"
                           "(scope_id,object_kind,object_id,source_uri,content_hash,reason,"
                           "requested_by,created_at) VALUES(?,?,?,?,?,?,?,?)", st, error)) {
            lastError_ = error;
            return false;
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, kind);
        bindText(st.stmt, 3, id);
        bindText(st.stmt, 4, uri);
        bindText(st.stmt, 5, hash);
        bindText(st.stmt, 6, reason);
        bindText(st.stmt, 7, requestedBy);
        bindText(st.stmt, 8, nowIso());
        if (stepRetry(st.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        if (sqlite3_changes(db_) > 0) result.tombstoneIds.push_back(kind + ":" + id);
        return true;
    };
    auto insertRemoval = [&](const std::string& kind, const std::string& id,
                             const std::string& why) -> bool {
        Stmt st;
        std::string error;
        if (!prepare(db_, "INSERT INTO memory_removal_log(scope_id,item_kind,item_id,reason,created_at) "
                           "VALUES(?,?,?,?,?)", st, error)) {
            lastError_ = error;
            return false;
        }
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, kind);
        bindText(st.stmt, 3, id);
        bindText(st.stmt, 4, why);
        bindText(st.stmt, 5, nowIso());
        return stepRetry(st.stmt) == SQLITE_DONE;
    };

    for (const auto& edgeId : deletedEdges) {
        Stmt query;
        std::string error;
        if (!prepare(db_, "SELECT status,COALESCE(metadata,'{}'),edge_hash,"
                           "COALESCE(claim_key,'') FROM memory_edge "
                           "WHERE id=? AND scope_id=?", query, error)) {
            return fail(error);
        }
        bindText(query.stmt, 1, edgeId);
        bindText(query.stmt, 2, scopeId);
        if (stepRetry(query.stmt) != SQLITE_ROW) continue;
        const std::string status = columnText(query.stmt, 0);
        const std::string metadata = columnText(query.stmt, 1);
        const std::string edgeHash = columnText(query.stmt, 2);
        const std::string claimKey = columnText(query.stmt, 3);
        if (!claimKey.empty()) deletedClaimKeys.insert(claimKey);
        if (status == "active") {
            Stmt update;
            if (!prepare(db_, "UPDATE memory_edge SET status='rejected',retracted_at=?,"
                               "valid_to=COALESCE(valid_to,?),"
                               "metadata=json_set(metadata,'$.retractionReason',?,"
                               "'$.retractedAt',?) WHERE id=? AND status='active'",
                         update, error)) {
                return fail(error);
            }
            const std::string at = nowIso();
            bindText(update.stmt, 1, at);
            bindText(update.stmt, 2, at);
            bindText(update.stmt, 3, reason);
            bindText(update.stmt, 4, at);
            bindText(update.stmt, 5, edgeId);
            if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
            ++result.claimsRetracted;
            result.retractedEdgeIds.push_back(edgeId);
            if (!insertTombstone("edge", edgeId, result.uri, edgeHash, reason) ||
                !insertRemoval("edge", edgeId, reason)) {
                return fail(lastError_);
            }
        }
    }
    for (const auto& proposalId : deletedProposals) {
        Stmt update;
        std::string error;
        if (!prepare(db_, "UPDATE memory_proposal SET payload_json='{}',evidence_json='[]',"
                           "status='rejected',reviewed_at=?,reviewed_by=? WHERE id=? AND scope_id=?",
                     update, error)) {
            return fail(error);
        }
        bindText(update.stmt, 1, nowIso());
        bindText(update.stmt, 2, requestedBy);
        bindText(update.stmt, 3, proposalId);
        bindText(update.stmt, 4, scopeId);
        if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        ++result.proposals;
        if (!insertTombstone("proposal", proposalId, result.uri, "", reason) ||
            !insertRemoval("proposal", proposalId, reason)) {
            return fail(lastError_);
        }
    }
    for (const auto& facetId : deletedFacets) {
        Stmt update;
        std::string error;
        if (!prepare(db_, "UPDATE memory_facet SET description='[apagado]',"
                           "supporting_claims_json='[]',status='retired' WHERE id=? AND scope_id=?",
                     update, error)) {
            return fail(error);
        }
        bindText(update.stmt, 1, facetId);
        bindText(update.stmt, 2, scopeId);
        if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        ++result.derivativesTombstoned;
        if (!insertTombstone("facet", facetId, result.uri, "", reason) ||
            !insertRemoval("facet", facetId, reason)) {
            return fail(lastError_);
        }
    }
    for (const auto& playbookId : deletedPlaybooks) {
        std::string taskType;
        {
            Stmt query;
            std::string queryError;
            if (prepare(db_, "SELECT task_type FROM memory_playbook WHERE id=? AND scope_id=?",
                        query, queryError)) {
                bindText(query.stmt, 1, playbookId);
                bindText(query.stmt, 2, scopeId);
                if (stepRetry(query.stmt) == SQLITE_ROW) taskType = columnText(query.stmt, 0);
            }
        }
        Stmt update;
        std::string error;
        if (!prepare(db_, "UPDATE memory_playbook SET title='[apagado]',steps_json='[]',"
                           "evidence_json='[]',status='retired' WHERE id=? AND scope_id=?",
                     update, error)) {
            return fail(error);
        }
        bindText(update.stmt, 1, playbookId);
        bindText(update.stmt, 2, scopeId);
        if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        ++result.derivativesTombstoned;
        if (!insertTombstone("playbook", playbookId, result.uri, "", reason) ||
            !insertRemoval("playbook", playbookId, reason)) {
            return fail(lastError_);
        }
        const std::string job = enqueueJob(
            scopeId, "recompile_playbook",
            taskforge::Json{{"playbookId", playbookId}, {"taskType", taskType}});
        if (!job.empty()) {
            ++result.jobs;
            result.jobIds.push_back(job);
        }
    }
    for (const auto& lessonId : deletedLessons) {
        ++result.derivativesTombstoned;
        if (!insertTombstone("lesson", lessonId, result.uri, "", reason) ||
            !insertRemoval("lesson", lessonId, reason)) {
            return fail(lastError_);
        }
        const std::string job = enqueueJob(
            scopeId, "reconsolidate_lesson", taskforge::Json{{"lessonId", lessonId}});
        if (!job.empty()) {
            ++result.jobs;
            result.jobIds.push_back(job);
        }
    }
    for (const auto& pathId : deletedPaths) {
        Stmt update;
        std::string error;
        if (!prepare(db_, "UPDATE memory_path SET state='retired',access_json='{}' "
                           "WHERE id=? AND scope_id=?", update, error)) {
            return fail(error);
        }
        bindText(update.stmt, 1, pathId);
        bindText(update.stmt, 2, scopeId);
        if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        ++result.derivativesTombstoned;
        if (!insertTombstone("path", pathId, result.uri, "", reason) ||
            !insertRemoval("path", pathId, reason)) {
            return fail(lastError_);
        }
    }

    for (const auto& edgeId : deletedEdges) {
        Stmt del;
        std::string error;
        if (!prepare(db_, "DELETE FROM memory_evidence WHERE scope_id=? AND target_id=? "
                           "AND target_kind IN ('claim','fact','edge')", del, error)) {
            return fail(error);
        }
        bindText(del.stmt, 1, scopeId);
        bindText(del.stmt, 2, edgeId);
        if (stepRetry(del.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        result.evidenceDeleted += static_cast<size_t>(sqlite3_changes(db_));
    }

    if (testInterleaveHook_) testInterleaveHook_("eraseSource:before_delete");
    std::vector<std::string> textHashes;
    for (const auto& text : passageTexts) {
        for (const auto& hash : textWindowHashes(text)) textHashes.push_back(hash);
    }
    for (const auto& passageId : passageIds) {
        Stmt delFts;
        std::string error;
        if (!prepare(db_, "DELETE FROM memory_passage_fts WHERE passage_id=?", delFts, error)) {
            return fail(error);
        }
        bindText(delFts.stmt, 1, passageId);
        stepRetry(delFts.stmt);
        Stmt delEmbedding;
        if (!prepare(db_, "DELETE FROM memory_embedding WHERE scope_id=? AND object_type='passage' "
                           "AND object_id=?", delEmbedding, error)) {
            return fail(error);
        }
        bindText(delEmbedding.stmt, 1, scopeId);
        bindText(delEmbedding.stmt, 2, passageId);
        stepRetry(delEmbedding.stmt);
        Stmt delExtraction;
        if (!prepare(db_, "DELETE FROM memory_claim_extraction WHERE scope_id=? AND passage_id=?",
                     delExtraction, error)) {
            return fail(error);
        }
        bindText(delExtraction.stmt, 1, scopeId);
        bindText(delExtraction.stmt, 2, passageId);
        stepRetry(delExtraction.stmt);
        Stmt delPassage;
        if (!prepare(db_, "DELETE FROM memory_passage WHERE id=? AND scope_id=?", delPassage,
                     error)) {
            return fail(error);
        }
        bindText(delPassage.stmt, 1, passageId);
        bindText(delPassage.stmt, 2, scopeId);
        if (stepRetry(delPassage.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        if (!insertRemoval("passage", passageId, reason)) return fail(lastError_);
    }
    for (const auto& source : sources) {
        Stmt update;
        std::string error;
        if (!prepare(db_, "UPDATE memory_source SET status='forgotten' WHERE id=? AND scope_id=?",
                     update, error)) {
            return fail(error);
        }
        bindText(update.stmt, 1, source.id);
        bindText(update.stmt, 2, scopeId);
        if (stepRetry(update.stmt) != SQLITE_DONE) return fail(sqlite3_errmsg(db_));
        if (!insertTombstone("source_version", source.id, source.uri, source.hash, reason) ||
            !insertTombstone("source_uri", MemorySources::normalizeUri(source.uri), source.uri,
                             source.hash, reason) ||
            !insertRemoval("source", source.id, reason)) {
            return fail(lastError_);
        }
    }

    taskforge::Json itemIds = taskforge::Json::array();
    for (const auto& id : deletedEdges) itemIds.push_back(id);
    for (const auto& id : deletedProposals) itemIds.push_back(id);
    for (const auto& id : deletedFacets) itemIds.push_back(id);
    for (const auto& id : deletedPlaybooks) itemIds.push_back(id);
    for (const auto& id : deletedLessons) itemIds.push_back(id);
    for (const auto& id : deletedPaths) itemIds.push_back(id);
    taskforge::Json creditKeyJson = taskforge::Json::array();
    for (const auto& id : deletedEdges) creditKeyJson.push_back("edge:" + id);
    for (const auto& key : deletedClaimKeys) creditKeyJson.push_back(key);
    for (const auto& id : deletedLessons) creditKeyJson.push_back("lesson:" + id);
    for (const auto& id : deletedPlaybooks) creditKeyJson.push_back("playbook:" + id);
    for (const auto& id : deletedPaths) creditKeyJson.push_back("path:" + id);
    for (const auto& id : deletedFacets) creditKeyJson.push_back("facet:" + id);
    for (const auto& id : passageIds) creditKeyJson.push_back("psg:" + id);
    taskforge::Json passageJson = taskforge::Json::array();
    for (const auto& id : passageIds) passageJson.push_back(id);
    taskforge::Json hashesJson = taskforge::Json::array();
    for (const auto& hash : textHashes) hashesJson.push_back(hash);
    const std::string jobId = enqueueJob(scopeId, "purge_telemetry",
        taskforge::Json{{"itemIds", itemIds}, {"passageIds", passageJson},
                        {"textHashes", hashesJson}, {"creditKeys", creditKeyJson},
                        {"reason", reason}});
    if (!jobId.empty()) {
        ++result.jobs;
        result.jobIds.push_back(jobId);
    }
    audit(requestedBy.empty() ? "forget" : requestedBy, "erase_source", scopeId, "",
          reason + " sources=" + std::to_string(sources.size()) +
          " passages=" + std::to_string(passageIds.size()) +
          " claims=" + std::to_string(result.claimsRetracted));
    if (!txn.commit(&txnError)) {
        result.error = txnError;
        return result;
    }
    std::string ignored;
    exec("PRAGMA wal_checkpoint(TRUNCATE);", &ignored);
    result.ok = true;
    return result;
}

std::string MemoryStore::ingestSource(const MemorySourceRecord& source,
                                      const std::vector<MemoryPassageRecord>& passages,
                                      size_t* insertedPassages, std::string& error,
                                      bool allowForgotten) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    error.clear();
    if (insertedPassages) *insertedPassages = 0;
    if (!db_ || source.scopeId.empty() || source.uri.empty() || source.contentHash.empty()) {
        error = "fonte invalida";
        return "";
    }
    const std::string uri = MemorySources::normalizeUri(source.uri);
    if (!allowForgotten) {
        Stmt tomb;
        std::string tombError;
        if (prepare(db_, "SELECT 1 FROM memory_tombstone WHERE scope_id=? "
                           "AND object_kind='source_uri' AND object_id=? LIMIT 1",
                    tomb, tombError)) {
            bindText(tomb.stmt, 1, source.scopeId);
            bindText(tomb.stmt, 2, uri);
            if (stepRetry(tomb.stmt) == SQLITE_ROW) {
                error = "source_forgotten";
                return "";
            }
        }
    }
    const std::string id = source.id.empty()
        ? MemorySources::sourceId(source.scopeId, uri, source.contentHash) : source.id;
    WriteTxn txn(*this, error);
    if (!txn.ok()) return "";

    auto rollback = [&]() {
        txn.rollback();
        return "";
    };
    if (allowForgotten) {
        Stmt tomb;
        std::string tombError;
        if (prepare(db_, "DELETE FROM memory_tombstone WHERE scope_id=? "
                           "AND object_kind='source_uri' AND object_id=?", tomb, tombError)) {
            bindText(tomb.stmt, 1, source.scopeId);
            bindText(tomb.stmt, 2, uri);
            stepRetry(tomb.stmt);
            audit("forget", "allow_forgotten", uri, "", source.scopeId);
        }
    }

    std::string activeId;
    std::string activeHash;
    Stmt st;
    if (!prepare(db_, "SELECT id,content_hash FROM memory_source WHERE scope_id=? AND uri=? "
                       "AND status='active' ORDER BY rowid DESC LIMIT 1", st, error)) {
        return rollback();
    }
    bindText(st.stmt, 1, source.scopeId);
    bindText(st.stmt, 2, uri);
    if (stepRetry(st.stmt) == SQLITE_ROW) {
        activeId = columnText(st.stmt, 0);
        activeHash = columnText(st.stmt, 1);
    }
    sqlite3_finalize(st.stmt);
    st.stmt = nullptr;

    const std::string now = nowIso();
    if (activeId == id && activeHash == source.contentHash) {
        Stmt update;
        if (!prepare(db_, "UPDATE memory_source SET last_seen=? WHERE id=?", update, error)) {
            return rollback();
        }
        bindText(update.stmt, 1, now);
        bindText(update.stmt, 2, id);
        if (stepRetry(update.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
        if (!txn.commit(&error)) return "";
        return id;
    }

    if (!activeId.empty() && activeId != id) {
        Stmt supersede;
        if (!prepare(db_, "UPDATE memory_source SET status='superseded' WHERE id=?", supersede, error)) {
            return rollback();
        }
        bindText(supersede.stmt, 1, activeId);
        if (stepRetry(supersede.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
        Stmt oldPassages;
        if (!prepare(db_, "UPDATE memory_passage SET status='superseded' WHERE source_id=?",
                     oldPassages, error)) {
            return rollback();
        }
        bindText(oldPassages.stmt, 1, activeId);
        if (stepRetry(oldPassages.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
    }

    {
        Stmt insert;
        if (!prepare(db_, "INSERT INTO memory_source(id,scope_id,uri,content_hash,kind,size,"
                           "first_seen,last_seen,status) VALUES(?,?,?,?,?,?,?,?,'active') "
                           "ON CONFLICT(id) DO UPDATE SET last_seen=excluded.last_seen,status='active'",
                     insert, error)) {
            return rollback();
        }
        bindText(insert.stmt, 1, id);
        bindText(insert.stmt, 2, source.scopeId);
        bindText(insert.stmt, 3, uri);
        bindText(insert.stmt, 4, source.contentHash);
        bindText(insert.stmt, 5, source.kind);
        sqlite3_bind_int64(insert.stmt, 6, source.size);
        bindText(insert.stmt, 7, now);
        bindText(insert.stmt, 8, now);
        if (stepRetry(insert.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
    }

    {
        Stmt removeFts;
        if (!prepare(db_, "DELETE FROM memory_passage_fts WHERE source_id=?", removeFts, error)) {
            return rollback();
        }
        bindText(removeFts.stmt, 1, id);
        if (stepRetry(removeFts.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
    }

    Stmt insertPassage;
    if (!prepare(db_, "INSERT INTO memory_passage(id,source_id,scope_id,ordinal,start,end,text,"
                       "text_hash,tokens,status,created_at) VALUES(?,?,?,?,?,?,?,?,?,'active',?) "
                       "ON CONFLICT(id) DO UPDATE SET start=excluded.start,end=excluded.end,"
                       "text=excluded.text,text_hash=excluded.text_hash,tokens=excluded.tokens,"
                       "status='active',created_at=excluded.created_at", insertPassage, error)) {
        return rollback();
    }
    Stmt insertFts;
    if (!prepare(db_, "INSERT INTO memory_passage_fts(passage_id,text,uri,source_id) VALUES(?,?,?,?)",
                 insertFts, error)) {
        return rollback();
    }
    for (const auto& passage : passages) {
        sqlite3_reset(insertPassage.stmt);
        sqlite3_clear_bindings(insertPassage.stmt);
        bindText(insertPassage.stmt, 1, passage.id);
        bindText(insertPassage.stmt, 2, id);
        bindText(insertPassage.stmt, 3, source.scopeId);
        sqlite3_bind_int64(insertPassage.stmt, 4, passage.ordinal);
        sqlite3_bind_int64(insertPassage.stmt, 5, passage.start);
        sqlite3_bind_int64(insertPassage.stmt, 6, passage.end);
        bindText(insertPassage.stmt, 7, passage.text);
        bindText(insertPassage.stmt, 8, passage.textHash);
        sqlite3_bind_int64(insertPassage.stmt, 9, passage.tokens);
        bindText(insertPassage.stmt, 10, now);
        if (stepRetry(insertPassage.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
        sqlite3_reset(insertFts.stmt);
        sqlite3_clear_bindings(insertFts.stmt);
        bindText(insertFts.stmt, 1, passage.id);
        bindText(insertFts.stmt, 2, passage.text);
        bindText(insertFts.stmt, 3, uri);
        bindText(insertFts.stmt, 4, id);
        if (stepRetry(insertFts.stmt) != SQLITE_DONE) {
            error = sqlite3_errmsg(db_);
            return rollback();
        }
        recordLineage(source.scopeId, "passage", passage.id, "source_version", id);
    }
    if (!txn.commit(&error)) return "";
    if (insertedPassages) *insertedPassages = passages.size();
    return id;
}

std::vector<MemoryPassageHit> MemoryStore::searchPassages(const std::string& scopeId,
                                                          const std::string& text,
                                                          size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryPassageHit> out;
    if (!db_ || text.empty()) return out;
    std::string escaped;
    for (char c : text) {
        if (c == '"') escaped += "\"\"";
        else escaped += c;
    }
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT p.id,p.source_id,s.uri,p.start,p.end,p.text "
                       "FROM memory_passage_fts f JOIN memory_passage p ON p.id=f.passage_id "
                       "JOIN memory_source s ON s.id=p.source_id "
                       "WHERE memory_passage_fts MATCH ? AND p.scope_id=? "
                       "AND p.status='active' AND s.status='active' "
                       "ORDER BY bm25(memory_passage_fts) LIMIT ?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, "\"" + escaped + "\"");
    bindText(st.stmt, 2, scopeId);
    sqlite3_bind_int(st.stmt, 3, static_cast<int>(std::max<size_t>(1, limit)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1), columnText(st.stmt, 2),
                       columnInt(st.stmt, 3), columnInt(st.stmt, 4), columnText(st.stmt, 5)});
    }
    return out;
}

taskforge::Json MemoryStore::sourceSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::object();
    if (!db_) return out;
    out["sources"] = 0;
    out["passages"] = 0;
    out["supersededSources"] = 0;
    out["supersededPassages"] = 0;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT "
                       "SUM(CASE WHEN status='active' THEN 1 ELSE 0 END),"
                       "SUM(CASE WHEN status='superseded' THEN 1 ELSE 0 END) "
                       "FROM memory_source WHERE scope_id=?", st, error)) {
        bindText(st.stmt, 1, scopeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            out["sources"] = columnInt(st.stmt, 0);
            out["supersededSources"] = columnInt(st.stmt, 1);
        }
        sqlite3_finalize(st.stmt);
        st.stmt = nullptr;
    }
    if (prepare(db_, "SELECT "
                       "SUM(CASE WHEN status='active' THEN 1 ELSE 0 END),"
                       "SUM(CASE WHEN status='superseded' THEN 1 ELSE 0 END) "
                       "FROM memory_passage WHERE scope_id=?", st, error)) {
        bindText(st.stmt, 1, scopeId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            out["passages"] = columnInt(st.stmt, 0);
            out["supersededPassages"] = columnInt(st.stmt, 1);
        }
    }
    return out;
}

std::vector<std::string> MemoryStore::usedPassageIds(const std::string& scopeId,
                                                     size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT DISTINCT h.memory_id FROM memory_hit h "
                       "JOIN memory_validation v ON v.scope_id=h.scope_id "
                       "AND v.run_id=h.run_id AND v.node_id=h.node_id AND v.ok=1 "
                       "WHERE h.scope_id=? AND h.used=1 AND h.memory_id LIKE 'psg_%' "
                       "ORDER BY h.memory_id LIMIT ?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int(st.stmt, 2, static_cast<int>(std::max<size_t>(1, limit)));
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    return out;
}

std::vector<MemoryClaimCandidate> MemoryStore::passagesForIds(
    const std::string& scopeId, const std::vector<std::string>& ids, size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryClaimCandidate> out;
    if (!db_ || ids.empty()) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT p.id,p.source_id,s.uri,p.text,p.text_hash,p.start,p.end "
                       "FROM memory_passage p JOIN memory_source s ON s.id=p.source_id "
                       "WHERE p.scope_id=? AND p.status='active' AND s.status='active' "
                       "AND p.id=? LIMIT 1", st, error)) {
        return out;
    }
    for (const auto& id : ids) {
        if (out.size() >= limit) break;
        sqlite3_reset(st.stmt);
        sqlite3_clear_bindings(st.stmt);
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, id);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            out.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                           columnText(st.stmt, 2), columnText(st.stmt, 3),
                           columnText(st.stmt, 4), columnInt(st.stmt, 5),
                           columnInt(st.stmt, 6)});
        }
    }
    return out;
}

bool MemoryStore::claimExtractionSeen(const std::string& scopeId,
                                      const std::string& passageHash,
                                      const std::string& fingerprint) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || passageHash.empty() || fingerprint.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT 1 FROM memory_claim_extraction WHERE scope_id=? "
                       "AND passage_hash=? AND extractor_fingerprint=? "
                       "AND status IN ('proposed','empty','abstained') LIMIT 1", st, error)) {
        return false;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, passageHash);
    bindText(st.stmt, 3, fingerprint);
    return stepRetry(st.stmt) == SQLITE_ROW;
}

bool MemoryStore::markClaimExtraction(const std::string& scopeId, const std::string& passageId,
                                      const std::string& passageHash,
                                      const std::string& fingerprint,
                                      const std::string& status, const std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || passageHash.empty() || fingerprint.empty()) return false;
    Stmt st;
    std::string sqlError;
    if (!prepare(db_, "INSERT INTO memory_claim_extraction(id,scope_id,passage_id,passage_hash,"
                       "extractor_fingerprint,status,error,created_at) VALUES(?,?,?,?,?,?,?,?) "
                       "ON CONFLICT(scope_id,passage_hash,extractor_fingerprint) DO UPDATE SET "
                       "status=excluded.status,error=excluded.error", st, sqlError)) {
        lastError_ = sqlError;
        return false;
    }
    bindText(st.stmt, 1, newId("cex"));
    bindText(st.stmt, 2, scopeId);
    bindText(st.stmt, 3, passageId);
    bindText(st.stmt, 4, passageHash);
    bindText(st.stmt, 5, fingerprint);
    bindText(st.stmt, 6, status);
    bindText(st.stmt, 7, error);
    bindText(st.stmt, 8, nowIso());
    return stepRetry(st.stmt) == SQLITE_DONE;
}

std::string MemoryStore::ensureLens(const std::string& scopeId, const std::string& name,
                                    const std::string& description) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || name.empty()) return "";
    const std::string id = "lens_" + hashHex(scopeId + "|" + name).substr(0, 24);
    Stmt existing;
    std::string error;
    if (!prepare(db_, "INSERT INTO memory_lens(id,scope_id,name,description,version,created_at,updated_at) "
                       "VALUES(?,?,?,?,1,?,?) ON CONFLICT(scope_id,name) DO UPDATE SET "
                       "description=CASE WHEN excluded.description<>'' THEN excluded.description "
                       "ELSE memory_lens.description END,"
                       "updated_at=excluded.updated_at,"
                       "version=CASE WHEN memory_lens.description<>'' AND excluded.description<>'' "
                       "AND memory_lens.description<>excluded.description THEN memory_lens.version+1 "
                       "ELSE memory_lens.version END", existing, error)) {
        lastError_ = error;
        return "";
    }
    const std::string now = nowIso();
    bindText(existing.stmt, 1, id);
    bindText(existing.stmt, 2, scopeId);
    bindText(existing.stmt, 3, name);
    bindText(existing.stmt, 4, description);
    bindText(existing.stmt, 5, now);
    bindText(existing.stmt, 6, now);
    if (stepRetry(existing.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    Stmt find;
    if (prepare(db_, "SELECT id FROM memory_lens WHERE scope_id=? AND name=?", find, error)) {
        bindText(find.stmt, 1, scopeId);
        bindText(find.stmt, 2, name);
        if (stepRetry(find.stmt) == SQLITE_ROW) return columnText(find.stmt, 0);
    }
    return id;
}

bool MemoryStore::renameLens(const std::string& scopeId, const std::string& lensId,
                             const std::string& newName, const std::string& description) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || lensId.empty() || newName.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_lens SET name=?,description=?,version=version+1,updated_at=? "
                       "WHERE scope_id=? AND id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, newName);
    bindText(st.stmt, 2, description);
    bindText(st.stmt, 3, nowIso());
    bindText(st.stmt, 4, scopeId);
    bindText(st.stmt, 5, lensId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return sqlite3_changes(db_) > 0;
}

std::vector<MemoryLensRecord> MemoryStore::listLenses(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryLensRecord> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,scope_id,name,description,version FROM memory_lens "
                       "WHERE scope_id=? ORDER BY name", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1), columnText(st.stmt, 2),
                       columnText(st.stmt, 3), columnInt(st.stmt, 4)});
    }
    return out;
}

bool MemoryStore::linkEdgeLens(const std::string& edgeId, const std::string& lensId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || edgeId.empty() || lensId.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, edgeId);
    bindText(st.stmt, 2, lensId);
    return stepRetry(st.stmt) == SQLITE_DONE;
}

std::vector<std::string> MemoryStore::edgeLensNames(const std::string& edgeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT l.name FROM memory_edge_lens el JOIN memory_lens l ON l.id=el.lens_id "
                       "WHERE el.edge_id=? ORDER BY l.name", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, edgeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    return out;
}

std::vector<std::string> MemoryStore::edgeIdsForLenses(
    const std::string& scopeId, const std::vector<std::string>& lensNames) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_ || lensNames.empty()) return out;
    std::string sql = "SELECT DISTINCT el.edge_id FROM memory_edge_lens el "
                      "JOIN memory_lens l ON l.id=el.lens_id "
                      "JOIN memory_edge e ON e.id=el.edge_id "
                      "WHERE e.scope_id=? AND e.status='active' AND l.name IN (";
    for (size_t i = 0; i < lensNames.size(); ++i) {
        if (i > 0) sql += ",";
        sql += "?";
    }
    sql += ")";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql.c_str(), st, error)) return out;
    bindText(st.stmt, 1, scopeId);
    for (size_t i = 0; i < lensNames.size(); ++i) {
        bindText(st.stmt, static_cast<int>(i + 2), lensNames[i]);
    }
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    return out;
}

size_t MemoryStore::backfillLenses(const std::string& scopeId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) return 0;
    size_t attached = 0;
    for (const auto& edge : listEdges(scopeId, "", 10000)) {
        if (edge.status != "active") continue;
        if (!edgeLensNames(edge.id).empty()) continue;
        auto metadata = taskforge::jsonutil::tryParse(edge.metadata);
        const taskforge::Json meta = metadata && metadata->is_object()
            ? *metadata : taskforge::Json::object();
        std::string name = meta.value("lens", meta.value("domain", edge.relation));
        if (name.empty()) name = edge.relation.empty() ? "default" : edge.relation;
        const std::string lensId = ensureLens(scopeId, name);
        if (!lensId.empty() && linkEdgeLens(edge.id, lensId)) ++attached;
    }
    return attached;
}

std::string MemoryStore::upsertFacet(const MemoryFacetRecord& facet, bool* changed) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (changed) *changed = false;
    if (!db_ || facet.scopeId.empty() || facet.entityId.empty() || facet.lensId.empty()) return "";
    const std::string id = "fct_" + hashHex(facet.scopeId + "|" + facet.entityId + "|" +
                                             facet.lensId).substr(0, 24);
    Stmt existing;
    std::string error;
    long long revision = 1;
    bool same = false;
    if (prepare(db_, "SELECT revision,claims_hash FROM memory_facet WHERE id=?", existing, error)) {
        bindText(existing.stmt, 1, id);
        if (stepRetry(existing.stmt) == SQLITE_ROW) {
            revision = columnInt(existing.stmt, 0);
            same = columnText(existing.stmt, 1) == facet.claimsHash;
            if (same) return id;
            ++revision;
        }
    }
    Stmt st;
    const std::string now = nowIso();
    if (!prepare(db_, "INSERT INTO memory_facet(id,scope_id,entity_id,lens_id,revision,description,"
                       "supporting_claims_json,claims_hash,status,embedding_fingerprint,created_at,updated_at) "
                       "VALUES(?,?,?,?,?,?,?,?,'active',?,?,?) "
                       "ON CONFLICT(id) DO UPDATE SET revision=excluded.revision,"
                       "description=excluded.description,supporting_claims_json=excluded.supporting_claims_json,"
                       "claims_hash=excluded.claims_hash,status='active',"
                       "embedding_fingerprint=excluded.embedding_fingerprint,updated_at=excluded.updated_at",
                     st, error)) {
        lastError_ = error;
        return "";
    }
    bindText(st.stmt, 1, id);
    bindText(st.stmt, 2, facet.scopeId);
    bindText(st.stmt, 3, facet.entityId);
    bindText(st.stmt, 4, facet.lensId);
    sqlite3_bind_int64(st.stmt, 5, revision);
    bindText(st.stmt, 6, facet.description);
    bindText(st.stmt, 7, facet.supportingClaimsJson);
    bindText(st.stmt, 8, facet.claimsHash);
    bindText(st.stmt, 9, facet.embeddingFingerprint);
    bindText(st.stmt, 10, now);
    bindText(st.stmt, 11, now);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    const auto claims = parseJsonOrNull(facet.supportingClaimsJson);
    if (claims.is_array()) {
        for (const auto& claim : claims) {
            if (claim.is_string()) recordLineage(facet.scopeId, "facet", id, "edge", claim.str());
        }
    }
    if (changed) *changed = true;
    return id;
}

std::vector<MemoryFacetRecord> MemoryStore::listFacets(const std::string& scopeId,
                                                       const std::string& entityId,
                                                       const std::string& lensId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryFacetRecord> out;
    if (!db_) return out;
    std::string sql = "SELECT id,scope_id,entity_id,lens_id,revision,description,"
                      "supporting_claims_json,claims_hash,status,embedding_fingerprint "
                      "FROM memory_facet WHERE scope_id=?";
    if (!entityId.empty()) sql += " AND entity_id=?";
    if (!lensId.empty()) sql += " AND lens_id=?";
    sql += " ORDER BY entity_id,lens_id";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql.c_str(), st, error)) return out;
    bindText(st.stmt, 1, scopeId);
    int index = 2;
    if (!entityId.empty()) bindText(st.stmt, index++, entityId);
    if (!lensId.empty()) bindText(st.stmt, index++, lensId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        out.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1), columnText(st.stmt, 2),
                       columnText(st.stmt, 3), columnInt(st.stmt, 4), columnText(st.stmt, 5),
                       columnText(st.stmt, 6), columnText(st.stmt, 7), columnText(st.stmt, 8),
                       columnText(st.stmt, 9)});
    }
    return out;
}

taskforge::Json MemoryStore::facetSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"lenses", 0LL}, {"facets", 0LL}, {"edgesWithLens", 0LL},
                        {"edgesWithoutLens", 0LL}};
    if (!db_) return out;
    auto scalar = [&](const char* sql) -> long long {
        Stmt st;
        std::string error;
        if (!prepare(db_, sql, st, error)) return 0;
        bindText(st.stmt, 1, scopeId);
        return stepRetry(st.stmt) == SQLITE_ROW ? columnInt(st.stmt, 0) : 0;
    };
    out["lenses"] = scalar("SELECT COUNT(*) FROM memory_lens WHERE scope_id=?");
    out["facets"] = scalar("SELECT COUNT(*) FROM memory_facet WHERE scope_id=? AND status='active'");
    out["edgesWithLens"] = scalar(
        "SELECT COUNT(DISTINCT e.id) FROM memory_edge e JOIN memory_edge_lens el ON el.edge_id=e.id "
        "WHERE e.scope_id=? AND e.status='active'");
    out["edgesWithoutLens"] = scalar(
        "SELECT COUNT(*) FROM memory_edge e WHERE e.scope_id=? AND e.status='active' "
        "AND NOT EXISTS (SELECT 1 FROM memory_edge_lens el WHERE el.edge_id=e.id)");
    return out;
}

std::vector<MemoryTraceRecord> MemoryStore::listTraces(const std::string& scopeId,
                                                       size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryTraceRecord> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT trace_id,scope_id,run_id,node_id,item_id,task_type,"
                       "intent_template_hash,input_hash,knowledge_version,model,tier,outcome,"
                       "anchors_json,steps_json,delivered_json,used_json,truncated_steps,created_at "
                       "FROM memory_trace WHERE scope_id=? ORDER BY created_at,rowid LIMIT ?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int(st.stmt, 2, static_cast<int>(std::max<size_t>(1, limit)));
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryTraceRecord trace;
        trace.traceId = columnText(st.stmt, 0);
        trace.scopeId = columnText(st.stmt, 1);
        trace.runId = columnText(st.stmt, 2);
        trace.nodeId = columnText(st.stmt, 3);
        trace.itemId = columnText(st.stmt, 4);
        trace.taskType = columnText(st.stmt, 5);
        trace.intentTemplateHash = columnText(st.stmt, 6);
        trace.inputHash = columnText(st.stmt, 7);
        trace.knowledgeVersion = columnInt(st.stmt, 8);
        trace.model = columnText(st.stmt, 9);
        trace.tier = columnText(st.stmt, 10);
        trace.outcome = columnText(st.stmt, 11);
        auto array = [&](int index) {
            taskforge::Json value = parseJsonOrNull(columnText(st.stmt, index));
            return value.is_array() ? value : taskforge::Json::array();
        };
        trace.anchors = array(12);
        trace.steps = array(13);
        trace.delivered = array(14);
        trace.used = array(15);
        trace.createdAt = columnText(st.stmt, 17);
        out.push_back(std::move(trace));
    }
    return out;
}

std::string MemoryStore::upsertPath(const std::string& scopeId, const std::string& taskType,
                                    const std::string& signature,
                                    const std::vector<std::string>& claimIds,
                                    const std::string& intentSample,
                                    const std::string& accessJson, bool* created) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (created) *created = false;
    if (!db_ || scopeId.empty() || signature.empty()) return "";
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return "";
    }
    const std::string schemaId = "psch_" + hashHex(scopeId + "|" + signature).substr(0, 24);
    const std::string pathId = "path_" + hashHex(scopeId + "|" + taskType + "|" + signature).substr(0, 24);
    const std::string now = nowIso();
    Stmt schema;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_path_schema(id,scope_id,signature,created_at) "
                       "VALUES(?,?,?,?)", schema, error)) {
        lastError_ = error;
        return "";
    }
    bindText(schema.stmt, 1, schemaId);
    bindText(schema.stmt, 2, scopeId);
    bindText(schema.stmt, 3, signature);
    bindText(schema.stmt, 4, now);
    if (stepRetry(schema.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    Stmt existing;
    if (prepare(db_, "SELECT 1 FROM memory_path WHERE id=?", existing, error)) {
        bindText(existing.stmt, 1, pathId);
        if (stepRetry(existing.stmt) == SQLITE_ROW) {
            Stmt update;
            if (prepare(db_, "UPDATE memory_path SET updated_at=? WHERE id=?", update, error)) {
                bindText(update.stmt, 1, now);
                bindText(update.stmt, 2, pathId);
                stepRetry(update.stmt);
            }
            if (!txn.commit(&txnError)) {
                lastError_ = txnError;
                return "";
            }
            return pathId;
        }
    }
    Stmt path;
    if (!prepare(db_, "INSERT INTO memory_path(id,scope_id,revision,schema_id,task_type,state,"
                       "intent_sample,access_json,created_at,updated_at) "
                       "VALUES(?,?,1,?,?,'candidate',?,?,?,?)", path, error)) {
        lastError_ = error;
        return "";
    }
    bindText(path.stmt, 1, pathId);
    bindText(path.stmt, 2, scopeId);
    bindText(path.stmt, 3, schemaId);
    bindText(path.stmt, 4, taskType);
    bindText(path.stmt, 5, intentSample);
    bindText(path.stmt, 6, accessJson.empty() ? "{}" : accessJson);
    bindText(path.stmt, 7, now);
    bindText(path.stmt, 8, now);
    if (stepRetry(path.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return "";
    }
    Stmt stats;
    if (prepare(db_, "INSERT OR IGNORE INTO memory_path_stats(path_id) VALUES(?)", stats, error)) {
        bindText(stats.stmt, 1, pathId);
        stepRetry(stats.stmt);
    }
    Stmt clearClaims;
    if (prepare(db_, "DELETE FROM memory_path_claim WHERE path_id=?", clearClaims, error)) {
        bindText(clearClaims.stmt, 1, pathId);
        stepRetry(clearClaims.stmt);
    }
    Stmt insertClaim;
    Stmt insertReverse;
    if (prepare(db_, "INSERT OR REPLACE INTO memory_path_claim(path_id,ordinal,claim_id,claim_revision) "
                       "VALUES(?,?,?,'')", insertClaim, error) &&
        prepare(db_, "INSERT OR IGNORE INTO memory_claim_path(claim_id,path_id) VALUES(?,?)",
                insertReverse, error)) {
        for (size_t i = 0; i < claimIds.size(); ++i) {
            sqlite3_reset(insertClaim.stmt);
            sqlite3_clear_bindings(insertClaim.stmt);
            bindText(insertClaim.stmt, 1, pathId);
            sqlite3_bind_int64(insertClaim.stmt, 2, static_cast<long long>(i));
            bindText(insertClaim.stmt, 3, claimIds[i]);
            stepRetry(insertClaim.stmt);
            sqlite3_reset(insertReverse.stmt);
            sqlite3_clear_bindings(insertReverse.stmt);
            bindText(insertReverse.stmt, 1, claimIds[i]);
            bindText(insertReverse.stmt, 2, pathId);
            stepRetry(insertReverse.stmt);
        }
    }
    if (created) *created = true;
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        if (created) *created = false;
        return "";
    }
    return pathId;
}

bool MemoryStore::recordPathEvent(const std::string& pathId, const std::string& traceId,
                                  const std::string& inputHash, bool success, long long tau,
                                  long long minDistinctInputs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || pathId.empty() || traceId.empty()) return false;
    std::string txnError;
    WriteTxn txn(*this, txnError);
    if (!txn.ok()) {
        lastError_ = txnError;
        return false;
    }
    Stmt seen;
    std::string error;
    if (!prepare(db_, "INSERT OR IGNORE INTO memory_path_event(trace_id,path_id,input_hash,created_at) "
                       "VALUES(?,?,?,?)", seen, error)) {
        lastError_ = error;
        return false;
    }
    bindText(seen.stmt, 1, traceId);
    bindText(seen.stmt, 2, pathId);
    bindText(seen.stmt, 3, inputHash);
    bindText(seen.stmt, 4, nowIso());
    if (stepRetry(seen.stmt) != SQLITE_DONE || sqlite3_changes(db_) == 0) return false;

    if (!inputHash.empty()) {
        Stmt input;
        if (prepare(db_, "INSERT OR IGNORE INTO memory_path_input(path_id,input_hash) VALUES(?,?)",
                     input, error)) {
            bindText(input.stmt, 1, pathId);
            bindText(input.stmt, 2, inputHash);
            stepRetry(input.stmt);
        }
    }
    long long distinctInputs = 0;
    Stmt count;
    if (prepare(db_, "SELECT COUNT(*) FROM memory_path_input WHERE path_id=?", count, error)) {
        bindText(count.stmt, 1, pathId);
        if (stepRetry(count.stmt) == SQLITE_ROW) distinctInputs = columnInt(count.stmt, 0);
    }
    Stmt update;
    if (!prepare(db_, "UPDATE memory_path_stats SET success=success+?,failure=failure+?,"
                       "uses=uses+1,last_tau=MAX(last_tau,?),distinct_inputs=? WHERE path_id=?",
                 update, error)) {
        lastError_ = error;
        return false;
    }
    sqlite3_bind_int64(update.stmt, 1, success ? 1 : 0);
    sqlite3_bind_int64(update.stmt, 2, success ? 0 : 1);
    sqlite3_bind_int64(update.stmt, 3, tau);
    sqlite3_bind_int64(update.stmt, 4, distinctInputs);
    bindText(update.stmt, 5, pathId);
    if (stepRetry(update.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    if (distinctInputs >= std::max<long long>(1, minDistinctInputs)) {
        Stmt activate;
        if (prepare(db_, "UPDATE memory_path SET state='active',updated_at=? "
                           "WHERE id=? AND state IN ('candidate','suspended')", activate, error)) {
            bindText(activate.stmt, 1, nowIso());
            bindText(activate.stmt, 2, pathId);
            stepRetry(activate.stmt);
        }
    }
    if (!txn.commit(&txnError)) {
        lastError_ = txnError;
        return false;
    }
    return true;
}

std::vector<MemoryPathRecord> MemoryStore::listPaths(const std::string& scopeId,
                                                     const std::string& state) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<MemoryPathRecord> out;
    if (!db_) return out;
    std::string sql = "SELECT p.id,p.scope_id,p.revision,p.schema_id,p.task_type,p.state,"
                      "p.intent_sample,p.access_json,COALESCE(s.success,0),COALESCE(s.failure,0),"
                      "COALESCE(s.last_tau,0),COALESCE(s.uses,0),COALESCE(s.distinct_inputs,0) "
                      "FROM memory_path p LEFT JOIN memory_path_stats s ON s.path_id=p.id "
                      "WHERE p.scope_id=?";
    if (!state.empty()) sql += " AND p.state=?";
    sql += " ORDER BY p.task_type,p.id";
    Stmt st;
    std::string error;
    if (!prepare(db_, sql.c_str(), st, error)) return out;
    bindText(st.stmt, 1, scopeId);
    if (!state.empty()) bindText(st.stmt, 2, state);
    std::vector<MemoryPathRecord> rows;
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        MemoryPathRecord path;
        path.id = columnText(st.stmt, 0);
        path.scopeId = columnText(st.stmt, 1);
        path.revision = columnInt(st.stmt, 2);
        path.schemaId = columnText(st.stmt, 3);
        path.taskType = columnText(st.stmt, 4);
        path.state = columnText(st.stmt, 5);
        path.intentSample = columnText(st.stmt, 6);
        path.accessJson = columnText(st.stmt, 7);
        path.success = columnInt(st.stmt, 8);
        path.failure = columnInt(st.stmt, 9);
        path.lastTau = columnInt(st.stmt, 10);
        path.uses = columnInt(st.stmt, 11);
        path.distinctInputs = columnInt(st.stmt, 12);
        path.utility = (static_cast<double>(path.success) + 1.0) /
                       static_cast<double>(path.success + path.failure + 2);
        rows.push_back(std::move(path));
    }
    for (auto& path : rows) {
        Stmt claims;
        if (prepare(db_, "SELECT claim_id FROM memory_path_claim WHERE path_id=? ORDER BY ordinal",
                     claims, error)) {
            bindText(claims.stmt, 1, path.id);
            while (stepRetry(claims.stmt) == SQLITE_ROW) path.claimIds.push_back(columnText(claims.stmt, 0));
        }
        out.push_back(std::move(path));
    }
    return out;
}

taskforge::Json MemoryStore::getPath(const std::string& scopeId,
                                     const std::string& pathId) const {
    const auto paths = listPaths(scopeId);
    for (const auto& path : paths) {
        if (path.id != pathId) continue;
        taskforge::Json claims = taskforge::Json::array();
        for (const auto& id : path.claimIds) claims.push_back(id);
        return taskforge::Json{{"id", path.id}, {"scopeId", path.scopeId},
                               {"revision", path.revision}, {"schemaId", path.schemaId},
                               {"taskType", path.taskType}, {"state", path.state},
                               {"intentSample", path.intentSample}, {"access", path.accessJson},
                               {"success", path.success}, {"failure", path.failure},
                               {"lastTau", path.lastTau}, {"uses", path.uses},
                               {"distinctInputs", path.distinctInputs},
                               {"utility", path.utility}, {"claims", claims}};
    }
    return taskforge::Json::object();
}

bool MemoryStore::setPathState(const std::string& scopeId, const std::string& pathId,
                               const std::string& state, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || pathId.empty() || state.empty()) return false;
    Stmt st;
    std::string error;
    if (!prepare(db_, "UPDATE memory_path SET state=?,updated_at=?,"
                       "access_json=json_set(COALESCE(access_json,'{}'),'$.stateReason',?) "
                       "WHERE id=? AND scope_id=?", st, error)) {
        lastError_ = error;
        return false;
    }
    bindText(st.stmt, 1, state);
    bindText(st.stmt, 2, nowIso());
    bindText(st.stmt, 3, reason);
    bindText(st.stmt, 4, pathId);
    bindText(st.stmt, 5, scopeId);
    if (stepRetry(st.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    const bool pathChanged = sqlite3_changes(db_) > 0;
    // O atalho e projecao do caminho: acompanha o estado sem virar evidencia.
    Stmt shortcut;
    if (prepare(db_, "UPDATE memory_edge SET status=?,valid_to=? "
                       "WHERE scope_id=? AND relation='via_caminho' "
                       "AND json_extract(metadata,'$.pathId')=?",
                 shortcut, error)) {
        bindText(shortcut.stmt, 1, state == "active" ? "active" : "superseded");
        bindNullableText(shortcut.stmt, 2, state == "active" ? std::string() : nowIso());
        bindText(shortcut.stmt, 3, scopeId);
        bindText(shortcut.stmt, 4, pathId);
        stepRetry(shortcut.stmt);
    }
    if (pathChanged && state != "active") {
        Stmt removal;
        std::string removalError;
        if (prepare(db_, "INSERT INTO memory_removal_log"
                           "(scope_id,item_kind,item_id,reason,created_at) VALUES(?,?,?,?,?)",
                     removal, removalError)) {
            bindText(removal.stmt, 1, scopeId);
            bindText(removal.stmt, 2, "path");
            bindText(removal.stmt, 3, pathId);
            bindText(removal.stmt, 4, reason.empty() ? state : reason);
            bindText(removal.stmt, 5, nowIso());
            stepRetry(removal.stmt);
        }
    }
    return pathChanged;
}

bool MemoryStore::bridgeProposalSeen(const std::string& scopeId,
                                     const std::string& pairKey) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || pairKey.empty()) return false;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT 1 FROM memory_proposal WHERE scope_id=? "
                       "AND kind='bridge_hypothesis' AND status<>'rejected' "
                       "AND json_extract(payload_json,'$.pairKey')=? LIMIT 1", st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, pairKey);
        return stepRetry(st.stmt) == SQLITE_ROW;
    }
    return false;
}

std::vector<std::string> MemoryStore::pathsForClaim(const std::string& claimId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_ || claimId.empty()) return out;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT path_id FROM memory_claim_path WHERE claim_id=?", st, error)) {
        bindText(st.stmt, 1, claimId);
        while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    }
    return out;
}

std::vector<std::string> MemoryStore::replacementClaimIds(const std::string& scopeId,
                                                         const std::string& claimId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_ || claimId.empty()) return out;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT e.id FROM memory_edge e "
                       "JOIN memory_edge old ON old.id=? "
                       "JOIN memory_node old_dst ON old_dst.id=old.dst_id "
                       "JOIN memory_node new_dst ON new_dst.id=e.dst_id "
                       "WHERE e.scope_id=? AND e.status='active' AND e.id<>old.id "
                       "AND e.src_id=old.src_id AND e.relation=old.relation "
                       "AND new_dst.kind=old_dst.kind "
                       "ORDER BY e.valid_from DESC,e.recorded_at DESC", st, error)) {
        bindText(st.stmt, 1, claimId);
        bindText(st.stmt, 2, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    }
    return out;
}

std::vector<std::string> MemoryStore::predicateClaimIds(const std::string& scopeId,
                                                        const std::string& claimId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_ || claimId.empty()) return out;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT e.id FROM memory_edge e "
                       "JOIN memory_edge old ON old.id=? "
                       "WHERE e.scope_id=? AND e.status='active' AND e.id<>old.id "
                       "AND e.src_id=old.src_id AND e.relation=old.relation "
                       "ORDER BY e.valid_from DESC,e.recorded_at DESC", st, error)) {
        bindText(st.stmt, 1, claimId);
        bindText(st.stmt, 2, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
    }
    return out;
}

bool MemoryStore::revalidatePathClaim(const std::string& scopeId, const std::string& pathId,
                                      const std::string& oldClaimId,
                                      const std::string& newClaimId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || pathId.empty() || oldClaimId.empty() || newClaimId.empty()) return false;
    Stmt update;
    std::string error;
    if (!prepare(db_, "UPDATE memory_path_claim SET claim_id=?,claim_revision=? "
                       "WHERE path_id=? AND claim_id=?",
                 update, error)) {
        lastError_ = error;
        return false;
    }
    bindText(update.stmt, 1, newClaimId);
    bindText(update.stmt, 2, newClaimId);
    bindText(update.stmt, 3, pathId);
    bindText(update.stmt, 4, oldClaimId);
    if (stepRetry(update.stmt) != SQLITE_DONE || sqlite3_changes(db_) == 0) return false;
    Stmt remove;
    if (prepare(db_, "DELETE FROM memory_claim_path WHERE claim_id=? AND path_id=?",
                 remove, error)) {
        bindText(remove.stmt, 1, oldClaimId);
        bindText(remove.stmt, 2, pathId);
        stepRetry(remove.stmt);
    }
    Stmt insert;
    if (prepare(db_, "INSERT OR IGNORE INTO memory_claim_path(claim_id,path_id) VALUES(?,?)",
                 insert, error)) {
        bindText(insert.stmt, 1, newClaimId);
        bindText(insert.stmt, 2, pathId);
        stepRetry(insert.stmt);
    }
    Stmt revision;
    if (prepare(db_, "UPDATE memory_path SET revision=revision+1,updated_at=? "
                       "WHERE id=? AND scope_id=?", revision, error)) {
        bindText(revision.stmt, 1, nowIso());
        bindText(revision.stmt, 2, pathId);
        bindText(revision.stmt, 3, scopeId);
        stepRetry(revision.stmt);
    }
    return true;
}

bool MemoryStore::syncPathShortcut(const std::string& scopeId, const std::string& pathId,
                                   bool active, const std::string& srcId,
                                   const std::string& dstId,
                                   const std::vector<std::string>& claimIds,
                                   double utility) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || scopeId.empty() || pathId.empty()) return false;
    Stmt existing;
    std::string error;
    std::string existingId;
    if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND relation='via_caminho' "
                       "AND json_extract(metadata,'$.pathId')=? LIMIT 1", existing, error)) {
        bindText(existing.stmt, 1, scopeId);
        bindText(existing.stmt, 2, pathId);
        if (stepRetry(existing.stmt) == SQLITE_ROW) existingId = columnText(existing.stmt, 0);
    }
    const std::string now = nowIso();
    taskforge::Json claimArray = taskforge::Json::array();
    for (const auto& id : claimIds) claimArray.push_back(id);
    if (!existingId.empty()) {
        Stmt update;
        if (!prepare(db_, "UPDATE memory_edge SET status=?,valid_to=?,metadata=? WHERE id=?",
                     update, error)) {
            lastError_ = error;
            return false;
        }
        bindText(update.stmt, 1, active ? "active" : "superseded");
        bindNullableText(update.stmt, 2, active ? std::string() : now);
        bindText(update.stmt, 3, taskforge::Json{{"pathId", pathId}, {"derived", true},
                                                 {"claimIds", claimArray},
                                                 {"utility", utility}}.dump());
        bindText(update.stmt, 4, existingId);
        if (stepRetry(update.stmt) != SQLITE_DONE) {
            lastError_ = sqlite3_errmsg(db_);
            return false;
        }
        return true;
    }
    if (!active || srcId.empty()) return true;
    const std::string id = "via_" + hashHex(scopeId + "|" + pathId).substr(0, 28);
    Stmt insert;
    if (!prepare(db_, "INSERT INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,"
                       "valid_from,valid_to,recorded_at,confidence,status,source_kind,source_ref,"
                       "edge_hash,metadata) VALUES(?,?,?,?,'via_caminho','{}',?,?,?,?,'active',"
                       "'path',?,?,?)", insert, error)) {
        lastError_ = error;
        return false;
    }
    bindText(insert.stmt, 1, id);
    bindText(insert.stmt, 2, scopeId);
    bindText(insert.stmt, 3, srcId);
    bindNullableText(insert.stmt, 4, dstId);
    bindText(insert.stmt, 5, now);
    bindNullableText(insert.stmt, 6, "");
    bindText(insert.stmt, 7, now);
    sqlite3_bind_double(insert.stmt, 8, std::max(0.0, std::min(1.0, utility)));
    bindText(insert.stmt, 9, pathId);
    bindText(insert.stmt, 10, hashHex(scopeId + "|via_caminho|" + pathId));
    bindText(insert.stmt, 11, taskforge::Json{{"pathId", pathId}, {"derived", true},
                                              {"claimIds", claimArray},
                                              {"utility", utility}}.dump());
    if (stepRetry(insert.stmt) != SQLITE_DONE) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

taskforge::Json MemoryStore::shortcutForPath(const std::string& scopeId,
                                             const std::string& pathId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || pathId.empty()) return taskforge::Json::object();
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT id,status,src_id,dst_id,metadata FROM memory_edge "
                       "WHERE scope_id=? AND relation='via_caminho' "
                       "AND json_extract(metadata,'$.pathId')=? LIMIT 1", st, error)) {
        bindText(st.stmt, 1, scopeId);
        bindText(st.stmt, 2, pathId);
        if (stepRetry(st.stmt) == SQLITE_ROW) {
            return taskforge::Json{{"id", columnText(st.stmt, 0)},
                                   {"status", columnText(st.stmt, 1)},
                                   {"srcId", columnText(st.stmt, 2)},
                                   {"dstId", columnText(st.stmt, 3)},
                                   {"metadata", columnText(st.stmt, 4)}};
        }
    }
    return taskforge::Json::object();
}

taskforge::Json MemoryStore::pathSummary(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out{{"paths", 0LL}, {"active", 0LL}, {"candidate", 0LL},
                        {"suspended", 0LL}, {"retired", 0LL}};
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (prepare(db_, "SELECT state,COUNT(*) FROM memory_path WHERE scope_id=? GROUP BY state", st, error)) {
        bindText(st.stmt, 1, scopeId);
        while (stepRetry(st.stmt) == SQLITE_ROW) {
            const std::string state = columnText(st.stmt, 0);
            const long long count = columnInt(st.stmt, 1);
            out[state] = count;
            out["paths"] = out.value("paths", 0LL) + count;
        }
    }
    return out;
}

std::vector<std::string> MemoryStore::approvedTraceIds(const std::string& scopeId,
                                                       size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    if (!db_) return out;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT DISTINCT t.trace_id FROM memory_trace t "
                       "JOIN memory_validation v ON v.scope_id=t.scope_id "
                       "AND v.run_id=t.run_id AND v.node_id=t.node_id AND v.ok=1 "
                       "WHERE t.scope_id=? ORDER BY t.created_at LIMIT ?", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    sqlite3_bind_int(st.stmt, 2, static_cast<int>(std::max<size_t>(1, limit)));
    while (stepRetry(st.stmt) == SQLITE_ROW) out.push_back(columnText(st.stmt, 0));
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
    WriteTxn txn(*this, error);
    if (!txn.ok()) return false;

    auto fail = [&](const std::string& message) {
        txn.rollback();
        error = message;
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
        "DELETE FROM memory_evidence WHERE scope_id=?",
        "DELETE FROM memory_working WHERE scope_id=?",
        "DELETE FROM memory_alias WHERE scope_id=?",
        "DELETE FROM memory_playbook WHERE scope_id=?",
        "DELETE FROM memory_facet WHERE scope_id=?",
        "DELETE FROM memory_edge_lens WHERE edge_id IN (SELECT id FROM memory_edge WHERE scope_id=?)",
        "DELETE FROM memory_lens WHERE scope_id=?",
        "DELETE FROM memory_claim_extraction WHERE scope_id=?",
        "DELETE FROM memory_claim_path WHERE path_id IN (SELECT id FROM memory_path WHERE scope_id=?)",
        "DELETE FROM memory_path_event WHERE path_id IN (SELECT id FROM memory_path WHERE scope_id=?)",
        "DELETE FROM memory_path_input WHERE path_id IN (SELECT id FROM memory_path WHERE scope_id=?)",
        "DELETE FROM memory_path_stats WHERE path_id IN (SELECT id FROM memory_path WHERE scope_id=?)",
        "DELETE FROM memory_path_claim WHERE path_id IN (SELECT id FROM memory_path WHERE scope_id=?)",
        "DELETE FROM memory_path WHERE scope_id=?",
        "DELETE FROM memory_path_schema WHERE scope_id=?",
        "DELETE FROM memory_passage_fts WHERE source_id IN (SELECT id FROM memory_source WHERE scope_id=?)",
        "DELETE FROM memory_passage WHERE scope_id=?",
        "DELETE FROM memory_source WHERE scope_id=?",
        "DELETE FROM memory_node_fts WHERE node_id IN (SELECT id FROM memory_node WHERE scope_id=?)",
        "DELETE FROM memory_edge WHERE scope_id=?",
        "DELETE FROM memory_node WHERE scope_id=?"
    };
    for (const char* sql : deletes) {
        Stmt st;
        if (!prepare(db_, sql, st, error)) return fail(error);
        bindText(st.stmt, 1, scopeId);
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
    if (!txn.commit(&commitError)) {
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
        item["evidenceCount"] = static_cast<long long>(evidenceCount(scopeId, "lesson", lesson.id));
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

size_t MemoryStore::repairOverlaps(const std::string& scopeId, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_) {
        error = "banco de memoria fechado";
        return 0;
    }
    WriteTxn txn(*this, error);
    if (!txn.ok()) return 0;
    struct EdgeView {
        std::string id;
        std::string src;
        std::string relation;
        std::string dst;
        std::string value;
        std::string from;
        std::string to;
        std::string contextKey;
        bool disputed = false;
    };
    std::vector<EdgeView> edges;
    Stmt st;
    if (!prepare(db_, "SELECT id,src_id,relation,COALESCE(dst_id,''),COALESCE(value_json,''),"
                       "COALESCE(valid_from,''),COALESCE(valid_to,''),COALESCE(context_key,''),"
                       "CASE WHEN metadata LIKE '%\"disputed\":true%' THEN 1 ELSE 0 END "
                       "FROM memory_edge WHERE scope_id=? AND status='active' ORDER BY id",
                 st, error)) {
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        edges.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                         columnText(st.stmt, 2), columnText(st.stmt, 3),
                         columnText(st.stmt, 4), columnText(st.stmt, 5),
                         columnText(st.stmt, 6), columnText(st.stmt, 7),
                         columnInt(st.stmt, 8) != 0});
    }
    auto lower = [](const std::string& value) {
        return value.empty() ? std::string("0000-00-00T00:00:00Z") : value;
    };
    auto upper = [](const std::string& value) {
        return value.empty() ? std::string("9999-12-31T23:59:59Z") : value;
    };
    size_t repaired = 0;
    for (size_t i = 0; i < edges.size(); ++i) {
        for (size_t j = i + 1; j < edges.size(); ++j) {
            const auto& a = edges[i];
            const auto& b = edges[j];
            if (a.disputed || b.disputed) continue;
            if (a.src != b.src || a.relation != b.relation || a.contextKey != b.contextKey) continue;
            if (!isSingleValuedPredicate(a.relation)) continue;
            if (!MemoryCatalog::instance().requiredContextKeys(a.relation).empty() &&
                a.contextKey.empty()) continue;
            if (a.dst == b.dst && a.value == b.value) continue;
            if (!(lower(a.from) < upper(b.to) && lower(b.from) < upper(a.to))) continue;
            for (const auto& edgeId : {a.id, b.id}) {
                Stmt mark;
                std::string markError;
                if (!prepare(db_, "UPDATE memory_edge SET metadata=json_set(metadata,'$.disputed',json('true')) "
                                   "WHERE scope_id=? AND id=?", mark, markError)) {
                    error = markError;
                    return repaired;
                }
                bindText(mark.stmt, 1, scopeId);
                bindText(mark.stmt, 2, edgeId);
                stepRetry(mark.stmt);
            }
            if (!a.dst.empty() && !b.dst.empty()) {
                taskforge::Json metadata{{"disputed", true},
                                         {"edgeA", a.id},
                                         {"edgeB", b.id},
                                         {"relation", a.relation}};
                const std::string conflict = recordEdge(
                    scopeId, a.dst, b.dst, "CONTRADICTS", taskforge::Json(), "", "",
                    0.5, "active", "repair", a.id + ":" + b.id, metadata);
                if (conflict.empty()) {
                    error = "falha ao criar CONTRADICTS de reparo";
                    return repaired;
                }
            }
            ++repaired;
        }
    }
    if (!txn.commit(&error)) return repaired;
    return repaired;
}

taskforge::Json MemoryStore::listMultiClosures(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json out = taskforge::Json::array();
    if (!db_) return out;
    MemoryCatalog& catalog = MemoryCatalog::instance();
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT id,relation,COALESCE(valid_to,''),COALESCE(superseded_by,''),"
                       "COALESCE(context_key,''),metadata FROM memory_edge "
                       "WHERE scope_id=? AND status<>'active' "
                       "AND json_extract(metadata,'$.invalidationReason') IN "
                       "('claim_conflict','single_valued_predicate') ORDER BY id", st, error)) {
        return out;
    }
    bindText(st.stmt, 1, scopeId);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        const std::string relation = columnText(st.stmt, 1);
        const std::string contextKey = columnText(st.stmt, 4);
        const auto* predicate = catalog.find(relation);
        const bool multi = !predicate || !predicate->claimable ||
                           predicate->cardinality == "multi";
        const bool structural = isStructuralPredicate(relation);
        const bool missingContext = predicate && predicate->claimable &&
            !predicate->requiredContextKeys.empty() && contextKey.empty();
        if (!multi && !structural && !missingContext) continue;
        auto metadata = taskforge::jsonutil::tryParse(columnText(st.stmt, 5));
        out.push_back(taskforge::Json{
            {"id", columnText(st.stmt, 0)},
            {"relation", relation},
            {"reason", metadata && metadata->is_object()
                ? metadata->value("invalidationReason", std::string()) : std::string()},
            {"validTo", columnText(st.stmt, 2)},
            {"supersededBy", columnText(st.stmt, 3)},
            {"contextKey", contextKey},
            {"newCardinality", predicate ? predicate->cardinality : "unknown"}});
    }
    return out;
}

size_t MemoryStore::claimSupportRootCount(const std::string& scopeId,
                                          const std::string& claimKey) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || claimKey.empty()) return 0;
    std::set<std::string> roots;
    Stmt st;
    std::string error;
    if (!prepare(db_, "SELECT metadata FROM memory_edge WHERE scope_id=? AND claim_key=? "
                       "AND status='active'", st, error)) {
        return 0;
    }
    bindText(st.stmt, 1, scopeId);
    bindText(st.stmt, 2, claimKey);
    while (stepRetry(st.stmt) == SQLITE_ROW) {
        auto metadata = taskforge::jsonutil::tryParse(columnText(st.stmt, 0));
        if (metadata && metadata->is_object()) {
            if (metadata->contains("supportRoots") && (*metadata)["supportRoots"].is_array()) {
                for (const auto& root : (*metadata)["supportRoots"]) {
                    if (root.is_string()) roots.insert(root.str());
                }
            }
            const std::string uri = metadata->value("sourceUri", std::string());
            if (!uri.empty()) roots.insert(MemorySources::normalizeUri(uri));
        }
    }
    return roots.size();
}

bool MemoryStore::reopenEdge(const std::string& scopeId, const std::string& edgeId,
                             const std::string& reason, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!db_ || edgeId.empty()) {
        error = "aresta invalida";
        return false;
    }
    WriteTxn txn(*this, error);
    if (!txn.ok()) return false;
    Stmt load;
    if (!prepare(db_, "SELECT relation,src_id,COALESCE(dst_id,''),COALESCE(context_key,''),status "
                       "FROM memory_edge WHERE scope_id=? AND id=?", load, error)) {
        return false;
    }
    bindText(load.stmt, 1, scopeId);
    bindText(load.stmt, 2, edgeId);
    if (stepRetry(load.stmt) != SQLITE_ROW) {
        error = "aresta nao encontrada";
        return false;
    }
    const std::string relation = columnText(load.stmt, 0);
    const std::string srcId = columnText(load.stmt, 1);
    const std::string dstId = columnText(load.stmt, 2);
    const std::string contextKey = columnText(load.stmt, 3);
    const std::string status = columnText(load.stmt, 4);
    if (status == "active") {
        error = "aresta ja esta ativa";
        return false;
    }
    if (isStructuralPredicate(relation)) {
        error = "predicado estrutural nao pode ser reaberto";
        return false;
    }
    if (isSingleValuedPredicate(relation) && !contextKey.empty()) {
        Stmt overlap;
        std::string overlapError;
        if (prepare(db_, "SELECT id FROM memory_edge WHERE scope_id=? AND src_id=? AND relation=? "
                           "AND context_key=? AND status='active' AND id<>? LIMIT 1",
                     overlap, overlapError)) {
            bindText(overlap.stmt, 1, scopeId);
            bindText(overlap.stmt, 2, srcId);
            bindText(overlap.stmt, 3, relation);
            bindText(overlap.stmt, 4, contextKey);
            bindText(overlap.stmt, 5, edgeId);
            if (stepRetry(overlap.stmt) == SQLITE_ROW) {
                error = "reabertura criaria sobreposicao funcional";
                return false;
            }
        }
    }
    Stmt update;
    if (!prepare(db_, "UPDATE memory_edge SET status='active',valid_to=NULL,superseded_by=NULL,"
                       "metadata=json_set(json_remove(metadata,'$.invalidationReason'),"
                       "'$.reopenedAt',?),dormant_at=NULL WHERE id=?",
                 update, error)) {
        return false;
    }
    bindText(update.stmt, 1, nowIso());
    bindText(update.stmt, 2, edgeId);
    if (stepRetry(update.stmt) != SQLITE_DONE) {
        error = sqlite3_errmsg(db_);
        return false;
    }
    audit("human", "reopen_edge", edgeId, "", reason);
    if (!txn.commit(&error)) return false;
    return true;
}

taskforge::Json MemoryStore::diagnostics(const std::string& scopeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    taskforge::Json checks = taskforge::Json::array();
    if (!db_) {
        return taskforge::Json{{"ok", false}, {"error", "banco de memoria fechado"},
                               {"checks", checks}};
    }
    auto addCheck = [&](const std::string& id, long long violations,
                        const taskforge::Json& details) {
        checks.push_back(taskforge::Json{
            {"id", id},
            {"status", violations == 0 ? "ok" : "violations"},
            {"violations", violations},
            {"details", details}});
    };

    // 1. Sobreposicao de valores ativos para predicados funcionais.
    {
        struct EdgeView {
            std::string id;
            std::string src;
            std::string relation;
            std::string dst;
            std::string value;
            std::string from;
            std::string to;
            std::string contextKey;
            bool disputed = false;
        };
        std::vector<EdgeView> edges;
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT id,src_id,relation,COALESCE(dst_id,''),COALESCE(value_json,''),"
                           "COALESCE(valid_from,''),COALESCE(valid_to,''),COALESCE(context_key,''),"
                           "CASE WHEN metadata LIKE '%\"disputed\":true%' THEN 1 ELSE 0 END "
                           "FROM memory_edge WHERE scope_id=? AND status='active' ORDER BY id",
                     st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                edges.push_back({columnText(st.stmt, 0), columnText(st.stmt, 1),
                                 columnText(st.stmt, 2), columnText(st.stmt, 3),
                                 columnText(st.stmt, 4), columnText(st.stmt, 5),
                                 columnText(st.stmt, 6), columnText(st.stmt, 7),
                                 columnInt(st.stmt, 8) != 0});
            }
        }
        std::map<std::string, std::vector<const EdgeView*>> groups;
        for (const auto& edge : edges) {
            if (!isSingleValuedPredicate(edge.relation)) continue;
            if (!MemoryCatalog::instance().requiredContextKeys(edge.relation).empty() &&
                edge.contextKey.empty()) continue;
            groups[edge.src + "\x1f" + edge.relation + "\x1f" + edge.contextKey].push_back(&edge);
        }
        auto lower = [](const std::string& value) {
            return value.empty() ? std::string("0000-00-00T00:00:00Z") : value;
        };
        auto upper = [](const std::string& value) {
            return value.empty() ? std::string("9999-12-31T23:59:59Z") : value;
        };
        long long violations = 0;
        taskforge::Json samples = taskforge::Json::array();
        for (const auto& group : groups) {
            const auto& list = group.second;
            for (size_t i = 0; i < list.size(); ++i) {
                for (size_t j = i + 1; j < list.size(); ++j) {
                    const auto& a = *list[i];
                    const auto& b = *list[j];
                    if (a.disputed || b.disputed) continue;
                    if (a.dst == b.dst && a.value == b.value) continue;
                    if (!(lower(a.from) < upper(b.to) && lower(b.from) < upper(a.to))) continue;
                    ++violations;
                    if (samples.size() < 20) {
                        samples.push_back(taskforge::Json{{"a", a.id}, {"b", b.id},
                                                          {"relation", a.relation}});
                    }
                }
            }
        }
        addCheck("functional_overlap", violations, taskforge::Json{{"samples", samples}});
    }

    // 2. edge_hash ativo duplicado.
    {
        Stmt st;
        std::string error;
        long long violations = 0;
        taskforge::Json samples = taskforge::Json::array();
        if (prepare(db_, "SELECT edge_hash,COUNT(*) FROM memory_edge "
                           "WHERE scope_id=? AND status='active' GROUP BY edge_hash HAVING COUNT(*)>1",
                     st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                ++violations;
                if (samples.size() < 20) {
                    samples.push_back(taskforge::Json{{"edgeHash", columnText(st.stmt, 0)},
                                                      {"count", columnInt(st.stmt, 1)}});
                }
            }
        }
        addCheck("duplicate_active_edge_hash", violations,
                 taskforge::Json{{"samples", samples}});
    }

    // 3. Fracao de afirmacoes com knownPredicate=false.
    {
        long long total = 0;
        long long unknown = 0;
        Stmt st;
        std::string error;
        if (prepare(db_, "SELECT COUNT(*),"
                           "COALESCE(SUM(CASE WHEN metadata LIKE '%\"knownPredicate\":false%' "
                           "THEN 1 ELSE 0 END),0) FROM memory_edge WHERE scope_id=?",
                     st, error)) {
            bindText(st.stmt, 1, scopeId);
            if (stepRetry(st.stmt) == SQLITE_ROW) {
                total = columnInt(st.stmt, 0);
                unknown = columnInt(st.stmt, 1);
            }
        }
        addCheck("unknown_predicate_fraction", unknown,
                 taskforge::Json{{"total", total}, {"unknown", unknown},
                                 {"fraction", total == 0 ? 0.0 : static_cast<double>(unknown) / total}});
    }

    // 4. Encerramentos por predicado e motivo.
    {
        Stmt st;
        std::string error;
        long long violations = 0;
        taskforge::Json groups = taskforge::Json::array();
        if (prepare(db_, "SELECT relation,"
                           "COALESCE(json_extract(metadata,'$.invalidationReason'),''),COUNT(*) "
                           "FROM memory_edge WHERE scope_id=? AND status<>'active' "
                           "AND json_extract(metadata,'$.invalidationReason') IS NOT NULL "
                           "GROUP BY relation,2 ORDER BY relation",
                     st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                ++violations;
                groups.push_back(taskforge::Json{{"relation", columnText(st.stmt, 0)},
                                                 {"reason", columnText(st.stmt, 1)},
                                                 {"count", columnInt(st.stmt, 2)}});
            }
        }
        addCheck("closure_reasons", 0,
                 taskforge::Json{{"groups", groups}, {"closures", violations}});
    }

    // 5. Derivados com entradas nao resolvidas (aproximacao por JSON).
    {
        long long violations = 0;
        taskforge::Json samples = taskforge::Json::array();
        auto edgeExists = [&](const std::string& id) {
            Stmt st;
            std::string error;
            if (!prepare(db_, "SELECT 1 FROM memory_edge WHERE scope_id=? AND id=? LIMIT 1", st, error)) {
                return false;
            }
            bindText(st.stmt, 1, scopeId);
            bindText(st.stmt, 2, id);
            return stepRetry(st.stmt) == SQLITE_ROW;
        };
        auto checkArray = [&](const std::string& kind, const std::string& owner,
                              const std::string& json) {
            auto parsed = taskforge::jsonutil::tryParse(json);
            if (!parsed || !parsed->is_array()) return;
            for (const auto& item : *parsed) {
                if (!item.is_string()) continue;
                if (edgeExists(item.str())) continue;
                ++violations;
                if (samples.size() < 20) {
                    samples.push_back(taskforge::Json{{"kind", kind}, {"owner", owner},
                                                      {"missing", item.str()}});
                }
            }
        };
        Stmt facets;
        std::string error;
        if (prepare(db_, "SELECT id,supporting_claims_json FROM memory_facet "
                           "WHERE scope_id=? AND status='active'", facets, error)) {
            bindText(facets.stmt, 1, scopeId);
            while (stepRetry(facets.stmt) == SQLITE_ROW) {
                checkArray("facet", columnText(facets.stmt, 0), columnText(facets.stmt, 1));
            }
        }
        Stmt playbooks;
        if (prepare(db_, "SELECT id,evidence_json FROM memory_playbook WHERE scope_id=?", playbooks, error)) {
            bindText(playbooks.stmt, 1, scopeId);
            while (stepRetry(playbooks.stmt) == SQLITE_ROW) {
                checkArray("playbook", columnText(playbooks.stmt, 0), columnText(playbooks.stmt, 1));
            }
        }
        Stmt evidence;
        if (prepare(db_, "SELECT target_kind,target_id FROM memory_evidence WHERE scope_id=?", evidence, error)) {
            bindText(evidence.stmt, 1, scopeId);
            while (stepRetry(evidence.stmt) == SQLITE_ROW) {
                const std::string kind = columnText(evidence.stmt, 0);
                const std::string id = columnText(evidence.stmt, 1);
                bool exists = false;
                Stmt probe;
                std::string probeError;
                std::string table;
                if (kind == "claim" || kind == "fact" || kind == "lesson" || kind == "edge") table = "memory_edge";
                else if (kind == "playbook") table = "memory_playbook";
                else if (kind == "episode") table = "memory_episode";
                else if (kind == "entity") table = "memory_node";
                else if (kind == "proposal") table = "memory_proposal";
                if (!table.empty() &&
                    prepare(db_, "SELECT 1 FROM " + table + " WHERE scope_id=? AND id=? LIMIT 1", probe, probeError)) {
                    bindText(probe.stmt, 1, scopeId);
                    bindText(probe.stmt, 2, id);
                    exists = stepRetry(probe.stmt) == SQLITE_ROW;
                }
                if (!exists) {
                    ++violations;
                    if (samples.size() < 20) {
                        samples.push_back(taskforge::Json{{"kind", "evidence"},
                                                          {"owner", kind + ":" + id},
                                                          {"missing", id}});
                    }
                }
            }
        }
        addCheck("unresolved_derived_inputs", violations, taskforge::Json{{"samples", samples}});
    }

    // 6. Itens entregues e nao citados que se repetem em tarefas rejeitadas.
    {
        Stmt st;
        std::string error;
        long long violations = 0;
        taskforge::Json samples = taskforge::Json::array();
        if (prepare(db_, "SELECT memory_id,kind,COUNT(*) FROM memory_hit "
                           "WHERE scope_id=? AND used=0 AND outcome='failure' "
                           "GROUP BY memory_id,kind HAVING COUNT(*)>1 ORDER BY memory_id",
                     st, error)) {
            bindText(st.stmt, 1, scopeId);
            while (stepRetry(st.stmt) == SQLITE_ROW) {
                ++violations;
                if (samples.size() < 20) {
                    samples.push_back(taskforge::Json{{"memoryId", columnText(st.stmt, 0)},
                                                      {"kind", columnText(st.stmt, 1)},
                                                      {"count", columnInt(st.stmt, 2)}});
                }
            }
        }
        addCheck("uncited_repeated_on_failure", violations,
                 taskforge::Json{{"samples", samples}});
    }

    bool ok = true;
    for (const auto& check : checks) {
        if (check.value("violations", 0LL) > 0) ok = false;
    }
    return taskforge::Json{{"ok", ok}, {"checks", checks}};
}

} // namespace memory
} // namespace taskforge
