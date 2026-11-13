# Release gate and artifacts

> **Phase 5A/5B1/5B2.** This is the reproducible validation layer before installer packaging.
> The Windows Inno Setup installer and the Linux Debian package are implemented; the portable tarball
> and the `.deb` were generated and validated on Ubuntu 24.04 (WSL2) on 2026-11-13. Clean-machine
> Windows validation, the AppImage and the signed remote catalog remain pending. See the
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
`.tar.gz` on Linux/macOS; on Linux, `--format both` creates the tarball and the `.deb`. The portable
package does **not** include MSVC/GCC. Compiling pipelines on the target still requires a C++20
compiler and the Visual C++ Redistributable on Windows.

On Linux, `arestrix-<version>-linux-x64.tar.gz` was generated, verified with `sha256sum -c`,
extracted to `/tmp` and validated outside the checkout with `arestrix-cli version`, `project
validate` on `examples/01-svg-agente` and a `SHA256SUMS` check (Ubuntu 24.04/WSL2, 2026-11-13). The
Studio window needs GTK 3 and WebKitGTK 4.1; on Linux it tries the WebView first and falls back to
the default browser when those libraries are missing. CLI, gateway and the CPU local runtime work
without those libraries.

## 3.2 Automated portable deploy (Windows)

`.github/workflows/deploy.yml` builds and publishes the portable package from GitHub Actions:

- a push to `main` updates the rolling
  [`latest`](https://github.com/frankalcantara/Arestrix/releases/tag/latest) pre-release;
- pushing a `v*` tag publishes or updates a stable release for that tag;
- a manual dispatch follows the selected branch/tag.

The staging folder records `skill-policy-version.txt`. On upgrade, the installer compares it with the previous installation and shows a warning when the skill policy changed; Studio then opens the revalidation modal for imported skills.

The workflow runs the canonical gate (`release_check.py --skip-e2e`), creates the ZIP and the Inno
Setup installer with `package_release.py --installer`, and attaches the archive, `.zip.sha256`,
`*-setup.exe`, `*-setup.exe.sha256`, `SHA256SUMS`, `SBOM.json`, `release-manifest.json` and
`release-report.json` to the release. The portable folder is ready to run from any directory
(`start-arestrix.bat`); no development checkout is needed.

Pull requests run the pre-merge gate in `.github/workflows/ci.yml`: quality/static checks, Vitest +
Studio build, native Windows CTest and native Linux build/CTest/`.deb` packaging. The six jobs
(`quality`, `frontend`, `native-windows`, `native-linux`, `lockfile` and `zizmor`) are required
status checks on `main`, and Dependabot keeps npm, GitHub Actions and Git submodules current. Submodule pointers
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
upgrade preserving user files/projects and a complete uninstall. Clean-machine install and
Intune/SCCM validation remain pending; the installer is included in the release but is not yet a
fully validated release artifact.

## 3.4 Debian package (phase 5B2 Linux)

On Linux, the Debian package is built from the same staging folder:

```bash
python3 scripts/package_release.py --format deb
# or together with the tarball:
python3 scripts/package_release.py --format both
```

The output is `release/arestrix_<version>_amd64.deb` (pre-release versions replace `-` with `~`, as
the Debian convention requires) with a `.sha256` sidecar. The package:

- installs the portable layout under `/usr/lib/arestrix` (Studio, CLI, gateway, memory tools, UI,
  runtime, examples, headers and documentation);
- creates `arestrix`, `arestrix-cli`, `arestrix-gateway` and `arestrix-memory` symlinks in `/usr/bin`;
- adds `/usr/share/applications/arestrix.desktop` and 16 to 256 px hicolor icons;
- installs `copyright`, `changelog.Debian.gz` and links to the README, CHANGELOG and SBOM under
  `/usr/share/doc/arestrix`;
- declares `Depends: libc6 (>= 2.38), libstdc++6 (>= 13), libgcc-s1` and recommends
  `libgtk-3-0t64 | libgtk-3-0`, `libwebkit2gtk-4.1-0` and `g++ | clang`. The builder reads the
  GLIBC/GLIBCXX symbols referenced by the binaries and refuses to package when `control` understates
  them.

Local install:

```bash
sudo apt install ./release/arestrix_2.10.0_amd64.deb
arestrix-cli version
```

In CI, the `native-linux` job builds the frontend, generates the manifest and runs the same
packaging on every pull request. Local validation covers `dpkg-deb --info`, `dpkg-deb -x`, running
`arestrix-cli`/`arestrix-memory` and `project validate` on the extracted tree, checking the symlinks
and `desktop-file-validate`. A real `apt`/`dpkg` install on a clean machine and the AppImage remain
pending.

## 4. Alternative: local PFX/thumbprint signing

The automated release publishes unsigned artifacts. The script below is an optional manual step for organizations that have their own code-signing certificate. After the gate passes, sign the executables with a local corporate certificate:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Alternatively use `ARESTRIX_SIGN_THUMBPRINT` for a certificate installed in `My`. The script covers
`arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe`, `arestrix-memory.exe` and an optional `-RuntimePath`.

## 5. Not in phase 5A

- Windows Inno Setup installer implemented (5B2 partial; clean-machine install, upgrade, uninstall,
  silent install and Intune/SCCM validation still pending), the Linux portable `.tar.gz` and Debian
  `.deb` packages validated; the AppImage is not implemented.
- E2E in the automated release path and a macOS CI matrix.
- Signed remote catalog (Ed25519/minisign) and hosting.

Those are the next phase-5 slices.
