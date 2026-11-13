# Arestrix project pending items

> Memory-specific pending work, including context compaction backed by existing
> memory, is tracked in [Memory pending items](memory-pending.md).

> **Version:** 2.9.0 · **Updated:** 2026-11-13
>
> Single project-wide pending list. The memory implementation plan (Phases A-C) and the advanced
> multigraph (Phase D / E11-E20) are complete. The remaining items are operational, performance and release tasks.
>
> Consolidated results: [Memory test results](memory-test-results.md).

## 1. Summary

| Area | State |
| :--- | :--- |
| Phases A-C of the memory plan | **Complete** |
| Modes, proposals, telemetry, validation, brief, state, embeddings and A/B | **Complete** |
| Phase D - advanced multigraph | **Implemented; opt-in switches** |
| A/B v2 execution harness | **Executed with a real provider on 2026-11-11 (parity/cost and value)** |
| Memory policy gate (`memory.gate`) | **Implemented; 3-repetition A/B complete** |
| Project memory scenarios (Consult / Consult and Decide / Learn) | **Implemented; memory on by default** |
| Experimental switches (`pathServing`, `traversal`, `bridges`, `routing.mode`) | **Mechanisms measured on a curated corpus; defaults stay `off`** |
| `institution` level in the assistant `memory_inspect` | Operational pending |
| Validation with real embedding providers | Operational pending |
| Studio UI to configure the memory agent | **Implemented and validated in the Studio** |
| Performance profiling (Tracy) and automatic correction loop for generated code via the Studio default model | Optional build ready; loop pending |
| Instrumentation of Arestrix itself with Tracy and self-optimization loop | Optional build ready; instrumentation pending |
| Full, detailed performance testing and optimization cycle (C++, TypeScript, etc.) on every release | Release pending |
| Clean-machine release validation | Release pending |

## 2. Completed memory work

- Per-vertex `off`, `consult` and `learn` modes, with project defaults.
- Lessons and playbooks as proposals/candidates; nothing activates on its own in Phase A.
- Telemetry separated from knowledge.
- Project-declared continuation state with `project`, `task_type` and `item` scopes, JSON schema,
  TTL, CAS, `replace`/`versioned` and history.
- Memory validation, `task_type`, canonical hash, `[MEMO:<id>]`, injected vs. used and utility
  separated from confidence.
- Enumerated `errorClass`; infrastructure failures do not become lesson proposals.
- Playbook gate with 3 distinct inputs, stable validator, 24h limit, unpromote and auto-demote.
- Hybrid search with full fingerprint, `ready/stale/pending/partial/failed` and lexical fallback.
- Brief guided by `task_type`, theme and prompt template; SQL theme filtering and invalidatable cache.
- Prompt/cache tokens in `memory_usage`.
- Single-valued and multi-valued fact predicates.
- A/B v2 with the versioned 12/4/4 suite, shuffled order, repetitions, paired deltas and 95% CI.
- Studio assistant tools: `memory_inspect`, `memory_search`, `memory_review_proposal`,
  `memory_declare_state_key`, `memory_unpromote_playbook` and `memory_retire_path`.
- CLI for memory, state, embeddings, proposals, workspace, paths, bridges, unpromote and A/B.

## 3. Phase D - advanced multigraph implemented

Phase D was implemented in E11-E20 with opt-in switches and reproducible measurement:

1. **Sources and claims** — versioned local ingestion, validated extraction and claim proposals.
2. **Lenses and facets** — deterministic per-entity/lens facets and search filtering.
3. **Paths and schemas** — idempotent consolidation of approved traces, Beta utility, invalidation
   and reconstructible shortcuts.
4. **Serving and traversal** — eligible paths before graph retrieval, Thompson exploration, bounded
   beam traversal and a coverage vector.
5. **Routing and bridges** — local/frontier decisions by `task_type` and interdomain hypotheses.

The conservative defaults remain: `memory.pathServing`, `memory.traversal`, `memory.bridges` and
`routing.mode` stay off until real-data measurement. The final report is
`tests/fixtures/memory-eval-v1-report.json`.

### Out of scope (not pending)

