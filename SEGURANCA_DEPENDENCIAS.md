# Pendencias de seguranca das dependencias npm

Estado em 2026-10-13: `npm audit` reporta 11 vulnerabilidades (2 low, 3 moderate, 4 high, 2 critical).
`npm audit fix` (sem `--force`) nao corrige nada. Nao usar `--force`: ele troca vitest para v5,
vite para v8, vite-plugin-singlefile para 0.9 e faz downgrade do monaco-editor para 0.56.

Quase tudo e dev-time (vitest, tinypool, esbuild/vite dev server, braces/micromatch no build).
O unico que entra no app distribuido e `dompurify` (via `monaco-editor`), cujo advisory trata
da opcao `IN_PLACE` com hook `afterSanitize`.

## Passo 2 - Atualizar vite e vitest (commit separado)

- [ ] `vite` 5 -> 6 ou superior (resolve esbuild/vite).
- [ ] `vitest` 2 -> 3 ou superior (resolve tinypool, @vitest/mocker, vite-node).
- [ ] Conferir compatibilidade do `vite-plugin-singlefile` com o novo vite
      (resolve braces/micromatch se a versao nova trouxer micromatch corrigido).
- [ ] Validar: `npm test` (192 testes) e `npm run build`, e abrir o app via `python CppCore/dev.py`.

## Passo 3 - Atualizar o monaco-editor

- [ ] Acompanhar uma versao do `monaco-editor` (>= 0.57) que use `dompurify` > 3.4.15.
- [ ] Alternativa: `overrides` no package.json forcando `dompurify` corrigido, testando hovers/markdown no editor.
- [ ] Revalidar o editor (hover, completions) e o build single-file.

## Verificacao

Rodar `npm audit` apos cada passo e atualizar este arquivo.
