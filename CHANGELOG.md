# Changelog

All notable changes to Arestrix are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
Pre-release identifiers follow SemVer (`2.3.0-beta.1`); the marketing name is **Beta 2.3**.

## [2.3.0-beta.1] - 2026-10-27

### Added

- **Assistant nested graph editing:** `add_node`, `update_node`, `remove_node`, `move_node`, `connect`,
  `disconnect`, `clear_graph`, `get_graph`, `get_node`, `validate_graph`, `attach_skill` and `attach_mcp`
  accept an optional `parent_path` with the chain of subflow/module ids. Nested edits return a single
  `set_child_graph` operation, so one Ctrl+Z restores the previous subgraph. Nested MCP governance is
  validated recursively.
- **Assistant workspace and project tools:** the default-model chat can list approved workspace **skills**,
  read them, attach them to agent vertices, discover **MCP** servers and their tools, and attach only
  **audited MCP servers** to agents or `mcp` vertices through `attach_mcp`. The Studio MCP ledger is
  mirrored to `.workspace/governance/mcp-ledger.json` so the native assistant refuses rejected or
  unaudited servers in the attach flow and in `validate_graph`. The project **name, version, description
  and output folder** can be updated after confirmation. Skill snapshots keep the audit metadata so the
  Studio validator can still enforce the governance ledger.

### Changed

- **File menu / Backstage:** the **Open** screen now follows the Microsoft Office file picker,
  with a locations list, search, recency groups, vector icons and the columns **Name / Date
  modified / Type / Size**.
- **File menu / Backstage:** **Info**, **New Project**, **Save**, **Export** and **Studio Options**
  were reviewed and aligned with the Office shell: shared form panels, action cards, vector icons,
  consistent primary/secondary buttons, and light/dark parity.
- **Studio Options** is now a settings hub for **Studio**, **Workspace** and **Catalogs**; folder and
  approved-skill/MCP lists moved to the single owner, **Workspace Properties**.
- **Screenshots and docs:** added the redesigned Open screen capture and refreshed the interface,
  properties and showcase documentation.
- **Library Phase 1:** the embedded GitHub-branded skill catalog is now the **Arestrix Library**.
  Fictional repository URLs were removed, the library import path runs through the Skill Guard
  auditor before touching a vertex, and `modal:open-github-repos` now deep-links to
  **Properties → Studio/Workspace → Libraries**.
- New **Libraries** section in Studio and Workspace Properties: create a local library and register
  a repository source directly in the Properties shell. Real GitHub synchronization remains an
  explicit, audited step planned for the next phases.
- **Library Phase 2:** asset model (`skill`/`mcp`), `LibraryStore`, `LibraryService`,
  `LibraryAuditGate` and adapters. The built-in and Workspace catalogs can be imported into a
  library with audit, hash and policy metadata; edits create immutable derivatives, and approved
  assets can be installed into `.workspace/skills`/`.workspace/mcps`. Synchronization only reads
  metadata; asset blobs will be downloaded on demand after project selection in Phase 5.
- Libraries section now exposes **Import built-in catalog** / **Import Workspace catalog** and
  shows the number of indexed assets.
- **Library Phase 3:** library/asset tree inside the Properties shell, with search, type filter,
  provenance/audit detail and actions to re-audit, duplicate/edit, install and remove. Libraries
  can also be exported, imported from a file and cloned. Studio, Workspace and Project now share
  the same library model; Project gets an enable/pin section (`ProjectLibraryStore`) whose pins
  will be consumed by the agent/graph selector in Phase 4.
- **Library Phase 4 (skills/MCPs):** `LibraryPickerModal` with audited asset selection, derive and
  create-new actions; the agent Skills tab now reads real library assets and re-audits before
  inserting a snapshot with `derivedFrom`/audit metadata. The agent Tools modal can install an MCP
  asset from a library into the Workspace and refresh the MCP picker. Project pins filter and
  prioritize the picker.
- **Library Phase 4.1:** the agent Skills editor detects when the library asset behind a snapshot
  changed, shows an update banner and can apply the re-audited library version after confirmation.
  Vertex templates are now library assets (`agent-template`): custom templates can be saved to a
  library with schema audit, and `TemplateModal → Libraries` registers and inserts a library
  template into the graph.
