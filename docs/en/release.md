# Release gate and artifacts

> **Phase 5A.** This is the reproducible validation layer before installer packaging and signing.
> The final certificate/HSM, installers, Linux CI and signed remote catalog remain pending; see the
> [roadmap](roadmap.md).

## 1. Prepare a clean tree

Release artifacts must come from a reviewed commit:

```bash
git status --porcelain
git switch main
git pull --ff-only
```

The gate refuses a dirty tree by default. `--allow-dirty` exists only for local rehearsal and records
`"dirty": true` in the manifest.

## 2. Run the gate

Windows:

```powershell
py scripts/release_check.py
```

Linux/macOS:

```bash
python3 scripts/release_check.py
```

The gate runs the canonical `CppCore/dev.py --full --e2e` sequence:

- frontend build (`tsc` + Vite);
- native C++20 build with hardening;
- CTest;
- Vitest;
- version, local assets, secret and third-party license checks;
- gateway HTTP smoke test;
- tutorial and examples E2E.

Use `--skip-e2e` only for a quicker rehearsal. A production gate must include E2E.

## 3. Inspect the generated folder

```text
release/
  release-report.json       steps, exit codes, durations and overall status
  release-manifest.json     version, commit, platform, artifact hashes
  SHA256SUMS                standard "hash  path" lines
  SBOM.json                 native submodules and npm components
```

Verify a download manually:

```powershell
Get-FileHash build\arestrix.exe -Algorithm SHA256
```

```bash
sha256sum -c release/SHA256SUMS
```

The manifest is generated from the actual files on disk. It never reads API keys, project secrets or
user data.

## 3.1 Portable package (phase 5B1)

After the release manifest exists, build the portable distribution:

```powershell
py scripts/package_release.py
```

```bash
python3 scripts/package_release.py
```

The script stages the exact layout expected by the Studio and the C++ pipeline compiler:

```text
arestrix(.exe), arestrix-cli(.exe), arestrix-gateway(.exe), arestrix-memory(.exe)
dist/                         compiled Studio UI
CppCore/src/                  headers/sources used by generated pipelines
CppCore/runtime/              local runtime catalog and manifest
runtime/lib/                  arestrix_core library
third_party/taskflow/         Taskflow headers
third_party/simdjson/         simdjson headers
examples/, docs/              examples and documentation
.agents/skills/               development skills (Arestrix Skill Guard)
package-manifest.json         file hashes
SHA256SUMS                    package checksums
SBOM.json                     component inventory
```

It runs `arestrix-cli version` and `arestrix-cli project validate` against an example before
archiving the package. A clean tree is required by default.

The generated archive is `release/arestrix-<version>-<platform>-<arch>.zip` on Windows or
`.tar.gz` on Linux/macOS; `--format both` creates both. The portable package does **not** include
MSVC/GCC. Compiling pipelines on the target still requires a C++20 compiler and the Visual C++
Redistributable on Windows.

## 3.2 Automated portable deploy (Windows)

`.github/workflows/deploy.yml` builds and publishes the portable package from GitHub Actions:

- a push to `main` updates the rolling
  [`latest`](https://github.com/frankalcantara/Arestrix/releases/tag/latest) pre-release;
- pushing a `v*` tag publishes or updates the release for that tag;
- a manual dispatch follows the selected branch/tag.

The workflow runs the canonical gate (`release_check.py --skip-e2e`), creates the ZIP with
`package_release.py` and attaches the archive, `.zip.sha256`, `SHA256SUMS`, `SBOM.json`,
`release-manifest.json` and `release-report.json` to the release. The portable folder is ready to
run from any directory (`start-arestrix.bat`); no development checkout is needed.

Pull requests run the pre-merge gate in `.github/workflows/ci.yml`: quality/static checks, Vitest +
Studio build, native Windows CTest and native Linux CTest. The four jobs are required status checks
on `main`, and Dependabot keeps npm, GitHub Actions and Git submodules current.

E2E remains a promotion requirement: run the full `release_check.py` (with E2E) from a clean tree
before tagging an official release.

## 4. Sign the Windows binaries

After the gate passes, sign the executables with the corporate certificate:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Alternatively use `ARESTRIX_SIGN_THUMBPRINT` for a certificate installed in `My`. The script covers
`arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe`, `arestrix-memory.exe` and an optional `-RuntimePath`.

## 5. Not in phase 5A

- MSI/MSIX or Inno Setup installer and Linux package (5B2; the portable package 5B1 is implemented).
- Code-signing certificate/HSM provisioning and signing inside CI.
- E2E in the automated release path and a macOS CI matrix.
- Signed remote catalog (Ed25519/minisign) and hosting.

Those are the next phase-5 slices.
