# Memory pending items

This file tracks work that is specific to project memory. It is intentionally
kept in the repository so the pending contract is reviewed with the code.

**Status:** memory v1 core and real-provider validation are complete; the items
below are the next memory work. No item here blocks the current release.

| Priority | Item | State |
| :--- | :--- | :--- |
| High | Context compaction backed by existing memory | Implemented behind `memory.contextCompaction` (off by default); A/B recorded |
| High | Versioned predicate catalog and canonical claim identity | Implemented in catalog v1/schema v6; D11 applied; `warn` default and `strict` opt-in |
| High | Extract memory into the standalone Mnemosyne module and submodule | Assessment complete; migration pending |
| High | Enable experimental switches only after real-data A/B | Pending |
| Medium | Institutional memory level in the Studio assistant | Pending |
| Medium | Real embedding-provider quality/latency validation | Pending |
| Low | Multi-process maintenance coordination | Out of scope for v1 |

## 1. Context compaction backed by existing memory

**Goal.** Reduce the prompt sent on later turns of a long agent interaction
without losing answer quality, using the memory that already exists.

The current real-provider tests prove two separate things:

1. memory is retrieved and changes the answer (`ORION-42` versus `UNKNOWN`);
2. static long context can be replaced by a short prompt plus memory, reducing
   prompt tokens by 87% in the controlled test.

They do **not** prove that the conversation history itself shrinks. With the
switch off, the agent still resends prior messages each turn and memory only adds
a brief. The bridge between those facts is now implemented behind
`memory.contextCompaction`, described below.

### Design constraints

- The source of truth is the existing memory: active lessons, claims, paths,
  facets, source passages and validated traces already in the project.
- Compaction must preserve citations and claim identity. A compacted item must
  still point to the original memory id; summarized text is not new evidence.
- The compactor must never promote claims, create edges or change support state.
- The original turns must remain available for fallback within the run; the
  trace records compaction metadata without storing message payloads.
- The prompt budget is explicit. The compacted turn must fit the configured
  budget; otherwise the runtime keeps the uncompacted turn.
- Secrets and untrusted text pass through the existing sanitizer before any
  summary is stored or sent.
- The behavior is off by default until the A/B demonstrates quality and cost.

### Candidate pipeline

1. At the end of a turn, identify finished messages that are outside the recent
   window.
2. Resolve their durable facts and rules against existing memory, preferring
   active claims and cited passages.
3. Build a compact block with stable `[MEMO:<id>]` references and a summary
   marked as derived, not as source evidence.
4. Replace only the finished turns; keep the system prefix, the current task and
   the last N turns untouched.
5. Record original token count, compacted token count, memory ids used and a
   fallback reason in the trace.
6. If compaction fails, send the original context unchanged.

### Acceptance criteria

- On the long-interaction fixture, later prompts are smaller than the
  uncompacted run by a documented threshold.
- The answer remains correct and cites the same memory ids.
- `knowledge_version` and claim/path support state do not change during
  compaction.
- A failed compactor leaves the original prompt intact.
- The A/B report records quality, prompt tokens, completion tokens, latency and
  the number of turns compacted.

### Implemented

`rt::ContextCompactor` implements the deterministic path. It is constructed per
agent run with an injected durable-memory provider and never writes knowledge.
It runs at the start of a turn, after the previous tool result finished, and
only changes the request copy: the full conversation remains in the agent history
for fallback. The existing trace records compaction metadata (counts, ids,
before/after sizes and fallback reason) but, by design, no message payloads.

- The switch is `memory.contextCompaction` (`false` by default; boolean or
  `{enabled, keepRecentMessages, minChars, maxSummaryChars, maxSummaryTokens,
  maxItems}`). `ARESTRIX_MEMORY_CONTEXT_COMPACTION=0|1` exists for A/B only.
- The first user prompt (current task), the system prompt and the last
  `keepRecentMessages` messages are never compacted. The cut never separates an
  assistant `tool_calls` message from its tool results.
- Durable items come from active lessons, active claim edges, active paths,
  active facets, passages cited in validated runs, approved traces and active
  playbooks. Every line keeps its physical id as `[MEMO:<id>]`.
- The block is marked as a derived summary, not source evidence. Text and tool
  arguments pass through `MemorySanitizer` before entering it.
- If the provider is empty or throws, the summary exceeds the explicit
  characters/tokens budget, or the block is not smaller than the removed
  messages, the original context is sent unchanged.
- The trace records one `context_compaction` step with switch state, applied
  flag, turns/messages compacted, characters and estimated tokens of the
  replaced prefix versus the derived block, memory ids and the fallback reason.
  The same object is stored in the episode metadata and the usage event.

Unit coverage is in `CppCore/tests/main.cpp`: switch off byte identity, task and
recent window preservation, empty memory, provider failure, `[MEMO:*]` ids,
character/token budgets, tool-call pairing, and unchanged `knowledge_version`,
edge status and path state.