- **Library Phase 5:** real GitHub source through the native C++ `fetch_library_source` endpoint,
  with host allowlist, commit pin, SPDX license check, size limits, short cache and optional PAT
  from `ARESTRIX_GITHUB_TOKEN`/`GITHUB_TOKEN`. Source synchronization reads only the manifest/tree;
  assets are created as `PENDING`, downloaded on demand after selection, audited and only then
  promoted. Rejections discard the downloaded content. The Properties UI can register a source
  with a ref, sync it, download/audit an asset and install it.
- **Library Phase 6:** removed the legacy `GitHubReposModal` and `GitHubSkillsRegistry`; the
  built-in catalog now lives in `BuiltinLibraryCatalog`. Ribbon, Backstage, Defaults and the node
  modal open **Properties → Libraries** directly. Legacy `arestrix_github_skills_repos` entries are
  migrated to real `ILibrarySource` entries, and the legacy tests were replaced by library tests.
- **Library test automation:** `npm run test:libraries` runs the full frontend E2E
  (source → sync → download → audit → install → derive → template → pins) plus the filtered C++
  GitHub-source tests. `npm run test:libraries:live` adds a live GitHub manifest check.
- **Library Phase 7 (homologation):** `npm run homologation:libraries` runs a deterministic
  frontend + native gate against a local GitHub mock and writes report, matrix, environment,
  request log and stage logs under `release/homologation/<runId>/`; `--live` adds a real GitHub
  manifest smoke. The test-only override requires `ARESTRIX_TEST_MODE=1` and loopback URLs. Three
  UI/install/offline cases remain manual; keyboard navigation and focus are approved.
- **Dark theme canvas:** switching the application theme now synchronizes the graph canvas
  background. In the dark canvas, vertices use a graphite body (`#1f2937`) with light title and
  port labels instead of the light white card.

### Fixed

- **Assistant parity with the current Studio:** the default-model chat now knows and creates
  **Call** vertices and `call` edges (with `maxCalls`, timeout, error policy and async settings),
  edits **Interaction** and **Call** settings, applies the same call/interaction rules as the
  Studio validator, lists artifacts from `exports/` and `workspace/`, and confines artifact reads
  to the project. When the default AI is missing, the banner opens **Properties → Studio →
  Studio model** instead of the project defaults.
- **Assistant run governance:** an Assistant-initiated run is now handed to the Studio's
  `PipelineRunner`, so the same `GraphValidator` gate (topology plus the skills/MCP audit ledger)
  applies before the pipeline starts, instead of calling the backend directly.

## [2.2.0-beta.1] - 2026-10-27

### Added

- Unified light/dark modal design tokens (`office-modals.css`) covering overlay, title bar, tabs,
  surfaces, text, borders, inputs, buttons, cards, tables, code panels and scrollbars.
- Office-style Properties hub in Backstage with shared screens for Studio, Workspace and Project,
  scope navigation, editable sections, origin badges and shortcuts to the existing specialist editors.
- Settings foundation for the Properties screens: Studio, Workspace and Project stores,
  `SettingsResolver` with Studio → Workspace → Project → Vertex precedence, secure
  `studio.config.json`/`workspace.config.json` read-write endpoints with local fallback, and
  origin badges on resolved values. No secret value is persisted in either configuration file.
- Studio Properties editing (stage 3): language, canvas background and the Studio/Assistant default
  model can now be changed with dirty-state tracking, Cancel/Save and immediate persistence. The
  default model is written to the same `ARESTRIX_DEFAULT_*` environment variables used by the native
  backend. Credentials and the AI Gateway are editable with write-only key fields.
- Project Properties editing (start of stage 5): project name, version, description and output folder
  can now be edited with the shared draft/save flow. Saving updates the manifest, the graph metadata
  and the active-project indicator.
- Studio credentials and AI Gateway editing: provider keys are write-only (status only, never displayed),
  the AI Gateway URL/virtual key/fail-closed flag can be updated, and corporate locked policies remain
  read-only. Secrets continue to live only in the OS environment/vault, never in JSON or localStorage.
- Project defaults and environment editing: provider/model, sandbox limits, allowed tools and memory
  flags can be overridden in the project; `.env` entries are sanitized so only `${VAR}` references are
  persisted and literal secrets are never shown or written to `arestrix.project.json`.
- Workspace sandbox and audit editing: isolation, network, memory and timeout defaults plus
  auto-verification and strict mode are editable. `workspace.config.json` is now read/written inside
  `.workspace/`, matching the manifest produced by `WorkspaceManager`. Rebuilding `.workspace` preserves
  edited properties, and “Restaurar padrões” resets only sandbox and audit settings.
