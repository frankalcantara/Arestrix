---
name: "Auditoria de Código C++20 & Taskflow"
description: "Revisão e endurecimento de algoritmos concorrentes em C++20"
license: "MIT"
tools:
  - read
  - write
  - edit
---

# Auditoria de Código C++20 & Taskflow

Especialização em segurança de memória, paralelismo massivo e compilação limpa com MSVC / Clang.

## Pontos Críticos de Checagem
- Ciclos em grafos Taskflow: verificar se há branches com retorno sem nó de condição intermediário.
- Alocações dinâmicas excessivas dentro de loops críticos concorrentes.
- Tratamento explícito de exceções dentro de lambdas paralelas.