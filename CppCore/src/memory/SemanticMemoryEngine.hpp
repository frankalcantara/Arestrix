#pragma once

// LEGADO (Fase 0): mantido apenas para compatibilidade de API e leitura de dados
// antigos. O caminho ativo usa MemoryService/MemoryStore (SQLite AMG).
// Nao adicione novos usos deste motor.

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct AgentLesson {
    std::string id;
    std::string projectId;      // Escopo do projeto para isolamento estrito
    std::string domain;         // ex: "svg_typography", "validation", "markdown"
    std::string triggerError;   // Erro que disparou a reflexão
    std::string lessonRule;     // Regra formulada para prevenção
    std::string sourceItem;     // Item gerador (ex: "ART-001")
    std::string createdAt;      // Timestamp
};

class SemanticMemoryEngine {
public:
    static SemanticMemoryEngine& getInstance();

    // Controle de Persistência por Projeto
    void setPersistence(const std::string& projectId, bool persistent);
    bool isPersistent(const std::string& projectId) const;

    // Registra uma nova lição aprendida associada ao projeto
    std::string recordLesson(
        const std::string& projectId,
        const std::string& domain,
        const std::string& triggerError,
        const std::string& lessonRule,
        const std::string& sourceItem = ""
    );

    // Consulta lições aprendidas de um projeto por domínio
    std::vector<AgentLesson> getLessons(const std::string& projectId, const std::string& domain = "") const;

    // Gera bloco contextual pronto para ser injetado no system prompt de um agente do projeto
    std::string formatPromptContext(const std::string& projectId, const std::vector<std::string>& domains = {}) const;

    // Limpa todas as lições de um projeto
    void clearLessons(const std::string& projectId);

    // Limpa apenas lições de sessão se o projeto for efêmero (!persistent)
    void clearSession(const std::string& projectId);

    // Salva atomicamente no disco (apenas se isPersistent(projectId) == true)
    bool saveToDisk(const std::string& projectId);

    // Carrega do disco se existir
    bool loadFromDisk(const std::string& projectId);

    // Sobrecargas de compatibilidade (escopo "default")
    std::string recordLesson(
        const std::string& domain,
        const std::string& triggerError,
        const std::string& lessonRule,
        const std::string& sourceItem = ""
    ) {
        return recordLesson("default", domain, triggerError, lessonRule, sourceItem);
    }

    std::vector<AgentLesson> getLessons(const std::string& domain = "") const {
        return getLessons("default", domain);
    }

    std::string formatPromptContext(const std::vector<std::string>& domains = {}) const {
        return formatPromptContext("default", domains);
    }

    void clearLessons() {
        clearLessons("default");
    }

    bool saveToDisk() {
        return saveToDisk("default");
    }

    bool loadFromDisk() {
        return loadFromDisk("default");
    }

    // Exporta estado completo em JSON
    taskforge::Json toJson(const std::string& projectId = "") const;

    // Importa estado a partir de JSON
    void fromJson(const taskforge::Json& j, const std::string& projectId = "default");

private:
    SemanticMemoryEngine();
    ~SemanticMemoryEngine() = default;
    SemanticMemoryEngine(const SemanticMemoryEngine&) = delete;
    SemanticMemoryEngine& operator=(const SemanticMemoryEngine&) = delete;

    std::string getDefaultStoragePath(const std::string& projectId) const;
    std::string getIsoTimestamp() const;

    mutable std::mutex mutex_;
    std::vector<AgentLesson> lessons_;
    // projectId -> persistent flag (default: false, memória efêmera / não obrigatória)
    std::unordered_map<std::string, bool> persistenceMap_;
};

} // namespace memory
} // namespace taskforge
