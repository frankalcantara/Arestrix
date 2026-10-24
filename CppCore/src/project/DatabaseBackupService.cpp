#include "DatabaseBackupService.hpp"
#include "GitSyncService.hpp"
#include "ProjectManager.hpp"
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

} // namespace

bool DatabaseBackupService::exportSqlDump(const std::filesystem::path& dbPath,
                                         const std::filesystem::path& sqlPath,
                                         std::string& error) {
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

    std::filesystem::path parent = sqlPath.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        std::filesystem::create_directories(parent, ec);
    }

    std::ofstream out(sqlPath, std::ios::trunc);
    if (!out.is_open()) {
        error = "Nao foi possivel criar o arquivo de dump SQL: " + sqlPath.string();
        sqlite3_close(db);
        return false;
    }

    out << "-- Arestrix Database Dump (Opcao A: DDL e Dados)\n"
        << "-- Backup em texto plano versionavel e compativel com o GitHub\n"
        << "-- Gerado em: " << getCurrentIsoTime() << "\n\n"
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

    // Exporta os dados linha a linha para cada tabela
    for (const auto& tblName : tablesToDump) {
        std::string selectSql = "SELECT * FROM \"" + escapeIdent(tblName) + "\";";

        sqlite3_stmt* dataStmt = nullptr;
        if (sqlite3_prepare_v2(db, selectSql.c_str(), -1, &dataStmt, nullptr) != SQLITE_OK) {
            continue;
        }

        int colCount = sqlite3_column_count(dataStmt);
        bool wroteHeader = false;

        while (sqlite3_step(dataStmt) == SQLITE_ROW) {
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

    out << "COMMIT;\n"
        << "PRAGMA foreign_keys=ON;\n";

    sqlite3_close(db);
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
                                                const std::filesystem::path& snapshotPath,
                                                std::string& error) {
    const std::string pId = projectId.empty() ? "default" : projectId;
    auto data = taskforge::memory::MemoryService::getInstance().toJson(pId);

    taskforge::Json snapshot = taskforge::Json::object();
    snapshot["format"] = "arestrix-memory-snapshot";
    snapshot["version"] = "1.0.0";
    snapshot["projectId"] = pId;
    snapshot["exportedAt"] = getCurrentIsoTime();
    snapshot["lessons"] = data.value("lessons", taskforge::Json::array());
    snapshot["working"] = data.value("working", taskforge::Json::object());

    // Se nao houver licoes e nem working, nao grava se o arquivo de snapshot nao existir
    auto lessons = snapshot["lessons"];
    auto working = snapshot["working"];
    if (lessons.empty() && working.empty() && !std::filesystem::exists(snapshotPath)) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(snapshotPath.parent_path(), ec);

    std::ofstream out(snapshotPath, std::ios::trunc);
    if (!out.is_open()) {
        error = "Falha ao abrir arquivo para snapshot de memoria: " + snapshotPath.string();
        return false;
    }

    out << snapshot.dump(2);
    out.close();
    return true;
}

bool DatabaseBackupService::restoreMemorySnapshot(const std::string& projectId,
                                                 const std::filesystem::path& snapshotPath,
                                                 std::string& error) {
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
        taskforge::memory::MemoryService::getInstance().configure(pId, true);

        // 1. Restaura licoes aprendidas
        if (snapshot.contains("lessons") && snapshot["lessons"].is_array()) {
            for (const auto& lesson : snapshot["lessons"]) {
                std::string domain = lesson.value("domain", "geral");
                std::string triggerError = lesson.value("triggerError", "");
                std::string lessonRule = lesson.value("lessonRule", "");
                std::string sourceItem = lesson.value("sourceItem", "");
                if (!lessonRule.empty()) {
                    taskforge::memory::MemoryService::getInstance().recordLesson(
                        pId, domain, triggerError, lessonRule, sourceItem, "snapshot_restore");
                }
            }
        }

        // 2. Restaura working memory (Blackboard compartilhado)
        if (snapshot.contains("working") && snapshot["working"].is_object()) {
            auto working = snapshot["working"];
            for (auto item = working.begin(); item != working.end(); ++item) {
                if (item.value().is_object()) {
                    for (auto key = item.value().begin(); key != item.value().end(); ++key) {
                        taskforge::memory::MemoryService::getInstance().setWorking(
                            pId, item.key(), key.key(), key.value());
                    }
                }
            }
        }

        std::cout << "[DatabaseBackupService] Memoria de agentes restaurada com sucesso a partir de: " << snapshotPath.string() << "\n";
        return true;
    } catch (const std::exception& e) {
        error = "Erro ao processar snapshot de memoria: " + std::string(e.what());
        return false;
    }
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

    std::string dumpErr;
    if (std::filesystem::exists(dbPath)) {
        if (!exportSqlDump(dbPath, sqlPath, dumpErr)) {
            std::cerr << "[DatabaseBackupService] Erro ao exportar dump SQL operacional: " << dumpErr << "\n";
            error += dumpErr + " ";
        } else {
            std::cout << "[DatabaseBackupService] Dump SQL gerado com sucesso em: " << sqlPath.string() << "\n";
        }
    }

    // 3. Exporta snapshot da memoria cognitiva dos agentes
    std::filesystem::path snapshotPath = storageDir / "memory.dump.json";
    std::string memErr;
    if (!exportMemorySnapshot(projectId, snapshotPath, memErr)) {
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

    // 2. Restaura memoria se o .db nao existir mas o snapshot existir
    std::filesystem::path memDbPath = projectRoot / ".arestrix" / "memory" / "memory.db";
    std::filesystem::path snapshotPath = storageDir / "memory.dump.json";
    if (!std::filesystem::exists(memDbPath) && std::filesystem::exists(snapshotPath)) {
        std::string memErr;
        restoreMemorySnapshot(projectId, snapshotPath, memErr);
        if (!memErr.empty()) error += memErr;
    }

    return true;
}

} // namespace project
} // namespace taskforge
