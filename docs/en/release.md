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

## 4. Sign the Windows binaries

After the gate passes, sign the executables with the corporate certificate:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Alternatively use `ARESTRIX_SIGN_THUMBPRINT` for a certificate installed in `My`. The script covers
`arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe` and an optional `-RuntimePath`.

## 5. Not in phase 5A

- MSI/MSIX or Linux installer packaging.
- Code-signing certificate/HSM provisioning and signing inside CI.
- Automated Windows/Linux CI matrix.
- Signed remote catalog (Ed25519/minisign) and hosting.
- Publishing checksums/SBOM to a release page.

Those are the next phase-5 slices.
