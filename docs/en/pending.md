# Arestrix project pending items

> Memory-specific pending work, including context compaction backed by existing
> memory, is tracked in [Memory pending items](memory-pending.md).

> **Version:** 2.6.0 · **Updated:** 2026-11-02
>
> Single project-wide pending list. The memory implementation plan (Phases A-C) and the advanced
> multigraph (Phase D / E11-E20) are complete. The remaining items are operational.

## 1. Summary

| Area | State |
| :--- | :--- |
| Phases A-C of the memory plan | **Complete** |
| Modes, proposals, telemetry, validation, brief, state, embeddings and A/B | **Complete** |
| Phase D - advanced multigraph | **Implemented; opt-in switches** |
| A/B v2 execution harness | **Implemented** |
| `institution` level in the assistant `memory_inspect` | Operational pending |
| Validation with real embedding providers | Operational pending |
| Studio UI to configure the memory agent | Product pending |
| Clean-machine release validation / signing | Release pending |

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
`runId`, `episodeId`, `arm`, `ok`, tokens, turns and latency. Monetary cost is not computed. The
remaining work is to run it with real providers and record the result in the report.

### 4.2 Institutional level in the assistant

`memory_inspect` covers `project`, `workspace`, `vertex` and `all`. The `institution` level depends on
a `MemoryService` status for the institutional service and explicit MCP/token configuration.

### 4.3 Real embedding providers

Profiles and fingerprints are implemented, but quality and latency validation with real
EmbeddingGemma 2 and Qwen3-Embedding-4B still needs to be recorded. Lexical fallback is the safe
behavior when no provider is available.

### 4.4 Studio UI for the memory agent

`memory.claimExtraction.agent` already accepts a Studio agent id or
`{ id, graph, node }` for a graph-backed agent. The Studio UI still needs to
choose that target per project, list registered agents/graphs, show the default
model fallback and explain the error when a configured target is unavailable.

## 5. Release pending items

- Clean-machine installer validation, upgrade, uninstall and Intune/SCCM.
- Code signing via SignPath or a corporate certificate.
- Linux package and Linux/macOS CI matrix.
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
