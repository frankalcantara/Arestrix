#include "DatabaseBackupService.hpp"
#include "GitSyncService.hpp"
#include "ProjectManager.hpp"
#include "memory/MemorySanitizer.hpp"
#include "memory/MemoryService.hpp"
#include "util/Json.hpp"
#include "sqlite3.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace taskforge {
namespace project {

namespace {

std::string getCurrentIsoTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string escapeSqlString(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        if (c == '\'') {
            out.push_back('\'');
            out.push_back('\'');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string escapeIdent(const std::string& ident) {
    std::string out;
    out.reserve(ident.size() + 4);
    for (char c : ident) {
        if (c == '"') {
            out.push_back('"');
            out.push_back('"');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::filesystem::path memoryDatabasePath(const std::filesystem::path& projectRoot) {
    return projectRoot / ".arestrix" / "memory" / "memory.db";
}

bool snapshotHasCognitiveContent(const std::filesystem::path& snapshotPath) {
    std::error_code ec;
    if (!std::filesystem::exists(snapshotPath, ec)) return false;

    std::ifstream in(snapshotPath, std::ios::binary);
    if (!in.is_open()) return true;   // conservador: nao sobrescrever o que nao conseguimos ler
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    auto parsed = taskforge::jsonutil::tryParse(content);
    if (!parsed || !parsed->is_object()) return true;   // snapshot invalido: preservar
    if (parsed->contains("lessons") && (*parsed)["lessons"].is_array() && !(*parsed)["lessons"].empty()) {
        return true;
    }
    if (parsed->contains("working") && (*parsed)["working"].is_object() && !(*parsed)["working"].empty()) {
        return true;
    }
    for (const char* key : {"nodes", "edges", "workingRows"}) {
        if (parsed->contains(key) && (*parsed)[key].is_array() && !(*parsed)[key].empty()) {
            return true;
        }
    }
    return false;
}

// Verifica se o arquivo e um SQLite saudavel e se o schema de memoria esta presente.
// Um arquivo de zero bytes e tratado como invalido para que o snapshot possa restaurar.
bool sqliteMemoryFileHealthy(const std::filesystem::path& path, std::string& detail) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        detail = "arquivo ausente";
        return false;
    }
    if (std::filesystem::file_size(path, ec) == 0) {
        detail = "arquivo vazio";
        return false;
    }

    sqlite3* db = nullptr;
    const std::string dbPath = path.string();
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        detail = db ? sqlite3_errmsg(db) : "falha ao abrir o banco";
        if (db) sqlite3_close(db);
        return false;
    }

    bool ok = false;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "PRAGMA quick_check;", -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        ok = text && std::string(reinterpret_cast<const char*>(text)) == "ok";
        if (!ok) detail = text ? reinterpret_cast<const char*>(text) : "quick_check falhou";
    } else {
        detail = sqlite3_errmsg(db);
    }
    if (stmt) sqlite3_finalize(stmt);
    stmt = nullptr;

    if (ok) {
        if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name='memory_scope' LIMIT 1;",
                               -1, &stmt, nullptr) == SQLITE_OK) {
            ok = sqlite3_step(stmt) == SQLITE_ROW;
            if (!ok) detail = "schema de memoria ausente";
        } else {
            ok = false;
            detail = sqlite3_errmsg(db);
        }
    }

    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return ok;
}

bool quarantineFile(const std::filesystem::path& path, std::string& error) {
    const auto stamp = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    std::filesystem::path target = path;
    target += ".corrupt-" + stamp;
    std::error_code ec;
    std::filesystem::rename(path, target, ec);
    if (ec) {
        error = "Falha ao colocar banco corrompido em quarentena (" + target.string() + "): " + ec.message();
        return false;
    }
    return true;
}

bool writeSnapshotFile(const std::filesystem::path& snapshotPath, const std::string& content,
                       std::string& error) {
    std::error_code ec;
    if (!snapshotPath.parent_path().empty()) {
        std::filesystem::create_directories(snapshotPath.parent_path(), ec);
    }
    std::filesystem::path tmp = snapshotPath;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            error = "Falha ao abrir arquivo temporario de snapshot: " + tmp.string();
            return false;
        }
        out << content;
        out.flush();
        if (!out) {
            error = "Falha ao gravar snapshot temporario: " + tmp.string();
            return false;
        }
    }
    // POSIX substitui atomicamente. No Windows, o rename falha se o alvo existir;
    // nesse caso removemos o alvo e tentamos novamente.
    std::error_code renameEc;
    std::filesystem::rename(tmp, snapshotPath, renameEc);
    if (renameEc) {
        std::error_code removeEc;
        std::filesystem::remove(snapshotPath, removeEc);
        renameEc.clear();
        std::filesystem::rename(tmp, snapshotPath, renameEc);
    }
    if (renameEc) {
        error = "Falha ao publicar snapshot de memoria: " + renameEc.message();
        std::error_code cleanup;
        std::filesystem::remove(tmp, cleanup);
        return false;
    }
    return true;
}

} // namespace

