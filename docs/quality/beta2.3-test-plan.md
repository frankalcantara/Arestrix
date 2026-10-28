# Beta 2.3 - Registro de pendências e plano de testes

> **Status:** pré-release `2.3.0-beta.2`; matriz completa e validação em máquina limpa pendentes.
>
> **Versão atual:** `2.3.0-beta.2` (Beta 2.3).
>
> **Gate:** instalador 5B2 e assinatura/CI 5C implementados; validação em máquina limpa, pacote Linux
> e a matriz completa 2.1-2.10 ainda pendentes.
>
> **Última revisão:** 2026-10-27.

Este documento registra o escopo do Beta 2.3 e a matriz de testes que precisa ser concluída para
promover a pré-release a uma release validada. A versão `2.3.0-beta.2` está no código e é publicada
como pré-release de desenvolvimento.

## 1. Escopo registrado

### Concluído antes do Beta 2

- Grafo de chamadas fechado com aresta `call`.
- Interações humanas com formulário seguro, canais Studio/Runner/CLI e auditoria.
- Formulário gerado pelo vértice (`schemaSource: vertex`).
- Chamadas paralelas, subflow chamável e vértice `Call` em fluxo.
- Notificações assíncronas, `call_result` e trace aberto.
- Gate de release reproduzível (`release_check.py`) com manifesto, `SHA256SUMS` e SBOM.
- Pacote portátil 5B1 (`package_release.py`) com smoke test.

### Entregue no Beta 2.3

- Biblioteca Arestrix com fases 1-7: catálogo embutido, stores e serviços, gate de auditoria,
  árvores Studio/Workspace/Projeto, picker de skills/MCPs, templates como assets, fonte GitHub
  nativa com allowlist/commit/licença/cache e migração do catálogo legado.
- Homologação automatizada de bibliotecas com mock GitHub, matriz de 25 casos, relatório e
  evidências em `release/homologation/<runId>/`.
- Revisão completa do menu Arquivo: Abrir no padrão Office, Info/Novo/Salvar/Exportar/Opções
  alinhados ao shell Office, ícones vetoriais, painéis e botões consistentes, tema escuro.
- Sincronização do canvas com o tema global Claro/Escuro/Seguir o sistema.
- Capturas `open-screen.webp`, `file-menu-*.webp` e `libraries-sources.webp`, com documentação de interface/showcase atualizada.

### Entregue no Beta 2.2 (base)

- Hub de Propriedades no Backstage com escopos Studio, Workspace e Projeto compartilhando o shell Office.
- Edição de idioma, tema global, canvas, modelo, credenciais write-only e AI Gateway no Studio.
- Edição de sandbox, auditoria, reconstrução e reset no Workspace.
- Edição de metadados, defaults, ambiente sanitizado e caminho SQLite no Projeto.
- Migração das entradas antigas para deep links nas Propriedades.
- Modais de vértice Sandbox/Modelo/Ferramentas/Memória no `BaseModal`.
- Tema global Claro/Escuro/Seguir o sistema.
- Capturas de tela e documentação atualizadas para o Beta 2.2.

### Pendente no Beta 2.3

- [ ] **5B2 — instalador Windows:** Inno Setup implementado; faltam instalação limpa, upgrade,
      uninstall, atalhos, modo silencioso e validação em Intune/SCCM.
- [ ] **5B2 — pacote Linux:** AppImage, `.deb` ou tarball instalável, com dependências documentadas.
- [ ] **5C — assinatura:** SignPath Foundation integrado no workflow (binários + instalador); faltam
      aprovação da conta/projeto, secrets e primeira release assinada. Certificado/HSM próprio e
      assinatura do desinstalador ficam para depois.
- [ ] **5C — CI:** matriz Windows/Linux, build nativo, CTest, Vitest, E2E, gateway smoke e release gate.
  - [x] Matriz Windows/Linux com CTest, Vitest e checks no `ci.yml`; gate e gateway smoke no `deploy.yml`.
  - [x] Assinatura SignPath Foundation integrada ao `deploy.yml` (binários, instalador, verificação e hashes).
  - [ ] Aprovação SignPath, primeira release assinada, E2E no CI e promoção automatizada por tag.
