# CI and dependency automation

The repository now runs CI on every pull request and keeps dependency updates
enabled. The Windows portable release pipeline remains separate because it
publishes a ready-to-run artifact.

| Workflow | Trigger | What it does |
| :--- | :--- | :--- |
| `workflows/ci.yml` | `pull_request`, manual | Quality checks, Vitest + Studio build, native Windows CTest and native Linux CTest. These four jobs are required status checks on `main`. |
| `workflows/deploy.yml` | push to `main`, `v*` tags, manual | Runs the release gate, builds the Windows x64 portable package and publishes it to GitHub Releases (`latest` for `main`, the tag name for releases). |
| `dependabot.yml` | weekly/monthly schedule | Opens grouped npm, GitHub Actions and Git submodule update pull requests. |

## Branch rules

The `main-protection` ruleset requires the `quality`, `frontend`,
`native-windows` and `native-linux` checks and a pull request before merge.
The repository owner has an `always` bypass so urgent fixes and release
maintenance can still be pushed directly; contributors should use a pull
request.

## Local equivalents

The canonical local gate is still:

```bash
python scripts/release_check.py
```

For a faster rehearsal, use `python scripts/release_check.py --skip-e2e` or run
the individual commands from `.github/workflows/ci.yml` (`npm test`, `npx tsc
--noEmit`, `python CppCore/dev.py --test --no-frontend`, repository checks).

E2E (`scripts/e2e_tutorial.py`, `scripts/e2e_examples.py`) remains a manual
promotion step before tagging an official release; the rolling `deploy.yml`
uses `--skip-e2e` to keep the `latest` build fast.
