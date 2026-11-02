# Estratégia de testes e validação da memória v1

**Escopo:** memória de projeto do Arestrix 2.5, do núcleo E0–E20 ao Studio,
CLI, assistente, workspace e serviço institucional.
**Base de código:** `main` após o merge E0–E20.
**Objetivo:** provar que a memória é correta, isolada, reproduzível e segura, e
que os interruptores experimentais não mudam o comportamento quando desligados.

## 1. Princípios

1. **Falha fechada.** Ausência de configuração, provedor ou validação nunca
   libera conhecimento remoto ou escrita não autorizada.
2. **Leitura não escreve conhecimento.** Busca, brief, travessia e inspeção só
   podem gravar telemetria.
3. **Determinismo.** Mesmo fixture, mesma semente e mesmo banco congelado
   produzem os mesmos contadores e fingerprints.
4. **Evidência por release.** Nenhuma release é aprovada sem artefatos de teste
   anexados e comparáveis.
5. **Interruptor desligado preserva a etapa anterior.** `pathServing`,
   `traversal`, `bridges` e `routing.mode` em `off` não podem alterar a linha de
   base.
6. **Sem dados reais em CI.** Fixtures, hashes e provedores simulados; dados de
   cliente só em ambiente controlado e anonimizado.
7. **Teste que falha é bloqueante.** Flake não vira "skip" sem issue, dono e
   prazo.

## 2. Camadas de teste

### L0 — Estática e empacotamento

- `py -3 CppCore/dev.py --check`
- `npx tsc --noEmit`
- `npm run build`
- `npm run check:secrets`
- `npm run check:local-assets`
- `npm run check:version`
- `python scripts/check_third_party_licenses.py`
- verificação de links relativos da documentação
- build MSVC e GCC com warnings relevantes

Saída esperada: binários `arestrix`, `arestrix-cli`, `arestrix-memory`,
`arestrix-gateway` gerados sem erro.

### L1 — Unidade

C++ (`CppCore/tests/main.cpp`):

- store, schema, migrações e sanitização;
- parser de predicados, claims, lentes e facetas;
- caminhos, utilidade, decaimento e invalidação;
- âncoras, travessia, orçamento e cobertura;
- roteador de modelos e privacidade;
- pontes e deduplicação;
- ferramentas do assistente e riscos de confirmação.

TypeScript/Vitest:

- serialização de configuração de memória;
- tema efetivo e herança;
- perfis de embedding e fingerprints;
- painel de memória e estados de interruptores.

### L2 — Propriedades e invariantes

Testes determinísticos que não dependem de um caso específico:

- `search` e `traversal` não incrementam `knowledge_version`;
- `path` nunca fica `active` com claim expirada ou inativa;
- atalho `via_caminho` não entra em busca, conflito ou evidência;
- reprocessar o mesmo `trace_id` não altera contagens;
- dois caminhos com a mesma assinatura compartilham esquema;
- citação de caminho e de claim expande o uso nos dois sentidos;
- `off`/`candidate`/`suspended` nunca são servidos como caminho ativo;
- bridge hypothesis nunca cria aresta sem decisão explícita;
- `localOnly` nunca escolhe modelo remoto;
- nenhum segredo conhecido aparece no brief, telemetria ou proposta.

### L3 — Integração determinística

Obrigatórios em todo PR que toque memória:

```powershell
py -3 scripts/e2e_memory.py
py -3 scripts/e2e_memory_ab.py
py -3 scripts/memory_baseline.py --runs 3 --check
py -3 scripts/memory_evaluation.py --runs 1 --check
```

Cobrem:

- ciclo learn/consult, propostas, validação e promoção;
- dados usados/consultados/citados;
- fontes, trechos, claims, lentes, facetas e caminhos;
- executor A/B com banco congelado e dois braços;
- variações lexical, híbrida com fallback, facetas+travessia,
  caminhos+serviço e roteamento shadow;
- linha de base congelada e schema fingerprint.

### L4 — Matriz de plataforma

Obrigatória no CI:

| Plataforma | Build | Testes |
| :--- | :--- | :--- |
| Windows MSVC | hardening Release | CTest |
| Windows MSVC + ONNX/DirectML | release path | CTest |
| Ubuntu GCC | hardening Release | CTest |
| Linux/macOS | build e smoke | quando o runner existir |

O job Linux já encontrou uma ambiguidade de `Json::at(size_t)` que o MSVC não
via; a matriz é obrigatória, não opcional.

### L5 — Provedores e modelos reais

Execução periódica e antes de release:

- embeddings reais EmbeddingGemma 2 e Qwen3-Embedding-4B;
- modelo local OpenAI-compatible para claims;
- provedores de fronteira (DeepSeek, Anthropic, OpenAI, Google, Mistral);
- gateway com budget, chave inválida e rate limit;
- modelo local indisponível, timeout e resposta inválida.

Resultado esperado: fallback lexical, falha fechada ou erro classificado; nunca
corrupção de índice ou promoção parcial.

Teste real controlado:

```powershell
py -3 scripts/e2e_memory_deepseek_long.py --keep
```

O fixture `tests/fixtures/memory-deepseek-long` roda o mesmo agente com memória
ON e OFF, seis gravações, uma busca e uma resposta final. A memória ON responde
`ORION-42`; a OFF responde `UNKNOWN`. O script exige `DEEPSEEK_API_KEY`, limita o
total de tokens e grava `tests/fixtures/memory-deepseek-long-report.json`.

Execução registrada: **22.639 tokens** (ON 14.164 / OFF 8.475), nove chamadas no
braço ON e oito no OFF; todos os checks passaram.

Teste de substituição de contexto:

```powershell
py -3 scripts/e2e_memory_prompt_reduction.py --keep
```

O braço `full` recebe um manual longo no prompt; o braço `memory` recebe um
prompt curto e a mesma verdade pela memória. Resultado registrado: **24.718
tokens de prompt no full contra 3.228 no memory (87% de redução)**, com a mesma
resposta `ORION-42`. O relatório fica em
`tests/fixtures/memory-prompt-reduction-report.json`.

### L6 — Desempenho e soak

Medições com dados sintéticos versionados:

- 10.000 traços consolidados dentro do orçamento de manutenção;
- 10.000 consultas de exploração com fração convergente;
- travessia com 200+ nós, limites de tempo, candidatos e profundidade;
- busca com índice `ready`, `partial` e `failed`;
- 24 h de worker de manutenção sem crescimento anormal do banco;
- memória de 10 mil claims, 5 mil caminhos e 2 mil facetas;
- compressão/retenção e expurgo sem quebrar evidência.

Metas iniciais, sujeitas a calibração com a máquina de referência:

| Métrica | Guardrail |
| :--- | ---: |
| busca lexical p95 | < 100 ms |
| busca híbrida p95 | < 250 ms |
| travessia p95 | < 500 ms |
| consolidação de 10 mil traços | < 60 s |
| worker ocioso | sem crescimento de RSS após 24 h |
| overhead do brief | dentro do `contextBudgetTokens` |

### L7 — Segurança e privacidade

- prompt injection em trechos, claims e bridge hypothesis;
- vazamento entre projetos e entre workspace/projeto;
- `localOnly` com provedor remoto configurado;
- política de acesso por claim/caminho;
- segredos em arquivos de fonte, logs e propostas;
- path traversal, symlink e arquivo gigante;
- banco corrompido e quarentena;
- assinatura/verificação do pacote.

## 3. Matriz de cobertura por funcionalidade

| Funcionalidade | L1 | L2 | L3 | L5 | L6 | L7 |
| :--- | :--: | :--: | :--: | :--: | :--: | :--: |
| Lições, fatos e playbooks | x | x | x | x |  | x |
| Fontes e trechos | x | x | x | x |  | x |
| Claims e lentes | x | x | x | x | x | x |
| Facetas | x | x | x | x | x |  |
| Caminhos e utilidade | x | x | x |  | x | x |
| Serviço de caminhos | x | x | x |  | x | x |
| Travessia e cobertura | x | x | x |  | x | x |
| Roteamento de modelos | x | x |  | x | x | x |
| Pontes | x | x | x |  |  | x |
| Assistente do Studio | x | x |  |  |  | x |
| CLI e migração | x |  | x |  |  | x |

