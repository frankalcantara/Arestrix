# Changelog

All notable changes to Arestrix are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-release identifiers follow SemVer (`1.5.0-beta.1`); the marketing name is **Beta 1.5**.

## [Unreleased]

### Added

- **Timed Git Push and Rate-Limit Protection (GitHub Abuse Prevention):** Decoupled local commit from remote push. Pipeline execution (`F5`) now records fast local commits without consuming remote GitHub network quotas.
- **Batched Timed Push Timer:** Background timer (default every 10 minutes) automatically batches and pushes pending changes or unpushed commits when detected.
- **Immediate Manual Push Action:** Dedicated one-click Push button on Quick Access Toolbar (QAT) and within `GitAccountModal` automation settings.
- **Git History Audit:** Verified entire repository history confirming `.arestrix` directories have never been tracked or committed.

- Project memory is now stored in SQLite (`.arestrix/memory/memory.db`) by `MemoryStore` and
  `MemoryService`, with scopes, nodes, temporal edges, episodes, evidence, audit and working state.
- Temporal multigraph queries (`as of`, invalidation, recursive neighbours) and FTS5 node search.
- `memory_usage` records API prompt/completion tokens, calls, latency and injected memory context
  for each agent; `scripts/memory_report.py` summarises it.
- Agents can use `memory_search` and `memory_record_lesson` when memory is enabled.
- Context briefs are bounded by `contextBudgetTokens` (~4 characters per token).
- `examples/04-memoria` and `scripts/e2e_memory.py` verify persistence, injection, filters,
  automatic failure lessons and API usage.
- MCP servers accept HTTP/Streamable HTTP and custom headers with `${VAR}` expansion, enabling
  hosted APIs such as Keenable (`X-API-Key`) without storing secrets in the project.
- `examples/05-matematica-com-memoria` wires a Keenable research vertex to a mathematics writer
  that uses project memory and delivers Markdown.
- Documentation now covers the AMG SQLite store, memory tools/budget/metrics, the new example and
  the headless `arestrix-cli` (`run`, `gen`, CI, exit codes and troubleshooting).
- AI Gateway records memory impact per request (`memory_enabled`, context chars, estimated memory
  tokens, agent/run) from native C++ headers and exposes it in `/v1/usage` and the usage report.
- The C++ agent emits one `usage` telemetry event per run, so the Standalone Runner diagnostics
  receive real prompt/completion/total tokens and memory context without Python or JS parsers.
- Memory runtime now records sanitized success/failure episodes, injected/used memory hits,
  retries and task outcomes. The brief compiler orders newer/higher-confidence lessons first and
  never emits partial Blackboard JSON.
- Studio **Global Defaults → Memory** now exposes Measurement / A-B mode and metric recording; the
  `ARESTRIX_MEMORY_DISABLE` / `ARESTRIX_MEMORY_METRICS` variables remain the automation fallback.
- `arestrix-cli` adds automation commands: `env set|reset|list|check`, `check`, `--project`,
  `--json`, `--timeout` and `--report`. Environment variables share the Studio's secure user store.
- `arestrix-cli doctor`, native `memory report`, `mcp list|test|tools` and
  `project info|graphs|validate` cover preflight, memory inspection, MCP diagnostics and project
  validation without Python or the Studio.
- `arestrix-cli` adds `--ci`, shell completion and sequential `run --batch` with checkpoint and
  `--resume`.
- Documentation now covers the instrumented memory (episodes, hits, outcomes, A/B, gateway impact),
  the complete CLI automation surface and native `memory report`.
- Legacy `AgentBlackboard`/`SemanticMemoryEngine` JSON files are imported into SQLite.
- `arestrix-cli --no-user-env` (and `ARESTRIX_DISABLE_USER_ENV=1`) skips the persisted user
  credential store, so CI and hermetic tests use only the explicit process environment.
- On Windows, `arestrix.exe` now hides the console window when launched outside a terminal
  (double-click or shortcut). It keeps the console attached when launched from a terminal and always
  mirrors the backend log to `%LOCALAPPDATA%\Arestrix\logs\studio.log` (rotated at 5 MB).
- Agents can call agents declared in the graph through dashed **call edges**. The runtime exposes
  `call_agent` only for the declared targets, returns the callee's answer synchronously, and enforces
  `maxCalls`, `ARESTRIX_MAX_CALL_DEPTH` and `ARESTRIX_MAX_TOTAL_CALLS`. Call targets are callable-only
  vertices and never become scheduled Taskflow tasks.
