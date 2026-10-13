// arestrix-cli: executa grafos sem interface (servidores, CI, testes).
#include "cli/Cli.hpp"

int main(int argc, char** argv) {
    return taskforge::cli::runCli(argc, argv);
}
