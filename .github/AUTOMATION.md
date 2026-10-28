# CI and dependency automation

The repository now runs CI on every pull request and keeps dependency updates
enabled. The Windows portable release pipeline remains separate because it
publishes a ready-to-run artifact.

| Workflow | Trigger | What it does |
| :--- | :--- | :--- |
| `workflows/ci.yml` | `pull_request`, manual | Quality checks, Vitest + Studio build, native Windows CTest and native Linux CTest. These four jobs are required status checks on `main`. |
| `workflows/deploy.yml` | push to `main`, `v*` tags, manual | Runs the release gate, builds the Windows x64 portable package and publishes it to GitHub Releases (`latest` for `main`, the tag name for releases). |
| `workflows/check-submodules.yml` | weekly schedule (Monday 08:00 UTC), manual | Compares every submodule gitlink with its upstream branch and opens/updates the `chore/submodule-updates` pull request. Never merges. |
| `dependabot.yml` | weekly/monthly schedule | Opens grouped npm, GitHub Actions and Git submodule update pull requests. |

## Submodule updates

Build workflows keep checking out the exact commits pinned in `main`
(`actions/checkout` with `submodules: recursive`). Upstream tracking lives only
in `check-submodules.yml`, so an upstream release never changes a build until
the pointer is merged.

- The workflow runs `node scripts/check-submodule-updates.mjs --apply`. The
  script reads `.gitmodules`, fetches each upstream, compares the pinned commit
  with the tip of the tracked branch and **fast-forwards only**. It checks out
  the exact upstream commit (detached HEAD) and stages the new gitlink; it never
  runs `git submodule update --remote --merge`, because a bot-created merge
  commit inside a submodule would be recorded as a commit that does not exist
  upstream.
- Diverged submodules (pinned commit has work that is not upstream) are
  reported, never applied. Missing/unreachable submodules fail the run after the
  report is uploaded.
- When there are fast-forward updates, the workflow opens or updates the
  `chore/submodule-updates` pull request. The body lists pinned/upstream
  commits, the latest upstream commit subjects and the validation commands. The
  owner reviews and merges (or closes) it; closing keeps the pinned versions.
- The weekly run is also the notification: the pull request and the
  `submodule-updates` artifact (Markdown + JSON) are the alert.

Reference skills need extra care: a change in `third_party/SkillSpector` or
`third_party/security-audit-skill` requires the review in
[`docs/quality/skill-reference-review.md`](../docs/quality/skill-reference-review.md),
a lock update and `npm run check:skill-references`. The PR body calls this out;
a green build alone does not approve the policy.

### Token for pull requests

By default the PR is created with `GITHUB_TOKEN`, and GitHub does **not** start
workflows for PRs authored by that token, so the four required checks stay
pending. Configure the `SUBMODULE_UPDATE_TOKEN` repository secret (fine-grained
PAT with *Contents: read/write* and *Pull requests: read/write* on this
repository) to make the automated PR run `ci.yml` normally. Without the secret,
dispatch `ci.yml` manually on the `chore/submodule-updates` branch, or use the
owner bypass to merge after reviewing the report.

Local equivalents remain `npm run submodule:check` and
`npm run submodule:update`; both use the same script and safety rules as CI.

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
