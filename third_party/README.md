# Third-party code

All external code lives in this directory. The policy is intentionally narrow so that Arestrix can
be distributed under the MIT license:

- allowed: MIT, BSD, ISC, Apache-2.0, public domain (`blessing`) and SIL OFL-1.1 for fonts;
- not allowed without an explicit, documented exception: copyleft licenses, source-available
  licenses with field-of-use restrictions, and code with no license.

The automated check is:

```bash
python scripts/check_third_party_licenses.py
```

## Submodules

| Path | Upstream | Pinned version | License |
| :--- | :--- | :--- | :--- |
| `taskflow/` | https://github.com/taskflow/taskflow | `v4.1.0` | MIT |
| `simdjson/` | https://github.com/simdjson/simdjson | `v5.0.4` | Apache-2.0 |
| `webui/` | https://github.com/webui-dev/webui | `2.4.2` | MIT |
| `i18next/` | https://github.com/i18next/i18next | `v26.4.4-2-g15d0b9d9` | MIT |
| `monaco-editor/` | https://github.com/microsoft/monaco-editor | `473775c` | MIT |
| `SkillSpector/` | https://github.com/NVIDIA/SkillSpector | `v2.12.0-630-g69d7d059` | Apache-2.0 |

Vendored (not submodules):

| Path | Version | License |
| :--- | :--- | :--- |
| `sqlite/` | 3.54.0 amalgamation | Public domain |
| `fonts/inter/` | Inter | SIL OFL-1.1 |

## Updating a submodule

```bash
npm run submodule:check    # report upstream commits without changing the working tree
npm run submodule:update   # move submodules to their latest upstream commit
```

Submodule updates are dependency changes: run the license check, the frontend tests, the C++
tests and the build before opening a pull request. Dependabot also opens monthly submodule pull
requests.

## Adding a new dependency

1. Confirm the license is on the allowed list.
2. Prefer a pinned submodule or the npm lockfile over an unpinned download.
3. Add the license text and attribution to `THIRD_PARTY_NOTICES.md`.
4. Run `python scripts/check_third_party_licenses.py`.
5. Document why the dependency is needed in the pull request.
