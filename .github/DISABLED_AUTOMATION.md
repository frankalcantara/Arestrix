# Disabled automation

The following GitHub automations are intentionally disabled during the Beta 1.0 stabilization
period:

| File | What it does | Re-enable with |
| :--- | :--- | :--- |
| `dependabot.yml.disabled` | Opens dependency update pull requests for npm, GitHub Actions and submodules. | `git mv .github/dependabot.yml.disabled .github/dependabot.yml` |
| `workflows/ci.yml.disabled` | Runs version, secret, license, frontend test and build checks on pushes and pull requests. | `git mv .github/workflows/ci.yml.disabled .github/workflows/ci.yml` |

## Why

The workflows were enabled before the repository settings, branch protection and native build
matrix were validated. The resulting failures created noise while the project was still stabilizing
the Beta 1.0 documentation and release process.

Until CI is re-enabled, the local release gate is the canonical automation:

```bash
python scripts/release_check.py
```

It runs `dev.py --full --e2e` and writes the release manifest, `SHA256SUMS` and SBOM under
`release/` (ignored by Git).

## Before re-enabling

1. Validate the CI jobs locally with the commands in `.github/workflows/ci.yml.disabled`.
2. Add the native Windows/Linux build and `ctest` job.
3. Configure branch protection and required checks.
4. Confirm that dependency update pull requests can be reviewed and merged on a schedule.
5. Re-add the CI badge to `README.md` and `README.pt-BR.md`.

## Existing pull requests and runs

Renaming these files stops new Dependabot pull requests and new CI runs, but it does not close
existing Dependabot pull requests or cancel runs already queued. Close those manually in the
repository's **Pull requests** and **Actions** tabs.
