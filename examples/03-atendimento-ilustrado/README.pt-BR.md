# Exemplo 3 — Atendimento ilustrado (um projeto de projetos)

[English](README.md) · [Todos os exemplos](../README.pt-BR.md)

O exemplo 2 e o exemplo 1 trabalhando juntos: a triagem de suporte roda como antes e, no **ramo de dúvidas (FAQ)**, a resposta
escrita é entregue ao agente de SVG, que desenha uma explicação animada em 3 passos e a manda para auditoria.

```
Receber ─► Leitor ─► Triagem ─┬─ urgente ─► Escalonador (MCP) ─► Entrega
 (exemplo 2)                  ├─ normal ──► Redator ⇄ Revisor ⇄ Qualidade ─► Entrega
                              ├─ duvida ──► FAQ ─► [ exemplo 1: Gerador ─► verificações ─► Auditor ─► Decisão ] ─► Entrega (resposta + SVG)
                              └─ revisao_humana ─► Revisão humana
```

- Os vértices do exemplo 2 têm o prefixo `atd_`, os do exemplo 1 `svg_`; a única aresta nova é **FAQ → gerador de SVG**, e o gerador
  recebe outro prompt ("explique `workspace/resposta.md` em 3 passos animados").
- O pipeline é **gerado** a partir das duas origens (`python scripts/build_examples.py`). Subfluxos aninhados ainda não são
  executados, por isso os dois fluxos são fundidos em um único grafo em vez de embutidos.
- Tudo o que vale para os exemplos 1 e 2 vale aqui: a mesma herança de modelo, o mesmo servidor MCP, os mesmos chamados em `samples/`.

**Precisa de:** um modelo + a chave de API dele, e Python. Experimente `ARESTRIX_EXAMPLE_TICKET=chamado_duvida.txt` para ver os dois
fluxos encadeados e depois `chamado_urgente.txt` para ver que o fluxo de SVG fica parado nos outros ramos.
