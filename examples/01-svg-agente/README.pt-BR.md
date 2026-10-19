# Exemplo 1 — Agente de SVG (o tutorial)

[English](README.md) · [Todos os exemplos](../README.pt-BR.md)

O projeto do [tutorial do agente SVG](../../docs/pt-BR/tutorials/svg-agent.md): um agente que cria um SVG animado e
outro que o audita.

```
Gerador (agente) ─┬─► Linter de tipografia ─┐
                  └─► Auditor de timeline SMIL ┴─► Join ─► Auditor (agente) ─► Decisão (C++) ─┬─► Entrega
        ▲                                                                                     │
        └──────────────────────────── laço de feedback (reprovado) ◄─────────────────────────┘
```

| Vértice | Tipo | O que faz |
| :--- | :--- | :--- |
| Agente_Gerador | agente | Grava `workspace/scene.svg` seguindo `skills/gerador.md`. |
| Linter / Auditor SMIL | estático (C++) | Duas verificações que rodam **em paralelo**. |
| Consolidador | estático | Junta os dois relatórios. |
| Agente_Auditor | agente | Lê os relatórios e o SVG e grava `workspace/audit_decision.txt` (`skills/jev.md`). |
| Decisor_Qualidade | condição (C++) | `STATUS: APROVADO` → entrega; qualquer outra coisa → volta ao gerador. |

**Precisa de:** um modelo e a chave de API dele (veja [como executar](../README.pt-BR.md#executar-um-exemplo)). Os dois agentes
herdam o modelo do projeto (DeepSeek `deepseek-flash` por padrão). Para usar outro modelo no auditor, abra o vértice, pressione
**Modelo**, desmarque *herdar* e escolha outro provedor (o tutorial usava um modelo de avaliação visual nessa etapa).

**Traga seus próprios agentes:** edite `skills/gerador.md` e `skills/jev.md`, ou mude o C++ das duas verificações. Cada agente
roda no máximo 10 vezes (`ARESTRIX_MAX_AGENT_RUNS`), então um laço que nunca converge para em vez de gastar sem limite.
