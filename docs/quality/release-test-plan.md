# Plano de testes de release — Arestrix 2.8

> **Status:** núcleo automatizado verde em 2026-11-12; evidência de release, distribuição e
> homologação manual ainda pendentes.
>
> **Versão atual:** `2.8.0` (2026-11-11).
>
> **Última revisão:** 2026-11-12.
>
> **Histórico:** este documento substitui o plano do Beta 2.3. O conteúdo anterior continua no
> histórico do Git e no `test-audit-log.md`; `beta2.3-test-plan.md` permanece apenas como ponte.

Este é o documento canônico para decidir se o Arestrix 2.8 pode sair de pré-release e ser promovido
a uma release validada. Ele separa o que já tem evidência automatizada do que ainda exige máquina
limpa, Linux ou homologação visual.

## 1. Estado atual

| Área | Evidência mais recente | Estado |
| :--- | :--- | :--- |
| Frontend | `npm test`: **82 arquivos / 650 testes**; `tsc` limpo; `npm run build` aprovado | Verde |
| C++ | **4.299/4.299** verificações | Verde |
| E2E | tutorial + **14 exemplos** | Verde na última execução registrada |
| Bibliotecas | `homologation:libraries:live`: **passed-automated**; 58 Vitest, 46/46 C++ mock, 2/2 live, captura web LIB-M-01 | Verde no escopo automatizado; 2 manuais pendentes |
| Segurança | `check:secrets` limpo; `npm audit` com **0 vulnerabilidades**; `dependency-advisories.md` revisado | Verde |
| Release gate | `release/release-report.json` é de 2026-11-11, commit `2147b74`, `dirty=true` | **Desatualizado** |
| Distribuição | Instalador 2.7.0 validado localmente; 2.8.0 ainda não | Pendente |
| Linux | Build, runtime, router, gateway e pacote | Pendente |
| CI | `deploy.yml` verde em pushes recentes; proteção da `main` sem required status checks | Enforcement pendente |
| Catálogo remoto | Assinatura Ed25519/minisign e hospedagem | Pendente |

## 2. Bloqueios para promoção

- [ ] Rodar o gate completo em árvore limpa no commit candidato:
      `py scripts/release_check.py --full --e2e`.
- [ ] Gerar `release-report.json`, `release-manifest.json`, `SHA256SUMS` e `SBOM.json` no mesmo
      commit, com `status=passed` e `dirty=false`.
- [ ] Validar instalação limpa, upgrade e desinstalação da versão 2.8.0 em máquina sem Arestrix.
- [ ] Validar Intune/SCCM e instalação silenciosa no candidato 2.8.0.
- [ ] Produzir e validar pacote Linux (AppImage, `.deb` ou tarball) com dependências documentadas.
- [ ] Fechar LIB-M-03 (instalação/upgrade preservando bibliotecas) e LIB-M-04 (offline após cache).
- [ ] Confirmar LIB-M-01 na aplicação nativa e capturar o seletor de biblioteca dentro do agente.
- [ ] Definir chave, custódia, rotação e hospedagem do catálogo remoto assinado.
- [ ] Tornar a matriz de CI obrigatória na proteção da `main`.
- [ ] Executar a homologação manual do Studio (seção 4).
- [ ] Publicar checksums, SBOM, notices e notas de release.

## 3. Matriz automatizada

### 3.1 Gate local reproduzível

- [ ] `py scripts/release_check.py --full --e2e` no commit candidato.
- [ ] `release/release-report.json` com `status=passed` e `dirty=false`.
- [ ] `release/release-manifest.json`, `SHA256SUMS` e `SBOM.json` gerados no mesmo commit.
- [ ] Conferir `SHA256SUMS` com `sha256sum -c`/`Get-FileHash`.

> A última execução registrada do gate é anterior ao HEAD atual e foi feita com árvore suja. Não use
> esses artefatos como evidência de release.

### 3.2 Checks estáticos

- [ ] `npm run check:version`.
- [ ] `npm run check:local-assets`.
- [x] `npm run check:secrets` — 2026-11-12.
- [ ] `npm run check:skill-references`.
- [ ] `python scripts/check_third_party_licenses.py`.
- [x] `npx tsc --noEmit` — 2026-11-12.
- [x] `npm run build` — 2026-11-12.

### 3.3 Frontend

- [x] `npm test` — **82 arquivos / 650 testes** em 2026-11-12.
- [x] Validação de grafos, ciclos, subfluxos, arestas `call`/`feedback`, undo/redo.
- [x] Inspector de interação, formulário gerado, Call vertex, async e notificações.
- [x] Trace overlay e resumo no console.
- [x] i18n pt-BR/en-US sem chaves faltando ou sobrando.
- [ ] Revisão final de contraste WCAG e zero sombras/glows em texto.

### 3.4 Backend nativo

- [x] C++ **4.299/4.299** verificações.
- [ ] `py CppCore/dev.py --test --no-frontend` / CTest no commit candidato.
- [ ] Compilação com hardening (`/GS`, `/guard:cf`, `/sdl` ou stack protector/RELRO).
- [ ] Opcional antes do release: `--asan`, `--clang-tidy`, `--cppcheck`.

