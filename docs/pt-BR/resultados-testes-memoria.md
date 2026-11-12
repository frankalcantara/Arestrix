# Resultados dos testes de memória

> **Aplica-se a:** Arestrix 2.8 (`2.8.0`) · **Última revisão:** 2026-11-12
>
> **Escopo:** resultados das campanhas de memória — linha de base, avaliação de
> regressão, A/B live de paridade e valor, porteiro de política (`memory.gate`) e
> interruptores avançados (`pathServing`, `traversal`, `bridges` e
> `routing.mode`), incluindo o corpus derivado dos 14 exemplos, o A/B real do
> roteamento binário e a validação de embeddings reais.
>
> **Detalhes:** [`memory-gate-ab.md`](../quality/memory-gate-ab.md),
> [`memory-advanced-ab.md`](../quality/memory-advanced-ab.md),
> [`memory-examples-advanced-ab.md`](../quality/memory-examples-advanced-ab.md),
> [`routing-binary-ab.md`](../quality/routing-binary-ab.md) e
> [`memory-embeddings-live.md`](../quality/memory-embeddings-live.md).
>
> **Pendências e limitações operacionais:** [`roadmap.md`](roadmap.md).

## 1. Resumo executivo

A memória entrega valor mensurável, mas custa mais tokens e latência quando o
agente decide consultá-la sozinho. O `memory.gate` reduz esse custo sem perder
acerto no piloto. Os interruptores avançados funcionam como mecanismos nos
corpora curado e derivado dos 14 exemplos; o roteamento binário real confirmou
paridade de qualidade e economia de 26,5% contra a fronteira, mas ainda não
superou o modelo local. Os padrões continuam desligados até a medição em
projetos reais de cliente.

| Campanha | O que mede | Resultado registrado | Limitação principal |
| :--- | :--- | :--- | :--- |
| Linha de base E0 | determinismo, brief, hits e telemetria | 3 execuções, 9/9 sucessos, 7.314 tokens, 0 citações inválidas | provedor simulado; não mede qualidade de modelo |
| Avaliação E20 | regressão entre variantes | 8 casos, 5 variantes, todas completas | as variantes produziram métricas idênticas; não exercitam os interruptores |
| A/B live — paridade | custo sem depender da memória | aprovação 1,000 × 1,000; 2,58× tokens; 1,69× latência | 20 tarefas curadas; sem dados de cliente |
| A/B live — valor | acerto quando a memória é necessária | aprovação 1,000 × 0,000; delta 1,000; 2,67× tokens; 1,62× latência | efeito binário por desenho do validador |
| `memory.gate` | reduzir buscas e custo sem perder acerto | Jev: 1,48×/1,58× tokens e 1,08×/1,09× latência, aprovação 1,000 | 3 repetições; dados curados |
| Interruptores — corpus curado | caminhos, travessia, pontes e roteamento | caminho 1,000/1,000; travessia 1,000/0,500; 1 ponte e 0 arestas; roteamento coerente | corpus curado; precisão da travessia ainda é 0,500 |
| Interruptores — 14 exemplos | mecanismos sobre projetos derivados | caminho 1,000/1,000; travessia 1,000/0,300–1,000; 5 pontes propostas e 0 arestas; roteamento correto nos 14 | corpus derivado dos exemplos, não dados de cliente |
| Roteamento binário real | qualidade e custo com dois modelos | aprovação 1,000 nos 3 braços; 20.444,40 contra 27.805,00 da fronteira; 8 decisões local e 6 frontier | qualidade empatada com o local; 7 tarefas e 2 repetições |
| Embeddings reais | qualidade e latência com dois modelos | Gemma 2: recall@1 0,875 e @3 1,000, consulta p50 134 ms; Qwen 768: 0,813/1,000, p50 8,5 s; reindex 4/4 nos dois | corpus de 16 documentos; Qwen em CPU; endpoint local, não de produção |

## 2. Linha de base e regressão

A linha de base E0 usa um provedor determinístico e um fixture congelado com
cinco lições, três fatos e um playbook. Em 3 execuções do grafo:

