# Como funciona a memória do Arestrix

> **Aplica-se a:** Arestrix 2.5 — última revisão: 2026-10-31
>
> Guia de uma página e referência da memória de projeto. A memória é opcional, local e
> pertence ao projeto. Não há importação automática de formatos antigos.

## 1. Princípios

- **Desligada por padrão.** Nenhum vértice consulta ou grava conhecimento sem configuração explícita.
- **Três modos.** `off`, `consult` e `learn`. O modo é decidido por vértice.
- **Aprender implica salvar.** `learn` grava um episódio e pode propor conhecimento; `consult` só lê.
- **Nada entra sozinho.** Lições e playbooks nascem como proposta/candidato e só viram conhecimento
  ativo depois de uma aprovação.
- **Telemetria separada.** Episódios, hits e uso de API ficam em banco próprio; limpar sessão ou
  apagar telemetria nunca apaga o conhecimento.
- **Local-first.** SQLite no disco do projeto, sem dependência de nuvem.

O ciclo é:

```text
consultar → executar → observar → propor → aprovar → consolidar → consultar
```

## 2. Onde a memória fica

| Escopo | Caminho | Papel |
| :--- | :--- | :--- |
| Conhecimento do projeto | `<projeto>/.arestrix/memory/memory.db` | lições, fatos, evidências e estado de trabalho |
| Telemetria do projeto | `<projeto>/.arestrix/telemetry/telemetry.db` | episódios, hits, uso de tokens e validações |
| Workspace | `<projeto>/.workspace/memory/memory.db` | conhecimento promovido, compartilhado |
| Instituição | banco do serviço institucional | conhecimento autenticado de vários projetos |

`ARESTRIX_MEMORY_DB` troca o banco de conhecimento e `ARESTRIX_TELEMETRY_DB` troca o de
telemetria. A raiz do projeto vem de `ARESTRIX_PROJECT_ROOT`.

## 3. Configuração do vértice

| Campo | Efeito |
| :--- | :--- |
| `mode: off` | Não lê nem grava memória. |
| `mode: consult` | Injeta o brief autorizado no prompt. Não grava conhecimento. |
| `mode: learn` | Consulta e, ao terminar, grava episódio e propostas de lição. |
| `theme` | Tema principal de leitura e escrita. Sem ele: `domains[0]` legado, depois nome da skill ativa, rótulo e id do vértice. |
| `readThemes` | Temas extras apenas de leitura. |
| `taskType` | Identidade estável de continuidade; padrão é o id do vértice. |
| `contextBudgetTokens` | Orçamento do brief; `0` desativa a injeção. |
| `maxLessonsPerRun` | Limite de propostas por execução. |
| `maxLessonChars` | Limite de caracteres por lição/trigger. |

A configuração do projeto define o padrão herdado pelos vértices: modo, tema, retenção,
consolidação e embeddings. O vértice pode sobrescrever a memória quando `inheritMemory` é falso.

O **tema efetivo** segue a mesma ordem no TypeScript e no C++: `theme` explícito; `domains[0]`
legado; nome da skill ativa; rótulo do vértice; id do vértice. Os três últimos viram identificador
(minúsculas, sem acentos, separadores `_`). A janela do vértice mostra o tema e a origem
(“da skill”, “do rótulo” ou “do id”). Trocar a skill ativa sem um `theme` explícito **muda o tema e,
portanto, o filtro de lições e a autorização de escrita**; para manter o tema, declare-o.

## 4. O que o brief contém

O brief é montado no início da execução, dentro do orçamento configurado. A consulta combina o
`task_type`, o tema e o **prompt-template do vértice** — nunca o texto de entrada do usuário. As
lições são filtradas por tema diretamente no SQL, sem varrer todas as lições do projeto, e o brief
é cacheado por vértice/execução; qualquer escrita de conhecimento, promoção, consolidação ou
reindexação invalida o cache.

