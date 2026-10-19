# Security and governance model

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-19

Arestrix uses defense in depth. The controls below are implemented in the product, but their
effectiveness depends on how a pipeline is configured and deployed. Validate them against your own
security baseline before production use.

## 1. Zero-secret core

- Project files may name an environment variable or use a `${NAME}` reference; they never store the
  value.
- At runtime, secrets are resolved through Windows Credential Manager (DPAPI) or Azure Key Vault
  with Managed Identity / IMDS.
- Sensitive buffers are scrubbed after use.
- The pre-commit hook and test suite scan for common credential formats and personal data.

See [SECURITY.md](../../SECURITY.md) for reporting vulnerabilities.

## 2. Cryptographic integrity

- Packaging produces an integrity manifest (`arestrix.manifest.sig`).
- The runner can verify the SHA-256 HMAC signature at startup.
- If verification fails, execution is denied (fail-closed).

## 3. Sandbox and cost confinement

- Tool-assisted file operations are confined to the project workspace.
- Agent loops have iteration and token budgets to prevent runaway model spending.
- Sandbox drivers isolate host, container and microVM execution paths.

## 4. Enterprise deployment

- The packaging flow supports MSI and MSIX artifacts for managed Windows deployment.
- Silent installation can be evaluated with `msiexec /i arestrix-runner.msi /qn`.
- Intune and SCCM compatibility depends on your organization's signing, allow-listing and update
  policies; validate the generated package in a pilot ring before broad rollout.

## 5. Dependency and supply-chain hygiene

- Third-party code is vendored under `third_party/` with pinned submodules and license checks.
- npm production dependencies are checked by `scripts/check_third_party_licenses.py`.
- Known advisories are tracked in [dependency-advisories.md](dependency-advisories.md).
- Releases should publish checksums and an SBOM; see the release section in
  [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Threat model boundaries

The following are outside the current beta scope:

- protecting a machine that is already compromised by an administrator-level attacker;
- multi-tenant isolation between users of the same operating-system account;
- model-provider security and data-retention policies;
- third-party MCP servers that intentionally execute arbitrary code outside the sandbox.

For a deployment review, start from the [architecture guide](../en/architecture.md) and the
[sandbox documentation](../en/agents-and-execution.md).
