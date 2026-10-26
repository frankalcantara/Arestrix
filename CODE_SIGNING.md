# Code signing policy

**Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).**

## Scope

This policy covers the Windows artifacts produced from the public
[`frankalcantara/Arestrix`](https://github.com/frankalcantara/Arestrix) repository:

- `arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe` and `arestrix-memory.exe`;
- the Inno Setup installer `arestrix-*-windows-x64-setup.exe`;
- the binaries inside the portable ZIP.

The Inno-generated `unins000.exe` is currently unsigned because SignPath's GitHub Action is
asynchronous; this is a tracked limitation in [signpath/README.md](signpath/README.md).

Only artifacts built by the repository's GitHub Actions workflows from public source code are
submitted for signing. Local or modified builds are not signed with the project certificate.

## Roles

| Role | Member |
| --- | --- |
| Authors / committers | [Frank de Alcantara (@frankalcantara)](https://github.com/frankalcantara) |
| Reviewers | [Frank de Alcantara (@frankalcantara)](https://github.com/frankalcantara) |
| Approvers | [Frank de Alcantara (@frankalcantara)](https://github.com/frankalcantara) |

Every signing request is reviewed and approved manually in SignPath. All maintainers use
multi-factor authentication for GitHub and SignPath.

## Build and signing process

1. GitHub Actions checks out the public repository and builds with MSVC.
2. The release gate runs the frontend, native CTest, Vitest and integrity checks.
3. SignPath verifies the GitHub-hosted build origin, signs the Windows binaries and returns them.
4. The installer is built from the signed binaries and submitted for a second signing request.
5. The signed installer and the signed binaries inside the portable ZIP are published on GitHub
   Releases together with SHA-256 checksums and the SBOM.

The artifact configurations are in [`signpath/artifact-configurations/`](signpath/artifact-configurations).
They enforce `ProductName=Arestrix`, the same `ProductVersion` for every release and
`CompanyName=Arestrix`; the PE metadata also carries a matching `FileVersion`.

## Verification

```powershell
Get-AuthenticodeSignature .\arestrix.exe
Get-AuthenticodeSignature .\arestrix-<version>-windows-x64-setup.exe
```

A valid signature reports `Status: Valid` and a signer certificate issued to **SignPath Foundation**.

## Privacy

See [PRIVACY.md](PRIVACY.md). Arestrix does not enable telemetry by default and only contacts
external services when the user configures a provider, downloads a model/runtime or runs Git/MCP
operations.

## Security reports

Please report vulnerabilities privately as described in [SECURITY.md](SECURITY.md). Do not open a
public issue for a suspected signing or supply-chain compromise.
