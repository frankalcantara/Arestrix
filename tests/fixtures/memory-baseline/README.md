# Linha de base da memoria (E0)

Fixture da etapa E0 do plano da memoria semantica. Ele congela o comportamento
observavel da versao 2.5.0 para que as etapas seguintes possam provar que nao
regrediram.

## Arquivos

| Arquivo | Papel |
| --- | --- |
| `arestrix.project.json` | manifesto do projeto temporario (`memoria-baseline`) |
| `pipeline.json` | tres vertices de agente: gerador com `validationRules`, auditor e consultor |
| `knowledge.json` | conjunto curado: cinco licoes, tres fatos e um playbook ativo |
| `baseline.json` | referencia gerada: contadores, linhas por tabela e impressao digital do esquema |

O `baseline.json` e versionado. Ele guarda o hash dos tres arquivos de entrada;
alterar qualquer um deles faz `--check` falhar ate que a referencia seja
regenerada de forma explicita.

## Conjunto curado

As licoes usam os ids conhecidos `[MEMO:baseline-lesson-1]` a
`[MEMO:baseline-lesson-5]`; a quinta tem tema `seguranca` e nao deve entrar no
brief do tema `baseline`. Os fatos usam `[MEMO:baseline-fact-1]` a
`[MEMO:baseline-fact-3]` e o playbook usa `[MEMO:baseline-playbook-1]`.

| Item | Tema / tipo | Entra no brief do tema `baseline`? |
| --- | --- | --- |
| `baseline-lesson-1..4` | `baseline` | sim |
| `baseline-lesson-5` | `seguranca` | nao |
| `baseline-fact-1..3` | `baseline` | so por `memory_search` (embeddings desligados) |
| `baseline-playbook-1` | `gerar_artefato` | sim, no vertice gerador |

## Provedor simulado

`scripts/memory_baseline.py` sobe um servidor HTTP local compativel com a API da
OpenAI e o desliga ao final. Ele responde pelo papel do vertice:

- **Gerador:** primeiro turno chama `write` e grava `artefatos/pipeline.md` com
  `STATUS_OK`; segundo turno cita `[MEMO:baseline-lesson-1]`.
- **Auditor:** responde `STATUS: APROVADO`.
- **Consultor:** primeiro turno chama `memory_search` com `query=baseline`;
  segundo turno cita `[MEMO:baseline-lesson-2]`.

Os tokens de prompt sao derivados do tamanho real das mensagens, entao mudancas
no brief aparecem no contador `tokens.prompt`.

## Comandos

```powershell
# Executa o cenario e grava a referencia
py -3 scripts/memory_baseline.py --runs 3 --write

# Confere se o comportamento continua identico
py -3 scripts/memory_baseline.py --runs 3 --check

# Mantem o projeto temporario para inspecao
py -3 scripts/memory_baseline.py --runs 3 --check --keep
```

O projeto temporario e recriado do zero a cada execucao: o banco e semeado, o
grafo roda N vezes e os contadores sao lidos do banco de conhecimento
(`.arestrix/memory/memory.db`) e do banco de telemetria
(`.arestrix/telemetry/telemetry.db`).

## Interpretacao dos contadores

- `tokens`, `turns`, `retries`, `brief.chars` e `invalidCitations` vem de
  `memory_usage`.
- `hits.injected` conta itens entregues pelo brief.
- `hits.consulted` conta itens devolvidos por `memory_search` (coluna
  `consulted`); nao implica uso.
- `hits.used` conta apenas citacoes `[MEMO:id]`. `hits.citedFromBrief` e
  `hits.citedFromSearch` separam a origem da citacao.
- `invalidCitations` conta citacoes de ids que nao foram entregues na execucao.
- `tables.*.rows.memory_meta` inclui `schema_version` (2 desde a E2).
- `tables.knowledge` e `tables.telemetry` trazem `rows` (total), `seeded`
  (linhas ja presentes depois da semeadura, antes das execucoes) e `created`
  (`rows - seeded`) para todas as tabelas `memory_*`, exceto as internas do
  FTS5. `seeded` de telemetria e zero porque o banco de telemetria nasce na
  primeira execucao.
- `schemaFingerprint` muda quando o esquema SQLite muda; a comparacao trata isso
  como mudanca explicita.

## Politica de atualizacao

1. Uma etapa que muda comportamento de forma intencional roda `--write`.
2. O diff de `baseline.json` e revisado junto com a etapa.
3. A justificativa vai para `docs/internal/memoria-linha-de-base.md` e para o
   CHANGELOG.
4. A tolerancia padrao e zero. Se algum contador realmente nao for
   deterministico, ele deve ser declarado em `tolerance.metrics`, com o motivo.
