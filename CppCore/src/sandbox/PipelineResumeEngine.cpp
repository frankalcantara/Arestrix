#include "PipelineResumeEngine.hpp"
#include "util/Json.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace taskforge {
namespace sandbox {

PipelineResumeEngine::PipelineResumeEngine(const std::string& stateFilePath)
    : m_stateFilePath(stateFilePath) {
    loadFromDisk();
}

PipelineResumeEngine& PipelineResumeEngine::getInstance() {
    static PipelineResumeEngine instance("pipeline-state.json");
    return instance;
}

void PipelineResumeEngine::setStateFilePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stateFilePath = path;
    loadFromDisk();
}

std::string PipelineResumeEngine::getStateFilePath() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stateFilePath;
}

std::string PipelineResumeEngine::currentTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string PipelineResumeEngine::resolvePattern(const std::string& pattern, const std::string& itemId) {
    std::string result = pattern;
    std::string token1 = "{itemId}";
    std::string token2 = "{id}";

    size_t pos = 0;
    while ((pos = result.find(token1, pos)) != std::string::npos) {
        result.replace(pos, token1.length(), itemId);
        pos += itemId.length();
    }

    pos = 0;
    while ((pos = result.find(token2, pos)) != std::string::npos) {
        result.replace(pos, token2.length(), itemId);
        pos += itemId.length();
    }

    return result;
}

bool PipelineResumeEngine::validateArtifact(const std::string& resolvedPath, size_t minBytes, const std::string& regexPattern) {
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return false;
    }

    if (minBytes > 0) {
        auto size = std::filesystem::file_size(resolvedPath, ec);
        if (ec || size < minBytes) {
            return false;
        }
    }

    if (!regexPattern.empty()) {
        std::ifstream file(resolvedPath);
        if (!file.is_open()) {
            return false;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        try {
            std::regex reg(regexPattern);
            if (!std::regex_search(content, reg)) {
                return false;
            }
        } catch (...) {
            return false;
        }
    }

    return true;
}

bool PipelineResumeEngine::isStageCompleted(
    const std::string& itemId, 
    const std::string& stageId, 
    const ArtifactValidationRule& rule, 
    const std::string& logFilePath, 
    bool force
) {
    if (force) return false;

    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. Verifica no estado persistido
    if (m_state.contains("items") && 
        m_state["items"].contains(itemId) && 
        m_state["items"][itemId].contains("stages") &&
        m_state["items"][itemId]["stages"].contains(stageId)) {
        
        std::string st = m_state["items"][itemId]["stages"][stageId].value("status", "");
        if (st == "concluido") {
            // Se houver regra de artefato, confirma integridade física
            if (!rule.targetPathPattern.empty()) {
                std::string resolved = resolvePattern(rule.targetPathPattern, itemId);
                if (validateArtifact(resolved, rule.minSizeBytes, rule.contentPattern)) {
                    return true;
                }
            } else {
                return true;
            }
        }
    }

    // 2. Auto-recuperação física: verifica artefato no disco
    if (!rule.targetPathPattern.empty()) {
        std::string resolved = resolvePattern(rule.targetPathPattern, itemId);
        if (validateArtifact(resolved, rule.minSizeBytes, rule.contentPattern)) {
            // Artefato existe e é válido: auto-recupera o estado
            if (!m_state.contains("items")) m_state["items"] = taskforge::Json::object();
            if (!m_state["items"].contains(itemId)) m_state["items"][itemId] = {{"stages", taskforge::Json::object()}};
            if (!m_state["items"][itemId].contains("stages")) m_state["items"][itemId]["stages"] = taskforge::Json::object();

            m_state["items"][itemId]["stages"][stageId] = {
                {"status", "concluido"},
                {"completedAt", currentTimestamp()},
                {"artifactPath", resolved},
                {"recoveredFromArtifact", true}
            };
            m_state["lastUpdated"] = currentTimestamp();
            saveToDisk();
            return true;
        }
    }

    // 3. Auto-recuperação por log de saída (EXIT CODE: 0)
    if (rule.checkLogExitCode && !logFilePath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(logFilePath, ec)) {
            std::ifstream lf(logFilePath);
            if (lf.is_open()) {
                std::stringstream buffer;
                buffer << lf.rdbuf();
                std::string logContent = buffer.str();
                if (logContent.find("EXIT CODE: 0") != std::string::npos) {
                    if (!m_state.contains("items")) m_state["items"] = taskforge::Json::object();
                    if (!m_state["items"].contains(itemId)) m_state["items"][itemId] = {{"stages", taskforge::Json::object()}};
                    if (!m_state["items"][itemId].contains("stages")) m_state["items"][itemId]["stages"] = taskforge::Json::object();

                    m_state["items"][itemId]["stages"][stageId] = {
                        {"status", "concluido"},
                        {"completedAt", currentTimestamp()},
                        {"logFile", logFilePath},
                        {"recoveredFromArtifact", true}
                    };
                    m_state["lastUpdated"] = currentTimestamp();
                    saveToDisk();
                    return true;
                }
            }
        }
    }

    return false;
}