- The graph schema adds `EdgeKind`/`CallSettings`, the agent `call` output port, dashed violet
  rendering, an Inspector **Delegation** section and call-graph validation.
- `examples/06-chamada-agente` and the examples E2E cover a coordinator calling a specialist,
  including the `call_agent` tool, the call result and the output artifact.
- The graph editor can author **interaction** vertices: a safe form schema (text, textarea, number,
  boolean, choice, multi-choice, date), channel, timeout, who answers and a sensitive-answer flag,
  with a rose card and an Inspector form builder.
- The interaction runtime validates the form and reads headless answers from `--answers`
  (`ARESTRIX_INTERACTION_ANSWERS`), writes `.arestrix/out/<nodeId>.txt`/`.json`, fails closed without
  a channel, and can be invoked by an agent through a call edge.
- `arestrix-cli run --answers <file.json>` and `examples/07-interacao` cover the headless flow,
  including the fail-closed case.
- Studio runs interactions through a bidirectional stdin/stdout host channel: the runtime emits
  `[ARX_INTERACTION]`, the Studio opens a safe form modal and answers through the
  `answer_interaction` RPC. Timeout, cancel and `fail`/`default`/`abort` policies are enforced.
- The interaction modal builds controls from the declared field types with DOM/textContent only;
  model-generated HTML/JavaScript is never rendered. Sensitive text fields are masked.
- The Studio Runner panel shows **Aguardando resposta** while an interaction is pending and resumes
  the status when it closes.
- `arestrix-cli run` prompts field by field in an interactive terminal (`:q` cancels); piped/non-TTY
  runs without `--answers` still fail closed. CLI and Studio use the same host protocol; exported
  runners need an interactive host or `ARESTRIX_INTERACTION_ANSWERS`.
- Every interaction attempt is audited at
  `.arestrix/runs/<runId>/interactions/<requestId>.json` with form, answers, channel, timestamps and
  `answeredBy`; sensitive values stay out of telemetry/logs. `runner_manifest.json` lists interaction
  requirements and warns when an exported runner has no interactive host.
- The examples E2E verifies the audit file for both answered and fail-closed interactions.
- Interaction forms can be generated by the vertex (`schemaSource: "vertex"`): a model receives the
  markdown context, `inputContext` and bounded artifact contents, returns a JSON schema, and the
  runtime validates ids/types/labels/options/limits before showing it. Invalid schemas or model
  failures apply `generator.fallback` (`fail` or `free_text`); HTML/JS is never rendered.
- The Inspector now edits schema source, generator provider/model/temperature/fallback and context
  artifacts; the modal lists artifacts and masks sensitive text fields.
- `examples/08-formulario-gerado` and the examples E2E cover a generated form, its audit, artifact
  context and the `free_text` fallback when the model returns an invalid schema.
- **Parallel delegation (phase 4A):** when a model requests several `call_agent` calls in the same
  turn, the Agent loop executes them concurrently and returns the tool results in the requested
  order; depth and call budgets are inherited per thread. Mixed turns stay sequential and
  `ARESTRIX_PARALLEL_CALLS=0` disables the parallel path.
- `examples/09-chamadas-paralelas` and the E2E cover two specialists delegated in one turn, with the
  call graph still closed to the declared targets.
- **Callable subflow (phase 4B):** a `subflow` target with a `childGraph` is compiled into a native
  function in the same executable. The runtime registers it, passes the call input as `inputContext`
  to child agents/interactions, runs the nested graph and returns the child output artifact. Nested
  call edges/subflows are rejected in v1.
- `examples/10-subflow-chamavel` and the E2E cover a coordinator calling a subflow, the nested worker
  receiving the call input, the child artifact and the returned text.
- **Database disaster recovery and Git sync safety:** binary SQLite databases are excluded from Git
  tracking in accordance with GitHub Acceptable Use Policies; `DatabaseBackupService` exports clean
  text SQL dumps (`storage/database.sql`) and cognitive memory snapshots (`storage/memory.dump.json`)
  on shutdown, and automatically restores them upon loading projects or cloning repositories.

## [1.5.0-beta.1] - 2026-10-19

Beta 1.5 focuses on local AI models and enterprise usage governance.

### Added

- Pre-approved GGUF model catalog with resumable downloads, SHA-256 verification and per-model
  provenance (`verifiedAt`, `verifiedWith`). Qwen2.5 7B/14B passed real tool-calling tests.
