# MemoryGate — desenho e A/B

> **Status:** implementado com `off`, `shadow` e `enforce`; piloto A/B de 1 repetição
> concluído em 2026-11-11 com DeepSeek e com Jev, e confirmação de 3 repetições
> concluída em 2026-11-12 com Jev.
> **Escopo:** porteiro de política da memória (`memory.gate`) e sua medição nas suítes
> de paridade e de valor do A/B v2.
> **Referências:** `docs/pt-BR/como-funciona-a-memoria.md`, `docs/PENDENCIAS.md` §4.1.

> **Versão consolidada:** [Resultados dos testes de memória](../pt-BR/resultados-testes-memoria.md) ·
> [English](../en/memory-test-results.md).

## Veredito

O gate com modelo reduz a paridade de **2,69× para 1,48× tokens** e de **1,60× para
1,08× latência** com Jev em 3 repetições, mantendo **1,000** de aprovação. No valor,
**1,58× tokens** e **1,09× latência**, com **1,000 × 0,000** de aprovação. Com
DeepSeek os números do piloto de 1 repetição ficam em 1,34×/1,16× e 1,47×/1,19×.
O gate determinístico fica em 1,79×/1,25×. O `shadow` registra sem alterar. O Jev
responde em ~270–420 ms; o DeepSeek em ~1,3 s.

## 1. Hipótese

O custo do braço ON no A/B vinha de `memory_search` que o agente disparava mesmo
quando o brief já bastava. Um porteiro de política, aplicado antes do loop, deve
reduzir esse custo sem perder o acerto nas tarefas que dependem da memória.

O gate não é outro agente de raciocínio: ele devolve uma decisão tipada
(`none`, `brief`, `search`, `learn`) e só escolhe entre comportamentos de memória
já aprovados. A decisão é determinística por padrão e pode ser refinada por um
modelo configurável (Jev, local ou API).

## 2. Modos

| Modo | Efeito |
| :--- | :--- |
| `off` | Comportamento atual; sem custo e sem passo no trace. |
| `shadow` | Decide e registra no trace; não altera brief nem ferramentas. |
| `enforce` | Aplica `none`/`brief`/`search`/`learn` ao brief e às ferramentas. |

`none` remove o brief e todas as ferramentas de memória; `brief` mantém o brief
e remove todas as ferramentas; `search` mantém o brief e `memory_search` com
teto; `learn` preserva propostas de lição.

Na UI do projeto, os três cenários mapeiam para: **Consultar** = `consult` +
`gate.mode=brief`; **Consultar e Decidir** = `consult` + `gate.mode=enforce` com
Jev; **Aprender com aprovação** = `learn` + `gate.mode=enforce`. Qualquer campo
avançado fora da assinatura do preset deriva o estado **Personalizado**.

## 3. Provedor de decisão

```json
"memory": {
  "gate": {
    "mode": "enforce",
    "provider": "jev",
    "model": "jev-latest",
    "maxSearches": 2,
    "minConfidence": 0.6,
    "fallback": "brief",
    "maxBriefChars": 1200
  }
}
```

- `provider`/`model` vazios: decisão determinística (sobreposição lexical e
  pistas explícitas como lição, memória, regra, estado, conformidade).
- Preenchidos: o runtime injeta a chamada de modelo; trocar Jev por um modelo
  local ou de API é apenas configuração.
- `memory.localOnly` pula o modelo a menos que `provider` seja `local`.
- `provider: "jev"` usa o adaptador nativo **System One**: `POST
  {JEV_BASE_URL}/v1/systemone` com uma pergunta `choice` (`none`, `brief`,
  `search`, `learn`). O padrão é `https://api.typesafe.ai`, o modelo padrão é
  `jev-latest` e a chave vem de `JEV_API_KEY`. O Jev real **não** é compatível
  com `/chat/completions`; o `provider: "jev"` de um vértice de agente continua
  sendo o caminho OpenAI-compatible para um servidor auto-hospedado, separado
  deste adaptador.
- Para um modelo local, use `provider: "local"` e o endpoint configurado no
  ambiente.
- Saída inválida, erro ou timeout caem na decisão determinística, com
  `source=fallback`.
- Se o validador exige um valor presente no brief, `none` é promovido a `brief`
  (`validator_requires_brief`). O gate nunca pode reprovar uma tarefa
  descartando a única fonte do valor exigido.
- Decisões são cacheadas por processo com a chave
  `(projeto, nó, task_type, input, brief, config)`; o trace marca `cached`.

O passo `memory_gate` no trace registra modo, decisão, motivo, confiança,
latência, origem, cache, provider/modelo e tokens do modelo do gate.

## 4. Desenho do A/B

- Suíte de **paridade**: o validador não depende da memória; mede custo e
  regressão de aprovação.
- Suíte de **valor**: o validador exige o token `AB-7731`, que só existe na lição
  curada; mede se o gate preserva o acerto.
- Braços: `off` (referência), `shadow` (auditoria) e `enforce` determinístico ou
  com modelo.
- Mesmo banco congelado, mesmas 20 tarefas, mesmo seed e mesma ordem do A/B v2.

Comandos:

```powershell
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --gate-mode shadow
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --gate-mode enforce
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --gate-mode enforce --gate-provider deepseek --gate-model deepseek-chat
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent --gate-mode enforce --gate-provider deepseek --gate-model deepseek-chat
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --gate-mode enforce --gate-provider jev --gate-model jev-latest
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent --gate-mode enforce --gate-provider jev --gate-model jev-latest
```