void PipelineResumeEngine::markStageStarted(const std::string& itemId, const std::string& stageId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.contains("items")) m_state["items"] = taskforge::Json::object();
    if (!m_state["items"].contains(itemId)) {
        m_state["items"][itemId] = {
            {"itemId", itemId},
            {"status", "em_progresso"},
            {"stages", taskforge::Json::object()}
        };
    } else {
        m_state["items"][itemId]["status"] = "em_progresso";
    }

    m_state["items"][itemId]["stages"][stageId] = {
        {"status", "executando"},
        {"startedAt", currentTimestamp()}
    };
    m_state["lastUpdated"] = currentTimestamp();
    saveToDisk();
}

void PipelineResumeEngine::markStageSuccess(
    const std::string& itemId, 
    const std::string& stageId, 
    const std::string& artifactPath, 
    const std::string& logPath
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.contains("items")) m_state["items"] = taskforge::Json::object();
    if (!m_state["items"].contains(itemId)) {
        m_state["items"][itemId] = {
            {"itemId", itemId},
            {"status", "em_progresso"},
            {"stages", taskforge::Json::object()}
        };
    }

    m_state["items"][itemId]["stages"][stageId] = {
        {"status", "concluido"},
        {"completedAt", currentTimestamp()},
        {"artifactPath", artifactPath},
        {"logFile", logPath},
        {"recoveredFromArtifact", false}
    };
    m_state["lastUpdated"] = currentTimestamp();
    saveToDisk();
}

void PipelineResumeEngine::markStageFailed(
    const std::string& itemId, 
    const std::string& stageId, 
    const std::string& errorMsg, 
    const std::string& logPath
) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.contains("items")) m_state["items"] = taskforge::Json::object();
    if (!m_state["items"].contains(itemId)) {
        m_state["items"][itemId] = {
            {"itemId", itemId},
            {"status", "erro"},
            {"stages", taskforge::Json::object()}
        };
    } else {
        m_state["items"][itemId]["status"] = "erro";
    }

    m_state["items"][itemId]["stages"][stageId] = {
        {"status", "erro"},
        {"errorMessage", errorMsg},
        {"logFile", logPath},
        {"failedAt", currentTimestamp()}
    };
    m_state["lastUpdated"] = currentTimestamp();
    saveToDisk();
}

void PipelineResumeEngine::resetCheckpoint(const std::string& itemId, const std::string& stageId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (itemId.empty()) {
        m_state = {
            {"pipelineId", "arestrix-batch"},
            {"lastUpdated", currentTimestamp()},
            {"items", taskforge::Json::object()}
        };
    } else if (stageId.empty()) {
        if (m_state.contains("items") && m_state["items"].contains(itemId)) {
            m_state["items"].erase(itemId);
        }
    } else {
        if (m_state.contains("items") && 
            m_state["items"].contains(itemId) && 
            m_state["items"][itemId].contains("stages") &&
            m_state["items"][itemId]["stages"].contains(stageId)) {
            m_state["items"][itemId]["stages"].erase(stageId);
        }
    }
    m_state["lastUpdated"] = currentTimestamp();
    saveToDisk();
}

taskforge::Json PipelineResumeEngine::getStateJson() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

void PipelineResumeEngine::loadFromDisk() {
    std::error_code ec;
    if (std::filesystem::exists(m_stateFilePath, ec)) {
        try {
            std::ifstream file(m_stateFilePath);
            if (file.is_open()) {
                m_state = taskforge::jsonutil::parseStream(file);
                return;
            }
        } catch (...) {
            // Em caso de falha de leitura, recria estrutura
        }
    }

    m_state = {
        {"pipelineId", "arestrix-batch"},
        {"lastUpdated", currentTimestamp()},
        {"items", taskforge::Json::object()}
    };
}

void PipelineResumeEngine::saveToDisk() {
    try {
        std::filesystem::path p(m_stateFilePath);
        if (p.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
        }

        std::string tmpPath = m_stateFilePath + ".tmp";
        std::ofstream file(tmpPath);
        if (file.is_open()) {
            file << m_state.dump(2);
            file.close();

            std::error_code ec;
            if (std::filesystem::exists(m_stateFilePath, ec)) {
                std::filesystem::remove(m_stateFilePath, ec);
            }
            std::filesystem::rename(tmpPath, m_stateFilePath, ec);
        }
    } catch (...) {}
}

bool PipelineResumeEngine::isItemFullyCompleted(const std::string& itemId, const std::vector<std::string>& allStageIds) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.contains("items") || !m_state["items"].contains(itemId)) {
        return false;
    }

    const auto& item = m_state["items"][itemId];
    if (!item.contains("stages")) return false;

    for (const auto& stageId : allStageIds) {
        if (!item["stages"].contains(stageId)) return false;
        if (item["stages"][stageId].value("status", "") != "concluido") return false;
    }

    return true;
}

} // namespace sandbox
} // namespace taskforge
