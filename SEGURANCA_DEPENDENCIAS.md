# Pendencias de seguranca das dependencias npm

Estado em 2026-10-16: `npm audit` reporta 3 vulnerabilidades (3 high), todas do MESMO advisory
(`braces`, GHSA-vfj7-8cjw-p6xm, exaustao de pilha com padroes profundamente aninhados). O advisory
esta aberto: NAO existe versao corrigida publicada (o `npm audit fix --force` sugeriria downgrade do
`vite-plugin-singlefile` para 0.9.0, o que e pior e quebra o build single-file).

O `braces` entra somente pela cadeia de BUILD (vite-plugin-singlefile -> micromatch -> braces), que
processa globs dos proprios arquivos do projeto em tempo de build. Nada disso vai para o app
distribuido nem processa entrada nao confiavel em tempo de execucao.

## Historico e o que ja foi corrigido

- [x] `vite` 5 -> 6.4.4 (resolve esbuild <= 0.24.2, moderate).
- [x] `vitest` 2 -> 4.1.12 (resolve @vitest/mocker <= 4.1.10, moderate, e tinypool <= 2.1.1,
      critical; RCE em worker options). Requer Node >= 20 para rodar os testes.
- [x] `dompurify` 3.4.15 -> 3.4.17 via `overrides` no package.json (resolve os advisories de XSS do
      DOMPurify; o monaco-editor 0.57.0 ainda fixa 3.4.15, que e a versao mais recente dele).
- [x] `monaco-editor` ja estava na ultima versao (0.57.0); o override cobre a lacuna.
- [x] Validado: `npx tsc --noEmit` limpo, 254 testes frontend passando e `npm run build` OK.

## Verificacao

- Rodar `npm audit` apos cada atualizacao e atualizar este arquivo.
- Acompanhar um release do `braces` com correcao do GHSA-vfj7-8cjw-p6xm e, quando sair, subir
  via override (`"braces": ">=x.y.z"`) ou via nova versao do micromatch/vite-plugin-singlefile.
- Ao atualizar o monaco-editor no futuro, reavaliar se o override do dompurify ainda e necessario.
