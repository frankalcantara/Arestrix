# Teste real longo de memória — DeepSeek

Fixture do teste manual `scripts/e2e_memory_deepseek_long.py`.

O agente executa seis gravações, uma busca de memória e uma resposta final. O
mesmo grafo roda duas vezes:

- memória ON: recebe o brief e pode consultar a memória;
- memória OFF: roda com `ARESTRIX_MEMORY_DISABLE=1`, sem brief e sem
  `memory_search`.

O código esperado (`ORION-42`) só existe na lição semeada. O teste exige
`DEEPSEEK_API_KEY` e tem teto de tokens para evitar consumo inesperado.
