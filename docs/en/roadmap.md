# Roadmap and known limitations

> **Applies to:** Arestrix Beta 2 (`2.0.0-beta.1`) · Last reviewed: 2026-10-21
>
> This page records what is intentionally incomplete and the order we plan to address it. It is not a
> commitment to dates.

## Delivered in Beta 1.5

- Local/OpenAI-compatible endpoints in project and vertex settings.
- Pre-approved GGUF catalog with resumable, SHA-256-verified downloads.
- Managed llama.cpp runtime with native `llama-server` router mode, loopback binding and an ephemeral
  key.
- Hardware detection, model recommendation, VRAM-aware `--models-max` and CPU fallback.
- Tool-calling verification for Qwen2.5 7B/14B.
- Quick setup and "Use in Assistant".
- On-prem `arestrix-gateway` with virtual keys, budgets by key/project/user/provider/model, rate limits,
  usage ledger, Prometheus metrics and fail-closed routing.
- Mistral provider support.
- Local asset validation and a Windows Authenticode signing script.
- Reproducible local release gate with SHA-256 manifest, `SHA256SUMS` and SBOM.
- Portable package with the Studio, CLI, gateway, UI, runtime library and compiler headers.

## Known limitations

### Platform and CI

- Local runtime and gateway have been validated end-to-end on **Windows** only.
- The Linux code paths exist, but the Linux build, runtime download/extraction, router and gateway are
  not yet covered by an end-to-end test.
- CI is intentionally disabled during beta stabilization; the release matrix is not yet automated.

### Distribution

- The final code-signing certificate/HSM and the signed release execution are pending.
- The portable package (5B1) is implemented. Installer packaging (MSI/WiX, MSIX or Inno Setup; Linux
  AppImage/deb/tarball) is pending in 5B2.
- A signed remote catalog/manifest (Ed25519/minisign) is not implemented. The embedded catalog is
  protected by the signed application binary; remote updates need a key pair and hosting decision.
- Runtime upgrades are tied to an application version because the manifest is embedded.

### AI Gateway

- SQLite single-node only. HA or multiple gateway instances require Postgres/Redis and distributed
  counters.
- Streaming (`stream: true`) is rejected with `400 streaming_unsupported`.
- Admin is CLI-only; there is no web console, SSO or role-based access.
- TLS is delegated to a reverse proxy; the gateway binds HTTP to loopback by default.
- Pricing is operator-maintained. Unknown models record cost zero (token budgets still apply).
- Budgets are reserved conservatively and reconciled with provider usage; a provider that ignores
  `max_tokens` can slightly overrun the reservation.
- Attachment/image/embedding endpoints are intentionally restricted; the gateway currently proxies
  chat/completions only.
- True enterprise enforcement depends on not distributing provider keys to runners and restricting
  runner egress to the gateway. `ARESTRIX_AI_GATEWAY_POLICY=locked` is the client/OS policy layer, not
  a substitute for network controls.

### Project memory (Beta 2)

Implemented in the core:

- AMG in SQLite (`.arestrix/memory/memory.db`) with temporal edges, evidence, audit, working memory
  and usage metrics.
- Lessons, Blackboard, `memory_search`/`memory_record_lesson` tools and a compiled
  `contextBudgetTokens` brief ordered by confidence/recency.
- Success/failure episodes, `memory_hit` (injected/consulted) and metrics with retries and outcome.
- One-time legacy JSON import and automatic agent-failure lessons.
- API-consumption panel in the UI, native `arestrix-cli memory report`, AI Gateway memory impact,
  Studio A/B and examples 04 and 05. `memory_report.py` remains optional for development.
- Workspace and institution scopes. `arestrix-memory` exposes authenticated REST, MCP HTTP `/mcp`,
  MCP stdio and memory-only tokens, fail-closed without tokens.
- Hybrid ranking with embeddings (EmbeddingGemma 2) and idempotent L2/M3 consolidation.
- Opt-in background consolidation per project (`off`, `idle` or `after_episodes`), with observable
  state in the panel and deterministic shutdown.
- Authenticated `/metrics` in `arestrix-memory`, with REST/MCP counters, 401 failures, response
  classes, uptime and version.
- Conflict list and consolidation state in the project memory panel.

Still pending:

- Full cognitive graph visualization and conflict editing in the UI (the conflict list already
  exists).
- LLM consolidation, `memory_handoff` and recursive learning.
- Consolidation coordination across multiple service processes/instances.

Limitations of the institution service and background worker:

- Single-node SQLite; no HA, multi-machine sync or coordination between instances.
- The service does not terminate TLS or enforce rate limits; both depend on a reverse
  proxy/gateway.
- Tokens load from file/JSON/env and live only in memory; restarting requires reloading.
- The consolidation worker runs inside the project process, only for persistent memory, and does not
  survive process shutdown.

### Models

- Only Qwen2.5 7B/14B have verified tool calling. Other catalog entries are curated but may be
  chat-only or unverified for tools.
- 14B quality/speed depends on GPU/VRAM; CPU-only execution is slow.
- No automatic model update, deduplication or disk quota management yet.
- Model weights are not redistributed; downloads depend on the upstream publisher and license.

## Beta 2 (in development)

The Beta 2 line is versioned as `2.0.0-beta.1`. The version bump is in place, but tagging and
publishing remain blocked until the Beta 2 gate passes.

Already delivered for the Beta 2 line: closed call graph, interactions, generated forms, parallel
calls, callable subflow, flow Call vertex, notifications, async `call_result`, open trace, release
gate, portable package and the consolidated SVG visual layer with per-vertex run records.

Pending before publishing Beta 2:

- Windows Inno Setup installer implemented; clean-machine install/upgrade/uninstall, silent install
  and Intune/SCCM validation plus the Linux package (5B2) remain.
- SignPath Foundation integration implemented; account/project approval, first signed release and
  signing of the Inno uninstaller remain. Company certificate migration is a later step.
- Code signing, Windows/Linux CI matrix and signed remote catalog (5C).
- The full test matrix in [Beta 2 test plan](../quality/beta2-test-plan.md), including clean-machine
  install/upgrade/uninstall and manual Studio homologation.
- Published checksums, SBOM, notices and release notes.

## Next steps

1. **Linux + CI**
   - End-to-end Linux build, runtime install, llama.cpp extraction, router and gateway tests.
   - CI matrix for Windows/Linux and automated catalog/manifest validation.

2. **Final distribution**
   - Choose and implement installer packaging.
   - Procure/configure the code-signing certificate and sign release artifacts in CI.
   - Define the catalog signing key pair, custody, rotation and hosting.
   - Publish checksums, notices and release notes.

3. **Gateway enterprise hardening**
   - Postgres/Redis for HA and distributed rate limits.
   - Streaming support with reconciliation.
   - Admin web/API, SSO, roles, audit export and alerts (80%/100%).
   - Optional OTLP/SIEM export.

4. **Model operations**
   - Automated tool-calling tests for new catalog entries.
   - Hardware profiles and per-model context/VRAM tuning.
   - Model update checks, disk quotas and cleanup policies.

5. **UX polish**
   - First-run wizard with hardware-based recommendation.
   - Richer diagnostics for GPU/driver/model failures.
   - One-click switch between managed local runtime and external Ollama/LM Studio/vLLM.

## Trust boundary

- Local models never pass through the AI Gateway and are exempt from budgets.
- Hosted-provider credentials belong to the gateway, not to runners.
- The gateway is the enforcement point for budgets and rate limits.
- The OS/MDM policy and network egress rules are the final boundary; the desktop UI is a convenience
  and cannot enforce policy against a local administrator.
