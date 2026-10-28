# Bibliotecas + GitHub — Plano de homologação (Fase 7)

> **Status:** automação implementada e executada em 2026-10-27 com `status=passed-automated`.
> **Escopo desta rodada:** Bibliotecas, assets e fonte GitHub.
> **Pendente para promoção:** 3 casos manuais (tema/tamanhos em revalidação, instalação/upgrade e
> offline). Navegação por teclado e foco foi aprovada manualmente em 2026-10-27.
> **Plano mestre:** [`libraries-plan.md`](./libraries-plan.md), seção 11, Fase 7.
> **Matriz de release:** [`beta2.3-test-plan.md`](./beta2.3-test-plan.md), seção 2.10.

## 1. Objetivo

Demonstrar, de forma repetível no mesmo commit, que o fluxo de Bibliotecas:

1. cria fontes e bibliotecas nas Propriedades;
2. sincroniza apenas manifesto/árvore, sem baixar blobs;
3. baixa, audita e promove assets somente depois da seleção;
4. instala skills/MCPs e permite derivados, pins e templates;
5. rejeita conteúdo hostil, licença proibida, path traversal e excesso de tamanho;
6. responde de forma estável a 404, rate limit e limite de tamanho;
7. preserva o comportamento offline e a migração legada;
8. produz evidências auditáveis para a promoção do Beta 2.3.

Esta rodada **não** substitui a homologação manual de interface nem a instalação em máquina limpa.
Ela fecha o gate determinístico e deixa os itens manuais explicitamente pendentes.

## 2. Estratégia em três camadas

| Camada | O que valida | Como | Quando falha |
| :--- | :--- | :--- | :--- |
| Frontend/integração | Modelo, store, serviço, gate, árvore, seletor, pins, migração e i18n | Vitest, 8 arquivos focados | Bloqueia a rodada |
| Nativo/mock | URL, commit pin, árvore, licença, download, 404, rate limit e tamanho | C++ com GitHub falso em loopback | Bloqueia a rodada |
| Live opcional | Contrato real do GitHub público | `fetch_library_source` contra `octocat/Hello-World` | Falha separada; não mascarado pelo mock |

A combinação evita duas armadilhas: um mock que “passa” sem exercitar o endpoint C++ real e um teste
live que depende de rede, rate limit e disponibilidade do GitHub.

## 3. Mock GitHub e override test-only

### 3.1 Mock

`scripts/mock-github-server.mjs` sobe um servidor HTTP em `127.0.0.1`, com porta aleatória, e serve:

- `e2e/library`: licença MIT, commit fixo, árvore com skill segura, skill com shell, skill hostil,
  MCP, skill grande, README e submodule;
- `blocked/license`: licença GPL-3.0;
- `rate/limit`: HTTP 403 com mensagem de rate limit;
- `missing/repo`: HTTP 404;
- conteúdo raw dos assets, incluindo um blob acima do limite.

Para inspeção manual, `npm run mock:github` sobe o mock na porta 4319 e imprime as bases de API e
raw. O runner usa porta aleatória e encerra o servidor ao final.

O mock registra cada requisição em memória. O runner grava a lista em
`mock-github-requests.json`, o que comprova que o C++ consultou o manifesto, o commit, a árvore e
apenas os blobs esperados.

### 3.2 Override no C++

`CppCore/src/libraries/GitHubSource.cpp` aceita `ARESTRIX_TEST_GITHUB_API_BASE` e
`ARESTRIX_TEST_GITHUB_RAW_BASE` **somente** quando:

1. `ARESTRIX_TEST_MODE=1`; e
2. a URL é `http://127.0.0.1:<porta>` ou `http://localhost:<porta>`.

Fora disso, as bases de produção continuam fixas em `https://api.github.com` e
`https://raw.githubusercontent.com`. O `requirePublicPeer` do cliente HTTP é desligado apenas para
a URL de loopback já validada. Não há redirect, host arbitrário, esquema diferente de HTTP nem
aceitação de IP privado fora de loopback.

O override existe para testes determinísticos. Ele não deve ser definido em produção; a combinação
`ARESTRIX_TEST_MODE` + loopback é uma trava contra uso acidental, não uma fronteira de segurança
contra um processo local malicioso.

## 4. Matriz automatizada

### 4.1 Funcional

| ID | Caso | Evidência |
| :--- | :--- | :--- |
| LIB-F-01 | Catálogo embutido e Biblioteca Arestrix | Vitest |
| LIB-F-02 | Criar fonte GitHub e biblioteca associada | Vitest |
| LIB-F-03 | Sincronizar sem baixar blobs | Vitest + requisições do mock |
| LIB-F-04 | Assets remotos entram como `PENDING` | Vitest |
| LIB-F-05 | Baixar, auditar e instalar skill | Vitest |
| LIB-F-06 | Baixar, auditar e instalar MCP | Vitest |
| LIB-F-07 | Derivar, editar e detectar atualização | Vitest |
| LIB-F-08 | Template de vértice como asset | Vitest |
| LIB-F-09 | Pins e habilitação no Projeto | Vitest |
| LIB-F-10 | Migração de `arestrix_github_skills_repos` | Vitest |

### 4.2 Segurança

