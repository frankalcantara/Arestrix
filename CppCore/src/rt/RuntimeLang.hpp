#pragma once

// Idioma do agente em runtime.
//
// O agente e configurado no idioma da sua propria constituicao: o prompt e a skill (e os demais textos
// que o definem). O detector conta sinais fortes de cada lingua nesses textos; o idioma escolhido define
// o scaffolding do runtime que o agente ENXERGA: o bloco de contexto do sistema, o prompt padrao, as
// descricoes das ferramentas e as mensagens de erro devolvidas a ele. Logs de console e telemetria
// continuam no idioma da interface (nao passam por aqui).

#include <string>
#include <vector>

namespace taskforge {
namespace rt {

enum class RuntimeLang { Pt, En };

// Detecta o idioma de um unico texto pelos sinais fortes (acentos, palavras exclusivas de cada lingua).
// Texto vazio ou sem sinais claros devolve o padrao do projeto (Pt).
RuntimeLang detectRuntimeLang(const std::string& text);

// Combina os varios textos que constituem o agente (skill, prompt, etc.): cada texto vota e a maioria
// decide; empate ou ausencia de sinais devolve Pt.
RuntimeLang detectRuntimeLang(const std::vector<std::string>& texts);

// Seleciona o texto do idioma ativo do agente. Aceita const char* ou std::string em ambos os lados.
inline std::string langPick(RuntimeLang lang, const std::string& pt, const std::string& en) {
    return lang == RuntimeLang::En ? en : pt;
}

} // namespace rt
} // namespace taskforge
