# npm dependency advisories

Last reviewed: 2026-10-19.

As of 2026-10-16, `npm audit` reports **3 high-severity vulnerabilities**, all from the same
advisory: [`braces` GHSA-vfj7-8cjw-p6xm](https://github.com/advisories/GHSA-vfj7-8cjw-p6xm)
(stack exhaustion with deeply nested patterns). No fixed release is available upstream.
`npm audit fix --force` would downgrade `vite-plugin-singlefile` to `0.9.0`, which breaks the
single-file build and is not an acceptable mitigation.

## Exposure

`braces` is reachable only through the **build chain**:

`vite-plugin-singlefile` → `micromatch` → `braces`

It processes glob patterns from the project's own files at build time. It is not shipped in the
distributed application and does not process untrusted input at runtime.

## History and mitigations

- [x] `vite` 5 → 6.4.4 (resolves the `esbuild <= 0.24.2` moderate advisory).
- [x] `vitest` 2 → 4.1.12 (resolves `@vitest/mocker <= 4.1.10`, moderate, and
      `tinypool <= 2.1.1`, critical; RCE through worker options). Requires Node.js >= 20.
- [x] `dompurify` 3.4.15 → 3.4.17 through a `package.json` override (resolves DOMPurify XSS
      advisories). `monaco-editor` 0.57.0 still pins 3.4.15, which is its latest release.
- [x] `monaco-editor` was already on the latest release (0.57.0); the override covers the gap.
- [x] Validated after the updates: `npx tsc --noEmit`, the frontend test suite and `npm run build`.

## Verification

- Run `npm audit` after every dependency update and update this file.
- Track a `braces` release that fixes GHSA-vfj7-8cjw-p6xm. When it is available, apply it through
  an override (`"braces": ">=x.y.z"`) or through a new `micromatch` / `vite-plugin-singlefile`
  release.
- Re-evaluate the `dompurify` override when `monaco-editor` is upgraded.
- Do not silence audit failures in CI. If a temporary exception is required, link it to an issue
  and record an expiry date here.