- [ ] **5C — catálogo remoto assinado:** par de chaves, custódia, rotação e hospedagem.
- [ ] **Distribuição:** publicar checksums, SBOM, notices e notas de release.
- [ ] **Testes e homologação** da matriz abaixo.

## 2. Matriz obrigatória de testes

### 2.1 Gate local reproduzível

- [ ] Árvore Git limpa no commit candidato.
- [ ] `py scripts/release_check.py` (ou `python3`) com `--full --e2e`.
- [ ] `release/release-report.json` com `status=passed` e `dirty=false`.
- [ ] `release/release-manifest.json`, `SHA256SUMS` e `SBOM.json` gerados.
- [ ] Conferir `SHA256SUMS` com `sha256sum -c`/`Get-FileHash`.

### 2.2 Checks estáticos

- [ ] `npm run check:version`.
- [ ] `npm run check:local-assets`.
- [ ] `npm run check:secrets`.
- [ ] `npm run check:skill-references`.
- [ ] `python scripts/check_third_party_licenses.py`.
- [ ] `npx tsc --noEmit`.
- [ ] `npm run build`.

### 2.3 Frontend

- [ ] `npm test` (Vitest) — 60 arquivos / 491 testes no commit atual.
- [ ] Validação de grafos, ciclos, subfluxos, arestas `call`/`feedback`, undo/redo.
- [ ] Inspector de interação, formulário gerado, Call vertex, async e notificações.
- [ ] Trace overlay e resumo no console.
- [ ] i18n pt-BR/en-US sem chaves faltando ou sobrando.
- [ ] Zero sombras/glows em texto e contraste WCAG.

### 2.4 Backend nativo

- [ ] `py CppCore/dev.py --test --no-frontend` / CTest.
- [ ] Todos os testes de runtime, memória, MCP, gateway, projetos e segurança.
- [ ] Compilação com hardening (`/GS`, `/guard:cf`, `/sdl` ou stack protector/RELRO).
- [ ] Opcional antes do release: `--asan`, `--clang-tidy`, `--cppcheck`.

### 2.5 E2E

- [ ] `py scripts/e2e_tutorial.py`.
- [ ] `py scripts/e2e_examples.py` — 12 exemplos.
- [ ] `py scripts/e2e_memory_institution.py` — HTTP/MCP, papéis e fail-closed.
- [ ] Exemplo 06: chamada síncrona, notificação e ausência de chave.
- [ ] Exemplo 07: interação com `--answers` e falha fechada.
- [ ] Exemplo 08: formulário gerado e fallback `free_text`.
- [ ] Exemplo 09: chamadas paralelas.
- [ ] Exemplo 10: subflow chamável e input no agente interno.
- [ ] Exemplo 11: Call vertex e artefato de saída.
- [ ] Exemplo 12: `call_agent` async, `call_result`, notificação e `trace.json`.
- [ ] Nenhum segredo, caminho pessoal ou artefato gerado em `examples/`.

### 2.6 Pacote portátil 5B1

- [ ] `py scripts/package_release.py`.
- [ ] ZIP e/ou `.tar.gz` gerados, com sidecar `.sha256`.
- [ ] `package-manifest.json` e `SHA256SUMS` internos consistentes.
- [ ] `SBOM.json` com o mesmo commit do HEAD.
- [ ] Extrair o arquivo em pasta limpa.
- [ ] `arestrix-cli version`.
- [ ] `arestrix-cli project validate --project examples/01-svg-agente`.
- [ ] Abrir o Studio extraído e confirmar que a UI e os projetos carregam.

### 2.7 Instalador 5B2 (Inno Setup implementado; validação pendente)

- [x] Build do instalador Inno Setup (`py scripts/package_release.py --installer`) e publicação no CI.
- [x] Preflight do ambiente: WebView2/VC++ obrigatórios; Git, identidade Git, credencial GitHub,
      MSVC Build Tools e chaves de API reportados sem expor valores.
