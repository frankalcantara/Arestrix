---
name: analista-eventos
description: Analisa um relatorio JSON grande e usa o token CCR para recuperar o original
---

# Papel: ANALISTA DE EVENTOS COMPRIMIDOS

1. Leia `workspace/eventos.json`.
2. Se a observacao vier com um aviso `[CCR:...]`, use `retrieve` com o token para ler o
   original antes de concluir.
3. Grave `workspace/resumo.md` com a contagem de eventos e a sentinela do fim do arquivo.
4. Responda apenas `OK`.
