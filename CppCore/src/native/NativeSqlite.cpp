// arestrix.sqlite: banco de dados SQLite embutido (amalgamation em third_party/sqlite), sem servidor e sem processo.
//
// Cada chamada abre o arquivo, executa UM comando e fecha: nao ha estado escondido entre chamadas. Um mutex global serializa
// os acessos do proprio processo (o SQLite nunca precisa esperar uma trava, entao nao ha pausas nem repeticoes).
//
// Seguranca:
//   * o banco e um arquivo DENTRO do projeto (.db, .sqlite ou .sqlite3), nunca em .git nem em pastas protegidas;
//   * valores entram por parametros (?), nunca por concatenacao de texto;
//   * um "autorizador" do SQLite recusa ATTACH, DETACH, PRAGMA, tabelas virtuais, gatilhos, transacoes e views temporarias;
//   * a carga de extensoes nao existe no binario (SQLITE_OMIT_LOAD_EXTENSION); sql_query abre o arquivo SOMENTE LEITURA;
//   * limite de tempo (5 s), de tamanho do banco (200 MB), do comando (100 KB) e de linhas devolvidas (1000).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>

#include "native/NativeTools.hpp"
#include "sqlite3.h"

namespace fs = std::filesystem;

namespace taskforge {
namespace native {

namespace {

using rt::ToolResult;

ToolResult fail(const std::string& m) { return {m, true}; }
ToolResult ok(const std::string& m) { return {m, false}; }
std::string str(const Json& a, const char* key, const std::string& def = "") {
    return a.contains(key) && a[key].is_string() ? a[key].str() : def;
}

constexpr const char* kDefaultDb = "data/arestrix.db";
constexpr size_t kMaxSqlBytes = 100 * 1024;
constexpr int kMaxRowsHard = 1000;
constexpr int kDefaultRows = 100;
constexpr auto kMaxQueryTime = std::chrono::seconds(5);

std::mutex g_dbMutex;

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Resolve e valida o arquivo do banco. Devolve "" e preenche `error` em caso de recusa.
fs::path resolveDb(const Context& ctx, const Json& a, std::string& error) {
    const std::string rel = str(a, "database", kDefaultDb);
    auto p = rt::resolveInWorkspace(ctx.workspaceRoot, rel);
    if (!p) { error = "Caminho do banco fora do projeto ou invalido: " + rel; return {}; }
    const std::string ext = lower(p->extension().string());
    if (ext != ".db" && ext != ".sqlite" && ext != ".sqlite3") {
        error = "O banco deve ter a extensao .db, .sqlite ou .sqlite3: " + rel;
        return {};
    }
    std::error_code ec;
    fs::path relative = fs::relative(*p, ctx.workspaceRoot, ec);
    if (!ec && !relative.empty() && relative.begin()->string() == ".git") { error = "A pasta .git nao e acessivel."; return {}; }
    if (rt::isProtectedPath(ctx.workspaceRoot, *p)) { error = "Caminho protegido (pipelines, configuracao e .git): " + rel; return {}; }
    return *p;
}

struct Deadline {
    std::chrono::steady_clock::time_point end;
};

int onProgress(void* user) {
    return std::chrono::steady_clock::now() > static_cast<Deadline*>(user)->end ? 1 : 0;   // 1 = interrompe o comando
}

struct Db {
    sqlite3* h = nullptr;
    Deadline deadline{};
    ~Db() { if (h) sqlite3_close(h); }
};

// O que o comando do usuario pode fazer. Leitura: so consultar. Escrita: alem disso, alterar dados e o esquema.
enum class Mode { Read, Write };

int authorize(void* user, int action, const char*, const char*, const char*, const char*) {
    const Mode mode = *static_cast<Mode*>(user);
    switch (action) {
        case SQLITE_SELECT:
        case SQLITE_READ:
        case SQLITE_FUNCTION:
        case SQLITE_RECURSIVE:
            return SQLITE_OK;
        case SQLITE_INSERT:
        case SQLITE_UPDATE:
        case SQLITE_DELETE:
        case SQLITE_CREATE_TABLE:
        case SQLITE_CREATE_INDEX:
        case SQLITE_CREATE_VIEW:
        case SQLITE_ALTER_TABLE:
        case SQLITE_DROP_TABLE:
        case SQLITE_DROP_INDEX:
        case SQLITE_DROP_VIEW:
            return mode == Mode::Write ? SQLITE_OK : SQLITE_DENY;
        default:   // ATTACH, DETACH, PRAGMA, tabelas virtuais, gatilhos, transacoes, objetos temporarios...
            return SQLITE_DENY;
    }
}

std::string lastError(sqlite3* h) { return sqlite3_errmsg(h); }

// Abre o banco com as protecoes. Em modo leitura abre somente leitura e nao cria o arquivo.
bool openDb(Db& db, const fs::path& path, Mode mode, std::string& error) {
    if (mode == Mode::Write) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    } else {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) { error = "O banco ainda nao existe: " + path.filename().string(); return false; }
    }
    const int flags = (mode == Mode::Write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY) | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(path.string().c_str(), &db.h, flags, nullptr) != SQLITE_OK) {
        error = "Nao foi possivel abrir o banco: " + (db.h ? lastError(db.h) : std::string("sem memoria"));
        return false;
    }
    sqlite3_busy_timeout(db.h, 0);
    sqlite3_db_config(db.h, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db.h, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 0, nullptr);
    sqlite3_db_config(db.h, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_limit(db.h, SQLITE_LIMIT_SQL_LENGTH, (int)kMaxSqlBytes);
    sqlite3_limit(db.h, SQLITE_LIMIT_LENGTH, 5 * 1000 * 1000);
    sqlite3_limit(db.h, SQLITE_LIMIT_ATTACHED, 0);
    if (mode == Mode::Write) sqlite3_exec(db.h, "PRAGMA max_page_count=51200;", nullptr, nullptr, nullptr);   // ~200 MB
    db.deadline.end = std::chrono::steady_clock::now() + kMaxQueryTime;
    sqlite3_progress_handler(db.h, 1000, onProgress, &db.deadline);
    return true;
}

bool bindParams(sqlite3_stmt* st, const Json& params, std::string& error) {
    if (params.is_null()) return true;
    if (!params.is_array()) { error = "'params' deve ser uma lista de valores."; return false; }
    const int expected = sqlite3_bind_parameter_count(st);
    if ((int)params.size() != expected) {
        error = "O comando tem " + std::to_string(expected) + " parametro(s) (?), mas 'params' trouxe " + std::to_string(params.size()) + ".";
        return false;
    }
    for (size_t i = 0; i < params.size(); ++i) {
        const Json& v = params[i];
        const int idx = (int)i + 1;
        int rc = SQLITE_OK;
        if (v.is_null()) rc = sqlite3_bind_null(st, idx);
        else if (v.is_boolean()) rc = sqlite3_bind_int(st, idx, v.get<bool>() ? 1 : 0);
        else if (v.is_number_integer()) rc = sqlite3_bind_int64(st, idx, v.get<int64_t>());
        else if (v.is_number()) rc = sqlite3_bind_double(st, idx, v.get<double>());
        else {
            const std::string text = v.is_string() ? v.str() : v.dump();
            rc = sqlite3_bind_text(st, idx, text.data(), (int)text.size(), SQLITE_TRANSIENT);
        }
        if (rc != SQLITE_OK) { error = "Parametro invalido na posicao " + std::to_string(idx); return false; }
    }
    return true;
}

Json columnValue(sqlite3_stmt* st, int i) {
    switch (sqlite3_column_type(st, i)) {
        case SQLITE_INTEGER: return Json((int64_t)sqlite3_column_int64(st, i));
        case SQLITE_FLOAT: return Json(sqlite3_column_double(st, i));
        case SQLITE_TEXT: return Json(std::string((const char*)sqlite3_column_text(st, i), (size_t)sqlite3_column_bytes(st, i)));
        case SQLITE_BLOB: return Json("[blob de " + std::to_string(sqlite3_column_bytes(st, i)) + " bytes]");
        default: return Json();
    }
}

// Prepara exatamente UM comando (varios comandos numa chamada sao recusados) com o autorizador do modo.
sqlite3_stmt* prepareOne(Db& db, const std::string& sql, Mode& mode, std::string& error) {
    if (sql.empty()) { error = "'sql' esta vazio."; return nullptr; }
    if (sql.size() > kMaxSqlBytes) { error = "Comando grande demais (limite de 100 KB)."; return nullptr; }
    sqlite3_set_authorizer(db.h, authorize, &mode);
    sqlite3_stmt* st = nullptr;
    const char* tail = nullptr;
    if (sqlite3_prepare_v2(db.h, sql.c_str(), (int)sql.size(), &st, &tail) != SQLITE_OK) {
        error = "SQL invalido ou nao permitido: " + lastError(db.h);
        return nullptr;
    }
    if (!st) { error = "O comando esta vazio (so comentarios?)."; return nullptr; }
    for (const char* p = tail; p && *p; ++p) {
        if (!std::isspace((unsigned char)*p) && *p != ';') {
            sqlite3_finalize(st);
            error = "Envie um comando SQL por chamada.";
            return nullptr;
        }
    }
    return st;
}

ToolResult runQuery(const Json& a, const Context& ctx) {
    std::string e;
    fs::path path = resolveDb(ctx, a, e);
    if (path.empty()) return fail(e);
    int maxRows = a.contains("max_rows") && a["max_rows"].is_number() ? (int)a["max_rows"].get<double>() : kDefaultRows;
    maxRows = std::max(1, std::min(maxRows, kMaxRowsHard));

    std::lock_guard<std::mutex> lock(g_dbMutex);
    Db db;
    if (!openDb(db, path, Mode::Read, e)) return fail(e);
    Mode mode = Mode::Read;
    sqlite3_stmt* st = prepareOne(db, str(a, "sql"), mode, e);
    if (!st) return fail(e);
    struct Fin { sqlite3_stmt* s; ~Fin() { sqlite3_finalize(s); } } fin{st};
    if (!sqlite3_stmt_readonly(st)) return fail("sql_query so executa consultas (SELECT). Para alterar dados use sql_execute.");
    if (!bindParams(st, a["params"], e)) return fail(e);

    const int ncol = sqlite3_column_count(st);
    Json rows = Json::array();
    bool truncated = false;
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if ((int)rows.size() >= maxRows) { truncated = true; break; }
        Json row = Json::object();
        for (int i = 0; i < ncol; ++i) row[sqlite3_column_name(st, i) ? sqlite3_column_name(st, i) : "?"] = columnValue(st, i);
        rows.push_back(row);
    }
    if (!truncated && rc != SQLITE_DONE) return fail("Erro ao consultar: " + lastError(db.h));
    Json out = Json::object();
    out["row_count"] = (int64_t)rows.size();
    out["truncated"] = truncated;
    out["rows"] = rows;
    return ok(out.dump());
}

