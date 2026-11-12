# npm dependency advisories

Last reviewed: 2026-11-12.

## Current status

`npm audit` reports **0 vulnerabilities** (0 low, 0 moderate, 0 high, 0 critical) on
2026-11-12.

The previous review (2026-10-31) reported three high-severity advisories from
[`braces` GHSA-vfj7-8cjw-p6xm](https://github.com/advisories/GHSA-vfj7-8cjw-p6xm)
(stack exhaustion with deeply nested patterns). That path is no longer reported by the current
dependency tree. Re-check it after any change to `vite-plugin-singlefile`, `micromatch` or the
build chain.

## History and mitigations

- [x] `vite` 5 → 6.4.4 (resolves the `esbuild <= 0.24.2` moderate advisory).
- [x] `vitest` 2 → 4.1.12 (resolves `@vitest/mocker <= 4.1.10`, moderate, and
      `tinypool <= 2.1.1`, critical; RCE through worker options). Requires Node.js >= 20.
- [x] `dompurify` 3.4.15 → 3.4.17 through a `package.json` override (resolves DOMPurify XSS
      advisories). `monaco-editor` 0.57.0 still pins 3.4.15, which is its latest release.
- [x] `monaco-editor` was already on the latest release (0.57.0); the override covers the gap.
- [x] Validated after the updates: `npx tsc --noEmit`, the frontend test suite and `npm run build`.
- [x] 2026-11-12: `npm audit` clean; `npm test` 82 files / 650 tests and `npm run build` green.

## Verification

- Run `npm audit` after every dependency update and update this file.
- Track a `braces` release that fixes GHSA-vfj7-8cjw-p6xm if it reappears. When available, apply it
  through an override (`"braces": ">=x.y.z"`) or through a new `micromatch` /
  `vite-plugin-singlefile` release.
- Re-evaluate the `dompurify` override when `monaco-editor` is upgraded.
- Do not silence audit failures in CI. If a temporary exception is required, link it to an issue
  and record an expiry date here.
