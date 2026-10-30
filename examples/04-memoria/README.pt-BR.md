# Exemplo 4 — Memória de projeto (proposta, aprovação e consulta)

[English](README.md) · [Todos os exemplos](../README.pt-BR.md)

Este exemplo mostra o ciclo atual da memória de projeto:

1. `Semear_Licao` propõe uma **lição**; nada é ativado sozinho.
2. Abra **Memória do Projeto**, revise e aprove a proposta pendente.
3. `Agente_Com_Memoria` recebe a lição aprovada no system prompt, filtrada por tema.
4. A resposta do agente vira um artefato em `exports/resposta.md`.
5. Na execução seguinte, a lição é carregada do disco, mesmo sem o nó de semeadura.

```
Semear_Licao (C++) ─► Agente_Com_Memoria (agente) ─► Entrega_Resposta (output)
       │                        ▲
       │                        └── memória injetada no prompt
       └── propõe uma lição para aprovação
```

| Vértice | O que faz |
| :--- | :--- |
| `Semear_Licao` | Propõe a lição `A resposta deve citar a regra aprendida` no domínio `memoria`. Aprove-a em **Memória do Projeto** antes da próxima execução. |
| `Agente_Com_Memoria` | Executa em modo `learn` com tema `memoria`; após a aprovação, a lição entra no brief. |
| `Entrega_Resposta` | Copia `workspace/resposta.txt` para `exports/resposta.md`. |

## Como executar

```bash
python scripts/copy_example.py 04-memoria C:/meus-projetos/memoria
```

Abra a pasta no Studio, escolha o modelo e pressione **F5**. Sem o Studio:

```bash
arestrix-cli run pipelines/main.json --root <a pasta copiada>
```

Rode **duas vezes**:

- na primeira, o nó `Semear_Licao` cria uma **proposta pendente de lição**;
- abra **Memória do Projeto**, revise a proposta e aprove;
- na segunda, a lição aprovada é carregada de `.arestrix/memory/memory.db` e injetada no início
  do nó de agente.

O conhecimento fica em `.arestrix/memory/memory.db`; a telemetria em
`.arestrix/telemetry/telemetry.db`. Ambos são ignorados pelo Git. A telemetria registra tokens de
entrada/saída, chamadas e tamanho do contexto de memória de cada agente.

## O que observar

- No console: `[MEMORIA] contexto injetado em Agente_Com_Memoria`.
- No prompt do agente: o bloco `[DADOS DE MEMORIA DO PROJETO ...]`, com a lição aprovada.
- Uma lição de outro tema **não** aparece no prompt.
- O artefato `exports/resposta.md` é criado pelo vértice de saída.

## Teste sem gastar nada

```bash
python scripts/e2e_memory.py
```

O teste executa dois grafos no mesmo projeto: o primeiro semeia, o segundo carrega do banco. Ele
confere a injeção no system prompt, os filtros, o consumo de API e a persistência.

## Avaliar o efeito no consumo de API

O runtime grava tokens, chamadas, retries, sucesso/falha, contexto e memórias injetadas/usadas.
No Studio, use **Padrões Globais → Memória → Modo de Medição / A-B**: rode com **Normal** e depois
com **A/B: memória OFF** + **Registrar métricas**, no mesmo grafo. Para automação:

```bash
# memória ligada, com métricas
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <projeto>

# memória OFF no mesmo grafo
ARESTRIX_MEMORY_METRICS=1 ARESTRIX_MEMORY_DISABLE=1 \
  arestrix-cli run pipelines/main.json --root <projeto>

# relatório nativo
arestrix-cli memory report --project <projeto>
```

## Limites desta fase

- A lição é registrada por um nó C++ explícito. O runtime já registra automaticamente falhas de
  agente; a lição derivada de uma falha de auditoria ainda é explícita no nó que interpreta o
  parecer.
- O brief de memória é limitado por `contextBudgetTokens` (400 neste exemplo); se necessário, o
  agente usa `memory_search` para buscar detalhes.
- O armazenamento é SQLite por projeto, com multigrafo temporal simples. Ainda não há extração
  semântica, embeddings, dedup, contradição nem playbooks; esses itens e os escopos de
  workspace/instituição entram nas próximas fases. O orçamento de contexto já é aplicado por
  `contextBudgetTokens`.

**Precisa de:** um modelo e a chave de API dele. Python não é necessário.
