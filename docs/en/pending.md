# Arestrix project pending items

> **Version:** 2.5.0 · **Updated:** 2026-10-31
>
> Single project-wide pending list. The memory implementation plan (Phases A-C) is complete. Phase D
> is a separate program, outside this plan, and does not block the release. The remaining items are
> operational.

## 1. Summary

| Area | State |
| :--- | :--- |
| Phases A-C of the memory plan | **Complete** |
| Modes, proposals, telemetry, validation, brief, state, embeddings and A/B | **Complete** |
| Phase D - advanced multigraph | Separate program, outside this plan |
| A/B v2 execution harness | Operational pending |
| `institution` level in the assistant `memory_inspect` | Operational pending |
| Validation with real embedding providers | Operational pending |
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
- Prompt/cache tokens in `run_usage`.
- Single-valued and multi-valued fact predicates.
- A/B v2 with the versioned 12/4/4 suite, shuffled order, repetitions, paired deltas and 95% CI.
- Studio assistant tools: `memory_inspect`, `memory_review_proposal`, `memory_declare_state_key`
  and `memory_unpromote_playbook`.
- CLI for memory, state, embeddings, proposals, workspace, unpromote and A/B.

## 3. Phase D - separate program, outside this plan

Embedding retrieval already exists: EmbeddingGemma 2 and Qwen3-Embedding-4B profiles, fingerprint,
RRF fusion and lexical fallback. Phase D is the **advanced multigraph**, a separate program that is
not part of the simplification plan closed by this release:

1. **Facets** — semantic classification of nodes and edges for aspect-based queries.
2. **Domain knowledge** — reusable domain layers across projects/workspace.
3. **Paths over claims** — explicit navigation across claims, evidence and contradictions.
4. **Model routing** — model choice by query/task type based on the graph.
5. **Semantic evaluation** — query set, relevance metrics and comparison with the current
   lexical/hybrid baseline.

Dependencies already in place for Phase D: fingerprint, validation, continuation state and
`task_type`.

### Out of scope (not pending)

- LLM extraction or consolidation; current memory is deterministic.
- Multi-machine synchronization and multi-worker coordination.
- Graph checkpoint/resume.
- Changes to the institutional HTTP/MCP protocol.
- Automatic migration of old projects; there is no legacy importer.

## 4. Operational pending items

### 4.1 A/B v2 execution harness

The A/B v2 core is ready: suite validation, order, paired analysis and CI. The missing harness runs
the 20 tasks, applies the validator and writes `ab-observations.json` with `ab_run_id`, `arm`, `ok`,
tokens, turns, cost and latency. Without it, the report is analytical only.

### 4.2 Institutional level in the assistant

`memory_inspect` covers `project`, `workspace`, `vertex` and `all`. The `institution` level depends on
a `MemoryService` status for the institutional service and explicit MCP/token configuration.

### 4.3 Real embedding providers

Profiles and fingerprints are implemented, but quality and latency validation with real
EmbeddingGemma 2 and Qwen3-Embedding-4B still needs to be recorded. Lexical fallback is the safe
behavior when no provider is available.

## 5. Release pending items

- Clean-machine installer validation, upgrade, uninstall and Intune/SCCM.
- Code signing via SignPath or a corporate certificate.
- Linux package and Linux/macOS CI matrix.
- Signed remote catalog.

## 6. Definition of done for Phase D

Phase D is complete when there is:

1. a specification for facets, domain, claims and routing;
2. a schema and migration compatible with the current database;
3. API/UI and assistant tools to inspect and operate those levels;
4. a versioned evaluation set and reproducible results;
5. green unit, integration and E2E tests;
6. updated PT/EN documentation and this item removed from the pending list.