### 3.5 E2E

- [x] `py scripts/e2e_tutorial.py` na última execução registrada.
- [x] `py scripts/e2e_examples.py` — **14 exemplos**.
- [x] `py scripts/e2e_memory_institution.py` — HTTP/MCP, papéis e fail-closed.
- [x] Exemplos 06–12: chamadas, interação, formulário, paralelismo, subflow, Call e async.
- [x] Nenhum segredo, caminho pessoal ou artefato gerado em `examples/`.

### 3.6 Bibliotecas + GitHub

- [x] `npm run homologation:libraries` com `status=passed-automated`.
- [x] `npm run homologation:libraries:live` com smoke live.
- [x] Matriz, ambiente, logs, requisições do mock e captura web LIB-M-01 em
      `release/homologation/<runId>/`.
- [x] Segurança adversarial: skill hostil, `CAUTION`, licença, path traversal e tamanho.
- [x] Resiliência: 404, rate limit, commit pin e árvore.
- [ ] LIB-M-03: instalação limpa/upgrade preservando projetos e bibliotecas.
- [ ] LIB-M-04: uso offline após sincronização/cache.
- [ ] Confirmação nativa de tema/tamanhos do LIB-M-01.
- [ ] Captura do seletor de biblioteca dentro do agente.

### 3.7 Pacote e distribuição

- [ ] `py scripts/package_release.py` para o candidato 2.8.0.
- [ ] ZIP e/ou `.tar.gz` com sidecar `.sha256`.
- [ ] `package-manifest.json` e `SHA256SUMS` internos consistentes.
- [ ] `SBOM.json` com o mesmo commit do HEAD.
- [ ] Extrair em pasta limpa e executar `arestrix-cli version`.
- [ ] `arestrix-cli project validate --project examples/01-svg-agente`.
- [ ] Abrir o Studio extraído e confirmar UI e projetos.
- [ ] Instalador Inno Setup 2.8.0: instalação limpa, upgrade, uninstall, atalhos e modo silencioso.
- [ ] Intune/SCCM e perfil sem privilégios administrativos.
- [ ] Pacote Linux com dependências e caminhos de dados documentados.

### 3.8 Segurança e integridade

- [x] `npm audit` com **0 vulnerabilidades** em 2026-11-12.
- [x] Nenhuma chave real em binários, logs, exemplos, trace, notificações ou SBOM na última revisão.
- [ ] `ARESTRIX_HOME` e caminhos de runtime confinados.
- [ ] Checksums verificados no fluxo de release.
- [ ] Política fail-closed de gateway/interação revalidada.
- [x] Dependências e avisos revisados em `docs/security/dependency-advisories.md`.

## 4. Homologação manual

| ID | Caso | Estado |
| :--- | :--- | :--- |
| LIB-M-01 | Tema claro/escuro e larguras 1024/1800 px | Web aprovado; confirmação nativa pendente |
| LIB-M-02 | Navegação por teclado e foco | Aprovado em 2026-10-27 |
| LIB-M-03 | Instalação limpa e upgrade | Pendente |
| LIB-M-04 | Offline após sincronização/cache | Pendente |
| STUDIO-M-01 | Criar, abrir, salvar e reabrir projeto | Pendente |
| STUDIO-M-02 | Editar grafo, mover/ligar vértices e undo/redo | Pendente |
| STUDIO-M-03 | Executar pipeline com agente, interação, call, async e trace | Pendente |
| STUDIO-M-04 | Artefatos, console, tema/idioma e janelas flutuantes | Pendente |
| STUDIO-M-05 | Mensagens de erro e estados de execução | Pendente |

## 5. CI e proteção de branch

- [x] `.github/workflows/ci.yml` define quality, frontend, native Windows, native Linux e gates de
      segurança de workflow.
- [x] `.github/workflows/deploy.yml` executa o gate e publica o pacote Windows em pushes para
      `main`; execuções recentes terminaram verdes.
- [ ] A proteção da `main` ainda **não exige** status checks (`required_status_checks=null`).
      Enquanto isso, o CI de pull request não bloqueia merges nem pushes diretos.
- [ ] Confirmar a matriz Windows/Linux como obrigatória antes da promoção.

## 6. Critérios de promoção

O Arestrix 2.8 pode ser promovido quando:

1. o gate completo passar no commit candidato com árvore limpa;
2. `release-report.json` tiver `status=passed` e `dirty=false`;
3. os casos manuais LIB-M-03, LIB-M-04, confirmação nativa de LIB-M-01 e STUDIO-M-01..05 tiverem
   evidência anexada;
4. o pacote Windows e o pacote Linux forem validados em ambiente limpo;
5. o catálogo remoto assinado estiver decidido e, se incluído, validado;
6. não houver falha P0/P1 conhecida;
7. os artefatos publicados tiverem checksums, SBOM e notices.

## 7. Evidências

Artefatos de homologação ficam em `release/homologation/<runId>/` e não são versionados. Cada
execução deve registrar commit, árvore limpa/suja, ambiente, logs, matriz e capturas. O resumo vai
para `docs/quality/test-audit-log.md`.
