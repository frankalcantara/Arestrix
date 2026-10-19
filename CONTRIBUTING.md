# Contributing to Arestrix

Thanks for taking the time to contribute. Arestrix is a C++20 engine plus a TypeScript studio,
with bilingual documentation and generated examples. This guide covers the workflow expected for
issues, pull requests and releases.

By participating, you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Before you start

- Search existing issues and pull requests to avoid duplicate work.
- Open an issue before large changes, new dependencies or architectural refactors.
- For small fixes (typos, broken links, documentation corrections), a pull request is enough.
- Security issues must follow [SECURITY.md](SECURITY.md), never a public issue.

## Development setup

Prerequisites: Git, Node.js 20+, Python 3.10+, CMake 3.20+ and a C++20 compiler.

```bash
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix
npm ci
npm run hooks:install
python CppCore/dev.py
```

Useful commands:

| Command | Purpose |
| :--- | :--- |
| `python CppCore/dev.py` | Build frontend and backend, then open the Studio. |
| `python CppCore/dev.py --test` | Build and run the C++ test suite with `ctest`. |
| `npm test` | Run the frontend Vitest suites. |
| `npm run build` | Type-check and build the single-file frontend bundle. |
| `npm run check:secrets` | Scan for credentials and personal data. |
| `npm run check:version` | Verify that all version files agree. |
| `python scripts/e2e_examples.py` | Run the examples against a scripted fake model. |
| `python scripts/check_third_party_licenses.py` | Verify third-party licenses. |

## Branches and commits

- Branch from `main`: `feat/<short-name>`, `fix/<short-name>`, `docs/<short-name>` or
  `chore/<short-name>`.
- Keep pull requests focused. Do not mix refactors, formatting and behavior changes.
- Use [Conventional Commits](https://www.conventionalcommits.org/): `feat:`, `fix:`, `docs:`,
  `test:`, `build:`, `ci:`, `refactor:`, `chore:`.
- Write commit messages in English, imperative mood, with a short subject line.

## Tests and quality gates

A pull request is ready for review when:

- `npm test` passes.
- `python CppCore/dev.py --test` passes for engine changes.
- `npm run check:secrets`, `npm run check:version` and the license check pass.
- New behavior has a test, or the pull request explains why a test is not feasible.
- User-facing documentation is updated in **both** `docs/en/` and `docs/pt-BR/`.
- Screenshots are regenerated with `npm run capture:screenshots` when the interface changes.

## Documentation

- English is the canonical language; Portuguese must mirror the same structure.
- Keep examples runnable and free of real credentials.
- Do not duplicate long explanations across READMEs. Link to the canonical guide under `docs/`.
- `version.txt` is the source of truth. When changing the version, update it together with
  `package.json`, `package-lock.json`, `CHANGELOG.md` and `README.md`; `npm run check:version`
  verifies that they agree.

## Secrets and generated artifacts

- Never commit API keys, tokens, `.env` files, local paths or customer data.
- Project files may contain environment variable **names** or `${NAME}` references, never values.
- Do not commit build outputs, `dist/`, `build/`, `runtime/`, databases or exported binaries.
- Do not commit generated example pipelines by hand. Edit the source and run
  `python scripts/build_examples.py`.

## Pull request checklist

- [ ] The change is focused and linked to an issue when applicable.
- [ ] Tests pass locally and new behavior is covered.
- [ ] Documentation was updated in English and Portuguese.
- [ ] `CHANGELOG.md` was updated for user-visible changes.
- [ ] No secrets, personal data or generated artifacts were added.
- [ ] Screenshots or recordings are attached for interface changes.
- [ ] The pull request describes the risk and the rollback plan for behavioral changes.

## Review and release

- Maintainers review for correctness, security, compatibility and documentation.
- Version bumps, tags and GitHub Releases are handled by maintainers.
- Contributions are accepted under the repository's [MIT License](LICENSE).
