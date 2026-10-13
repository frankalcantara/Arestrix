#pragma once

// Dicionario de sinonimos da busca no projeto, por idioma.
//
// Alem dos grupos embutidos (pt/en), cada idioma pode ter um arquivo gerado por um agente com o
// modelo padrao do Studio (veja SynonymAgent.hpp). Os arquivos ficam na maquina do usuario, fora
// dos projetos:  <config do usuario>/arestrix/synonyms/<locale>.json
//
//   { "version": 1, "locale": "es-ES", "model": "deepseek/deepseek-flash", "generatedAt": "...",
//     "groups": [["queue", "cola", "fila"], ...] }
//
// Cada grupo e um conjunto de palavras que a busca trata como equivalentes (peso menor que o termo
// digitado). Todo grupo traz o termo de referencia em ingles, o que liga os idiomas entre si.

#include <string>
#include <vector>

namespace taskforge {
namespace search {

using SynonymGroups = std::vector<std::vector<std::string>>;

// Pasta dos dicionarios gerados.
std::string synonymDir();
// So para testes: redireciona a pasta (vazio restaura o padrao).
void setSynonymDirForTesting(const std::string& dir);

bool hasSynonymFile(const std::string& locale);

// Conceitos de referencia (em ingles) que o agente traduz e expande para o idioma.
const std::vector<std::string>& synonymSeedConcepts();

// Prompt de sistema e de usuario enviados ao modelo.
std::string synonymSystemPrompt();
std::string synonymUserPrompt(const std::string& locale);

// Extrai e valida os grupos da resposta do modelo (aceita cerca ```json e texto ao redor).
// Descarta o que nao for palavra simples; limita o tamanho. Retorna vazio se nada aproveitavel.
SynonymGroups parseSynonymResponse(const std::string& modelText);

// Grava <synonymDir>/<locale>.json de forma atomica.
bool writeSynonymFile(const std::string& locale, const std::string& modelName, const SynonymGroups& groups,
                      std::string* error = nullptr);

// Grupos de todos os arquivos instalados, ja normalizados (sem acento, com stem) para casar com o indice.
// Faz cache; `reloadSynonyms()` invalida (chamado ao gravar um arquivo).
SynonymGroups installedSynonymGroups();
void reloadSynonyms();

} // namespace search
} // namespace taskforge