- Claim extraction by a local model already exists behind `memory.claimExtraction`; LLM
  consolidation/generalization remains out of scope.
- Multi-machine synchronization and multi-worker coordination (out of scope for this release).
- Graph checkpoint/resume.
- Changes to the institutional HTTP/MCP protocol.
- Automatic migration of old projects; there is no legacy importer.

## 4. Operational pending items

### 4.1 A/B v2 execution harness

The harness is implemented in `memory ab run` and covered by `scripts/e2e_memory_ab.py`: 20 tasks x
3 repetitions x 2 arms, frozen knowledge, validator and an `observations-<timestamp>.json` file with
`runId`, `episodeId`, `arm`, `ok`, tokens, turns and latency. Monetary cost is not computed.

It was executed against a real provider on **2026-11-11** by
`scripts/e2e_memory_ab_live.py`: DeepSeek (`deepseek-chat`), 120 executions and frozen knowledge.
The report is `tests/fixtures/memory-ab-live-report.json`; raw observations are in
`tests/fixtures/memory-ab-live/`.

| Metric | OFF | ON |
| :--- | ---: | ---: |
| Approval | 60/60 (1.000) | 60/60 (1.000) |
| Prompt tokens | 99,428 | 298,588 |
| Cached prompt tokens | 38,400 | 204,288 |
| Completion tokens | 46,792 | 78,790 |
| Total tokens | 146,220 | 377,378 |
| Mean turns | 2.00 | 3.52 |
| Mean latency | 6.46 s | 10.95 s |

Frozen knowledge stayed unchanged and there were no infrastructure failures or invalid tasks. The
approval delta was 0 (95% CI [0,0]) because the pilot validator does not depend on memory. The
higher ON cost comes from `memory_search` calls the agent makes on its own — 20/20 traces in the
20-run pilot, 1.8 searches per trace on average — not from the brief: the injected context averages
~451 characters.

The memory-dependent variant, executed on 2026-11-11 with `--memory-dependent`,
requires the `AB-7731` token, which exists only in a curated lesson. It shows the
value signal: approval 60/60 (1.000) in ON versus 0/60 (0.000) in OFF, delta
1.000 (95% CI [1.000, 1.000]), with no infrastructure failure or invalid task.
ON still costs more (2.67x total tokens and 1.62x latency) because the agent
issues its own memory searches; the lesson is effectively used, but the effect is
binary by validator design. Report:
`tests/fixtures/memory-ab-live-value-report.json`.

This closes the value gap for memory delivery. The advanced switches have their
own mechanism measurement in section 4.1.2; the value A/B still measures lesson
delivery, not the quality of advanced retrieval.

### 4.1.2 Advanced switches: mechanism measurement

The corpus `tests/fixtures/memory-advanced-v1.json` and the harness
`scripts/memory_advanced_eval.py` measure the four switches without a model
call, over frozen knowledge and 10 tasks (path, expired path, three-hop
traversal chain, no-anchor query, three bridge cases and three routing cases).
Each task runs on a copy of the template, with 3 repetitions.

| Scenario | Result |
| :--- | :--- |
| `pathServing` | path task: recall 1.000 and precision 1.000; baseline 0.000 |
| `traversal` | three-hop chain: recall 1.000 and precision 0.500; baseline 0.000 |
| `bridges` | 1 correct hypothesis (`Guia`, lenses `contraste`/`tipografia`), 0 edges created; same-lens and incompatible-access cases were not proposed |
| `routing.mode=shadow` | decision recorded, executed tier stays `current` |
| `routing.mode=binary` | `local_large` at high confidence, `frontier` at low confidence and `frontier` without history |
| `all_on` | every positive task passed and no forbidden item was injected |

The measurement found and fixed two defects: traversal did not resolve anchors
from natural-language prompts (it now tokenizes the query when the whole phrase
is not an alias) and `shadow` switched the executed tier when there was no
history (it now keeps the current tier and only records the suggestion).

Decision: the mechanisms are validated, but defaults stay `off`. Promotion still
requires repeating the corpus on real projects and a quality/cost A/B with the
chosen model. Reproducible report: `docs/quality/memory-advanced-ab.md`.

