# CI and dependency automation

The repository now runs CI on every pull request and keeps dependency updates
enabled. The Windows portable release pipeline remains separate because it
publishes a ready-to-run artifact.

| Workflow | Trigger | What it does |
| :--- | :--- | :--- |
| `workflows/ci.yml` | `pull_request`, manual | Quality checks, Vitest + Studio build, native Windows CTest, native Linux CTest and lockfile verification. The four build jobs are required status checks on `main`; `lockfile` is advisory. |
| `workflows/deploy.yml` | push to `main`, `v*` tags, manual | Runs the release gate, builds the Windows x64 portable package and publishes it to GitHub Releases (`latest` for `main`, the tag name for stable tag releases). |
| `workflows/check-submodules.yml` | weekly schedule (Monday 08:00 UTC), manual | Compares every submodule gitlink with its upstream branch and opens/updates the `chore/submodule-updates` pull request. Never merges. |
| `dependabot.yml` | weekly/monthly schedule | Opens grouped npm, GitHub Actions and Git submodule update pull requests. |

## Action lockfile

Every action a workflow uses is pinned through `.github/workflows/actions.lock`.
The workflow files keep readable tags (`actions/checkout@v7.0.1`) and the
lockfile records the commit that actually runs, including transitive
dependencies. The runner refuses to start a job whose locked entry is missing
or no longer reachable from its recorded tag, so a stale lockfile fails closed.

The lockfile replaces the older "pin every action to a full SHA" advice. The
commit SHA is the code that runs; `owner_id` and `repo_id` also record *which*
repository that SHA came from, which a SHA alone cannot tell you.

### Updating it

Never edit `actions.lock` by hand.

- After adding a workflow, or adding/removing/changing any `uses:`, run
  `gh actions-lock` from the repository root and commit the workflow change
  plus the lockfile together. `ci.yml` fails when the two drift apart.
- `gh actions-lock --verify` runs the same checks without writing (this is the
  CI job). `gh actions-lock --verify-local` is the offline pre-commit variant:
  coverage only, no provenance.
- Dependabot updates the lockfile in the same pull request as the version bump,
  so no extra configuration is needed. The `github-actions` ecosystem also has a
  `cooldown` block: an action release must be at least 7 days old before it
  becomes a pull request (`github-actions` supports `default-days` only, not the
  `semver-*-days` variants).

### Reviewing a lockfile diff

This is the part that makes the lockfile a control rather than paperwork.
`/.github/` is already owned in `CODEOWNERS`, so the diff lands in review
automatically:

- A changed `commit:` is a change to the code your CI and release pipeline run.
  Treat it like any other dependency update; do not merge on green alone.
- A changed `owner_id:` or `repo_id:` is rare and means the repository behind
  that name is not the one originally locked. Investigate before merging.
- Do not auto-merge `github-actions` pull requests.

### Tooling pin

`ci.yml` installs the extension with `--pin v0.4.1`; the lockfile is only
trustworthy if the tool verifying it is pinned too. Dependabot does not manage
that line, so bump it by hand after reviewing the
[releases](https://github.com/github/gh-actions-lock/releases), then run
`gh actions-lock` again and commit the result.

`lockfile` is intentionally **not** in the `main-protection` ruleset yet. Run it
advisory until it has a clean track record, then add it as a required check.

### What the lockfile does not cover

Reusable workflows called at `jobs.<id>.uses` from another repository (none here),
container images (none here), the runner image, npm dependencies and git
submodules. Those keep their own controls: `npm ci`, the submodule workflow
above and `npm run check:skill-references` for reference skills.

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
