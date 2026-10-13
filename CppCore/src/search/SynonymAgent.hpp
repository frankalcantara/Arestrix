#pragma once

// Agente que gera o dicionario de sinonimos de um idioma com o MODELO PADRAO DO STUDIO.
//
// O modelo padrao do Studio fica so na maquina do usuario, em variaveis de ambiente (nunca no projeto):
//   ARESTRIX_DEFAULT_PROVIDER   deepseek | openai | anthropic | google | groq | jev | local
//   ARESTRIX_DEFAULT_MODEL      nome do modelo (opcional para os provedores com modelo padrao)
// As chaves seguem as variaveis de sempre (DEEPSEEK_API_KEY, OPENAI_API_KEY, ... LOCAL_LLM_BASE_URL).
// O valor "auto" (gravado pela tela de Padroes Globais) equivale a nao definir.
// Se ARESTRIX_DEFAULT_PROVIDER nao existir, usa o primeiro provedor que tiver chave configurada.

#include <optional>
#include <string>

namespace taskforge {
namespace search {

struct StudioModel {
    std::string provider;
    std::string model;
    std::string describe() const { return provider + "/" + model; }
};

// Modelo padrao do Studio segundo as variaveis de ambiente; vazio se nenhum estiver configurado.
std::optional<StudioModel> studioDefaultModel();

struct SynonymGenResult {
    // "exists" (ja havia dicionario) | "generated" | "no_model" | "failed" | "busy"
    std::string status;
    std::string locale;
    std::string model;
    size_t groups = 0;
    std::string error;
};

// Gera <synonymDir>/<locale>.json. Sem `force`, nao faz nada se o arquivo ja existir.
// Bloqueia durante a chamada ao modelo; so uma geracao por idioma por vez.
SynonymGenResult ensureSynonymsFor(const std::string& locale, bool force = false);

} // namespace search
} // namespace taskforge