| Métrica | Resultado |
| :--- | ---: |
| Execuções do grafo | 3 |
| Execuções de agente | 9 |
| Chamadas de modelo | 15 |
| Tokens totais | 7.314 |
| Brief injetado | 6.795 caracteres |
| Hits injetados / consultados / usados | 39 / 21 / 6 |
| Citações inválidas | 0 |
| Sucessos de agente | 9/9 |

A avaliação E20 comparou `baseline_lexical`, `hybrid_fallback`,
`facets_traversal`, `paths_serving` e `routing_shadow` sobre 8 casos (positivos e
negativos). Todas terminaram, mas as cinco variantes produziram os mesmos
contadores (2.438 tokens, 5 chamadas, 13 itens injetados, 2 usados, 0 citações
inválidas). Isso é uma limitação do corpus E20: ele verifica que o desligamento
não altera a linha de base, mas **não exercita** caminhos, travessia ou pontes.
Por isso foi criado o corpus avançado da seção 5.

## 3. A/B live de paridade e valor

O A/B v2 usa 20 tarefas, 3 repetições e 2 braços, com banco de conhecimento
congelado: 120 execuções por suíte. O agente foi `deepseek-chat`; não houve
falha de infraestrutura, tarefa inválida ou alteração do conhecimento congelado.

### 3.1 Paridade

O validador não depende da memória; a suíte mede custo e regressão de aprovação.

| Métrica | OFF | ON | ON/OFF |
| :--- | ---: | ---: | ---: |
| Aprovação | 60/60 (1,000) | 60/60 (1,000) | delta 0 (IC95 [0, 0]) |
| Tokens totais | 146.220 | 377.378 | **2,58×** |
| Prompt tokens | 99.428 | 298.588 | — |
| Completion tokens | 46.792 | 78.790 | — |
| Turnos médios | 2,00 | 3,52 | — |
| Latência média | 6,46 s | 10,95 s | **1,69×** |

O custo maior do braço ON vem das chamadas `memory_search` feitas pelo próprio
agente (20/20 traces no ensaio de 20 execuções, média de 1,8 busca por trace),
não do brief: o contexto injetado tem ~451 caracteres em média.

### 3.2 Valor

O validador exige o token `AB-7731`, que só existe em uma lição curada.

| Métrica | OFF | ON | ON/OFF |
| :--- | ---: | ---: | ---: |
| Aprovação | 0/60 (0,000) | 60/60 (1,000) | delta 1,000 (IC95 [1,000, 1,000]) |
| Tokens totais | 151.539 | 405.161 | **2,67×** |
| Latência média | 7,12 s | 11,56 s | **1,62×** |

A lição é efetivamente usada, mas o efeito é binário por desenho do validador.
O A/B de valor mede a entrega da lição, não a qualidade da recuperação avançada.

## 4. Porteiro de política (`memory.gate`)

O gate decide entre `none`, `brief`, `search` e `learn` antes do laço do agente.
A confirmação de 3 repetições (2026-11-12) usou 20 tarefas × 2 braços × 3
repetições = 120 execuções por suíte, com 0 falhas de infraestrutura, 0 tarefas
inválidas e conhecimento congelado inalterado.

| Cenário | Aprovação ON | ON/OFF tokens | ON/OFF latência | ON total | OFF total |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, paridade | 1,000 | 1,48× | 1,08× | 215.381 (173.453 agente + 41.928 gate) | 145.623 |
| Jev, valor | **1,000 × 0,000** | 1,58× | 1,09× | 238.059 (193.311 agente + 44.748 gate) | 151.129 |

O piloto histórico de 1 repetição, com DeepSeek e gate determinístico, permanece
em `docs/quality/memory-gate-ab.md`; a tabela acima é a confirmação com Jev. O
gate tomou 39 `brief`, 18 `none` e 3 `search` na paridade, e 57 `brief` + 3
`search` no valor. A latência média do gate foi 271,6 ms na paridade e 420,3 ms
no valor. O delta de aprovação foi 0 na paridade (IC95 [0,0]) e 1,000 no valor
(IC95 [1,000,1,000]).

O piso de validação foi necessário: no caso `fail-fonte`, o modelo escolheu
`none`, mas o validador exigia `AB-7731`, presente apenas no brief; o piso
promoveu a decisão para `brief` e preservou o acerto.

## 5. Interruptores avançados

