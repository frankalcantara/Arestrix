#pragma once

namespace taskforge {
namespace cli {

// Modo linha de comando (sem interface grafica):
//   arestrix run <grafo.json> [--root <pasta-do-projeto>]   gera, compila e executa o grafo
//   arestrix gen <grafo.json>                               imprime o C++ gerado
// Devolve o codigo de saida do processo.
int runCli(int argc, char** argv);

} // namespace cli
} // namespace taskforge
