#include "rt/ArtifactValidation.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "project/RunnerSecurity.hpp"
#include "sandbox/PipelineResumeEngine.hpp"

namespace taskforge {
namespace rt {

namespace {

namespace fs = std::filesystem;

bool readFileBytes(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

// Le o arquivo inteiro para a checagem de padrao.
bool readTextFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

} // namespace

ArtifactValidationResult validateArtifacts(const taskforge::Json& rules,
                                           const std::string& itemId) {
    ArtifactValidationResult result;
    if (!rules.is_array() || rules.empty()) return result;
    result.hasRules = true;
    result.rulesTotal = static_cast<int>(rules.size());

    // O Json usa std::map para objetos, entao o dump compacto e canonico:
    // a mesma regra escrita com chaves em ordem diferente produz o mesmo hash.
    result.validatorVersion =
        project::RunnerSecurity::sha256(rules.dump());

    std::set<std::string> artifactPaths;

    for (const auto& rule : rules) {
        if (!rule.is_object()) {
            result.reasons.push_back("regra invalida (nao e objeto)");
            continue;
        }
        const std::string pattern = rule.value("targetPathPattern", std::string());
        const long long rawMin = rule.value("minSizeBytes", 0LL);
        const size_t minBytes = rawMin > 0 ? static_cast<size_t>(rawMin) : 0;
        const std::string contentPattern = rule.value("contentPattern", std::string());

        if (pattern.empty()) {
            result.reasons.push_back("regra sem targetPathPattern");
            continue;
        }
        const std::string resolved =
            sandbox::PipelineResumeEngine::resolvePattern(pattern, itemId);
        result.resolvedPaths.push_back(resolved);

        std::error_code ec;
        const bool exists = fs::is_regular_file(resolved, ec);
        if (!exists) {
            result.reasons.push_back("arquivo ausente: " + resolved);
            continue;
        }
        artifactPaths.insert(resolved);

        // Reaproveita a checagem do resume engine e, se falhar, diagnostica
        // qual condicao quebrou para o veredito carregar motivos uteis.
        if (!sandbox::PipelineResumeEngine::validateArtifact(resolved, minBytes, contentPattern)) {
            if (minBytes > 0) {
                const auto size = fs::file_size(resolved, ec);
                if (ec || size < minBytes) {
                    result.reasons.push_back("arquivo " + resolved + " com " +
                                             (ec ? std::string("tamanho ilegivel")
                                                 : std::to_string(size)) +
                                             " bytes; minimo " + std::to_string(minBytes));
                    continue;
                }
            }
            if (!contentPattern.empty()) {
                std::string content;
                if (!readTextFile(resolved, content)) {
                    result.reasons.push_back("arquivo ilegivel: " + resolved);
                    continue;
                }
                try {
                    const std::regex regex(contentPattern);
                    if (!std::regex_search(content, regex)) {
                        result.reasons.push_back("padrao nao encontrado em " + resolved +
                                                 ": " + contentPattern);
                        continue;
                    }
                } catch (const std::exception&) {
                    result.reasons.push_back("padrao invalido: " + contentPattern);
                    continue;
                }
            }
            result.reasons.push_back("regra reprovada: " + resolved);
            continue;
        }
        ++result.rulesPassed;
    }

    result.ok = result.rulesTotal > 0 && result.rulesPassed == result.rulesTotal;
    result.score = result.rulesTotal > 0
        ? static_cast<double>(result.rulesPassed) / static_cast<double>(result.rulesTotal)
        : 0.0;

    // Hash do conteudo dos artefatos existentes, em ordem de caminho. Para um
    // unico arquivo, equivale ao SHA-256 do conteudo.
    std::string concatenated;
    bool any = false;
    for (const auto& path : artifactPaths) {
        std::string bytes;
        if (!readFileBytes(path, bytes)) continue;
        concatenated += bytes;
        any = true;
    }
    if (any) result.artifactHash = project::RunnerSecurity::sha256(concatenated);

    taskforge::Json reasons = taskforge::Json::array();
    for (const auto& reason : result.reasons) reasons.push_back(reason);
    result.reasonsJson = reasons.dump();
    return result;
}

} // namespace rt
} // namespace taskforge
