#pragma once

#include <string>
#include <filesystem>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace project {

/**
 * Serviço de backup e recuperação de desastres para bancos de dados do Arestrix.
 *
 * Permite excluir arquivos binários SQLite (*.sqlite, *.db) do Git para cumprir
 * as regras de uso aceitável do GitHub, mantendo a capacidade de recuperação total
 * através de dumps em texto puro SQL (DDL + dados) e snapshots JSON de memória.
 */
class DatabaseBackupService {
public:
    // Exporta o banco de dados operacional SQLite para um dump SQL em texto puro (DDL + INSERTs)
    // Se dbPath não existir ou estiver vazio, retorna true sem gerar arquivo.
    static bool exportSqlDump(const std::filesystem::path& dbPath,
                             const std::filesystem::path& sqlPath,
                             std::string& error);

    // Restaura o banco de dados operacional SQLite a partir do dump SQL em texto
    // Se dbPath já existir e overwrite for false, retorna true sem sobrescrever.
    static bool restoreFromSqlDump(const std::filesystem::path& sqlPath,
                                  const std::filesystem::path& dbPath,
                                  bool overwrite,
                                  std::string& error);

    // Exporta o snapshot da memória cognitiva dos agentes (AMG: lições aprendidas, blackboard)
    // para um arquivo JSON estruturado e versionável.
    static bool exportMemorySnapshot(const std::string& projectId,
                                    const std::filesystem::path& snapshotPath,
                                    std::string& error);

    // Restaura o snapshot da memória cognitiva dos agentes a partir do arquivo JSON.
    static bool restoreMemorySnapshot(const std::string& projectId,
                                     const std::filesystem::path& snapshotPath,
                                     std::string& error);

    // Rotina completa de backup de encerramento do projeto (onShutdown).
    // Executa o dump do banco operacional (storage/database.sql) e da memória (storage/memory.dump.json).
    static bool performProjectShutdownBackup(const std::filesystem::path& projectRoot,
                                            const std::string& projectId,
                                            std::string& error);

    // Rotina de restauração automática executada ao abrir o projeto (Disaster Recovery / Clone limpo).
    // Se os bancos SQLite binários estiverem ausentes, restaura a partir dos dumps SQL/JSON.
    static bool restoreProjectIfNeeded(const std::filesystem::path& projectRoot,
                                      const std::string& projectId,
                                      std::string& error);
};

} // namespace project
} // namespace taskforge
