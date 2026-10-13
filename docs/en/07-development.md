# Development

[Português](../pt-BR/07-desenvolvimento.md) · [Index](../../README.md#documentation)

## Repository map

| Path | Content |
| :--- | :--- |
| `src/` | TypeScript frontend (core, view, ui, i18n). |
| `CppCore/` | C++20 backend, the runtime and the CLI. `dev.py` is the build entry point. |
| `tests/` | Vitest suites for the frontend. |
| `scripts/` | End-to-end tutorial, license check, submodule helper. |
| `third_party/` | All external code (submodules or vendored files with their license). |
| `docs/` | This documentation, in `en/` and `pt-BR/`. |
| `TUTORIAL_AGENTE_SVG_ARESTRIX.md` | A complete worked example (in Portuguese). |

## Tests

```bash
npm test                      # frontend: Vitest
python CppCore/dev.py --test  # backend: ctest
python scripts/e2e_tutorial.py  # end to end, against a scripted fake LLM
```

The frontend suite covers graph validation and cycles, undo/redo, subflows, templates, settings
inheritance, telemetry, the resume engine, memory, MCP, projects and workspaces, the Office interface, SVG
vertices and i18n parity.

## Adding a language

Arestrix ships `pt-BR` and `en-US`. A new language needs five edits (example: `es-ES`):

1. **Dictionary.** `cp src/i18n/locales/en-US.json src/i18n/locales/es-ES.json` and translate the values;
   keep every key.
2. **Type.** In `src/i18n/types.ts`, add the code to `SupportedLocale` and an entry to `SUPPORTED_LOCALES`.
3. **Catalog.** In `src/i18n/index.ts`, import the JSON, add it to `resources`, and allow the code in the
   `setLocale` check.
4. **Selector.** Add an `<option>` to `#backstage-language-select` in `src/ui/ribbon/BackstageView.ts`.
5. **Verify.** `npm test` fails if any key is missing or extra between catalogs. Then `npm run build`
   (it also copies `dist/` to `CppCore/dist/`).

Every user-visible string goes through `t('key', 'fallback')`, never hard-coded.

## Third-party code policy

All external code is in `third_party/` and must be compatible with Taskflow's **MIT** license:
permissive licenses only (MIT, BSD, ISC, Apache-2.0). The single documented exception is the Inter font
(SIL OFL 1.1), which allows embedding. JavaScript dependencies are declared in `package.json` and checked
against the same list.

```bash
python scripts/check_third_party_licenses.py
```

Submodules:

```bash
npm run submodule:check    # report new upstream commits
npm run submodule:update   # move to the latest upstream
```

## Conventions that matter

- **No text shadows or glows** on any text, in any theme. Contrast comes from color and weight.
- **Ignore rules anchored with `/`** (`/build/`, `/runtime/`): an unanchored `build/` once hid
  `CppCore/src/build/` from the repository.
- Edits to the graph go through `ICommand` implementations so they stay undoable.
- JSON in the C++ backend goes through `Json` (simdjson), not another parser.

## Known pending items

- Dependency advisories: see [`SEGURANCA_DEPENDENCIAS.md`](../../SEGURANCA_DEPENDENCIAS.md)
  (update `vite`/`vitest`, then `monaco-editor` or pin `dompurify`).
- Vite warnings: `webui.js` loaded without `type="module"`, and `fs`/`path` imported in
  `GenericPiProjectScanner.ts`.

## Related

- [Architecture](01-architecture.md)
- [Brand](08-brand.md)
