#pragma once

// Decisao de caminho de um vertice "switch" sem escrever codigo:
//   * "rules": o primeiro caso cujas palavras aparecem no texto;
//   * "model": um modelo de IA escolhe um dos rotulos dos casos (veja decideCase em Runtime.hpp).
// O ultimo caso e sempre o "default": ele e usado quando nada casa.

#include <string>
#include <vector>

namespace taskforge {
namespace rt {

// Primeiro caso (na ordem) com alguma palavra/frase presente no texto, ignorando acentos, caixa e
// pontuacao, e respeitando limites de palavra. Devolve `defaultIndex` se nenhum casar.
int pickByKeywords(const std::string& text, const std::vector<std::vector<std::string>>& rules, int defaultIndex);

// Interpreta a resposta de um modelo: igualdade exata com um rotulo, ou o rotulo mais longo contido na
// resposta. Devolve `defaultIndex` se a resposta nao for reconhecida.
int matchLabel(const std::string& reply, const std::vector<std::string>& labels, int defaultIndex);

// Prompts enviados ao modelo (expostos para teste).
std::string decisionSystemPrompt();
std::string decisionUserPrompt(const std::string& instruction, const std::vector<std::string>& labels,
                               const std::string& text);

} // namespace rt
} // namespace taskforge
