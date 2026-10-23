#pragma once

// Saida do Arestrix Studio no Windows.
//
// O Studio e um aplicativo de console (o FastWebUI/WebUI usa o console do processo), mas
// nao deve mostrar uma janela de terminal quando aberto por duplo clique:
//   * se foi iniciado a partir de um terminal (inclusive CppCore/dev.py), mantem o console
//     visivel e espelha stdout/stderr no terminal e no arquivo de log;
//   * se o console pertence somente ao Studio (duplo clique/atalho), oculta a janela e
//     espelha a saida apenas no arquivo de log.
//
// O log fica em %LOCALAPPDATA%\Arestrix\logs\studio.log, com rotacao simples em 5 MB.
// Em outros sistemas a funcao e um no-op: o Studio continua escrevendo no terminal.

#include <filesystem>

namespace taskforge {
namespace platform {

// Caminho do log do Studio (mesmo quando o arquivo ainda nao existe).
std::filesystem::path studioLogPath();

// Configura console e log. Deve ser chamada no inicio de main(), antes de qualquer saida.
void setupStudioConsole();

} // namespace platform
} // namespace taskforge