### 4.1.1 Memory policy gate (`memory.gate`)

Implemented with `off`, `shadow` and `enforce` modes, a deterministic decision
and an optional model (`provider`/`model`; Jev, local or API). The gate chooses
`none`, `brief`, `search` or `learn`, writes a `memory_gate` trace step and has a
validation floor: if the validator requires a value present in the brief, `none`
is promoted to `brief`. The historical 1-repetition pilot (2026-11-11) remains
as a reference; the 3-repetition confirmation completed on 2026-11-12 with Jev.

| Scenario | ON approval | ON/OFF tokens | ON/OFF latency |
| :--- | ---: | ---: | ---: |
| Baseline, parity | 1.000 | 2.69x | 1.60x |
| Deterministic gate, parity | 1.000 | 1.79x | 1.25x |
| Model gate, parity | 1.000 | 1.34x | 1.16x |
| Model gate, value | 1.000 | 1.47x | 1.19x |
| Jev gate, parity | 1.000 | 1.39x | 1.01x |
| Jev gate, value | 1.000 | 1.51x | 1.03x |

The DeepSeek gate figures include the gate model's tokens and latency (~9
thousand tokens and ~1.3 s per execution); the agent alone would be 1.17x/0.96x
in parity and 1.28x/0.99x in value. Jev uses ~14 thousand tokens and ~270 ms per
execution.

In the historical pilot, the DeepSeek gate chose `brief` 17 times, `none` twice
and `search` once in parity, and `brief` 19 times + `search` once in value; Jev
(`jev-latest`, TypeSafe `/v1/systemone`) chose 13 `brief`, 6 `none`, 1 `search`
in parity and 19 `brief`, 1 `search` in value.

**Confirmation (3 repetitions, 2026-11-12)**

20 tasks × 2 arms × 3 repetitions = 120 executions per suite; 0 infrastructure
failures, 0 invalid tasks and unchanged frozen knowledge.

| Scenario | ON approval | ON/OFF tokens | ON/OFF latency | ON total | OFF total |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, parity | 1.000 | 1.48x | 1.08x | 215,381 (173,453 agent + 41,928 gate) | 145,623 |
| Jev, value | **1.000 vs 0.000** | 1.58x | 1.09x | 238,059 (193,311 agent + 44,748 gate) | 151,129 |

Decisions: parity 39 `brief`, 18 `none` and 3 `search`; value 57 `brief` and 3
`search`. Average gate latency: 271.6 ms in parity and 420.3 ms in value. The
parity approval delta was 0 (95% CI [0, 0]); the value delta was 1.000
(95% CI [1.000, 1.000]). The confirmation closes the pilot gap; gate defaults
remain opt-in and production promotion still requires the quality/cost A/B with
the chosen model. Details in `docs/quality/memory-gate-ab.md`.

### 4.2 Institutional level in the assistant

`memory_inspect` covers `project`, `workspace`, `vertex` and `all`. The `institution` level depends on
a `MemoryService` status for the institutional service and explicit MCP/token configuration.

### 4.3 Real embedding providers

Profiles and fingerprints are implemented, but quality and latency validation with real
EmbeddingGemma 2 and Qwen3-Embedding-4B still needs to be recorded. Lexical fallback is the safe
behavior when no provider is available.

### 4.4 Studio UI for the memory agent

The `memory.claimExtraction.agent` target is now registered from the active
pipeline's agent nodes in `AssistantSession`, exposed through the
`memory_claim_agents` list and editable in File -> Properties -> Project -> Memory,
with an explicit default-model fallback and a warning when the saved target no
longer exists in the current pipeline. The `memory claims extract` CLI also
registers the project pipeline before running.

Manual validation in the Studio is complete: the screen lists the pipeline's
three agents, enables the selector when extraction is on, saves
`{ id, graph, node }` and restores the selection after reopening the project.
The target comparison was also fixed so it no longer depends on JSON key order.

Automated validation covers registration/listing and an extraction e2e with a
fake HTTP provider (`scripts/e2e_memory_claim_agent.py`): the selected agent
receives the passage, catalog and schema, and the claim proposal is stored.

