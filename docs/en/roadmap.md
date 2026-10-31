# Roadmap and known limitations

> **Applies to:** Arestrix 2.5 (`2.5.0`) · Last reviewed: 2026-10-31
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

### Project memory

Implemented in the core:

- AMG in SQLite (`.arestrix/memory/memory.db`) with temporal edges, evidence, audit and separate
  telemetry (`.arestrix/telemetry/telemetry.db`).
- Per-vertex `off`/`consult`/`learn` modes, lessons, `memory_search` and pending
  `memory_propose_lesson` proposals, plus a compiled `contextBudgetTokens` brief ordered by
  confidence/recency.
- Success/failure episodes, `memory_hit` (injected/consulted) and metrics with retries and outcome.
- Automatic failure proposals (never activated without approval) and examples 04 and 05.
- API-consumption panel in the UI, native `arestrix-cli memory report`, AI Gateway memory impact and
  context A/B in the Studio. `memory_report.py` remains optional for development.
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

### Agents: distance from chat harnesses

Comparison made on 2026-10-28 against the conceptual agent model described in the VS Code/Copilot
documentation — loop, context, multi-agent and control — not the IDE. The list covers only the agent
loop: deterministic execution, skill/MCP governance, project memory and observability are axes where
Arestrix already goes beyond that model.

| Gap | Weight | Status |
| :--- | :--- | :--- |
| **Context compaction and budget inside the loop.** Each tool output is capped at 32 KB (`kMaxToolOutput`), but `chat.messages` only grows (up to 30 turns): there is no summarization, eviction or global budget, and the model window is not measured. | High | After memory. The cheap step is not a per-tool cap (that already exists), but a **global** character ceiling per run, with summarization/eviction of older history when it is exceeded. |
| **Per-tool-call policy** (`allow` / `ask` / `deny`, with argument patterns). The allowlist is binary today: the tool exists or does not, and there is no pause for approval inside a turn. | High | To be decided. The `ask` path reuses the existing interaction channel (C++ blocks, the Studio shows the request, with timeout and audit). |
| **Handoff with context transfer.** Calls isolate the target on purpose (explicit input only); there is no handing the baton over with the conversation. | Medium | Evaluate when a conversational use case appears; the interaction vertex covers approval between steps. |
| **Progressive skill loading.** The whole `SKILL.md` goes into the system prompt; there is no discovery by name/description nor on-demand reading of the folder resources. | Medium | Grows with the number of skills and support files. |
| **Native `web` tool.** The native set is `read`, `write`, `edit`, `bash`, `validate_svg` and the memory search/proposal/state tools. | Medium-low | Covered by MCP; check demand before adding a native tool. |
| **Per-agent model fallback.** There is a model per vertex, but no preference list (A → B) for 429s/unavailability. | Medium-low | Per-provider resilience; the gateway fails closed, it does not fall back. |
| **Always-on, path-scoped project instructions** (`AGENTS.md`, `applyTo`) and parameterized **prompt files**. | Medium for teams | Per-agent skills, project defaults and templates cover part of it; there is no global file nor glob scope. |
| **OAuth for MCP HTTP.** We support stdio and HTTPS with headers/env and public-URL validation. | Low | No interactive authorization flow. |
| **Lifecycle hooks** (`preToolUse`, `postToolUse`, `sessionStart`…). | Not planned as a generic system | Partially covered by MCP vertices, static validation, critical sections and interaction — but none of them vetoes a call **inside** the loop. The real case (a destructive `bash` command) should be handled by a command policy, not a hook runtime. |
| **Remote/cloud execution and background sessions.** | Out of scope | The product is desktop + CLI + gateway; the standalone Runner covers packaged execution. |

Two recorded decisions:

- **Memory before compaction.** Memory solves the problem *across* runs (curated brief, notes, lessons)
  and reduces pressure on the context, but it does not replace compaction *inside* a run: the message
  list keeps growing. The global context ceiling does not depend on memory and can land before it.
- **MCP vertices do not replace hooks.** An MCP vertex is a deterministic graph task: it adds capacity
  and enforces order. A hook intercepts the lifecycle and can deny an action the model chose. What
  they share is "runtime decision, not model decision"; what is missing is the veto inside the loop.

## Beta 2.3 (pre-release)

The Beta 2.3 line is versioned as `2.3.0-beta.2` and is published as a development pre-release
through the deploy workflow.

Delivered in Beta 2.3: the **Arestrix Library** phases 1-7, with audited skill/MCP/template assets,
Studio/Workspace/Project scopes, project pins, a native GitHub source with host allowlist, commit
pin, license check and on-demand asset download; the **File menu** review, including the redesigned
Office-style **Open** screen and the Info/New/Save/Export/Options refresh; and canvas background
synchronization with the global Light/Dark/Follow-system theme.

Pending after the `2.3.0-beta.2` pre-release:

- Clean-machine install/upgrade/uninstall, silent install and Intune/SCCM validation, plus the Linux
  package (5B2).
- SignPath account/project approval, first signed release and signing of the Inno uninstaller.
- Windows/Linux CI matrix, signed remote catalog (5C) and the full test matrix in
  [Beta 2.3 test plan](../quality/beta2.3-test-plan.md).
- Three manual library homologation cases: light/dark sizes, clean install/upgrade and offline use.

## Beta 2.2 (previous pre-release)

The Beta 2.2 line is versioned as `2.2.0-beta.1` and is published as a development pre-release
through the deploy workflow.

Delivered in Beta 2.2: Office-style Properties hub with editable Studio, Workspace and Project
scopes; explicit inheritance and origin badges; secure Studio/Workspace settings stores; write-only
provider keys and AI Gateway editing; project defaults/environment/SQLite editing; Workspace sandbox,
audit, rebuild and reset actions; Project Manager settings deep-link; Office `BaseModal` convergence
for vertex Sandbox/Model/Tools/Memory; and the global Light/Dark/Follow-system theme.

Pending after the `2.2.0-beta.1` pre-release:

- Clean-machine install/upgrade/uninstall, silent install and Intune/SCCM validation, plus the Linux
  package (5B2).
- SignPath account/project approval, first signed release and signing of the Inno uninstaller.
- Windows/Linux CI matrix, signed remote catalog (5C) and the full test matrix in
  [Beta 2.3 test plan](../quality/beta2.3-test-plan.md).
- Remaining contrast review for secondary legacy editors and final Studio homologation.

## Beta 2.1 (previous pre-release)

The Beta 2.1 line is versioned as `2.1.0-beta.1` and is published as a development pre-release
through the deploy workflow.

Already delivered for the Beta 2.1 line: closed call graph, interactions, generated forms, parallel
calls, callable subflow, flow Call vertex, notifications, async `call_result`, open trace, release
gate, portable package, Windows Inno Setup installer, SignPath Foundation integration, canvas
appearance preferences, execution-focused console/HUD behavior, the consolidated SVG visual layer
with per-vertex run records, and the Office 365 ribbon refresh across all tabs.

Pending after the `2.1.0-beta.1` pre-release:

- Clean-machine install/upgrade/uninstall, silent install and Intune/SCCM validation, plus the Linux
  package (5B2).
- SignPath account/project approval, first signed release and signing of the Inno uninstaller.
  Company certificate migration is a later step.
- Windows/Linux CI matrix, signed remote catalog (5C) and the full test matrix in
  [Beta 2.3 test plan](../quality/beta2.3-test-plan.md).
- Manual Studio homologation and publication of checksums, SBOM, notices and release notes.

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