| ID | Caso | Evidência |
| :--- | :--- | :--- |
| LIB-S-01 | Skill hostil rejeitada e conteúdo descartado | Vitest |
| LIB-S-02 | Veredito `CAUTION` exige confirmação | Vitest |
| LIB-S-03 | Licença fora da allowlist bloqueada | Vitest |
| LIB-S-04 | Path traversal e payload inválido recusados | C++ |
| LIB-S-05 | Asset acima do limite recusado | C++ |
| LIB-S-06 | Segredos MCP somente por `${VAR}` | Vitest |

### 4.3 Resiliência

| ID | Caso | Evidência |
| :--- | :--- | :--- |
| LIB-R-01 | Manifesto válido com commit e licença SPDX | C++ + mock |
| LIB-R-02 | Repositório inexistente retorna erro estável | C++ + mock |
| LIB-R-03 | Rate limit retorna erro estável | C++ + mock |
| LIB-R-04 | Commit pin e árvore estáveis | C++ + mock |
| LIB-R-05 | Smoke live do manifesto GitHub | C++ + `--live` (opcional) |

## 5. Homologação manual

Estes casos exigem a aplicação nativa e evidência visual. A pasta
`release/homologation/<runId>/screenshots/` deve receber as capturas no momento da execução.

| ID | Caso | Critério | Status |
| :--- | :--- | :--- | :--- |
| LIB-M-01 | Tema claro/escuro e larguras 1024/1800 px | Árvore, detalhe e seletor legíveis, sem corte ou sobreposição | Reprovado em 2026-10-27: o canvas não seguia o tema e o vértice ficava branco. Correção aplicada; revalidar. |
| LIB-M-02 | Navegação por teclado e foco visível | Tab/Shift+Tab, Enter, Esc e foco consistente em árvore, detalhe e modal | **Aprovado em 2026-10-27.** |
| LIB-M-03 | Instalação limpa e upgrade | Instala, abre, preserva projetos e bibliotecas; desinstala sem resíduo crítico | Pendente. |
| LIB-M-04 | Offline após sincronização/cache | Assets já instalados continuam utilizáveis sem rede; erro de rede é claro | Pendente. |

Checklist mínimo de evidência manual:

- [x] Teclado e foco validados manualmente em 2026-10-27 (LIB-M-02).
- [ ] Captura da tela de Bibliotecas no Studio em tema claro.
- [ ] Captura da mesma tela em tema escuro, com canvas e vértices escuros.
- [ ] Captura da árvore com um asset `PENDING`, um aprovado e um rejeitado.
- [ ] Captura do seletor no agente com asset de biblioteca.
- [ ] Captura/log da instalação limpa e do upgrade.
- [ ] Captura/log do uso offline.

## 6. Evidências geradas

Cada execução cria `release/homologation/<runId>/` com:

| Arquivo | Conteúdo |
| :--- | :--- |
| `environment.json` | Commit, dirty tree, SO, Node, binários e opções |
| `homologation-report.json` | Status, estágios, matriz e pendências manuais |
| `matrix.csv` | Matriz achatada para conferência rápida |
| `frontend-e-integracao-vitest-.log` | Saída do Vitest |
| `c-mock-github-.log` | Saída do C++ com mock |
| `mock-github-requests.json` | Requisições recebidas pelo mock |
| `cpp-smoke-github-live.log` | Saída do smoke live, quando `--live` |

`release/` é ignorado pelo Git; as evidências são locais e devem ser anexadas ao registro de
homologação ou ao artefato de release, não commitadas.

## 7. Como executar

```bash
# Gate determinístico completo (frontend + C++ com mock)
npm run homologation:libraries

# Inclui o manifesto real do GitHub
npm run homologation:libraries:live

# Apenas frontend, em ambiente sem binário nativo
node scripts/homologation-libraries.mjs --no-native
```

O runner não compila nada por conta própria. Antes da execução nativa, o binário
`build/arestrix_tests.exe` (Windows) ou `build/arestrix_tests` (Linux) precisa existir no commit
corrente. O smoke live também exige acesso à rede e respeita o rate limit anônimo do GitHub.
`--no-native` gera `status=partial`, pois os casos nativos ficam `skipped`.

## 8. Critérios de saída

A Fase 7 pode ser considerada concluída quando:

1. `npm run homologation:libraries` termina com `status=passed-automated` (sem `--no-native`);
2. o mesmo commit tem `npx tsc --noEmit`, `npm test`, C++ completo e `npm run build` verdes;
3. os 3 casos manuais pendentes têm evidência anexada; LIB-M-02 já está aprovado;
4. o smoke live foi executado ao menos uma vez no commit candidato;
5. não há falha P0/P1 conhecida;
6. o resultado entra no `test-audit-log.md` e na seção 2.10 do plano Beta 2.3.

`status=passed-automated` **não** é aprovação de release. A promoção continua bloqueada enquanto
`pendingManual > 0` e enquanto o gate de release não tiver `dirty=false`.

## 9. Limitações conhecidas

- O mock não substitui o GitHub real para reprodutibilidade de rate limit, redirects e mudanças de
  contrato; por isso o smoke live é separado.
- O runner não controla a WebView nem captura screenshots; os casos de UI continuam manuais.
- O mock não testa autenticação com PAT nem repositórios privados; isso permanece fora do MVP.
- O tempo de execução nativo depende do build local e do cache do sistema; a matriz registra logs,
  não benchmarks.