O corpus `memory-advanced-v1` tem 10 tarefas, 7 variantes e 3 repetições (210
medições determinísticas), sem chamada de modelo. Cada tarefa roda em uma cópia
do banco congelado.

| Cenário | Recall | Precisão | Brief | Leitura |
| :--- | ---: | ---: | ---: | :--- |
| `pathServing` em caminho | 1,000 | 1,000 | 422 chars | baseline 0,000; caminho expirado não é servido |
| `traversal` em cadeia de 3 saltos | 1,000 | 0,500 | 736 chars | baseline 0,000; consulta sem âncora abstém; aresta `local` não entra |
| `all_on` — caminho | 1,000 | 1,000 | 422 chars | caminho servido antes da travessia |
| `all_on` — travessia | 1,000 | 0,500 | 736 chars | travessia só quando não há caminho |

- **Pontes:** 1 hipótese correta (`Guia`, lentes `contraste`/`tipografia`), com
  os claimIds esperados e **0 arestas criadas**. Casos de mesma lente e acesso
  incompatível não geraram proposta.
- **Roteamento `shadow`:** a decisão foi registrada, mas o tier executado
  permaneceu `current`, inclusive sem histórico.
- **Roteamento `binary`:** escolheu `local_large` com confiança alta, `frontier`
  com confiança baixa e `frontier` sem histórico.
- **Latência do harness:** ~44–82 ms por consulta local, sem modelo.

A medição encontrou e corrigiu dois defeitos: a travessia não resolvia âncoras
de prompts em linguagem natural (agora tokeniza a consulta quando a frase
inteira não é um alias) e o modo `shadow` trocava o tier executado quando não
havia histórico (agora mantém o tier atual e apenas registra a sugestão).

### 5.1 Corpus derivado dos 14 exemplos

Como não há projetos reais de cliente disponíveis, o harness
`scripts/memory_examples_eval.py` copia cada um dos 14 projetos de `examples/`,
semeia o banco a partir dos pipelines e mede os interruptores sem chamada de
modelo. O projeto original não é alterado. São 3 repetições por projeto; o
corpus é **derivado dos exemplos**, não dado de cliente.

| Cenário | Resultado nos 14 projetos |
| :--- | :--- |
| `pathServing` | recall 1,000 e precisão 1,000 em todos |
| `traversal` | recall 1,000 em todos; precisão entre 0,300 e 1,000 |
| `bridges` | 5 projetos com estrutura natural de ponte (`06`, `09`, `10`, `11`, `12`), 5 propostas corretas e **0 arestas criadas** |
| `routing.mode=shadow` | decisão registrada e tier `current` mantido nos 14 |
| `routing.mode=binary` | `local_large` com confiança alta, `frontier` com confiança baixa e `frontier` sem histórico nos 14 |

A precisão da travessia varia porque o pacote inclui afirmações relacionadas ao
mesmo anchor além do alvo. O comportamento é aceitável como contexto, mas ainda
pede ranking ou poda. Relatório:
[`memory-examples-advanced-ab.md`](../quality/memory-examples-advanced-ab.md).

### 5.2 Roteamento binário real

O A/B de roteamento usou 7 tarefas (4 fáceis e 3 difíceis), 2 repetições e
chamadas reais ao provedor `deepseek`: `deepseek-flash` como nível local e
`deepseek-v4-pro` como fronteira. O custo ponderado aplica 0,2 ao token local e
1,0 ao token de fronteira.

| Braço | Aprovação | Tokens | Custo ponderado | Latência média | Decisões |
| :--- | ---: | ---: | ---: | ---: | :--- |
| local | 1,000 | 18.819 | 3.763,80 | 14.963 ms | — |
| frontier | 1,000 | 27.805 | 27.805,00 | 25.679 ms | — |
| binary | 1,000 | 28.458 | 20.444,40 | 20.447 ms | 8 `local_large`, 6 `frontier` |

O binário ficou **26,5% abaixo da fronteira** em custo ponderado, mas custou
**5,43× o local**. Não houve ganho de qualidade: o modelo local acertou também
as tarefas difíceis. O valor medido está no custo, não na qualidade; por isso a
promoção para padrão continua exigindo um corpus maior e telemetria de produção.
Relatório: [`routing-binary-ab.md`](../quality/routing-binary-ab.md).

