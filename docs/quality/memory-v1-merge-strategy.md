# Estratégia de merge do multigrafo (E0–E20)

**Decisão:** trem de PRs empilhados, na ordem `E0 → E1 → … → E20`.
**Base:** `main` em `5bec4aa` (tag `v2.5.0`).
**Topo da cadeia:** `mem/E20-avaliacao-final`.
**Estado:** as 21 branches já estão publicadas em `origin`.

## 1. Por que PRs empilhados

A cadeia é linear: cada etapa parte da anterior e `main` é ancestral de todas.
O merge não tem conflito estrutural; o risco é revisar 16 mil linhas de uma vez e
perder a capacidade de:

- identificar a etapa que introduziu uma regressão;
- revisar schema, migração e comportamento em blocos pequenos;
- reverter uma única etapa sem derrubar o restante;
- preservar o histórico de decisões do plano.

Squash da cadeia inteira e merge direto da E20 estão descartados.

## 2. Ordem e branches

| Ordem | Etapa | Branch |
| ---: | --- | --- |
| 0 | Linha de base | `mem/E0-linha-de-base` |
| 1 | Validações e veredito | `mem/E1-validacoes-e-veredito` |
| 2 | Consultado vs. usado | `mem/E2-consultado-vs-usado` |
| 3 | Indexação automática | `mem/E3-indexacao-automatica` |
| 4 | Evidência autocontida | `mem/E4-evidencia-autocontida` |
| 5 | Classificação de falha | `mem/E5-classificacao-falha` |
| 6 | Tema padrão por skill | `mem/E6-tema-padrao-skill` |
| 7 | Embeddings MRL | `mem/E7-embeddings-mrl` |
| 8 | Cache de prompt | `mem/E8-cache-prompt` |
| 9 | Executor A/B | `mem/E9-executor-ab` |
| 10 | Marco de recorrência | `mem/E10-marco-recorrencia` |
| 11 | Versão de traços | `mem/E11-versao-tracos` |
| 12 | Fontes e trechos | `mem/E12-fontes-trechos` |
| 13 | Afirmações | `mem/E13-afirmacoes` |
| 14 | Lentes e facetas | `mem/E14-lentes-facetas` |
| 15 | Caminhos | `mem/E15-caminhos` |
| 16 | Serviço de caminhos | `mem/E16-servico-caminhos` |
| 17 | Travessia em feixe | `mem/E17-travessia-feixe` |
| 18 | Roteador de modelos | `mem/E18-roteador-modelos` |
| 19 | Pontes interdomínio | `mem/E19-pontes-interdominio` |
| 20 | Avaliação final + integração do assistente | `mem/E20-avaliacao-final` |

### Como abrir cada PR

Sem `gh` instalado, use a URL de comparação do GitHub:

```text
https://github.com/frankalcantara/Arestrix/pull/new/<branch>
```

O PR deve apontar para `main`, com o título e o corpo do modelo abaixo. Abra
somente o próximo PR da ordem; os seguintes podem existir como rascunho, mas não
devem ser mergeados fora de ordem.

## 3. Regras do trem

1. Criar tag `memory-v1.0-rc1` no topo atual antes do primeiro merge.
2. Abrir um PR por etapa, sempre com base em `main`.
3. Fazer merge **somente** do PR da vez. Não abrir caminho paralelo para E2 antes
   de E0 e E1 estarem em `main`.
4. Usar **rebase and merge** (ou fast-forward quando o GitHub permitir),
   preservando um commit por etapa. Não usar squash.
5. Não mergear com checks vermelhos, revisão pendente ou documentação de etapa
   ausente.
6. Se uma etapa falhar depois do merge, pausar o trem e corrigir na própria
   branch antes de continuar.
7. Ao final, criar a tag `memory-v1.0` no commit da E20 já em `main`.

## 4. Gates por PR

Obrigatórios no GitHub:

- `quality`
- `frontend`
- `native-windows`
- `native-linux`

Obrigatórios localmente, no topo da branch:

```powershell
py -3 CppCore/dev.py --full
py -3 scripts/e2e_memory.py
py -3 scripts/e2e_memory_ab.py
py -3 scripts/memory_baseline.py --runs 3 --check
py -3 scripts/memory_evaluation.py --runs 1 --check
```

Em toda etapa que altere o gerador:

```powershell
py -3 scripts/e2e_examples.py
```

Critérios adicionais de revisão:

- migração de schema idempotente e compatível com banco 2.5.0;
- nenhuma escrita de conhecimento em busca/travessia;
- interruptores novos desligados por padrão;
- baseline atualizado somente quando a mudança for intencional e documentada;
- `CHANGELOG.md` com a etapa;
- documentação PT/EN coerente com o comportamento;
- risco de segurança e confinamento revisto.

## 5. Modelo de PR

Título:

```text
memory(E<n>): <título da etapa>
```

Corpo mínimo:

```markdown
## Etapa
E<n> — <título>

## Escopo
- ...

## Invariantes verificadas
- busca somente leitura;
- conhecimento novo entra como proposta;
- interruptor desligado preserva a etapa anterior;
- schema/migração documentados.

## Evidência
- [ ] `quality`
- [ ] `frontend`
- [ ] `native-windows`
- [ ] `native-linux`
- [ ] `dev.py --full`
- [ ] `e2e_memory.py`
- [ ] `e2e_memory_ab.py`
- [ ] `memory_baseline.py --check`
- [ ] `memory_evaluation.py --check`
```

## 6. Rollback

- Antes do merge: fechar o PR e corrigir a branch.
- Depois do merge, antes da etapa seguinte: `git revert <commit-da-etapa>` em
  `main`, abrir PR de revert e pausar o trem.
- Depois de várias etapas: não tentar reescrever a cadeia; reverter a etapa
  suspeita e manter as seguintes, ou restaurar a tag `memory-v1.0-rc1` como
  referência de comparação.
- Nunca fazer force-push em `main`.

## 7. Depois do merge

1. Branch `docs/memory-v1`: revisão integral da documentação PT/EN contra o
   comportamento real, flags, CLI, UI, banco e limitações.
2. Branch `test/memory-v1-strategy`: estratégia formal de testes e validação,
   separando contrato, determinismo, integração, provedores reais, A/B,
   desempenho, soak e release.
3. Só depois avaliar a promoção de padrões: `pathServing`, `traversal`,
   `bridges` e `routing.mode` continuam `off` até haver medição real.
