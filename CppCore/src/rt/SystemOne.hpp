#pragma once

#include <string>

namespace taskforge {
namespace rt {

// Cliente minimo do System One (Jev/TypeSafe). O Jev nao e compativel com
// /chat/completions: ele recebe um `state` e perguntas tipadas e devolve
// respostas tipadas. Este adaptador faz uma pergunta Choice e devolve um JSON
// no formato que o MemoryGate entende.
struct SystemOneResult {
    bool ok = false;
    std::string text;   // JSON: useMemory, maxSearches, reason, confidence
    std::string error;
    long long inTokens = 0;
    long long outTokens = 0;
};

SystemOneResult systemOneMemoryDecision(const std::string& model,
                                        const std::string& state,
                                        int maxSearches);

}  // namespace rt
}  // namespace taskforge
