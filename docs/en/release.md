# Release gate and artifacts

> **Phase 5A.** This is the reproducible validation layer before installer packaging and signing.
> The Windows Inno Setup installer and the SignPath Foundation workflow are implemented; SignPath
> account approval, clean-machine validation, Linux package, Linux CI and the signed remote catalog
> remain pending. See the [roadmap](roadmap.md).

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

The workflow runs the canonical gate (`release_check.py --skip-e2e`), creates the ZIP and the Inno
Setup installer with `package_release.py --installer`, and attaches the archive, `.zip.sha256`,
`*-setup.exe`, `*-setup.exe.sha256`, `SHA256SUMS`, `SBOM.json`, `release-manifest.json` and
`release-report.json` to the release. The portable folder is ready to run from any directory
(`start-arestrix.bat`); no development checkout is needed.

Pull requests run the pre-merge gate in `.github/workflows/ci.yml`: quality/static checks, Vitest +
Studio build, native Windows CTest and native Linux CTest. The four jobs are required status checks
on `main`, and Dependabot keeps npm, GitHub Actions and Git submodules current. Submodule pointers
only move through a reviewed pull request: the weekly `Submodule updates` workflow compares each
pinned gitlink with its upstream branch and opens `chore/submodule-updates` (see
[`../../.github/AUTOMATION.md`](../../.github/AUTOMATION.md)).

E2E remains a promotion requirement: run the full `release_check.py` (with E2E) from a clean tree
before tagging an official release.

## 3.3 Windows installer (phase 5B2, Inno Setup)

The Windows installer is built from the same staging folder as the portable package:

```powershell
py scripts/package_release.py --installer
```

Inno Setup 6 must be installed (`ISCC.exe` on `PATH`, `ISCC` pointing to it, or a standard
`Program Files\Inno Setup 6` installation). The output is
`release/arestrix-<version>-windows-x64-setup.exe` with a `.sha256` sidecar.

The installer:

- installs per user under `%LOCALAPPDATA%\Programs\Arestrix`, but an existing installation keeps its
  previous directory/tasks on upgrade (stable `AppId`, `UsePreviousAppDir`, `UsePreviousTasks`);
- creates Start menu and **desktop shortcuts by default**; the desktop shortcut can be unchecked in
  the wizard, and silent installs can skip it with `/MERGETASKS="!desktopicon"`;
- never writes to or deletes user project folders; upgrades replace only application files and
  preserve projects, workspaces, logs, models and runtime downloads;
- registers the app in **Apps & Features** and adds an `arestrix.exe` App Paths entry;
- checks for Microsoft Edge WebView2 Runtime and the Visual C++ Redistributable x64, aborting with
  the official download links when either is missing;
- runs an environment preflight and reports (without exposing values) whether Git, the Git identity,
  a GitHub credential (HTTPS/SSH/`gh`), MSVC Build Tools and at least one provider API key are
  available; missing optional items produce a warning with the installer continuing only after the
  user confirms, and silent installs write the report to the setup log instead of blocking;
- accepts `/PREFLIGHT=1` to force the environment report even when everything is configured;
- includes the same binaries, UI, runtime library, examples, documentation and manifests as the
  portable package;
- is signed automatically when `ARESTRIX_SIGN_PFX`/`ARESTRIX_SIGN_PFX_PASSWORD` or
  `ARESTRIX_SIGN_THUMBPRINT` are set (the same variables used by `sign-windows.ps1`).

Local validation on Windows 11 x64 covered the environment preflight (all optional items detected),
silent install with the report written to the setup log, the desktop shortcut default and its
`/MERGETASKS="!desktopicon"` opt-out, Start menu shortcuts, `App Paths` registration, an in-place
upgrade preserving user files/projects and a complete uninstall. Clean-machine install, signing and
Intune/SCCM validation remain pending; the installer is included in the pre-release but is not yet a
fully validated release artifact.

## 4. Code signing with SignPath Foundation

Arestrix is configured for [SignPath Foundation](https://signpath.org) code signing. The certificate
is issued to **SignPath Foundation**, so Windows shows `SignPath Foundation` as the publisher. The
acceptance and operational details are in [CODE_SIGNING.md](../CODE_SIGNING.md); account and
repository setup is in [signpath/README.md](../signpath/README.md).

The deploy workflow has optional signing steps controlled by `SIGNPATH_ENABLED=true` and a `v*` tag.
Pushes to `main` keep publishing the rolling `latest` pre-release without signing, so manual approval
is required only for tagged releases. The signing steps:

1. upload the four unsigned executables and submit them for signing;
2. wait for manual approval in SignPath, download and verify the signed binaries;
3. rebuild the release manifest with the signed hashes;
4. build the portable ZIP and installer from the signed binaries;
5. submit the setup for a second signing request, verify it and rewrite its SHA-256 sidecar.

Required repository variables: `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`,
`SIGNPATH_SIGNING_POLICY_SLUG`, `SIGNPATH_BINARIES_ARTIFACT_CONFIG_SLUG` and
`SIGNPATH_INSTALLER_ARTIFACT_CONFIG_SLUG`. The API token is the `SIGNPATH_API_TOKEN` secret.

The Inno-generated uninstaller remains unsigned for now because SignPath's GitHub Action is
asynchronous; this is documented in [signpath/README.md](../signpath/README.md).

## 4.1 Alternative: local PFX/thumbprint signing

After the gate passes, sign the executables with a local corporate certificate:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Alternatively use `ARESTRIX_SIGN_THUMBPRINT` for a certificate installed in `My`. The script covers
`arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe`, `arestrix-memory.exe` and an optional `-RuntimePath`.

## 5. Not in phase 5A

- SignPath Foundation account/project approval and the first signed release (workflow implemented).
- Windows Inno Setup installer implemented (5B2 partial; clean-machine install, upgrade, uninstall,
  silent install and Intune/SCCM validation still pending) and the Linux package (5B2).
- A company-owned certificate/HSM and signing of the Inno uninstaller.
- E2E in the automated release path and a macOS CI matrix.
- Signed remote catalog (Ed25519/minisign) and hosting.

Those are the next phase-5 slices.
