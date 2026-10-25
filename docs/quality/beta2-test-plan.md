# Beta 2 — Registro de pendências e plano de testes

> **Status:** registrado, não liberado.
>
> **Versão atual:** `1.5.0-beta.1` (Beta 1.5).
>
> **Versão alvo do Beta 2:** `2.0.0-beta.1`, somente após o gate completo e o aceite abaixo.
>
> **Última revisão:** 2026-10-24.

Este documento registra o escopo do Beta 2 e a matriz de testes que precisa ser concluída antes de
bump de versão, tag, assinatura e publicação. O Beta 2 ainda não é uma release.

## 1. Escopo registrado

### Concluído antes do Beta 2

- Grafo de chamadas fechado com aresta `call`.
- Interações humanas com formulário seguro, canais Studio/Runner/CLI e auditoria.
- Formulário gerado pelo vértice (`schemaSource: vertex`).
- Chamadas paralelas, subflow chamável e vértice `Call` em fluxo.
- Notificações assíncronas, `call_result` e trace aberto.
- Gate de release reproduzível (`release_check.py`) com manifesto, `SHA256SUMS` e SBOM.
- Pacote portátil 5B1 (`package_release.py`) com smoke test.

### Pendente no Beta 2

- [ ] **5B2 — instalador Windows:** MSI (WiX/Inno) ou MSIX, instalação limpa, upgrade, uninstall,
      atalhos, modo silencioso e validação em Intune/SCCM.
- [ ] **5B2 — pacote Linux:** AppImage, `.deb` ou tarball instalável, com dependências documentadas.
- [ ] **5C — assinatura:** certificado/HSM, Authenticode no Windows, assinatura dos artefatos no CI.
- [ ] **5C — CI:** matriz Windows/Linux, build nativo, CTest, Vitest, E2E, gateway smoke e release gate.
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

- [ ] `npm test` (Vitest) — 49 arquivos / 385 testes no commit atual.
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

### 2.7 Instalador 5B2 (quando implementado)

- [ ] Instalação limpa em Windows x64 sem dependências ocultas.
- [ ] Atalhos (Menu Iniciar/Desktop) e desinstalação completa.
- [ ] Upgrade sobre a versão anterior preservando projetos/configuração do usuário.
- [ ] Instalação silenciosa e códigos de saída documentados.
- [ ] Assinatura Authenticode de todos os executáveis.
- [ ] Teste de instalação em perfil sem privilégios administrativos, quando aplicável.
- [ ] Teste em máquina limpa com apenas o runtime VC++ exigido.
- [ ] Linux: pacote equivalente com dependências e caminhos de dados documentados.

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

## 3. Critérios de aceite do Beta 2

O Beta 2 só pode ser tagueado/publicado quando:

1. Todos os itens obrigatórios de 2.1–2.9 estiverem verdes no mesmo commit.
2. O gate de release tiver `dirty=false` e `status=passed`.
3. O instalador 5B2 e o pacote portátil tiverem sido testados em máquina limpa.
4. Os artefatos estiverem assinados, com checksums e SBOM publicados.
5. Não houver falha P0/P1 conhecida.
6. A versão for atualizada para `2.0.0-beta.1` em `version.txt`, `package.json`,
   `package-lock.json`, `CHANGELOG.md` e banners de documentação.
7. O release for revisado e aprovado pelo mantenedor.

## 4. Decisões abertas

- Tecnologia do instalador Windows: WiX v4, Inno Setup ou MSIX.
- Formato Linux prioritário: AppImage, `.deb` ou tarball.
- Certificado/HSM de assinatura e custódia da chave.
- Estratégia de catálogo remoto assinado e hospedagem.
- Confirmar `2.0.0-beta.1` como numeração final do Beta 2 (recomendado) ou definir outra antes do bump.

## 5. Registro de bloqueio

O 5B2 não foi iniciado em 2026-10-24. A prioridade imediata é executar e consolidar a matriz de
testes acima antes de retomar instalador, assinatura e CI.
