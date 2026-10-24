#pragma once

#include <string>
#include <filesystem>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace project {

/**
 * Servico de backup e recuperacao de desastres para bancos de dados do Arestrix.
 *
 * Permite excluir arquivos binarios SQLite (*.sqlite, *.db) do Git para cumprir
 * as regras de uso aceitavel do GitHub, mantendo a capacidade de recuperacao total
 * atraves de dumps em texto puro SQL (DDL + dados ou schema-only) e snapshots JSON de memoria.
 */
class DatabaseBackupService {
public:
    // Exporta o banco de dados operacional SQLite para um dump SQL em texto puro
    // schemaOnly: se true, exporta apenas DDL/estruturas (sem dados/inserts).
    // Se dbPath nao existir ou estiver vazio, retorna true sem gerar arquivo.
    static bool exportSqlDump(const std::filesystem::path& dbPath,
                             const std::filesystem::path& sqlPath,
                             std::string& error,
                             bool schemaOnly = false);

    // Restaura o banco de dados operacional SQLite a partir do dump SQL em texto
    // Se dbPath ja existir e overwrite for false, retorna true sem sobrescrever.
    static bool restoreFromSqlDump(const std::filesystem::path& sqlPath,
                                  const std::filesystem::path& dbPath,
                                  bool overwrite,
                                  std::string& error);

    // Exporta o snapshot da memoria cognitiva dos agentes (AMG: licoes aprendidas, blackboard)
    // para um arquivo JSON estruturado e versionavel.
    static bool exportMemorySnapshot(const std::string& projectId,
                                    const std::filesystem::path& snapshotPath,
                                    std::string& error);

    // Restaura o snapshot da memoria cognitiva dos agentes a partir do arquivo JSON.
    static bool restoreMemorySnapshot(const std::string& projectId,
                                     const std::filesystem::path& snapshotPath,
                                     std::string& error);

    // Rotina completa de backup de encerramento do projeto (onShutdown).
    // Executa o dump do banco operacional (storage/database.sql) e da memoria (storage/memory.dump.json).
    static bool performProjectShutdownBackup(const std::filesystem::path& projectRoot,
                                            const std::string& projectId,
                                            std::string& error);

    // Rotina de restauracao automatica executada ao abrir o projeto (Disaster Recovery / Clone limpo).
    // Se os bancos SQLite binarios estiverem ausentes, restaura a partir dos dumps SQL/JSON.
    static bool restoreProjectIfNeeded(const std::filesystem::path& projectRoot,
                                      const std::string& projectId,
                                      std::string& error);
};

} // namespace project
} // namespace taskforge