bool DatabaseBackupService::exportSqlDump(const std::filesystem::path& dbPath,
                                         const std::filesystem::path& sqlPath,
                                         std::string& error,
                                         bool schemaOnly) {
    if (!std::filesystem::exists(dbPath)) {
        return true; // Nada a exportar se o banco nao existir no disco
    }

    std::error_code ec;
    auto sz = std::filesystem::file_size(dbPath, ec);
    if (ec || sz == 0) {
        return true;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db) : "Nao foi possivel abrir o banco SQLite para leitura do dump.";
        if (db) sqlite3_close(db);
        return false;
    }

    std::ostringstream out;
    out << "-- Arestrix Database Dump (DDL e Dados)\n"
        << "-- Backup em texto plano versionavel e compativel com o GitHub\n\n"
        << "PRAGMA foreign_keys=OFF;\n"
        << "BEGIN TRANSACTION;\n\n";

    const char* masterQuery =
        "SELECT type, name, sql FROM sqlite_master "
        "WHERE sql IS NOT NULL AND name NOT LIKE 'sqlite_%' "
        "ORDER BY CASE type "
        "  WHEN 'table' THEN 1 "
        "  WHEN 'view' THEN 2 "
        "  WHEN 'index' THEN 3 "
        "  WHEN 'trigger' THEN 4 "
        "  ELSE 5 END, name;";

    sqlite3_stmt* masterStmt = nullptr;
    if (sqlite3_prepare_v2(db, masterQuery, -1, &masterStmt, nullptr) != SQLITE_OK) {
        error = "Falha ao consultar catalogo de objetos sqlite_master: " + std::string(sqlite3_errmsg(db));
        sqlite3_close(db);
        return false;
    }

    std::vector<std::string> tablesToDump;

    while (sqlite3_step(masterStmt) == SQLITE_ROW) {
        const char* typeC = reinterpret_cast<const char*>(sqlite3_column_text(masterStmt, 0));
        const char* nameC = reinterpret_cast<const char*>(sqlite3_column_text(masterStmt, 1));
        const char* sqlC = reinterpret_cast<const char*>(sqlite3_column_text(masterStmt, 2));

        std::string type = typeC ? typeC : "";
        std::string name = nameC ? nameC : "";
        std::string sql = sqlC ? sqlC : "";

        if (type == "table") {
            out << "-- Estrutura da Tabela: " << name << "\n";
            out << sql << ";\n\n";
            tablesToDump.push_back(name);
        } else {
            out << "-- " << type << ": " << name << "\n";
            out << sql << ";\n\n";
        }
    }
    sqlite3_finalize(masterStmt);

    const size_t kMaxDumpBytes = 95 * 1024 * 1024; // 95 MB (evita limite rigido de 100 MB do GitHub GH001)
    const size_t kWarnDumpBytes = 50 * 1024 * 1024; // 50 MB (alerta de arquivo volumoso)
    bool truncated = false;

    // Exporta os dados linha a linha para cada tabela (se nao for schema_only)
    if (!schemaOnly) {
        for (const auto& tblName : tablesToDump) {
            if (static_cast<size_t>(out.tellp()) >= kMaxDumpBytes) {
                truncated = true;
                break;
            }

            std::string selectSql = "SELECT * FROM \"" + escapeIdent(tblName) + "\";";
            sqlite3_stmt* dataStmt = nullptr;
            if (sqlite3_prepare_v2(db, selectSql.c_str(), -1, &dataStmt, nullptr) != SQLITE_OK) {
                continue;
            }

            int colCount = sqlite3_column_count(dataStmt);
            bool wroteHeader = false;

            while (sqlite3_step(dataStmt) == SQLITE_ROW) {
                if (static_cast<size_t>(out.tellp()) >= kMaxDumpBytes) {
                    truncated = true;
                    break;
                }

                if (!wroteHeader) {
                    out << "-- Dados para tabela: " << tblName << "\n";
                    wroteHeader = true;
                }
                out << "INSERT INTO \"" << escapeIdent(tblName) << "\" VALUES (";
                for (int i = 0; i < colCount; ++i) {
                    if (i > 0) out << ", ";
                    int colType = sqlite3_column_type(dataStmt, i);
                    switch (colType) {
                        case SQLITE_NULL:
                            out << "NULL";
                            break;
                        case SQLITE_INTEGER:
                            out << sqlite3_column_int64(dataStmt, i);
                            break;
                        case SQLITE_FLOAT: {
                            double val = sqlite3_column_double(dataStmt, i);
                            if (std::isnan(val)) {
                                out << "NULL";
                            } else if (std::isinf(val)) {
                                out << (val < 0 ? "-1e999" : "1e999");
                            } else {
                                char buf[64];
                                snprintf(buf, sizeof(buf), "%.17g", val);
                                out << buf;
                                if (strchr(buf, '.') == nullptr && strchr(buf, 'e') == nullptr && strchr(buf, 'E') == nullptr) {
                                    out << ".0";
                                }
                            }
                            break;
                        }
                        case SQLITE_TEXT: {
                            const unsigned char* txt = sqlite3_column_text(dataStmt, i);
                            int bytes = sqlite3_column_bytes(dataStmt, i);
                            std::string s(reinterpret_cast<const char*>(txt), bytes);
                            out << "'" << escapeSqlString(s) << "'";
                            break;
                        }
                        case SQLITE_BLOB: {
                            const unsigned char* blob = static_cast<const unsigned char*>(sqlite3_column_blob(dataStmt, i));
                            int bytes = sqlite3_column_bytes(dataStmt, i);
                            out << "X'";
                            static const char hexDigits[] = "0123456789ABCDEF";
                            for (int b = 0; b < bytes; ++b) {
                                out << hexDigits[(blob[b] >> 4) & 0x0F];
                                out << hexDigits[blob[b] & 0x0F];
                            }
                            out << "'";
                            break;
                        }
                    }
                }
                out << ");\n";
            }
            sqlite3_finalize(dataStmt);
            if (wroteHeader) out << "\n";
        }
    }

    if (truncated) {
        out << "\n-- AVISO: Dados truncados automaticamente pelo Arestrix para proteger o repositorio do limite rigido de 100 MB do GitHub (GH001).\n";
        error = "Aviso: Dados do banco SQLite truncados no dump SQL por excederem 95 MB (limite maximo do GitHub).";
        std::cerr << "[DatabaseBackupService] " << error << "\n";
    }

    out << "COMMIT;\n"
        << "PRAGMA foreign_keys=ON;\n";

    sqlite3_close(db);

    std::string newContent = out.str();
    if (newContent.size() > kWarnDumpBytes && !truncated) {
        std::cerr << "[DatabaseBackupService] Aviso: Dump SQL operacional atingiu " << (newContent.size() / 1024 / 1024) << " MB (> 50 MB).\n";
    }

    // Se o arquivo ja existe e o conteudo for identico, NAO regrava (evita timestamp falso e ghost commits no Git)
    if (std::filesystem::exists(sqlPath)) {
        std::ifstream in(sqlPath, std::ios::binary);
        std::string existingContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (existingContent == newContent) {
            return true; // Conteudo 100% identico: preserva last_write_time e nao gera commit no Git
        }
    }

    std::filesystem::path parent = sqlPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream fileOut(sqlPath, std::ios::binary | std::ios::trunc);
    if (!fileOut.is_open()) {
        error = "Nao foi possivel criar o arquivo de dump SQL: " + sqlPath.string();
        return false;
    }
    fileOut << newContent;
    fileOut.close();
    return true;
}

