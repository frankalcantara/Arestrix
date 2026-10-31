#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <regex>
#include <chrono>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace sandbox {

struct ArtifactValidationRule {
    std::string targetPathPattern;      // ex: "aulas/{itemId}.md"
    size_t minSizeBytes = 0;           // ex: 1000
    std::string contentPattern;        // ex: "^---[\\s\\S]+---"
    bool checkLogExitCode = true;       // ex: procura "EXIT CODE: 0" no log
};

struct StageCheckpointInfo {
    std::string status = "pendente";   // "pendente", "executando", "concluido", "erro"
    std::string completedAt;
    std::string logFile;
    std::string artifactPath;
    bool recoveredFromArtifact = false;
    std::string errorMessage;
};

class PipelineResumeEngine {
public:
    explicit PipelineResumeEngine(const std::string& stateFilePath = "pipeline-state.json");

    static PipelineResumeEngine& getInstance();

    void setStateFilePath(const std::string& path);
    std::string getStateFilePath() const;

    // Resolve placeholders como {itemId} ou {id} no caminho. Estatico para ser
    // reaproveitado pela validacao de artefato do runtime de agentes.
    static std::string resolvePattern(const std::string& pattern, const std::string& itemId);

    // Valida artefato fisico no disco. Nao depende do estado do resume engine.
    static bool validateArtifact(const std::string& resolvedPath, size_t minBytes,
                                 const std::string& regexPattern);

    // Verifica se etapa já foi concluída (com auto-recuperação física)
    bool isStageCompleted(
        const std::string& itemId, 
        const std::string& stageId, 
        const ArtifactValidationRule& rule, 
        const std::string& logFilePath = "", 
        bool force = false
    );

    // Marca início de execução de etapa
    void markStageStarted(const std::string& itemId, const std::string& stageId);

    // Marca conclusão com sucesso
    void markStageSuccess(
        const std::string& itemId, 
        const std::string& stageId, 
        const std::string& artifactPath = "", 
        const std::string& logPath = ""
    );

    // Marca falha de etapa
    void markStageFailed(
        const std::string& itemId, 
        const std::string& stageId, 
        const std::string& errorMsg, 
        const std::string& logPath = ""
    );

    // Reseta estado (tudo, ou item específico, ou etapa específica)
    void resetCheckpoint(const std::string& itemId = "", const std::string& stageId = "");

    // Obtém snapshot JSON completo do checkpoint
    taskforge::Json getStateJson();

    // Carrega do disco
    void loadFromDisk();

    // Salva atomicamente no disco
    void saveToDisk();

    // Checa se todas as etapas de um item foram concluídas
    bool isItemFullyCompleted(const std::string& itemId, const std::vector<std::string>& allStageIds);

private:
    std::string m_stateFilePath;
    taskforge::Json m_state;
    mutable std::mutex m_mutex;

    std::string currentTimestamp() const;
};

} // namespace sandbox
} // namespace taskforge
