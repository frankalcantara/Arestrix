# Security Policy

Arestrix handles model credentials, local files and generated executables, so security reports
are taken seriously. Please follow this policy instead of opening a public issue.

## Supported versions

| Version | Supported |
| :--- | :---: |
| `2.2.0-beta.1` (Beta 2.2) | Yes |
| Older pre-release builds | No |

During the beta, only the latest published pre-release receives security fixes.

## Reporting a vulnerability

Use **GitHub private vulnerability reporting**:

1. Open the repository's **Security** tab.
2. Select **Advisories** → **Report a vulnerability**.
3. Include the affected version, operating system, reproduction steps and, when possible,
   a minimal proof of concept.

Do **not** include real API keys, tokens or customer data in the report. Use test credentials
and redact local paths.

If private reporting is unavailable, email [contact@arestrix.com](mailto:contact@arestrix.com)
before disclosing anything publicly. Do not send secrets or customer data in the first message.

## What to include

- Arestrix version (`version.txt` or the release tag) and commit hash.
- Windows or Linux distribution and version.
- Whether the issue affects the Studio, the CLI, the generated runner, or the packaging flow.
- Logs, screenshots or a minimal project folder that reproduces the issue.
- Whether the vulnerability is already public or has been reported elsewhere.

## Scope

In scope:

- Studio, CLI and generated runner code in this repository.
- Credential handling (DPAPI, Azure Key Vault, environment variables).
- Sandbox confinement and filesystem boundaries.
- HMAC manifest generation and verification.
- Release packaging and update artifacts.

Out of scope:

- Vulnerabilities in third-party dependencies without a demonstrated impact on Arestrix.
  Report those upstream and, when relevant, open a regular issue so the dependency can be updated.
- Social engineering, physical access and denial-of-service against your own machine.
- Issues that require the attacker to already control the user's account or environment.

## Response process

- Acknowledgement: best effort within 3 business days.
- Triage and severity assessment: best effort within 7 business days.
- Fixes are prepared privately and published with a GitHub Security Advisory when appropriate.
- Credit is given to the reporter unless anonymity is requested.

This is a community project and does not offer a bug bounty or a contractual response SLA.

## Dependency advisories

Known dependency issues and their mitigation status are tracked in
[`docs/security/dependency-advisories.md`](docs/security/dependency-advisories.md).