### Evaluation

Use `scripts/e2e_memory_deepseek_long.py` as the long-interaction baseline.
It now runs `on`, `off` and `compacted` over the same fixture, model, question
and a 45,000-token ceiling. Recorded on 2026-11-02: ON answered `ORION-42` with
13,780 prompt tokens; the compacted arm also answered `ORION-42` with 12,884
prompt tokens (**6.5% fewer**, 14,253 &rarr; 13,404 total tokens), kept the same
memory id (`deepseek-long-lesson`) and reported 18 compacted turns. Repeated runs
measured 5.5% to 7.1% fewer prompt tokens, so the switch stays opt-in until a
broader real-data A/B covers more task shapes.

## 2. Standalone Mnemosyne module and submodule

**Repository:** <https://github.com/frankalcantara/Mnemosyne>
**Status:** repository created, assessment complete, migration not started.
**Current contents:** only `LICENSE` and `README.md`; there is no usable module
to pin as a submodule yet.

**Goal.** Make project memory a standalone C++20 library that any harness can
consume, while Arestrix keeps only its runtime, UI, CLI and assistant adapters.

### Scope boundary

Mnemosyne core should own:

- SQLite schema, migrations and fingerprints;
- store, knowledge graph, lessons, facts, claims, lenses, facets, sources,
  passages, paths, shortcuts, traversal and consolidation;
- lexical/hybrid retrieval and embedding-provider interfaces;
- telemetry contracts and maintenance interfaces;
- configuration, scoped storage and access-policy hooks;
- public headers, versioning, CMake install/export and standalone tests.

Arestrix should own:

- agent loop, tools, Taskflow generator and generated code;
- CLI, UI, assistant tools and project/workspace configuration;
- `RunnerSecurity`, environment mapping and application-specific paths;
- adapters from Arestrix configuration to Mnemosyne configuration.

### Coupling to remove

The current memory tree has 47 files and approximately 15.8k lines. The main
couplings are:

- `taskforge::Json` and `util/Json*` in public headers;
- `project/RunnerSecurity` for SHA-256;
- `rt/Http` in embeddings and the institution service;
- `rt/MemoryFailure` in the A/B protocol;
- `MemoryService` singleton, `projectId`, environment variables and workspace
  paths;
- generated code including `memory/MemoryService.hpp`;
- memory tests embedded in the Arestrix monolithic C++ test file.

### Delivery sequence

1. Scaffold Mnemosyne with `include/mnemosyne`, `src`, `tests`, CMake package and
   versioned public API.
2. Extract a vertical slice: JSON/hash utilities, schema/store, lexical search
   and standalone tests.
3. Add the advanced layers: claims, facets, paths, traversal and bridges.
4. Introduce an instance-based `mnemosyne::Engine` with injected clock, hasher,
   embedding provider, telemetry sink and storage path.
5. Keep an Arestrix compatibility adapter behind `MemoryService`.
6. Tag Mnemosyne `v0.1.0`, then add it as a Git submodule:

```bash
git submodule add https://github.com/frankalcantara/Mnemosyne third_party/Mnemosyne
git submodule update --init --recursive
```

7. Link `mnemosyne::core` from Arestrix and migrate generated code to the public
   include path.
8. Remove the old `CppCore/src/memory` tree only after E0-E20, baseline, A/B and
   real-provider tests pass through the module.

### Effort

- **MVP extraction** (module compiles, Arestrix consumes it, tests still mostly
  in Arestrix): **2-3 weeks**.
- **Production-grade standalone module** (public API, no Arestrix dependencies,
  install/export, tests, docs, cross-platform): **6-10 weeks**.
- Main risk is not file moves; it is splitting `MemoryStore.cpp` (~318 KB) and
  `MemoryService.cpp` (~138 KB) and replacing the singleton/environment
  assumptions.

### Acceptance criteria

- Mnemosyne builds and tests without the Arestrix source tree.
- Arestrix builds with Mnemosyne as a pinned submodule and no duplicated memory
  sources.
- Public API is documented and has a small harness example.
- Schema and migration remain compatible with `memory-v1.0.1` databases.
- Arestrix E0-E20, baseline, A/B and real DeepSeek tests remain green.

## 3. Experimental switch promotion

`memory.pathServing`, `memory.traversal`, `memory.bridges` and
`routing.mode=binary` remain opt-in. Promotion requires a real-data A/B with a
quality gain, no security regression and a documented cost budget.

## 4. Real embedding-provider validation

Profiles, fingerprints and fallback are implemented. Quality, latency and
failure behavior with real EmbeddingGemma 2 and Qwen3-Embedding-4B endpoints
still need a recorded run.

## 5. Institutional memory level in the Studio assistant

`memory_inspect` covers project, workspace, vertex and all. The `institution`
level needs an explicit service status and MCP/token configuration.

## 6. Multi-process maintenance coordination

Out of scope for memory v1. The current worker runs inside the project process.