bool DatabaseBackupService::restoreFromSqlDump(const std::filesystem::path& sqlPath,
                                              const std::filesystem::path& dbPath,
                                              bool overwrite,
                                              std::string& error) {
    if (!std::filesystem::exists(sqlPath)) {
        error = "Arquivo de dump SQL ausente: " + sqlPath.string();
        return false;
    }

    if (std::filesystem::exists(dbPath) && !overwrite) {
        return true; // Ja existe e nao e para sobrescrever
    }

    std::ifstream in(sqlPath, std::ios::binary);
    if (!in.is_open()) {
        error = "Nao foi possivel abrir o arquivo SQL para leitura: " + sqlPath.string();
        return false;
    }
    std::string sqlContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::error_code ec;
    std::filesystem::create_directories(dbPath.parent_path(), ec);

    if (overwrite && std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath, ec);
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        error = db ? sqlite3_errmsg(db) : "Nao foi possivel inicializar o banco SQLite para restauracao.";
        if (db) sqlite3_close(db);
        return false;
    }

    char* errmsg = nullptr;
    if (sqlite3_exec(db, sqlContent.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        error = errmsg ? errmsg : "Falha na execucao do dump SQL de restauracao.";
        if (errmsg) sqlite3_free(errmsg);
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    std::cout << "[DatabaseBackupService] Banco operacional restaurado com sucesso a partir de: " << sqlPath.string() << "\n";
    return true;
}

bool DatabaseBackupService::exportMemorySnapshot(const std::string& projectId,
                                                const std::filesystem::path& projectRoot,
                                                const std::filesystem::path& snapshotPath,
                                                std::string& error) {
    const std::string pId = projectId.empty() ? "default" : projectId;
    if (projectRoot.empty()) {
        error = "Raiz do projeto vazia ao exportar snapshot de memoria.";
        return false;
    }

    // O banco de memoria e ancorado na raiz do projeto, nunca no CWD do processo.
    // Se ainda nao existe, nao criamos um banco vazio nem tocamos no snapshot.
    const std::filesystem::path memDbPath = memoryDatabasePath(projectRoot);
    std::error_code existsEc;
    if (!std::filesystem::exists(memDbPath, existsEc)) {
        return true;
    }

    auto& memoryService = taskforge::memory::MemoryService::getInstance();
    if (!memoryService.configure(pId, true, memDbPath.string())) {
        error = "Nao foi possivel abrir o banco de memoria para exportacao: " + memDbPath.string();
        return false;
    }

    auto data = memoryService.toCognitiveJson(pId);

    taskforge::Json snapshot = data;
    snapshot["format"] = "arestrix-memory-snapshot";
    snapshot["version"] = "2.0.0";
    snapshot["projectId"] = pId;
    snapshot["lessons"] = data.value("lessons", taskforge::Json::array());
    snapshot["working"] = data.value("working", taskforge::Json::object());
    taskforge::memory::sanitizeJsonInPlace(snapshot);

    const bool emptyCognitive =
        (!snapshot.contains("lessons") || snapshot["lessons"].empty()) &&
        (!snapshot.contains("working") || snapshot["working"].empty()) &&
        (!snapshot.contains("nodes") || snapshot["nodes"].empty()) &&
        (!snapshot.contains("edges") || snapshot["edges"].empty()) &&
        (!snapshot.contains("workingRows") || snapshot["workingRows"].empty());
    if (emptyCognitive && snapshotHasCognitiveContent(snapshotPath)) {
        // Um banco recem-criado/vazio nao pode apagar um snapshot com conhecimento.
        // A limpeza explicita de memoria remove o snapshot em ui_clear_memory.
        std::cout << "[DatabaseBackupService] Snapshot vazio ignorado para preservar memoria existente: "
                  << snapshotPath.string() << "\n";
        return true;
    }

    std::string newJson = snapshot.dump(2);

    // Se o arquivo ja existir e o JSON for identico, NAO regrava (evita ghost commits)
    if (std::filesystem::exists(snapshotPath)) {
        std::ifstream in(snapshotPath, std::ios::binary);
        std::string existingContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (existingContent == newJson) {
            return true;
        }
    }

    return writeSnapshotFile(snapshotPath, newJson, error);
}

bool DatabaseBackupService::restoreMemorySnapshot(const std::string& projectId,
                                                 const std::filesystem::path& projectRoot,
                                                 const std::filesystem::path& snapshotPath,
                                                 std::string& error) {
    if (projectRoot.empty()) {
        error = "Raiz do projeto vazia ao restaurar snapshot de memoria.";
        return false;
    }
    if (!std::filesystem::exists(snapshotPath)) {
        return false;
    }

    std::ifstream in(snapshotPath);
    if (!in.is_open()) {
        error = "Nao foi possivel abrir o snapshot de memoria: " + snapshotPath.string();
        return false;
    }

    try {
        taskforge::Json snapshot = taskforge::jsonutil::parseStream(in);
        in.close();

        const std::string pId = projectId.empty() ? snapshot.value("projectId", "default") : projectId;
        const std::filesystem::path memDbPath = memoryDatabasePath(projectRoot);
        auto& memoryService = taskforge::memory::MemoryService::getInstance();
        if (!memoryService.configure(pId, true, memDbPath.string())) {
            error = "Nao foi possivel abrir o banco de memoria para restauracao: " + memDbPath.string();
            return false;
        }

        const bool isV2 = (snapshot.contains("nodes") && snapshot["nodes"].is_array()) ||
                          (snapshot.contains("edges") && snapshot["edges"].is_array());
        if (isV2) {
            std::string importError;
            if (!memoryService.importCognitiveJson(pId, snapshot, importError)) {
                error = "Restauracao do snapshot v2 falhou: " + importError;
                return false;
            }
            std::cout << "[DatabaseBackupService] Memoria de agentes restaurada (snapshot v2) a partir de: "
                      << snapshotPath.string() << "\n";
            return true;
        }

        // Fallback v1: importacao transacional de licoes e Blackboard.
        std::string legacyError;
        if (!memoryService.importLegacySnapshot(pId, snapshot, legacyError)) {
            error = "Restauracao do snapshot v1 falhou: " + legacyError;
            return false;
        }

        std::cout << "[DatabaseBackupService] Memoria de agentes restaurada com sucesso a partir de: " << snapshotPath.string() << "\n";
        return true;
    } catch (const std::exception& e) {
        error = "Erro ao processar snapshot de memoria: " + std::string(e.what());
        return false;
    }
}

bool DatabaseBackupService::pruneMemoryOnShutdown(const std::filesystem::path& projectRoot,
                                                 const std::string& projectId,
                                                 std::string& error) {
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) return true;

    std::filesystem::path manifestPath = projectRoot / "arestrix.project.json";
    if (!std::filesystem::exists(manifestPath)) {
        manifestPath = projectRoot / "taskforge.project.json";
    }
    if (!std::filesystem::exists(manifestPath)) return true;

    int retentionDays = 180;
    try {
        std::ifstream mf(manifestPath);
        auto j = taskforge::jsonutil::parseStream(mf);
        if (j.contains("globalDefaults") && j["globalDefaults"].is_object() &&
            j["globalDefaults"].contains("memory") && j["globalDefaults"]["memory"].is_object()) {
            retentionDays = j["globalDefaults"]["memory"].value("retentionDays", 180);
        }
    } catch (...) {
        return true;   // manifesto ilegivel: nao apaga telemetria por engano
    }
    if (retentionDays <= 0) return true;

    const std::filesystem::path memDbPath = memoryDatabasePath(projectRoot);
    std::error_code ec;
    if (!std::filesystem::exists(memDbPath, ec)) return true;

    auto& memoryService = taskforge::memory::MemoryService::getInstance();
    if (!memoryService.configure(projectId, true, memDbPath.string())) {
        error = "Nao foi possivel abrir o banco de memoria para retencao: " + memDbPath.string();
        return false;
    }
    const auto pruned = memoryService.prune(projectId, retentionDays);
    if (pruned.episodes > 0 || pruned.hits > 0 || pruned.usage > 0) {
        std::cout << "[DatabaseBackupService] Retencao de memoria (" << retentionDays
                  << " dias): " << pruned.episodes << " episodio(s), "
                  << pruned.hits << " hit(s), " << pruned.usage << " usage removidos.\n";
    }
    return true;
}

