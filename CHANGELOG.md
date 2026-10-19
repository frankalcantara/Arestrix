# Changelog

All notable changes to Arestrix are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-release identifiers follow SemVer (`1.0.0-beta.1`); the marketing name is **Beta 1.0**.

## [1.0.0-beta.1] - 2026-10-19

First public beta of Arestrix Studio and the native C++20 pipeline compiler.

### Added

- Visual Studio canvas for designing multi-agent pipelines as a directed graph.
- Native C++20 / Taskflow code generation and compilation from the visual graph.
- Standalone runner export with execution timeline, diagnostics and telemetry.
- Model Context Protocol (MCP) client with a tool registry and per-vertex tool selection.
- Local/OpenAI-compatible model endpoints (Ollama, LM Studio, llama.cpp, vLLM) configurable per
  project and per vertex, with native model discovery. No inference runtime is bundled or started.
- Pre-approved GGUF model catalog with resumable downloads and SHA-256 verification, plus a pinned
  llama.cpp binary manifest (`b11669`).
- Managed local runtime: on-demand llama.cpp download/extraction, native `llama-server` router mode,
  loopback-only binding, ephemeral API key, generated model preset, health checks and logs.
- Local Models screen with runtime install/start/stop, per-model download/verify/delete and one-click
  "Use" that starts the runtime and fills the project provider/model.
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

[1.0.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v1.0.0-beta.1