ToolResult runExecute(const Json& a, const Context& ctx) {
    std::string e;
    fs::path path = resolveDb(ctx, a, e);
    if (path.empty()) return fail(e);

    std::lock_guard<std::mutex> lock(g_dbMutex);
    Db db;
    if (!openDb(db, path, Mode::Write, e)) return fail(e);
    Mode mode = Mode::Write;
    sqlite3_stmt* st = prepareOne(db, str(a, "sql"), mode, e);
    if (!st) return fail(e);
    struct Fin { sqlite3_stmt* s; ~Fin() { sqlite3_finalize(s); } } fin{st};
    if (!bindParams(st, a["params"], e)) return fail(e);

    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {}   // um INSERT ... RETURNING devolveria linhas; aqui so interessa concluir
    if (rc != SQLITE_DONE) return fail("Erro ao executar: " + lastError(db.h));
    Json out = Json::object();
    out["changes"] = (int64_t)sqlite3_changes64(db.h);
    out["last_insert_rowid"] = (int64_t)sqlite3_last_insert_rowid(db.h);
    return ok(out.dump());
}

// Consultas internas (sem o autorizador do usuario) para listar tabelas e colunas.
ToolResult runInternal(const Json& a, const Context& ctx, const char* sql, const std::string& bindText, bool listColumns) {
    std::string e;
    fs::path path = resolveDb(ctx, a, e);
    if (path.empty()) return fail(e);
    std::lock_guard<std::mutex> lock(g_dbMutex);
    Db db;
    if (!openDb(db, path, Mode::Read, e)) return fail(e);
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db.h, sql, -1, &st, nullptr) != SQLITE_OK) return fail(lastError(db.h));
    struct Fin { sqlite3_stmt* s; ~Fin() { sqlite3_finalize(s); } } fin{st};
    if (!bindText.empty()) sqlite3_bind_text(st, 1, bindText.c_str(), (int)bindText.size(), SQLITE_TRANSIENT);
    Json rows = Json::array();
    while (sqlite3_step(st) == SQLITE_ROW) {
        Json row = Json::object();
        const int n = sqlite3_column_count(st);
        for (int i = 0; i < n; ++i) row[sqlite3_column_name(st, i)] = columnValue(st, i);
        rows.push_back(row);
    }
    if (listColumns && rows.size() == 0) return fail("Tabela nao encontrada: " + bindText);
    return ok(rows.dump());
}

} // namespace