1. **Lições ativas** do tema, ordenadas por confiança e recência.
2. **Fatos ativos** ligados ao tema e às consultas da tarefa.
3. **Evidências** das lições e fatos injetados.
4. **Estado de trabalho** apenas quando o formato legado/manual o habilita. O grafo gerado pelo
   Arestrix não usa esse bloco.

Playbooks compilados ficam como **candidatos** e não entram no brief até serem promovidos.
Conhecimento com baixa confiança, conflito aberto ou validade expirada não é injetado.

Com `globalDefaults.memory.promptCache` ligado, o prefixo **skill + brief** é enviado ao provedor
Anthropic como um bloco de system com `cache_control: ephemeral`; o contexto de execução fica em um
segundo bloco, sem cache. A ordem skill, brief e contexto é fixa e o brief é idêntico em todos os
turnos da mesma execução, então provedores com cache automático de prefixo (OpenAI-compatíveis)
também aproveitam o reenvio. `memory report` mostra a fração de tokens de entrada servida pelo
cache. O interruptor é desligado por padrão.

Cada item injetado recebe um rótulo `[MEMO:<id>]`. No fim da execução, o runtime só marca como
**usado** o item cujo ID foi citado pelo agente na resposta final. “Injetado”, “consultado” e
“usado” são sinais distintos: resultados de `memory_search` também trazem `[MEMO:<id>]` e a mesma
instrução de citação, mas devolvê-los marca apenas `consulted`. Citar um id que não foi entregue
naquela execução é ignorado e contado como citação inválida.

O uso altera a **utilidade** do item, não a confiança. Confiança é sustentação (evidência,
validação e promoção); utilidade é o histórico de uso em execuções bem ou mal sucedidas.

## 5. Como o aprendizado entra

Uma execução `learn` grava um **episódio** com entrada, resultado, validação e hashes. A partir
dele, o sistema pode criar:

- **proposta de lição** (`memory_propose_lesson`) quando o agente declara uma regra;
- **proposta automática de falha** quando a tarefa falha (o validador reprova ou o agente estoura
  `maxTurns`);
- **candidato de playbook** quando há sequência suficiente de passos bem-sucedidos.

Falhas de infraestrutura são classificadas por `errorClass` enumerado — transporte/timeout,
429, 5xx, autenticação/chave, gateway/orçamento, configuração, sandbox/ferramenta ou cancelamento —
e **não** geram proposta de lição. O agente lança `AgentFailure` com essa classe; o wrapper gerado
consulta a classe, nunca o texto da exceção. `task_max_turns` é falha da tarefa, não de
infraestrutura. O texto da proposta é normalizado antes da deduplicação: UUIDs, datas/horas e
números longos viram marcadores, então duas falhas iguais com ids diferentes geram uma proposta só.

### Validação de artefato

Quando o vértice declara `validationRules` — caminho com `{itemId}`, tamanho mínimo e padrão de
conteúdo —, o runtime avalia as regras **depois** que o agente termina sem exceção. O veredito é
gravado em `memory_validation`, ligado ao episódio do vértice, com `validator_id=artifact-rules`, a
versão SHA-256 das regras e o hash dos artefatos lidos. Falha de infraestrutura ou de tarefa não
gera veredito: o episódio fica **sem veredito**.

Com `globalDefaults.memory.validatedReinforcement` ligado, o resultado do agente deixa de alterar a
utilidade; só o veredito reforça os itens usados naquela execução. Desligado (padrão), o
comportamento da 2.5.0 é preservado. O gate de playbooks sempre usa o veredito, com ou sem o
interruptor.

Propostas ficam pendentes no painel **Memória do Projeto**. Aprovar promove o item para o
conhecimento ativo; rejeitar mantém o registro para auditoria. Não existe gravação direta de
lição por ferramenta.

Toda promoção e despromoção grava um **instantâneo de evidência** em `memory_evidence`, no banco
de conhecimento: alvo, ação, episódio de origem (quando houver), validador, hash do artefato,
resumo e hash do trecho. O instantâneo **não tem chave estrangeira para o episódio**, então a
retenção da telemetria pode apagar episódios, hits e validações antigas sem apagar a prova. O
relatório e o painel mostram quantos instantâneos existem e quantos itens ativos estão sem
evidência.

