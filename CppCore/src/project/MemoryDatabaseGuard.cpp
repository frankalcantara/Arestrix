#include "MemoryDatabaseGuard.hpp"
#include "memory/MemoryService.hpp"
#include "util/Json.hpp"
#include "sqlite3.h"

#include <chrono>
#include <fstream>
#include <iostream>

namespace taskforge {
namespace project {

namespace {

std::filesystem::path memoryDatabasePath(const std::filesystem::path& projectRoot) {
    return projectRoot / ".arestrix" / "memory" / "memory.db";
}

// Verifica se o arquivo e um SQLite saudavel e se o schema de memoria esta presente.
// Um arquivo de zero bytes tambem e tratado como invalido.
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

} // namespace

bool MemoryDatabaseGuard::quarantineIfCorrupt(const std::filesystem::path& projectRoot,
                                              std::string& error) {
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) return true;

    const std::filesystem::path memDbPath = memoryDatabasePath(projectRoot);
    std::error_code ec;
    if (!std::filesystem::exists(memDbPath, ec)) return true;

    std::string healthDetail;
    if (sqliteMemoryFileHealthy(memDbPath, healthDetail)) return true;

    if (!quarantineFile(memDbPath, error)) return false;

    std::cout << "[MemoryDatabaseGuard] Banco de memoria invalido (" << healthDetail
              << ") colocado em quarentena.\n";
    return true;
}

bool MemoryDatabaseGuard::applyRetentionOnShutdown(const std::filesystem::path& projectRoot,
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
        std::cout << "[MemoryDatabaseGuard] Retencao de memoria (" << retentionDays
                  << " dias): " << pruned.episodes << " episodio(s), "
                  << pruned.hits << " hit(s), " << pruned.usage << " usage removidos.\n";
    }
    return true;
}

} // namespace project
} // namespace taskforge