bool DatabaseBackupService::performProjectShutdownBackup(const std::filesystem::path& projectRoot,
                                                        const std::string& projectId,
                                                        std::string& error) {
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) return true;

    // 1. Garante que as regras seguras de .gitignore estejam no diretorio do projeto
    GitSyncService::ensureSafeGitignore(projectRoot);

    // 2. Localiza o banco de dados operacional configurado
    std::filesystem::path storageDir = projectRoot / "storage";
    std::filesystem::path dbPath = storageDir / "database.sqlite";
    std::filesystem::path sqlPath = storageDir / "database.sql";

    bool gitEnabled = false;
    std::string backupMode = "ddl_and_data";

    std::filesystem::path manifestPath = projectRoot / "arestrix.project.json";
    if (!std::filesystem::exists(manifestPath)) {
        manifestPath = projectRoot / "taskforge.project.json";
    }
    if (std::filesystem::exists(manifestPath)) {
        try {
            std::ifstream mf(manifestPath);
            auto j = taskforge::jsonutil::parseStream(mf);
            if (j.contains("git")) {
                gitEnabled = j["git"].value("enabled", false);
                backupMode = j["git"].value("backupMode", "ddl_and_data");
            }
            if (j.contains("database") && j["database"].contains("filePath")) {
                std::string relPath = j["database"]["filePath"].str();
                if (!relPath.empty()) {
                    dbPath = projectRoot / relPath;
                    std::string baseName = dbPath.stem().string();
                    sqlPath = dbPath.parent_path() / (baseName + ".sql");
                }
            }
        } catch (...) {}
    }

    // Se a sincronizacao Git estiver desativada (padrao), opera em modo 100% local e dispensa dumps
    if (!gitEnabled) {
        return true;
    }

    bool schemaOnly = (backupMode == "schema_only");
    std::string dumpErr;
    if (std::filesystem::exists(dbPath)) {
        if (!exportSqlDump(dbPath, sqlPath, dumpErr, schemaOnly)) {
            std::cerr << "[DatabaseBackupService] Erro ao exportar dump SQL operacional: " << dumpErr << "\n";
            error += dumpErr + " ";
        } else {
            std::cout << "[DatabaseBackupService] Dump SQL gerado com sucesso em: " << sqlPath.string() << "\n";
        }
    }

    // 3. Exporta snapshot da memoria cognitiva dos agentes
    std::filesystem::path snapshotPath = storageDir / "memory.dump.json";
    std::string memErr;
    if (!exportMemorySnapshot(projectId, projectRoot, snapshotPath, memErr)) {
        std::cerr << "[DatabaseBackupService] Erro ao exportar snapshot de memoria: " << memErr << "\n";
        error += memErr;
    } else {
        std::cout << "[DatabaseBackupService] Snapshot de memoria gerado em: " << snapshotPath.string() << "\n";
    }

    return error.empty();
}