- Legacy settings entries now deep-link to the new Properties screens: Backstage Options language and
  governance, Ribbon pipeline/tools defaults and the Assistant defaults alias open the owning scope and
  category instead of the old monolithic modal.
- Vertex Sandbox, Model, Code Mode/Tools and Memory dialogs now extend the shared `BaseModal` Office
  shell, with the same title bar, resizable window and standard footer while keeping explicit
  inheritance and per-vertex overrides.
- Studio Advanced now documents the Windows/Linux `studio.config.json` paths and can restore visual
  preferences without touching language, model or credentials. Fixed explicit light-canvas persistence
  when the previous fallback preference was dark.
- Project General now edits the SQLite database path alongside name, version, description and output
  folder. The Project Manager settings tab is now a read-only summary that deep-links to Project
  Properties, removing the duplicated editor.
- Global theme preference (Light / Dark / Follow system) is available in Studio Properties. The dark
  theme now covers the ribbon, task pane, console, status bar, Backstage and Office modals, while the
  canvas background remains an independent preference.

### Removed

- Database backup/dump subsystem: `DatabaseBackupService`, `backupMode` (DDL + data / schema-only),
  the shutdown SQL dump (`storage/database.sql`) and the memory snapshot (`storage/memory.dump.json`)
  export/restore cycle. Arestrix no longer exports, versions or restores database backups; that
  responsibility is external to the product. Local integrity checks (corrupt-database quarantine) and
  telemetry retention moved to `MemoryDatabaseGuard`, and the safe `.gitignore` now also blocks
  `*.sql`, `*.dump.json` and `*.db.corrupt-*`.

### Changed

- All Studio dialogs now share the Office 365 modal shell: neutral title bar, elevated surface,
  10px radius, 34px tabs, 32px form controls, right-aligned 58px footer and compact Fluent buttons.
- Modal title bars now use the Arestrix SVG mark and SVG window controls instead of emoji/ad-hoc
  glyphs.
- Legacy inline modal colors are remapped to the shared tokens in dark mode, including MCP cards,
  template cards, Graphviz viewport, importers and runner surfaces.

### Fixed

- Graphviz export dialog recenters after being shown, using its real percentage-based dimensions.
- Project Manager recent-projects tab now resolves the existing `project_manager_modal.tab_recents`
  translation instead of displaying the raw i18n key.
- Explicit light-canvas persistence no longer falls back to a previous dark preference.

### Known limitations

- The dark theme was validated at 1600×1000 and 1024×768 on the main shell and Properties screens;
  secondary legacy editors may still need contrast polish.
- Clean-machine install/upgrade/uninstall, Linux packaging, signed remote catalog and the full
  acceptance matrix remain pending.

## [2.1.0-beta.1] - 2026-10-26

### Added

- SignPath Foundation code-signing integration: artifact configurations for the four Windows
  executables and the Inno Setup installer, optional signing steps in the deploy workflow (skipped
  until `SIGNPATH_ENABLED=true`), signature verification, signed-artifact reinstall and post-signing
  hash refresh. The certificate is issued to SignPath Foundation, not to the maintainer; the policy is
  in `CODE_SIGNING.md` and the account setup in `signpath/README.md`. The generated uninstaller
  remains unsigned until synchronous Inno signing is available.
- Windows PE version metadata (`ProductName`, `ProductVersion`, `FileVersion`, `CompanyName`) for all
  shipped executables, required by SignPath metadata restrictions.
- Windows Inno Setup installer (5B2, work in progress): `scripts/package_release.py --installer`
  builds a per-user setup from the validated staging tree and runs an environment preflight:
  WebView2/VC++ are hard requirements; Git, Git identity, GitHub credential, MSVC Build Tools and
  provider API keys are reported without exposing values, and missing optional items ask for
  confirmation (silent installs log the report). The setup creates Start menu/desktop shortcuts
  (desktop checked by default and optional), registers an `App Paths` entry and is published
  alongside the portable ZIP. Upgrades reuse the same `AppId` and previous install
  directory/tasks, replacing only application files so user projects are preserved. The deploy
  workflow installs Inno Setup and uploads the setup plus its SHA-256 sidecar. Clean-machine
  install/upgrade/uninstall, silent install and Intune/SCCM validation remain pending for a future
  validation gate.