### 5.3 Embeddings reais

A validação usou um endpoint local OpenAI-compatible com os modelos reais. O
corpus tem 16 documentos em pt-BR e 16 consultas mistas pt-BR/en, com 3
repetições. O EmbeddingGemma 2 rodou em GPU; o Qwen3-Embedding-4B rodou em CPU
porque o modelo de 4B não cabe nos 4 GB da GPU.

| Modelo | Dimensões | Recall@1 | Recall@3 | MRR@10 | Consulta p50 | Índice/doc |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| EmbeddingGemma 2 | 768 | **0,875** | **1,000** | **0,938** | **134 ms** | **56 ms** |
| Qwen3-Embedding-4B (768) | 2560 → 768 | 0,813 | 1,000 | 0,906 | 8.474 ms | 9.627 ms |
| Qwen3-Embedding-4B (2560) | 2560 | 0,875 | 1,000 | 0,927 | 8.411 ms | 8.918 ms |
| Lexical BM25 | — | 0,813 | 0,938 | 0,865 | — | — |

O EmbeddingGemma 2 teve o melhor top-1 e a menor latência. O Qwen3 completo
empatou no recall@1, mas o truncamento para 768 caiu para o nível lexical neste
corpus. A integração passou nos dois modelos: `embeddings test` e reindexação
4/4, sem falhas. Com o endpoint desligado, o teste retornou `ok=false` e a busca
lexical permaneceu como fallback. Relatório:
[`memory-embeddings-live.md`](../quality/memory-embeddings-live.md).

## 6. Testes de integração e release

| Verificação | Resultado |
| :--- | :--- |
| Suíte C++ | 4.289/4.289 verificações aprovadas, incluindo os testes de âncoras e do roteador |
| Vitest | 640/640 |
| Tutorial ponta a ponta | aprovado |
| Exemplos 01–14 | 14/14 aprovados |
| Corpus avançado (`--check`) | reproduzido |
| Corpus dos 14 exemplos (`--check`) | reproduzido |
| Agente de claims (registro + extração) | e2e com provedor falso: 1 candidato, 1 extraído, 1 proposta, 0 falhas |
| `check:secrets` | nenhum segredo ou dado pessoal |
| `check:version` | 2.8.0 consistente |

## 7. Limitações ainda enfrentadas

1. **A/B live com dados curados e corpus derivado.** As 20 tarefas, o corpus
   curado e os 14 exemplos não substituem projetos reais de cliente; os exemplos
   são copiados e semeados pelo harness, não são dados de cliente. Os resultados
   mostram direção, não generalização.
2. **Latência variável do provedor.** O gate Jev foi confirmado em 3 repetições,
   mas o agente DeepSeek ainda varia; tokens e latência podem mudar com o
   provedor escolhido.
3. **Valor binário.** O validador da suíte de valor exige um token que só existe
   na lição curada; isso prova a entrega, mas não mede gradação de qualidade.
4. **Roteamento com paridade de qualidade.** O A/B real do `routing.mode=binary`
   teve aprovação 1,000 nos três braços e economia de 26,5% contra a fronteira,
   mas o modelo local também acertou as tarefas difíceis. Não houve ganho de
   qualidade, o binário custou 5,43× o local, e o corpus tem 7 tarefas e 2
   repetições.
5. **Precisão da travessia.** A cadeia de 3 saltos teve recall 1,000 e precisão
   0,500 no corpus curado; nos 14 exemplos a precisão ficou entre 0,300 e 1,000.
   O pacote inclui afirmações relacionadas ao mesmo anchor além do alvo. É
   aceitável como contexto, mas ainda pede ajuste de ranking ou poda.
6. **Cold start de caminhos.** `pathServing` depende de caminhos consolidados a
   partir de traços aprovados; projetos novos não têm caminhos até acumular uso.
7. **Pontes são hipóteses.** O detector não cria arestas; a promoção é manual e
   exige revisão humana.
8. **Embeddings reais.** A validação com EmbeddingGemma 2 e Qwen3-Embedding-4B
   foi concluída em endpoint local e corpus de 16 documentos. O Qwen3 de 4B
   rodou em CPU e ficou caro (8,5 s por consulta); o EmbeddingGemma 2 foi o
   melhor custo-benefício. Ainda falta um endpoint de produção e dados de
   cliente.