## 4. Dados e fixtures

- `tests/fixtures/memory-baseline/`: linha de base curada e `baseline.json`.
- `tests/fixtures/memory-ab/` + `memory-ab-v1.json`: piloto pareado.
- `tests/fixtures/memory-claims/annotated.json`: 50 trechos anotados.
- `tests/fixtures/memory-eval-v1.json` + relatório: casos negativos e variantes.
- Testes C++ sintéticos para grafo, ciclos, acesso e orçamento.

Regras:

- toda fixture tem hash/versionamento;
- nenhuma fixture contém chave, PII ou caminho absoluto de máquina;
- mudança de fixture exige atualização explícita do relatório e do baseline;
- dados reais só em ambiente isolado, com retenção definida.

## 5. Migração e compatibilidade

Testar, no mínimo:

1. banco 2.5.0 sem tabelas E11–E20;
2. abertura, criação idempotente e fingerprint novo;
3. leitura/escrita após upgrade;
4. banco somente leitura não sofre migração;
5. import/export de conhecimento cognitivo;
6. rollback: restaurar backup e abrir com a versão anterior;
7. banco corrompido vai para quarentena sem apagar o original.

Critério: nenhuma migração destrutiva; toda tabela nova é `CREATE IF NOT
EXISTS`; `memory_meta.schema_version` e `memory health` refletem o estado real.

## 6. A/B e decisão de interruptores

Protocolo:

- mesma semente, mesmo `input_hash`, mesmo banco congelado;
- braço ON somente leitura e braço OFF com `ARESTRIX_MEMORY_DISABLE=1`;
- validador independente e tarefas sem validador fora do par;
- relatório com IC 95%, deltas pareados, tokens, turnos e latência;
- fingerprint do banco antes/depois obrigatório;
- custo monetário só entra quando houver tabela de preços confiável.

Um interruptor só pode virar padrão `on` se:

1. a variante tiver ganho estatístico e operacional sobre a referência;
2. não houver regressão de segurança, isolamento ou determinismo;
3. o custo e o tempo de manutenção estiverem dentro do orçamento;
4. a decisão estiver registrada no relatório e no `CHANGELOG`.

## 7. Gates de release

### PR

- L0, L1, L2, L3;
- quatro checks obrigatórios do CI;
- revisão de schema/migração quando aplicável.

### Merge train

- PR verde e atualizado com `main`;
- sem conflito de migração;
- baseline e avaliação reproduzidas;
- tag de candidato após o último PR.

### Release candidate

- L4 completo;
- L5 executado com provedores reais;
- L6 com números registrados;
- L7 sem achado crítico/alto aberto;
- instalação limpa, upgrade, uninstall e assinatura.

## 8. Evidência e retenção

Cada release guarda:

- `baseline.json` e relatório A/B;
- `memory-eval-v1-report.json`;
- logs dos jobs Windows/Linux;
- lista de provedores e modelos testados;
- schema fingerprint e versão;
- issues de flake/security com dono e prazo.

Artefatos ficam no repositório ou no anexo da release; logs com dados de cliente
são proibidos.

## 9. Lacunas atuais

1. Validação com provedores reais ainda não foi executada.
2. Não há runner macOS na matriz.
3. O teste de 10 mil traços e o soak de 24 h ainda são metas, não gates.
4. A cobertura de segurança de prompt injection em bridge hypothesis precisa de
   casos adversariais dedicados.
5. Migração de um banco 2.5.0 real ainda deve ser exercitada em release candidate.

## 10. Ordem de execução

1. Fechar L0–L3 e a matriz Windows/Linux.
2. Executar L5 com os provedores reais.
3. Rodar L6 e registrar os números.
4. Executar L7.
5. Repetir o A/B e decidir os interruptores.
6. Só então promover `memory-v1.0` para estável.