Playbooks só saem de `candidate` com validação independente. O gate exige **3 `input_hash`
distintos** entre as últimas 20 execuções validadas do mesmo `task_type`, validador estável e no
máximo **1 promoção por `task_type` em 24h**. Duas falhas consecutivas ou taxa acima de 30% em 10
execuções validadas devolvem o playbook a candidato. No painel, um playbook ativo tem o botão
**Voltar a candidato**; a CLI equivalente é `memory unpromote`.

Fatos também têm **cardinalidade de predicado**. Predicados de acumulação — `USES`, `USES_TOOL`,
`PRODUCES`, `READS`, `WRITES`, `TOUCHES`, `CITES`, `INCLUDES`, `TAGGED_WITH`, `HAS`, `DEPENDS_ON` —
aceitam vários objetos ativos. Predicados funcionais e desconhecidos são single-valued: promover um
novo valor encerra o anterior (`superseded`, com histórico), em vez de deixar dois valores ativos.
O detector de contradições ignora predicados multi-valued.

## 6. Ferramentas do agente

| Ferramenta | Efeito |
| :--- | :--- |
| `memory_search` | Busca somente leitura no conhecimento do projeto e no workspace autorizado. |
| `memory_propose_lesson` | Registra uma proposta pendente de lição; não ativa conhecimento. |
| `memory_state_get` | Lê uma chave de estado declarada; o valor volta rotulado como não confiável. |
| `memory_state_set` | Grava com CAS (`expected_version`); só em modo `learn`. |
| `memory_state_list` | Lista chaves visíveis e metadados, sem valores. |
| `memory_state_history` | Lista versões de uma chave `versioned`. |

`memory_record_lesson`, `memoryWriteKeys` e o servidor nativo `arestrix.memoria` foram removidos.
O conhecimento é acessado pelo runtime e pelo painel, não por um chave-valor paralelo.

## 7. Busca e embeddings

A busca combina:

- **lexical** (FTS) sempre disponível;
- **vetorial** opcional, quando um provedor de embeddings está configurado;
- **fusão RRF** para ordenar os dois resultados.

A busca **não indexa** durante a consulta. A indexação acontece fora do caminho de leitura: no fim
de cada execução com memória persistente (orçamento padrão de **10 s ou 64 objetos**), no worker de
manutenção (`idle`/`after_episodes`) e nas ações explícitas **Testar** e **Reindexar** do painel.
Falha do provedor de embeddings não derruba a execução. Perfis prontos:

| Perfil | Dimensão armazenada | Observação |
| :--- | :--- | :--- |
| EmbeddingGemma 2 | 768 | vetor completo; também aceita 512, 256 e 128 via MRL |
| Qwen3-Embedding-4B | 768 | truncado de 2560; aceita de 32 a 2560 via MRL |

Sem provedor disponível, a busca continua funcionando no modo lexical.

Cada modelo tem seu prefixo de representação. O **Qwen3** recebe, na consulta, a instrução
`Instruct: Given a task description, retrieve memory items that help complete it` seguida de
`Query:`; o documento não recebe prefixo. O **EmbeddingGemma 2** usa prefixos distintos para
consulta e documento. O texto da instrução é versionado e entra no **fingerprint**: mudá-lo
invalida o índice.

O campo **revisão do modelo** (`embeddingRevision`) registra o digest/revisão do servidor, quando
informado; também entra no fingerprint. O **truncamento MRL** (`embeddingTruncateDim`, 0 = vetor
completo) reduz o vetor e o **renormaliza para norma 1**; vetores com NaN/Inf ou norma zero são
recusados. Dimensões aceitas: EmbeddingGemma 2 em 768, 512, 256 ou 128; Qwen3 de 32 a 2560.

