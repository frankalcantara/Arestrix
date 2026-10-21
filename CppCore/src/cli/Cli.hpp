#pragma once

namespace taskforge {
namespace cli {

// Modo linha de comando (sem interface grafica):
//   arestrix-cli run <grafo> [--root DIR] [--project DIR] [--json] [--timeout N] [--report FILE]
//   arestrix-cli gen <grafo>
//   arestrix-cli check [--project DIR | <grafo>] [--require-env NOME]
//   arestrix-cli env set|reset|list|check
//   arestrix-cli version
// Devolve 0 em sucesso, 1 em runtime/build, 2 em uso, 3 em configuracao, 4 em dependencia,
// 5 em preflight e 124 em timeout.
int runCli(int argc, char** argv);

} // namespace cli
} // namespace taskforge
