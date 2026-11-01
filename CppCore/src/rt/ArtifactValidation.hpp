#pragma once

#include <string>
#include <vector>

#include "util/Json.hpp"

namespace taskforge {
namespace rt {

// Resultado da avaliacao independente das regras de artefato de um vertice.
// `hasRules` separa "vertice sem regras" (sem veredito) de "regras avaliadas
// e reprovadas". O score e a fracao de regras aprovadas.
struct ArtifactValidationResult {
    bool hasRules = false;
    bool ok = false;
    double score = 0.0;
    int rulesTotal = 0;
    int rulesPassed = 0;
    std::string validatorId = "artifact-rules";
    std::string validatorVersion;
    std::string artifactHash;
    std::string reasonsJson = "[]";
    std::vector<std::string> reasons;
    std::vector<std::string> resolvedPaths;
};

// Avalia as regras no formato de IArtifactValidationRule:
//   { "targetPathPattern": "artefatos/{itemId}.md",
//     "minSizeBytes": 400, "contentPattern": "STATUS_OK" }
// Resolve {itemId}/{id}, confere existencia, tamanho e padrao, e devolve o
// veredito com a versao canonica das regras e o hash dos artefatos lidos.
// Nunca grava nada e nunca chama modelo de linguagem.
ArtifactValidationResult validateArtifacts(const taskforge::Json& rules,
                                           const std::string& itemId);

} // namespace rt
} // namespace taskforge