O índice guarda um **fingerprint** SHA-256 de endpoint, modelo, revisão, dimensão efetiva,
truncamento, prefixos/instrução, normalização, métrica e versão do índice. A busca só usa vetores
do fingerprint atual; nunca mistura índices antigos. Mudar modelo, revisão, dimensão, truncamento
ou instrução deixa o índice `stale` e exige reindexação. O status mostra modelo, dimensão, revisão,
itens indexados, candidatos, pendentes e um fingerprint curto, com os estados:

| Estado | Significado | Busca |
| :--- | :--- | :--- |
| `ready` | Fingerprint atual, índice completo | híbrida |
| `stale` | Modelo/dimensão/endpoint/template mudou | lexical |
| `pending` | Reindexação em andamento | lexical |
| `partial` | Índice incompleto | híbrida: vetorial nos indexados, lexical nos demais |
| `failed` | Reindexação falhou | lexical + erro |

Em `partial`, os itens com vetor entram pela fusão lexical + vetorial; os itens ainda sem vetor
entram apenas pela parte lexical, sem desligar a busca híbrida. `memory health` e o painel mostram
os totais de indexados e pendentes.

Trocar de perfil invalida o índice e oferece reindexação. A reindexação é idempotente e descarta
vetores de fingerprints antigos.

## 8. Medição A/B

O painel oferece **Pré-visualizar o tamanho do brief**. A ação:

1. cria uma cópia consistente do conhecimento com `VACUUM INTO`;
2. calcula o fingerprint SHA-256 da cópia;
3. mede, por vértice ativo, tokens e caracteres do brief, lições, fatos e playbooks;
4. compara com o baseline sem memória (zero).

Essa é a versão **A/B v1**: mede custo e conteúdo do contexto, não a taxa de aprovação do
validador. O executor completo é o comando `memory ab run` descrito abaixo.

O **A/B v2** é o protocolo estatístico completo. O conjunto versionado
[`tests/fixtures/memory-ab-v1.json`](../../tests/fixtures/memory-ab-v1.json) tem 20 tarefas —
12 com conhecimento relevante, 4 de controle e 4 de falha conhecida — e cada uma declara seu
`input_hash`. A CLI valida o conjunto, sorteia uma ordem intercalada com semente e analisa as
observações:

```bash
arestrix-cli memory ab validate --suite tests/fixtures/memory-ab-v1.json
arestrix-cli memory ab order --suite tests/fixtures/memory-ab-v1.json \
  --repetitions 3 --seed 42 --json
arestrix-cli memory ab run --suite tests/fixtures/memory-ab-v1.json \
  --pipeline pipeline.json --node gerador --repetitions 3 --seed 42 --json
```

`memory ab run` executa de fato o piloto. Ele congela o conhecimento com `VACUUM INTO` e abre essa
cópia apenas para leitura (`SQLITE_OPEN_READONLY` e `PRAGMA query_only`) no braço ON; no braço OFF
define `ARESTRIX_MEMORY_DISABLE=1`. As duas metades gravam telemetria no mesmo banco de medição, com
`ab_arm` e `ab_repetition` em cada linha. Falhas de infraestrutura são repetidas até duas vezes e
contadas fora do veredito; tarefas sem validador aparecem em `invalidTasks` e não entram no par.

A métrica primária é a **taxa de aprovação do validador**, comparada em pares. O relatório traz
deltas pareados, IC 95% (t de Student), tokens, turnos, custo e latência, além do fingerprint do
conhecimento antes e depois da medição. `memory ab report` continua disponível para analisar um
arquivo de observações já coletado.

### Marco de recorrência

A E10 mede se as tarefas se repetem o suficiente para justificar caminhos. O
episódio grava `intent_template_hash`, o SHA-256 do `promptTemplate` do vértice
antes da substituição de variáveis. O template é configuração, não o prompt do
usuário, então esse campo não armazena texto de entrada.

```bash
arestrix-cli memory report --recurrence --json
arestrix-cli memory report --recurrence --since 2026-10-01T00:00:00Z \
  --min-verdict-rate 0.70 --min-citation-rate 0.30 \
  --min-executions 5 --min-distinct-inputs 3 --min-reuse-rate 0.20
```