- Managed local runtime: on-demand llama.cpp download/extraction, native `llama-server` router mode,
  generated preset, loopback-only binding, ephemeral API key, health checks and logs.
- Hardware detection (RAM, CPU threads, GPUs/VRAM), model recommendation, VRAM-aware `--models-max`
  and automatic CPU fallback when the primary backend fails to start.
- Runtime version upgrades with cleanup of old versions, local asset validation and a Windows
  Authenticode signing script.
- Local Models screen with runtime install/start/stop, per-model download/verify/delete, quick setup
  for the recommended model and "Use in Assistant".
- On-prem `arestrix-gateway`: OpenAI/Anthropic-compatible proxy with virtual keys, budgets by
  key/project/user/provider/model, rate limits, usage ledger, Prometheus metrics and fail-closed
  runner integration.
- AI Gateway settings in Global Defaults, plus an enterprise policy lock
  (`ARESTRIX_AI_GATEWAY_POLICY=locked`).
- Mistral provider support (API, Studio, vertex settings and gateway route).
- `dev.py` verification modes: `--test-all`, `--check`, `--gateway-smoke`, `--e2e` and `--full`;
  a fake-upstream HTTP smoke test for the gateway.

### Changed

- Version and documentation banners moved to Beta 1.5 (`1.5.0-beta.1`).
- The `third_party/webui` submodule was replaced by `third_party/FastWebUI`
  (https://github.com/frankalcantara/FastWebUI), a faster, API-compatible fork of WebUI
  (`2.5.0-beta.4`). `CMakeLists.txt`, `dev.py` and the license check were updated accordingly; on
  Windows the build now also compiles `src/webview/win32_wv2.cpp`.- Hosted providers can be routed through the AI Gateway; local providers (`local`/`jev`) remain
  direct and exempt from budgets.

### Known limitations

- Linux end-to-end validation and CI remain pending.
- Final distribution (code-signing certificate, remote signed catalog, installer packaging) remains
  pending and depends on external key/certificate decisions.
- `arestrix-gateway` is single-node SQLite, without streaming, web admin or SSO; TLS is delegated to
  a reverse proxy.
- Pricing for unknown models defaults to zero; operators must maintain the price table. Token budgets
  still apply.
- A signed remote catalog (Ed25519/minisign) is not implemented yet; the catalog is embedded in the
  signed application binary.

## [1.0.0-beta.1] - 2026-10-19

First public beta of Arestrix Studio and the native C++20 pipeline compiler.

### Added

- Visual Studio canvas for designing multi-agent pipelines as a directed graph.
- Native C++20 / Taskflow code generation and compilation from the visual graph.
- Standalone runner export with execution timeline, diagnostics and telemetry.
- Model Context Protocol (MCP) client with a tool registry and per-vertex tool selection.
- Local/OpenAI-compatible model endpoints (Ollama, LM Studio, llama.cpp, vLLM) configurable per
  project and per vertex, with native model discovery. No inference runtime is bundled or started.
- Projects, workspaces, persistent memory, local skills and skill catalogs.
- Bilingual interface and documentation (`en-US` and `pt-BR`).
- Worked examples covering an SVG agent pipeline, support triage and a composed
  "project of projects" workflow.
- Automated test suites for the frontend, the C++ runtime and the examples.

### Security

- Credential resolution through Windows DPAPI and Azure Key Vault / Managed Identity.
- SHA-256 HMAC integrity manifest verified at runner startup (fail-closed).
- Sandbox confinement for tool-assisted file operations.
- Pre-commit secret scanning and third-party license verification.

### Changed

- Repository versioning is now centralized in `version.txt` and checked by
  `npm run check:version`.
- Repository governance files and GitHub collaboration templates were added.
- Documentation was reorganized under `docs/`, with a bilingual hub, semantic file names,
  version banners and separate product, quality and security sections.
- READMEs were shortened to focus on what the project is, how to run it and where to find the
  detailed documentation.
- Screenshots were converted to WebP and resized to reduce repository and page weight.

### Known limitations

- Binaries generated from a graph currently target Windows. The Studio itself runs on
  Windows and Linux.
- Beta APIs, project formats and command-line flags may change before general availability.
- Third-party license verification is automated; a consolidated `THIRD_PARTY_NOTICES.md`
  is planned for a follow-up release.
- The `braces` advisory documented in `docs/security/dependency-advisories.md` affects the build chain only;
  no fixed release is available upstream yet.

[1.5.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v1.5.0-beta.1
[1.0.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v1.0.0-beta.1
