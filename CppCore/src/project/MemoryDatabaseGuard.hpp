#pragma once

#include <string>
#include <filesystem>

namespace taskforge {
namespace project {

/**
 * Manutencao local do banco de memoria (.arestrix/memory/memory.db).
 *
 * O Arestrix nao gera, versiona nem restaura dumps de bancos de dados.
 * Backup, redundancia e recuperacao de desastre sao responsabilidade externa
 * ao produto; aqui ficam apenas cuidados locais de integridade e retencao.
 */
class MemoryDatabaseGuard {
public:
    // Coloca o memory.db em quarentena (memory.db.corrupt-<stamp>) quando o
    // arquivo esta vazio, corrompido ou sem o schema de memoria.
    // Banco ausente ou saudavel nao e tocado. Retorna false apenas se a
    // quarentena nao puder ser concluida.
    static bool quarantineIfCorrupt(const std::filesystem::path& projectRoot,
                                    std::string& error);

    // Aplica a retencao de telemetria configurada no manifesto do projeto
    // (globalDefaults.memory.retentionDays). 0 = manter tudo.
    // Independe de o Git estar habilitado.
    static bool applyRetentionOnShutdown(const std::filesystem::path& projectRoot,
                                         const std::string& projectId,
                                         std::string& error);
};

} // namespace project
} // namespace taskforge