Por `task_type`, o relatório traz execuções, execuções com veredito, taxa de
aprovação, `input_hash` distintos, pares (`task_type`, `intent_template_hash`),
taxa de citação, taxa de reuso e tokens por execução. A taxa de reuso é a fração
das execuções que citam um item já citado numa execução anterior do mesmo
`task_type`. O resultado aponta uma das decisões: `continue_full`,
`continue_base_knowledge` (E11-E14), `fix_validators` ou
`insufficient_data`. Os limiares usados ficam gravados no JSON.

### Fontes locais e trechos

Com `globalDefaults.memory.sources` ligado, a memória ingere arquivos de texto,
Markdown e código **depois** da execução, sem chamar modelo de linguagem:

- arquivos lidos com sucesso pela ferramenta `read`;
- pastas ou arquivos declarados em `memory.sources.paths`.

A ingestão aplica allowlist de extensões, limite `maxFileBytes` (padrão 512 KiB),
`maxFiles` (padrão 2.000) por execução, exclusões fixas (`.git`, `node_modules`, `.arestrix`,
`.workspace/memory`) e sanitização de segredos. O conteúdo é segmentado de forma
determinística: Markdown por títulos/parágrafos, código por blocos de topo e
texto em janelas de 1.000 caracteres com sobreposição de 200.

Cada versão da fonte tem `content_hash`; mudanças criam uma nova versão e os
trechos antigos ficam `superseded`, preservados como evidência. Reingerir o mesmo
conteúdo é uma operação nula. `memory_search` devolve trechos com
`[MEMO:psg:<id>]`, caminho e posição; citar o ID marca o trecho como usado. O
brief não inclui trechos nesta etapa.

### Afirmações a partir de trechos

Com `globalDefaults.memory.claimExtraction` ligado, a manutenção escolhe
trechos com `used=1` em execuções aprovadas, ainda não extraídos pelo fingerprint
atual do extrator. O modelo local (`memory.extractor`: `baseUrl`, `model`,
`revision`, versões de prompt/schema, `maxItems` e timeout) devolve JSON e nunca
roda durante a execução do agente.

Cada afirmação vira uma proposta `claim` pendente com sujeito, predicado, objeto
ou valor, unidade, polaridade, qualificadores, validade, lente e `quote`. A
validação exige JSON válido, `quote` literal no trecho, datas e unidade
analisáveis e resolução de entidade por alias; menção ambígua gera abstenção.
Predicados fora do catálogo continuam como proposta marcada.

A promoção é manual e grava uma aresta com lente, citação e fingerprint do
extrator no metadata, mais um instantâneo de evidência. Para predicados de valor
único, só valores com validade sobreposta encerram o anterior; intervalos
disjuntos permanecem como história.

### Lentes e facetas

Com `globalDefaults.memory.lenses` ligado, a manutenção garante uma lente para
cada aresta ativa (a lente vem da extração ou do tema/domínio de quem escreveu)
e gera facetas determinísticas por entidade e lente. A faceta reúne apenas
afirmações ativas; afirmações propostas ou contestadas ficam de fora. Quando o
`claims_hash` muda, a faceta é atualizada e recebe nova revisão; reprocessar o
mesmo conjunto não muda a revisão.

As facetas usam o papel **simétrico** dos embeddings, com prefixo próprio
(EmbeddingGemma: `task: sentence similarity`; Qwen3: instrução de similaridade) e
fingerprint diferente do papel de recuperação. `memory_search` aceita filtro por
lentes, derivadas dos temas do vértice, e devolve apenas as arestas daquela
lente.

### Caminhos canônicos

Com `globalDefaults.memory.paths` ligado, a manutenção consolida traços aprovados
em caminhos canônicos de afirmações. O consolidador é idempotente por `trace_id`:
reprocessar o mesmo traço não altera estatísticas. Um caminho nasce como
`candidate` e só fica `active` depois de um número configurável de entradas
distintas (`minDistinctInputs`, padrão 3).