- Canvas background preference: the floating canvas HUD can switch the graph canvas between the
  default light background and a pure black background with a slate grid. The choice is persisted
  in `localStorage` (`arestrix_canvas_background`) and reapplied on startup without modifying the
  graph or project files.

### Changed

- Running a pipeline no longer expands the console automatically. A collapsed terminal stays
  collapsed for the whole run; the user opens it explicitly when needed.
- The floating graph visualization controls (layout, follow, clear, fit, zoom and grid) are hidden
  while a pipeline is compiling or executing and return when it succeeds or fails.
- Ribbon visual refresh (stages 1–2): the title bar, tab strip and every ribbon tab (Home, Insert,
  Agents, Connectors, Data, Run and contextual tabs) now use the Office 365 visual language
  (neutral chrome, elevated white panel, 34px tabs, Segoe UI typography, larger command buttons,
  compact labels and a unified vector SVG icon set). Emoji command icons were removed from the
  ribbon; the remaining status glyphs are limited to autosave/Git state and validation feedback.

### Known limitations

- `2.1.0-beta.1` is a development pre-release. The Windows installer and SignPath integration are
  included, but clean-machine install/upgrade/uninstall, silent install and Intune/SCCM validation
  remain pending. Linux packages, the signed remote catalog and the full acceptance matrix in
  [docs/quality/beta2-test-plan.md](docs/quality/beta2-test-plan.md) also remain pending.
- Tagged releases are signed when `SIGNPATH_ENABLED=true` and the SignPath variables/secrets are
  configured; otherwise the workflow publishes unsigned artifacts.

## [2.0.0-beta.1] - 2026-10-26

Beta 2 focuses on a single SVG graph layer, a cleaner post-run editing experience and console
usability.

### Changed

- The graph now has a single visual layer. `NodeView` and `ConnectionView` keep logic, geometry and
  state only; the duplicate Three.js vertex/edge meshes, raycasts, text SDF factories and the
  `troika-three-text` dependency were removed. WebGL remains only for the grid and rubber-band.
- Finishing a run returns every vertex to edit mode (opaque, selectable, draggable). The result is
  preserved as a compact header badge (`✓ duration`, `✕ duration` or `—`), with status, duration and
  time in its tooltip. Starting a new run or the canvas broom clears the badges.
- The console collapse button now uses the same `ᐱ`/`ᐯ` glyph as the ribbon, including when the panel
  is collapsed by the splitter or reopened automatically.
- Terminal and telemetry lines are selectable and copyable again; the global `user-select: none`
  rule no longer blocks marking text in `.console-line`.
- The virtual `webui.js` runtime script is injected after Vite HTML processing, removing the
  `can't be bundled without type="module"` build warning while preserving the tag in the final HTML.

### Fixed

- Late telemetry can no longer re-enable execution visuals after a pipeline finishes; vertices stay
  in edit mode and the trace overlay only reapplies results.
- Memory snapshots now resolve `.arestrix/memory/memory.db` from the project root, never from the
  process working directory. Export refuses to create an empty database and preserves an existing
  snapshot; startup quarantines a corrupt memory database after `PRAGMA quick_check` before
  restoring it.
- `memory_search` now enforces the vertex `domains` and the requested result limit. Generic facts
  without an explicit domain are hidden from restricted agents, and expired temporal edges are no
  longer returned.
- Consulted memories are recorded in `memory_hit` even when they were discovered by `memory_search`
  and were not injected in the brief. Hit summaries now expose `consulted` and `usedInjected`.
- `contextBudgetTokens = 0` now disables the memory brief instead of meaning "unlimited budget".
- Memory writes are sanitized before persistence: lessons, episodes, edges and the Blackboard
  redact known secrets (OpenAI/Stripe, Google, AWS, GitHub, Slack, Bearer, URL parameters and PEM
  private keys). Snapshots are sanitized before Git export, including legacy rows.
- Removed the dead `generateBatchSource` path and the legacy `AgentBlackboard`/
  `SemanticMemoryEngine` usage from active generation. The classes remain only for old-data
  compatibility and are marked as legacy. Documentation now reflects the real memory behavior.
- The contextual ribbon now edits only fields the C++ generator consumes. Inherited agent
  temperature/timeout are materialized when overridden, Switch cases are inserted before the
  `default` and participate in undo/autosave, critical sections generate one `tf::Semaphore` per
  `criticalSectionId` (with `parameters.maxConcurrency`), Subflow navigation uses the real graph
  stack, MCP testing executes the configured tool, and ribbon dialogs replace `prompt`/`confirm`.
  The ribbon body grows with its content and respects the window width.