bool DatabaseBackupService::restoreProjectIfNeeded(const std::filesystem::path& projectRoot,
                                                  const std::string& projectId,
                                                  std::string& error) {
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) return false;

    // 1. Restaura banco operacional se o arquivo .sqlite nao existir mas o .sql existir
    std::filesystem::path storageDir = projectRoot / "storage";
    std::filesystem::path dbPath = storageDir / "database.sqlite";
    std::filesystem::path sqlPath = storageDir / "database.sql";

    std::filesystem::path manifestPath = projectRoot / "arestrix.project.json";
    if (!std::filesystem::exists(manifestPath)) {
        manifestPath = projectRoot / "taskforge.project.json";
    }
    if (std::filesystem::exists(manifestPath)) {
        try {
            std::ifstream mf(manifestPath);
            auto j = taskforge::jsonutil::parseStream(mf);
            if (j.contains("database") && j["database"].contains("filePath")) {
                std::string relPath = j["database"]["filePath"].str();
                if (!relPath.empty()) {
                    dbPath = projectRoot / relPath;
                    std::string baseName = dbPath.stem().string();
                    sqlPath = dbPath.parent_path() / (baseName + ".sql");
                }
            }
        } catch (...) {}
    }

    if (!std::filesystem::exists(dbPath) && std::filesystem::exists(sqlPath)) {
        std::string restoreErr;
        restoreFromSqlDump(sqlPath, dbPath, false, restoreErr);
        if (!restoreErr.empty()) error += restoreErr + " ";
    }

    // 2. Restaura memoria se o .db estiver ausente ou corrompido e o snapshot existir
    std::filesystem::path memDbPath = memoryDatabasePath(projectRoot);
    std::filesystem::path snapshotPath = storageDir / "memory.dump.json";

    if (std::filesystem::exists(memDbPath)) {
        std::string healthDetail;
        if (!sqliteMemoryFileHealthy(memDbPath, healthDetail)) {
            std::string quarantineErr;
            if (quarantineFile(memDbPath, quarantineErr)) {
                std::cout << "[DatabaseBackupService] Banco de memoria invalido (" << healthDetail
                          << ") colocado em quarentena.\n";
            } else {
                error += quarantineErr + " ";
            }
        }
    }

    if (!std::filesystem::exists(memDbPath) && std::filesystem::exists(snapshotPath)) {
        std::string memErr;
        if (!restoreMemorySnapshot(projectId, projectRoot, snapshotPath, memErr)) {
            error += memErr;
        }
    }

    return true;
}

} // namespace project
} // namespace taskforge