A utilidade usa a média Beta com prior (`(sucessos + prior) / (sucessos +
falhas + 2·prior)`) e decaimento temporal opcional (`mu`). O prior impede que um
único sucesso chegue a 1,0; traços fora de ordem não geram decaimento negativo.
Quando uma afirmação dependente é retratada, expira ou deixa de estar ativa, o
caminho correspondente é suspenso na mesma manutenção. Se todas as dependências
voltarem a vigorar, ele é reativado; caso contrário, pode ser retirado.

```bash
arestrix-cli memory paths list --project . --json
arestrix-cli memory paths show <path> --project . --json
arestrix-cli memory paths retire <path> --project .
```

### Serviço de caminhos

Com `globalDefaults.memory.pathServing` ligado, o brief e `memory_search`
consultam primeiro os caminhos ativos compatíveis com o `task_type`. Todas as
afirmações precisam estar ativas, dentro da validade e liberadas pela política de
acesso; se uma falhar, o caminho inteiro não é servido. O caminho aparece como
`[MEMO:path:<id>]` com as afirmações e suas fontes. Citar o caminho conta como
uso das afirmações; citar uma afirmação também marca o caminho. Uma fração
configurável das consultas usa amostragem de Thompson e fica registrada no
traço.

Caminhos com utilidade e número de usos suficientes materializam arestas
derivadas `via_caminho`. Elas são projeções reconstruíveis, não são evidência,
não entram na detecção de conflito nem na busca normal, e acompanham o estado do
caminho. O interruptor permanece desligado até o A/B ser registrado.

### Travessia em feixe

Quando nenhum caminho cobre a consulta, `globalDefaults.memory.traversal` liga
uma travessia limitada a partir de âncoras resolvidas por alias, FTS e
candidatos vetoriais. A busca usa largura de feixe, profundidade, candidatos,
tempo e caracteres limitados; itens inelegíveis nunca entram no top-k e ciclos
não repetem nós. `memory_search` aceita `mode: "graph"` para esse caminho.

O pacote devolve cada item com fonte, validade e justificativa, e grava no traço
o vetor de cobertura: utilidade do melhor caminho, fração sustentada, conflitos,
distância de intenção e abstenção. A travessia só lê o grafo e não escreve
conhecimento.

### Roteamento de modelos

O roteador usa a taxa de validação por `task_type`, com prior Beta, para estimar
a confiança de um nível local. Em `routing.mode=binary`, ele escolhe entre local
e fronteira por `p_min` e custo esperado; em `shadow`, registra a decisão sem
trocar o modelo executado. Afirmações marcadas como somente locais bloqueiam o
nível de fronteira. Reprovação no validador, abstenção ou conflito podem
escalar para fronteira, e a decisão fica no traço. O modo padrão é `off`.

### Traços de recuperação

Toda escrita de conhecimento incrementa `knowledge_version` na mesma transação;
leituras e telemetria não incrementam. Cada execução de vértice com memória grava
um `memory_trace` com a versão consultada, o brief e as buscas, os candidatos
com escores lexical/vetorial/fundido, posição e se foram entregues, além dos
itens efetivamente usados. O traço fecha junto com o episódio e é idempotente por
`(run_id, node_id)`.

```bash
arestrix-cli memory trace show <run> <node> --project . --json
```

Cada consulta guarda até 50 candidatos e cada execução até 8 passos; o excedente
é contado em `truncatedCandidates`/`truncatedSteps`. A consulta em si é guardada
como hash, não como texto.

### Custo por execução

A telemetria registra prompt, saída e os tokens de **prompt cache**: leitura de cache
(`cached_tokens`) e escrita de cache (`cache_write_tokens`), quando o provedor informa. O painel e
`arestrix-cli memory report` mostram o total; o custo efetivo usa o preço do provedor para tokens
normais, cached e de escrita.

## 9. Retenção, segurança e escopo

- **Escopo estrito:** o conhecimento do projeto não vaza para outro projeto; o workspace só é
  lido quando configurado e autorizado.