- SVG vertices now capture pointerdown on the whole visual surface. Text titles, the type label, the
  header icon and the status pills were painted above the body and swallowed the click, so selecting
  a vertex sometimes required two or three attempts; ports and action-bar buttons keep their own
  interactions. Selection events also carry the node data consistently, and quick-action buttons now
  route selection through the canvas so the selection ring and Delete/Backspace stay in sync.
- Double-clicking a Subflow/Module on the SVG layer now dives into it (the SVG overlay previously
  swallowed the canvas `dblclick`), and entering/leaving a hierarchy level fits the new level in the
  viewport. A vertex drag now starts only after a 4px threshold, so a click with hand jitter no
  longer moves the vertex or creates spurious undo entries, and the drag is finalized when the
  pointer is released over an input port or the `pointerup` is lost. Wheel zoom again anchors on the
  real cursor position when the canvas is offset by the ribbon/breadcrumbs, and interaction/camera
  listeners are removed on disposal. Vertex titles now wrap into up to two lines instead of
  overflowing the card (the shared text helper never inserted line breaks), and a truncated title
  exposes the full label through a native SVG tooltip.
- `.workspace/` directories are now ignored by Git and excluded from portable packages. They hold
  workspace assets with machine-specific absolute paths (for example
  `C:/Projetos/.../.workspace/workspace.config.json`) and must never be published in a release.
- Project auto-commit no longer depends on a machine-wide Git identity. When `user.name`/`user.email`
  are absent (clean machines and CI runners), the commit uses a one-shot
  `Arestrix <arestrix@localhost>` fallback via `git -c ... commit` instead of failing with
  "Author identity unknown". No local config is persisted, so a global identity configured later
  still applies; the native test no longer requires a pre-configured OS identity.
- `DefaultIntelligenceService` now honors an injected API caller even when the provider key is not
  set. Tests and embedders no longer silently fall back to the static 80/85 heuristic on clean
  environments; the HTTP path still requires a real key.
- Cross-platform CI hardening: skill-reference SHA-256 hashes now normalize CRLF before hashing
  (Windows CRLF checkouts and Linux LF checkouts agree; the policy lock was regenerated), Vitest
  forces the canonical `pt-BR` locale instead of relying on `navigator.language`, and the native test
  translation unit includes `<cstring>` so `std::memset` compiles with GCC.
- The interaction host stdin reader is now intentionally kept for the process lifetime. Destroying
  its mutex/condition variable at shutdown while the detached reader thread was still blocked made
  successful host interactions exit non-zero on POSIX (the Linux CTest job exposed it).

### Added

- Automated Windows x64 portable deploy: `.github/workflows/deploy.yml` runs the release gate,
  builds the portable ZIP with `package_release.py` and publishes it to GitHub Releases. Pushes to
  `main` update the rolling `latest` pre-release; `v*` tags publish/update their own release. The
  release carries the ZIP, `.zip.sha256`, `SHA256SUMS`, `SBOM.json`, `release-manifest.json` and
  `release-report.json`, so testers can run the Studio from any folder without a development
  checkout. Both READMEs now point to the `latest` build.
- Pull-request CI: `.github/workflows/ci.yml` runs quality/static checks, Vitest + Studio build,
  native Windows CTest and native Linux CTest. The four jobs are required status checks on `main`;
  the repository owner keeps an `always` bypass for urgent direct pushes. Dependabot is enabled for
  npm, GitHub Actions and Git submodules, and both workflows now use Node 24 with `checkout@v7`,
  `setup-node@v7`, `setup-python@v7` and `action-gh-release@v3`.
- Arestrix Skill Guard: the `security-audit-skill` (Cloudflare) submodule, a project skill at
  `.agents/skills/arestrix-skill-guard/`, and a second deterministic audit layer for supply chain,
  persistence, credential access, tool abuse, data isolation and resource exhaustion. Skill
  approvals now carry a `policyVersion`; changing the NVIDIA/Cloudflare references or the local rule
  set invalidates old ledger entries and requires re-audit (existing custom skills appear as
  `PENDING` after this upgrade). `npm run check:skill-references` pins
  commits, trees and `SKILL.md` hashes; the review runbook is in
  `docs/quality/skill-reference-review.md`.
