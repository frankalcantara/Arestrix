# Changelog

All notable changes to Arestrix are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-release identifiers follow SemVer (`1.5.0-beta.1`); the marketing name is **Beta 1.5**.

## [Unreleased]

### Added

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
- Legacy `AgentBlackboard`/`SemanticMemoryEngine` JSON files are imported into SQLite.

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
- Hosted providers can be routed through the AI Gateway; local providers (`local`/`jev`) remain
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