9. **UI do agente de extração de claims.** Registro/listagem, seletor em
   Propriedades, salvamento por projeto, extração via CLI e validação manual no
   Studio estão concluídos. A validação encontrou e corrigiu um bug de comparação
   do alvo salvo causado pela ordem das chaves JSON.

10. **Uma máquina.** Sincronização multi-máquina e coordenação de múltiplos
    workers continuam fora do escopo desta release.
11. **Custo monetário.** Os relatórios medem tokens e latência, não preço em
    dólares nem custo de infraestrutura.
12. **Gate não é prova de correção.** O passo `memory_gate` é a fonte da decisão
    efetiva; `memory_hit` pode registrar injeção do brief antes do gate. O gate
    reduz custo, mas não substitui validação e A/B.

## 8. Reprodução

```powershell
# A/B live de paridade e valor (requer provedor e chave)
py -3 scripts/e2e_memory_ab_live.py --repetitions 3
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent

# Gate com Jev (o agente ainda é DeepSeek)
py -3 scripts/e2e_memory_ab_live.py --repetitions 1 --gate-mode enforce `
  --gate-provider jev --gate-model jev-latest
py -3 scripts/e2e_memory_ab_live.py --repetitions 1 --memory-dependent `
  --gate-mode enforce --gate-provider jev --gate-model jev-latest

# Interruptores avançados (determinístico, sem modelo)
py -3 scripts/memory_advanced_eval.py --write --runs 3
py -3 scripts/memory_advanced_eval.py --check --runs 3

# Interruptores sobre os 14 exemplos (determinístico, sem modelo)
py -3 scripts/memory_examples_eval.py --write --runs 3

# Roteamento binário com chamadas reais (requer provedor e chave)
py -3 scripts/routing_binary_eval.py --write --repetitions 2
py -3 scripts/routing_binary_eval.py --check --repetitions 2

# Embeddings reais (requer endpoint OpenAI-compatible)
py -3 scripts/memory_embeddings_eval.py --base-url http://127.0.0.1:18081/v1 `
  --model embeddinggemma-2 --repetitions 3
py -3 scripts/memory_embeddings_eval.py --base-url http://127.0.0.1:18082/v1 `
  --model qwen3-embedding-4b --truncate-dim 768 --repetitions 3
```

## 9. Evidências

| Evidência | Arquivo |
| :--- | :--- |
| Linha de base E0 | `tests/fixtures/memory-baseline/baseline.json` |
| Avaliação E20 | `tests/fixtures/memory-eval-v1-report.json` |
| A/B live — paridade | `tests/fixtures/memory-ab-live-report.json` |
| A/B live — valor | `tests/fixtures/memory-ab-live-value-report.json` |
| Gate DeepSeek — paridade | `tests/fixtures/memory-ab-live-gate-enforce-deepseek-report.json` |
| Gate DeepSeek — valor | `tests/fixtures/memory-ab-live-value-gate-enforce-deepseek-report.json` |
| Gate Jev — paridade | `tests/fixtures/memory-ab-live-gate-enforce-jev-report.json` |
| Gate Jev — valor | `tests/fixtures/memory-ab-live-value-gate-enforce-jev-report.json` |
| Interruptores avançados | `tests/fixtures/memory-advanced-report.json` |
| Corpus avançado | `tests/fixtures/memory-advanced-v1.json` |
| Interruptores nos 14 exemplos | `tests/fixtures/memory-examples-advanced-report.json` |
| Roteamento binário real | `tests/fixtures/routing-binary-report.json` |
| Embeddings reais | `tests/fixtures/memory-embeddings-live-report.json` |
| Relatório do gate | `docs/quality/memory-gate-ab.md` |
| Relatório dos interruptores | `docs/quality/memory-advanced-ab.md` |
| Relatório dos exemplos | `docs/quality/memory-examples-advanced-ab.md` |
| Relatório do roteamento | `docs/quality/routing-binary-ab.md` |
| Relatório dos embeddings | `docs/quality/memory-embeddings-live.md` |