- Institution memory service: the new `arestrix-memory` executable exposes the
  `MemoryInstitutionService` contract over authenticated HTTP (`/v1/status`, `/v1/context`,
  `/v1/lessons`, `/v1/policies`) and MCP (`tools/list`, `tools/call`, stdio and `/mcp`). Tokens come
  from `--tokens-file`, `--tokens-json` or `ARESTRIX_INSTITUTION_TOKENS`, stay in memory only and
  fail closed when absent. `scripts/e2e_memory_institution.py` covers roles, HTTP and MCP.
- Per-vertex memory write governance: `writeKeys`, `maxLessonsPerRun` (default 20),
  `maxLessonChars` (default 4000) and a global 5000-active-lesson cap. The limits are
  configurable both on the vertex and in **Global Defaults → Memory**.
- Project memory purge and retention: `retentionDays` (default 180; 0 = keep) in
  **Global Defaults → Memory**, shutdown pruning, `arestrix-cli memory prune|purge`,
  `secure_delete=ON`, full purge of hits/evidence/audit and `VACUUM`.
- Memory snapshot v2 exports the full cognitive graph (scopes, closure, nodes, temporal edges
  and working rows), imports transactionally, preserves IDs/timestamps/confidence/status and
  rebuilds FTS5. Snapshot v1 remains importable and is also restored in a transaction.
- Generated pipelines resolve the memory database from `ARESTRIX_PROJECT_ROOT` (honoring
  `ARESTRIX_MEMORY_DB` first), use `ARESTRIX_RUN_ID` for hits/usage, retry short SQLite
  `SQLITE_BUSY`/`SQLITE_LOCKED` races, and enforce a unique key on `memory_hit`.
- L2 schema foundation: `memory_proposal`, `memory_embedding`, `memory_playbook` and
  `memory_alias`; `memory_episode` now stores task type, outcome, tool sequence, files,
  artifacts, evidence, success signals and redaction count.
- Deterministic L2 extractor turns successful/failed episodes into pending proposals (lesson,
  fact, entity, playbook, summary). Promotion is manual with evidence checks and audit, exposed
  in the Studio Memory Center and via `arestrix-cli memory proposals list|accept|reject`.
- Optional hybrid retrieval: OpenAI-compatible `/v1/embeddings` provider (default
  `embeddinggemma-2`, 768d, asymmetric search/document prefixes, off), SQLite BLOB vector
  index, incremental reindex and reciprocal-rank fusion of lexical, graph and vector
  candidates. Lexical fallback is always preserved; `memory embeddings status|test` and
  `memory reindex` are available in the CLI, with configuration/test/reindex in the Studio.
- Deterministic memory consolidation: alias-based entity resolution, duplicate lesson/fact
  merge with evidence preservation, `SUPERSEDES`/`CONTRADICTS`/`GENERALIZES`, fact expiration,
  decay into `dormant_at`, and hit-based reinforcement. Conflicts and decay are visible in the
  Studio; `memory consolidate` runs the same pipeline from the CLI.
- Opt-in background consolidation per project (`consolidationMode=off|idle|after_episodes`,
  idle/episode thresholds, dormant days), only for persistent memory. The worker runs outside the
  recall path, exposes runs/last run/next trigger/error in the Studio, and stops deterministically
  when the project or process closes.
- Authenticated `GET /metrics` on `arestrix-memory`, with Prometheus counters for REST/MCP requests,
  MCP notifications, 401 failures, response classes, uptime and version. `/metrics` requires a
  `viewer` bearer token and adds no high-cardinality labels.
- Procedural playbooks: successful trajectories compile by `task_type`, become `candidate` on
  the first evidence and `active` on the second, track success/failure counts and evidence,
  are recovered into the memory brief within budget, appear in the Studio and persist in
  snapshot v2. Failures never create a new playbook.
- Workspace memory: separate `.workspace/memory/memory.db`, project-reads-workspace
  inheritance, explicit evidence-backed promotion of lessons/facts/playbooks with audit, CLI
  and Studio flows, and sibling isolation. Institution contract adds in-memory tokens,
  viewer/editor/admin roles and immutable ancestor policies; `arestrix-memory` exposes it over
  authenticated HTTP, `/mcp` and MCP stdio.