### 4.5 Performance observation and automatic correction loop for generated code (Tracy + Studio default model)

> Detailed plan: [`../quality/tracy-profiling-plan.md`](../quality/tracy-profiling-plan.md).

Use the **Tracy Profiler** (`third_party/tracy`) in a closed performance-observation and
self-correction loop for pipelines generated by Arestrix:

- **Automatic instrumentation of generated code**: emit Tracy zones and markers (`ZoneScoped`,
  `FrameMark`, memory counters, locks and Taskflow task events) in the C++20 code generated by
  `TaskflowGenerator`.
- **Profile capture and analysis**: run the generated binary with automated Tracy trace capture
  (headless/CLI capture and statistical export of hot zones, per-vertex latency, allocations and thread
  contention).
- **Automatic correction with the Studio default model**: feed the structured Tracy bottleneck report
  and the corresponding generated code into the **Studio's configured default model**, running an
  iterative diagnosis → refactoring/optimization → recompilation → re-profiling and functional
  validation loop until performance targets are met without semantic regression.

### 4.6 Instrumentation of Arestrix itself with Tracy and self-optimization loop

Include the **Tracy Profiler** directly in Arestrix's own codebase (`CppCore` and the `arestrix`,
`arestrix-cli`, `arestrix-gateway` and `arestrix-memory` binaries) and run a similar continuous
observation and optimization loop:

- **Native instrumentation**: cover the Taskflow compiler/generator, agent runtime, SQLite memory
  engine (lexical/hybrid search, consolidation, beam traversal, ingestion), AI Gateway, MCP
  client/server, IPC/WebUI bridge and heavy Studio operations.
- **Self-observation and optimization loop**: capture traces across benchmark and test suites,
  identify latency/memory hotspots and regressions via Tracy, and use the default-model-assisted loop
  to propose, validate and apply optimizations to Arestrix's own code.

## 5. Release pending items

- **Complete, detailed performance testing and optimization cycle on every release**:
  - Integrate a mandatory profiling, detailed benchmarking and performance optimization cycle into the
    release gate (`scripts/release_check.py`) before closing each release.
  - Thoroughly test **all operations across C++, TypeScript and integrations**:
    - **C++ (`CppCore`)**: Taskflow code generation and compilation, graph and subflow execution,
      SQLite database and memory multigraph operations (FTS/vector queries, consolidation, extraction,
      snapshots, traversal and bridges), `arestrix-gateway` routing and budgets, `arestrix-memory`
      REST/MCP endpoints, sandboxes and JSON serialization (`simdjson`).
    - **TypeScript / Studio (`src/`)**: in-memory graph engine operations (`Graph`, `Validator`),
      command history (`undo`/`redo`), project serialization/deserialization, WebGL/Three.js canvas and
      SVG layer rendering and interactions (`NodeView`, `ConnectionView`), property scope resolution
      (`Studio`/`Workspace`/`Project`), skill/MCP auditing and hashing, and UI responsiveness.
  - Compare metrics against the previous release baseline, block regressions above the threshold and
    run the Tracy-guided optimization cycle before packaging release artifacts.
- The 2.7.0 installer was validated locally on 2026-11-11: silent install on a machine without
  Arestrix, binary smoke tests, Studio launch, upgrade 2.1.0-beta.1 -> 2.7.0 preserving a user
  project and full uninstall. A truly clean machine (only the VC++ runtime) and Intune/SCCM are
  still pending.
- Linux package validated (`linux-x64` tarball) and Linux CI in the `native-linux` job; macOS matrix pending.
- Signed remote catalog.

## 6. Phase D closed

Phase D is complete with:

1. a specification for sources, claims, lenses/facets, paths, traversal, routing and bridges;
2. an idempotent schema compatible with the current database;
3. API, CLI, panel and assistant tools (`memory_inspect`, `memory_search`, `memory_retire_path`);
4. a versioned, reproducible evaluation set in `tests/fixtures/memory-eval-v1-report.json`;
5. green unit, integration, E2E and baseline tests;
6. updated PT/EN documentation.

The new switches stay conservative and should only be enabled after real-data measurement.