Server makeSqliteServer() {
    Server s;
    s.id = std::string(kIdPrefix) + "sqlite";
    s.name = "Banco de dados SQLite";
    s.description = "Banco de dados local embutido: tabelas, consultas e gravacoes em um arquivo do projeto. Sem servidor e sem instalacao.";
    s.category = "Dados";

    const Prop db{"database", "string", "Arquivo do banco dentro do projeto (.db, .sqlite ou .sqlite3). Padrao: data/arestrix.db"};

    s.tools.push_back({"list_tables", "Lista as tabelas e views do banco.", schema({db}, {}), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           return runInternal(a, ctx,
                                              "SELECT name, type FROM sqlite_master WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' ORDER BY name",
                                              "", false);
                       }});

    s.tools.push_back({"describe_table", "Mostra as colunas de uma tabela (nome, tipo, obrigatoria, chave primaria).",
                       schema({db, {"table", "string", "Nome da tabela"}}, {"table"}), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           return runInternal(a, ctx,
                                              "SELECT name, type, \"notnull\" AS required, dflt_value AS default_value, pk AS primary_key FROM pragma_table_info(?1)",
                                              str(a, "table"), true);
                       }});

    s.tools.push_back({"sql_query",
                       "Executa uma consulta SELECT (somente leitura) e devolve as linhas. Use ? no comando e passe os valores em 'params'.",
                       schema({db, {"sql", "string", "Comando SELECT. Exemplo: SELECT * FROM clientes WHERE email = ?"},
                               {"params", "array", "Valores para os ? do comando, na ordem"},
                               {"max_rows", "integer", "Maximo de linhas devolvidas (padrao 100, maximo 1000)"}}, {"sql"}),
                       "safe", runQuery});

    s.tools.push_back({"sql_execute",
                       "Altera o banco: CREATE TABLE, INSERT, UPDATE, DELETE, DROP... (um comando por chamada; cria o arquivo se nao existir). "
                       "Use ? no comando e passe os valores em 'params'.",
                       schema({db, {"sql", "string", "Comando SQL. Exemplo: INSERT INTO clientes (nome, email) VALUES (?, ?)"},
                               {"params", "array", "Valores para os ? do comando, na ordem"}}, {"sql"}),
                       "moderate", runExecute});
    return s;
}

} // namespace native
} // namespace taskforge
