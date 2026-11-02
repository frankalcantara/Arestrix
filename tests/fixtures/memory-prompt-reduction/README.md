# Teste de redução de prompt — memória como substituta de contexto

Este fixture é usado por `scripts/e2e_memory_prompt_reduction.py`.

O script gera dois grafos com a mesma pergunta:

- **full**: prompt contém um manual operacional longo, com o código `ORION-42`,
  e roda com memória desligada;
- **memory**: prompt curto, memória ligada, lição com o mesmo código.

A qualidade é considerada igual quando os dois braços gravam `ORION-42` em
`artefatos/final.txt`. O teste compara tokens de prompt e exige redução mínima de
30% no braço com memória.

Execução registrada: 24.718 tokens de prompt no braço full e 3.228 no braço
memory — 87% de redução com a mesma resposta.

Exige `DEEPSEEK_API_KEY`, não roda no CI normal e tem teto de tokens.
