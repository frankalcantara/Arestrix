# Exemplo 4 — Memória de projeto (lições e Blackboard)

[English](README.md) · [Todos os exemplos](../README.pt-BR.md)

Este exemplo mostra a memória de projeto funcionando de ponta a ponta:

1. `Semear_Licao` grava uma **lição semântica** e um valor no **Blackboard**.
2. `Agente_Com_Memoria` recebe os dois no system prompt, com filtros de domínio e de chaves.
3. A resposta do agente vira um artefato em `exports/resposta.md`.
4. Na execução seguinte, a lição e o Blackboard são carregados do disco, mesmo sem o nó de semeadura.

```
Semear_Licao (C++) ─► Agente_Com_Memoria (agente) ─► Entrega_Resposta (output)
       │                        ▲
       │                        └── memória injetada no prompt
       └── grava lição + resumo no Blackboard
```

| Vértice | O que faz |
| :--- | :--- |
| `Semear_Licao` | Registra a lição `A resposta deve citar a regra aprendida` no domínio `memoria` e grava `resumo` no Blackboard. |
| `Agente_Com_Memoria` | Lê `semanticLessons` e `readBlackboard`, com `domains: ["memoria"]` e `allowedKeys: ["resumo"]`. |
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

- na primeira, o nó `Semear_Licao` cria a lição e o Blackboard;
- na segunda, a lição e o resumo são carregados de `.arestrix/memory/memory.db` e injetados
  novamente no agente. O nó de semeadura deduplica a lição, mas a carga acontece antes de o grafo
  começar.

O banco fica em `.arestrix/memory/memory.db` e é ignorado pelo Git. A tabela `memory_usage`
registra tokens de entrada/saída, chamadas e tamanho do contexto de memória de cada agente.

## O que observar

- No console: `[MEMORIA] contexto injetado em Agente_Com_Memoria`.
- No prompt do agente: o bloco `[DADOS DE MEMORIA DO PROJETO ...]`, com a lição e o `resumo`.
- A lição de outro domínio e a chave não autorizada **não** aparecem no prompt.
- O artefato `exports/resposta.md` é criado pelo vértice de saída.

## Teste sem gastar nada

```bash
python scripts/e2e_memory.py
```

O teste executa dois grafos no mesmo projeto: o primeiro semeia, o segundo carrega do banco. Ele
confere a injeção no system prompt, os filtros, o consumo de API e a persistência.

## Avaliar o efeito no consumo de API

O runtime grava tokens, chamadas, latência e tamanho do contexto de memória em `memory_usage`.
Para comparar com e sem memória:

```bash
# memória desligada, mas com métricas
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <projeto>

# memória ligada (configuração do exemplo)
arestrix-cli run pipelines/main.json --root <projeto>

python scripts/memory_report.py <projeto>
```

## Limites desta fase

- A lição é registrada por um nó C++ explícito. O runtime já registra automaticamente falhas de
  agente; a lição derivada de uma falha de auditoria ainda é explícita no nó que interpreta o
  parecer.
- O brief de memória é limitado por `contextBudgetTokens` (400 neste exemplo); se necessário, o
  agente usa `memory_search` para buscar detalhes.
- O armazenamento é SQLite por projeto, com multigrafo temporal simples. Os escopos de
  workspace/instituição e a aprendizagem recursiva entram nas próximas fases; o orçamento de
  contexto já é aplicado por `contextBudgetTokens`.

**Precisa de:** um modelo e a chave de API dele. Python não é necessário.