- [ ] Instalação limpa em Windows x64 sem dependências ocultas.
- [x] Atalhos (Menu Iniciar/Desktop padrão, com opção de desmarcar) e desinstalação completa validadas localmente.
- [x] Upgrade sobre a versão anterior preservando projetos/configuração do usuário validado localmente.
- [x] Instalação silenciosa e códigos de saída documentados (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`; `/MERGETASKS="!desktopicon"` para pular o atalho).
- [ ] Assinatura Authenticode de todos os executáveis.
- [x] Instalação em perfil sem privilégios administrativos (per-user em `%LOCALAPPDATA%\Programs\Arestrix`).
- [ ] Teste em máquina limpa com apenas o runtime VC++ exigido.
- [ ] Linux: pacote equivalente com dependências e caminhos de dados documentados.

> **Validação local (2026-10-26, Windows 11 x64):** build do instalador, preflight do ambiente
> (incluindo detecção de chave de API ausente sem bloquear), instalação silenciosa, atalho de desktop
> padrão e exclusão opcional, atalhos do Menu Iniciar, entrada `App Paths`, upgrade in-place
> preservando arquivos/projetos do usuário e desinstalação completa validados (códigos 0).
> Instalação em máquina limpa, assinatura, Intune/SCCM e pacote Linux continuam pendentes.

### 2.8 Segurança e integridade

- [ ] Nenhuma chave real em binários, logs, exemplos, trace, notificações ou SBOM.
- [ ] `ARESTRIX_HOME` e caminhos de runtime confinados.
- [ ] Assinatura/checksum verificados no fluxo de release.
- [ ] Política fail-closed de gateway/interação/assinatura revalidada.
- [ ] Dependências e avisos revisados em `docs/security/dependency-advisories.md`.

### 2.9 Homologação manual do Studio

- [ ] Criar, abrir, salvar e reabrir projeto.
- [ ] Editar grafo, mover/ligar vértices, undo/redo.
- [ ] Executar pipeline com agente, interação, call, async e trace.
- [ ] Visualizar artefatos e console.
- [ ] Testar tema/idioma e janelas flutuantes.
- [ ] Verificar mensagens de erro e estados de execução.

### 2.10 Bibliotecas + fonte GitHub

- [x] `npm run homologation:libraries` com `status=passed-automated` no commit candidato.
- [x] Matriz, ambiente, logs e requisições do mock em `release/homologation/<runId>/`.
- [x] Segurança adversarial: skill hostil, `CAUTION`, licença, path traversal e tamanho.
- [x] Resiliência: 404, rate limit, commit pin e árvore.
- [x] Smoke live do manifesto GitHub (`npm run homologation:libraries:live`) em 2026-10-27.
- [x] Navegação por teclado e foco visível (aprovado manualmente em 2026-10-27).
- [ ] Tema claro/escuro e larguras 1024/1800 px: canvas e vértices escuros corrigidos;
      revalidar visualmente.
- [ ] Instalação limpa e upgrade preservando projetos/bibliotecas.
- [ ] Uso offline após sincronização/cache.
- [ ] Evidências manuais anexadas (capturas/logs) para os 3 casos pendentes e registro no
      `test-audit-log.md`.
- Plano detalhado: [`libraries-homologation-plan.md`](./libraries-homologation-plan.md).

## 3. Critérios de aceite para promoção

A pré-release Beta 2.3 pode ser promovida quando:

1. Todos os itens obrigatórios de 2.1–2.10 estiverem verdes no mesmo commit.
2. O gate de release tiver `dirty=false` e `status=passed`.
3. O instalador 5B2 e o pacote portátil tiverem sido testados em máquina limpa.
4. Os artefatos estiverem assinados, com checksums e SBOM publicados.
5. Não houver falha P0/P1 conhecida.
6. A versão for atualizada para `2.3.0-beta.2` em `version.txt`, `package.json`,
   `package-lock.json`, `CHANGELOG.md` e banners de documentação.
7. O release for revisado e aprovado pelo mantenedor.

## 4. Decisões abertas

- Tecnologia do instalador Windows: WiX v4, Inno Setup ou MSIX.
- Formato Linux prioritário: AppImage, `.deb` ou tarball.
- Certificado/HSM de assinatura e custódia da chave.
- Estratégia de catálogo remoto assinado e hospedagem.

## 5. Registro de bloqueio

O 5B2 não foi iniciado em 2026-10-24. A prioridade imediata é executar e consolidar a matriz de
testes acima antes de retomar instalador, assinatura e CI.
