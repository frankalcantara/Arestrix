# SignPath Foundation signing

Arestrix uses **SignPath Foundation** for Windows Authenticode signing. The certificate is issued to
**SignPath Foundation**, so Windows shows `SignPath Foundation` as the publisher. This is intentional:
a free certificate in an individual's name is not available; see [SignPath Foundation](https://signpath.org).

The project must be public, actively maintained, use an OSI-approved license, and publish a
[code signing policy](../CODE_SIGNING.md). Arestrix already meets these conditions; the remaining
steps are account setup and repository secrets.

## 1. Apply and configure SignPath

1. Apply at <https://signpath.org/apply> and install the
   [SignPath GitHub App](https://github.com/apps/signpath) on the repository.
2. Create a SignPath project for `frankalcantara/Arestrix` and link the GitHub.com trusted build
   system.
3. Create two artifact configurations from the XML files in
   [`artifact-configurations/`](artifact-configurations):
   - `binaries.xml` — signs the four executables inside the uploaded ZIP;
   - `windows-installer.xml` — signs the Inno Setup executable.
4. Create signing policies, for example:
   - `test-signing` for rehearsal (SignPath test certificate, not trusted by Windows; the workflow
     then checks that a signer exists instead of requiring a trusted chain);
   - `release-signing` for production, requiring manual approval and a valid trusted signature.
5. Create an API token for a user with submitter permissions and least privilege.
6. Enable MFA on GitHub and SignPath; assign Authors, Reviewers and Approvers in the project.

The repository currently names the maintainer in all three roles. If SignPath requires a separate
second reviewer/approver for the project, add a trusted collaborator to those roles before applying.

The manual approval in SignPath is required for every release; the workflow waits for it.

## 2. Configure the repository

Create these **Actions variables** (Settings → Secrets and variables → Actions → Variables):

| Variable | Value |
| --- | --- |
| `SIGNPATH_ENABLED` | `true` to enable signing |
| `SIGNPATH_ORGANIZATION_ID` | SignPath organization ID |
| `SIGNPATH_PROJECT_SLUG` | project slug |
| `SIGNPATH_SIGNING_POLICY_SLUG` | `release-signing` (or `test-signing` while testing) |
| `SIGNPATH_BINARIES_ARTIFACT_CONFIG_SLUG` | slug of `binaries.xml` |
| `SIGNPATH_INSTALLER_ARTIFACT_CONFIG_SLUG` | slug of `windows-installer.xml` |

Create the secret:

| Secret | Value |
| --- | --- |
| `SIGNPATH_API_TOKEN` | API token for the submitter user |

Until `SIGNPATH_ENABLED=true`, the deploy workflow keeps publishing unsigned artifacts.

## 3. What the workflow does

`.github/workflows/deploy.yml` signs **tagged releases** (`v*`). Pushes to `main` keep publishing
the rolling `latest` pre-release without signing, so there is no manual approval on every commit.

1. builds and tests the project;
2. packages the four unsigned executables into `unsigned-binaries.zip`;
3. uploads it as a GitHub artifact and submits it to SignPath;
4. waits for approval, downloads the signed executables and replaces `build/*.exe`;
5. refreshes the release manifest/SHA-256 hashes;
6. builds the portable ZIP and the Inno Setup installer from the signed binaries;
7. submits the setup to a second SignPath request, downloads it and verifies the signature;
8. recomputes the setup `.sha256` and publishes the release.

The portable ZIP is built after the binaries are signed, so its checksums match the signed files.

## 4. Known limitation: uninstaller

The Inno-generated `unins000.exe` is created during setup compilation. Signing it requires Inno
Setup's synchronous `SignTool` hook; the asynchronous SignPath GitHub Action cannot sign it at that
point. The setup and the application binaries are signed, but the uninstaller remains unsigned for
now. This will be addressed with a synchronous signing tool (Inno `SignTool` + SignPath PowerShell or
REST) when the release flow is stabilized.

## 5. Verification

After a signing run:

```powershell
Get-AuthenticodeSignature .\arestrix.exe
Get-AuthenticodeSignature .\arestrix-cli.exe
Get-AuthenticodeSignature .\arestrix-gateway.exe
Get-AuthenticodeSignature .\arestrix-memory.exe
Get-AuthenticodeSignature .\arestrix-<version>-windows-x64-setup.exe
```

Expected: `Status` = `Valid`, signer subject contains `SignPath Foundation`, and the signature has a
timestamp.

## 6. Future migration to a company certificate

SignPath can host multiple certificates and signing policies. When a company certificate is
available:

1. create/import the certificate in SignPath (CSR generated on its HSM);
2. create a new signing policy and artifact configuration if the publisher changes;
3. update `SIGNPATH_SIGNING_POLICY_SLUG` (and certificate selection) in the repository;
4. keep the old releases signed and timestamped; new releases use the company identity.

No workflow redesign is required: only the SignPath project/policy and certificate change.