- **Sanitização:** segredos conhecidos são removidos antes de qualquer gravação, leitura legada
  ou exportação.
- **Retenção:** afeta telemetria (episódios, hits, uso). Conhecimento não é apagado por rotina de
  retenção.
- **Limpeza de sessão:** em memória efêmera, limpa apenas a sessão; nunca purga um banco de arquivo.
- **Auditoria:** aprovações, rejeições, promoções, conflitos e expurgos deixam rastro.

## 10. Estado de continuação

O estado de continuação substitui o antigo Blackboard e o `arestrix.memoria`. Ele fica **fora do
brief** e só é acessível pelas ferramentas.

- A chave é **declarada pelo projeto**; o agente nunca cria chave.
- Identidade: `(scope_kind, scope_owner, key)`.
- Escopos: `project`, `task_type` e `item`. Cada chave tem um único escopo, sem merge entre eles.
- O valor é validado por um subconjunto de JSON Schema **depois** da sanitização de segredos.
- `replace` mantém só a versão atual; `versioned` mantém as últimas K versões.
- TTL é aplicado na leitura.
- CAS: `expected_version` ausente cria; versão igual grava; versão diferente devolve conflito com a
  versão atual, sem sobrescrever.
- Mudança de schema marca o valor antigo como `stale`; a próxima escrita informa a versão atual.
- `memory_state_list` não devolve valores. Leituras são rotuladas como dados de execução anterior
  não confiáveis.
- Escritas, recusas e conflitos são auditados; leituras não.

No Studio, a aba **Estado** do painel **Memória do Projeto** lista as chaves declaradas e permite
criar uma chave com escopo, schema, TTL, modo e versões mantidas. O agente continua sem permissão
para criar chaves.

Declaração e operação manual pela CLI:

```bash
arestrix-cli memory state declare --project . --key cursor --scope project \
  --schema '{"type":"object","properties":{"n":{"type":"integer"}},"required":["n"]}'
arestrix-cli memory state set --project . --key cursor --value '{"n":1}'
arestrix-cli memory state get --project . --key cursor --json
arestrix-cli memory state list --project . --json
```

## 11. Referência rápida

```text
Projeto
  .arestrix/memory/memory.db       conhecimento
  .arestrix/telemetry/telemetry.db telemetria
  .workspace/memory/memory.db      workspace (opcional)

Vértice
  off      não usa memória
  consult  lê o brief e registra hits
  learn    lê, grava episódio e propõe lições

Ciclo
  executar -> episódio -> proposta -> aprovação -> conhecimento ativo -> brief

Estado de continuação
  chave declarada -> memory_state_get/set/list/history -> fora do brief
```

Para operar pela linha de comando, consulte [`arestrix-cli.md`](arestrix-cli.md). Para a visão de
produto e os limites atuais, consulte [`workspaces-memoria-skills-mcp.md`](workspaces-memoria-skills-mcp.md).

## 12. Instituição

O serviço `arestrix-memory` expõe o conhecimento institucional por HTTP autenticado e MCP (stdio e
`/mcp`), com tokens, papéis `viewer`/`editor`/`admin` e escopo invariante. Ele é opcional e separado
do banco privado do projeto.

## 13. Status da implantação

A implantação operacional da memória está concluída: modos, propostas, validação, estado de
continuação, telemetria, embeddings com fingerprint, playbooks com gate, A/B v2 e ferramentas do
assistente do Studio.

O plano de simplificação (Fases A–C) está concluído. A Fase D — multigrafo avançado com facetas,
conhecimento de domínio, caminhos sobre afirmações e roteamento de modelos — é um programa separado,
fora deste plano, e não bloqueia o release. A recuperação por embeddings já está implementada.

O nível institucional no assistente e a validação com provedores reais são pendências operacionais.
O harness do A/B está implementado em `memory ab run` e roda com o provedor simulado no e2e.
Consulte [`PENDENCIAS.md`](../PENDENCIAS.md).