- **Strictly Optional Git Synchronization (100% Local by Default):** Git/GitHub synchronization is now opt-in (`enabled: false` by default for new projects). If disabled, all project files, SQLite databases (`storage/database.sqlite`), and cognitive memory (`.arestrix/memory/memory.db`) reside exclusively on local disk with zero Git operations, zero network overhead, and zero dump exports.
- **Configurable Backup Mode (Privacy & LGPD/GDPR Compliance):** Projects support two backup modes: "DDL + Data" (full tabular export) and "DDL Only / Schema Only" (table structure without rows), preventing sensitive data and PII from reaching Git.
- **Deterministic Plaintext Dumps & Ghost Commit Elimination:** Volatile timestamp headers were removed from SQL and JSON memory dumps. Files are only updated on disk if their actual contents have changed, preventing packfile bloat and delta churn.
- **95 MB Dump Ceiling Protection (GitHub GH001 Hard Limit Safeguard):** `DatabaseBackupService` enforces a strict 95 MB size check to avoid pushing files exceeding GitHub's 100 MB hard limit.
- **Native Pre-Commit Secret Scanning:** `GitSyncService` scans staged diffs for common API keys (`sk-...`, `AIza...`, `AKIA...`, `ghp_...`) and blocks commits with a clear error if exposed secrets are detected.
- **Expanded `.gitignore` Rules:** Automatic exclusions for local AI model weights (`*.gguf`, `*.safetensors`, `*.bin`, `*.onnx`, `models/`), columnar datasets (`*.parquet`, `*.arrow`, `*.feather`, `*.h5`), media, and runtime caches.
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
- **Flow Call vertex (phase 4C):** the new `call` vertex executes a declared callable target inside
  the Taskflow flow. It has exactly one outgoing call edge, gathers a fixed/previous/file/none input,
  invokes the closed-world runtime synchronously, writes `.arestrix/out/<call-id>.txt` and hands the
  answer to the next vertex. It does not expose `call_agent` to a model.
- `examples/11-call-vertex` and the E2E cover a preparer, the Call vertex and the called specialist,
  including the input crossing the flow boundary and the exported answer.
- **Call notifications (phase 4D):** completed/failed calls publish a non-blocking notification to
  `.arestrix/runs/<runId>/notifications.jsonl` and to the `notification` telemetry event; the Studio
  shows it as a toast. Notifications carry IDs/status/metadata, never input or output, and can be
  disabled per call edge or with `ARESTRIX_NOTIFICATIONS=0`. Calls remain synchronous in v1.
- **Asynchronous calls (phase 4E):** when a call edge allows async, `call_agent` accepts
  `async: true`, starts the target in the background and returns a pending `callId`; the agent polls
  with `call_result`, which returns `pending` or the final JSON. Async is opt-in per edge, can be
  disabled with `ARESTRIX_ASYNC_CALLS=0`, and pending calls are drained before process exit.
- `examples/12-chamadas-assincronas` and the E2E cover a coordinator starting, polling and delivering
  an async specialist result.
- **Open trace (phase 4F):** every run writes `.arestrix/runs/<runId>/trace.json` with tasks, calls
  (including parent/child call IDs), interactions, status and timing, never input/output. The Studio
  loads the latest trace after the run, highlights traversed dashed call edges and prints a compact
  call tree in the console. The E2E asserts the trace records the async specialist call.
- **Release gate (phase 5A):** `scripts/release_check.py` runs the canonical `dev.py --full --e2e`
  gate from a clean tree and generates `release/release-manifest.json`, `SHA256SUMS`, `SBOM.json` and
  `release-report.json`. `release_manifest.py` hashes the actual binaries/dist/runtime files and
  inventories native submodules and npm components. The Windows signing script now includes
  `arestrix-gateway.exe`.
- **Portable package (phase 5B1):** `scripts/package_release.py` stages the Studio binaries, UI,
  CppCore sources, runtime library, Taskflow/simdjson headers, examples and documentation in the
  layout expected by `locateRuntime`/`BuildService`. It runs a CLI smoke test, writes
  `package-manifest.json` and package-local `SHA256SUMS`, copies the SBOM and creates a ZIP or
  `.tar.gz`. MSI/MSIX installers remain for 5B2.
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

[2.3.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v2.3.0-beta.1
[2.2.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v2.2.0-beta.1
[2.1.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v2.1.0-beta.1
[2.0.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v2.0.0-beta.1
[1.5.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v1.5.0-beta.1
[1.0.0-beta.1]: https://github.com/frankalcantara/Arestrix/releases/tag/v1.0.0-beta.1