O A/B rodou primeiro com DeepSeek e depois com **Jev** (`jev-latest`) usando o
adaptador System One. `JEV_API_KEY` está configurada; `JEV_BASE_URL` usa o
padrão `https://api.typesafe.ai`. O caminho local continua sendo configuração,
sem mudança de código.

## 5. Piloto histórico (1 repetição)

| Cenário | Aprovação ON | ON tokens | OFF tokens | ON/OFF | Latência ON/OFF |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Baseline (sem gate) | 1,000 | 140.599 | 52.223 | 2,69× | 11,57/7,25 s |
| Gate determinístico | 1,000 | 86.126 | 47.992 | 1,79× | 8,09/6,49 s |
| Gate + modelo | 1,000 | 58.863 + 8.853 | 50.495 | 1,34× | 7,72/6,67 s |
| Gate + modelo, valor | **1,000** | 66.144 + 9.729 | 51.611 | 1,47× | 7,84/6,59 s |

Os tokens do ON somam o agente e o modelo do gate; a latência também inclui a
chamada do gate (~1,3 s). Só o agente, sem o gate, ficaria em 1,17× tokens e
0,96× latência na paridade, e 1,28× e 0,99× no valor. O gate é barato, mas não
é gratuito: ~9 mil tokens e ~1,3 s por execução no piloto.

O gate determinístico já removeu as ferramentas de memória de 10/20 tarefas de
paridade. Com modelo, as decisões da última execução foram 17 `brief`, 2 `none`
e 1 `search` na paridade, e 19 `brief` + 1 `search` no valor; as tarefas ficaram
em 2 turnos. O modelo do gate consumiu 8.035 tokens de entrada e 818 de saída na
paridade (8.935 + 794 no valor), já contabilizados no trace.

Na suíte de valor, o gate manteve 100% de acerto e o custo ficou próximo do OFF.
O caso `fail-fonte` mostrou por que o piso de validação é necessário: o modelo
decidiu `none` porque a tarefa pedia para citar uma fonte inexistente, mas o
validador exigia `AB-7731`, presente apenas no brief. Com o piso, a decisão virou
`brief` e o acerto voltou a 100%.

### 5.1 Jev (System One)

O Jev respondeu em **267–270 ms** por decisão, contra ~1,3 s do DeepSeek. O
estado e a pergunta `choice` foram enviados a `/v1/systemone`; o custo de entrada
foi de ~13–14 mil tokens por suíte, com saída fixa de ~920 tokens.

| Cenário | Aprovação ON | ON tokens | OFF tokens | ON/OFF | Latência ON/OFF |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, paridade | 1,000 | 41.619 + 13.976 | 52.448 | 1,39× | 7,18/7,12 s |
| Jev, valor | **1,000** | 45.676 + 14.916 | 51.796 | 1,51× | 7,03/6,80 s |

Decisões: **13 `brief`, 6 `none`, 1 `search`** na paridade; **19 `brief`, 1
`search`** no valor. O gate com Jev é mais rápido que o DeepSeek e mantém 100%
no valor; o custo total fica em 1,39×/1,51×, contra 2,58×/2,67× sem gate.

## 6. Confirmação (3 repetições, 2026-11-12)

A confirmação foi concluída com Jev: 20 tarefas × 2 braços × 3 repetições = 120
execuções por suíte, com 0 falhas de infraestrutura, 0 tarefas inválidas e
conhecimento congelado inalterado.

| Cenário | Aprovação ON | ON tokens | OFF tokens | ON/OFF tokens | Latência ON/OFF |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, paridade | 1,000 | 173.453 + 41.928 | 145.623 | 1,48× | 7,13/6,57 s |
| Jev, valor | **1,000 × 0,000** | 193.311 + 44.748 | 151.129 | 1,58× | 7,58/6,93 s |

Os tokens do ON somam o agente e o gate; a latência inclui a chamada do gate. As
decisões foram 39 `brief`, 18 `none` e 3 `search` na paridade, e 57 `brief` + 3
`search` no valor. O gate Jev respondeu em 271,6 ms na paridade e 420,3 ms no
valor. O delta de aprovação foi 0 na paridade (IC95 [0,0]) e 1,000 no valor
(IC95 [1,000,1,000]). O piloto de 1 repetição da seção 5 permanece como
referência histórica.

Para reproduzir:

```powershell
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --gate-mode enforce `
  --gate-provider jev --gate-model jev-latest
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent `
  --gate-mode enforce --gate-provider jev --gate-model jev-latest
```

Os relatórios ficam em `tests/fixtures/memory-ab-live-gate-enforce-jev-report.json`
e `tests/fixtures/memory-ab-live-value-gate-enforce-jev-report.json`.

## 7. Limitações

- O gate avalia a decisão **depois** de o brief local ter sido construído; ele
  reduz o custo do prompt/ferramentas, não o custo da consulta SQLite local.
  O `recordBriefHits` do código gerado roda antes do gate, então `memory_hit`
  pode registrar injeção mesmo quando a decisão é `none`; o passo `memory_gate`
  é a fonte para a decisão efetiva.
- O modelo do gate adiciona uma chamada por execução (com cache por input). Um
  provedor lento pode anular o ganho; o modo determinístico é o fallback.
- A decisão do modelo não é prova de correção; `shadow` e o A/B são obrigatórios
  antes de `enforce` em produção.
- A UI do Studio ainda não edita `memory.gate`; a configuração é feita no
  pipeline/projeto.
