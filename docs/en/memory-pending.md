# Memory pending items

This file tracks work that is specific to project memory. It is intentionally
kept in the repository so the pending contract is reviewed with the code.

**Status:** memory v1 core and real-provider validation are complete; the items
below are the next memory work. No item here blocks the current release.

| Priority | Item | State |
| :--- | :--- | :--- |
| High | Context compaction backed by existing memory | Pending |
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

They do **not** prove that the conversation history itself shrinks. Today the
agent resends prior messages each turn. Memory adds a brief; it does not evict or
summarize old turns. The pending work is the bridge between those facts.

### Design constraints

- The source of truth is the existing memory: active lessons, claims, paths,
  facets, source passages and validated traces already in the project.
- Compaction must preserve citations and claim identity. A compacted item must
  still point to the original memory id; summarized text is not new evidence.
- The compactor must never promote claims, create edges or change support state.
- The original turns must remain available for audit and for a fallback when
  compaction fails.
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

### Evaluation

Use `scripts/e2e_memory_deepseek_long.py` as the long-interaction baseline and
add a compacted arm. Keep the same fixture, model and token budget. The test must
show both lower prompt size and equal answer quality before the switch is
enabled by default.

## 2. Experimental switch promotion

`memory.pathServing`, `memory.traversal`, `memory.bridges` and
`routing.mode=binary` remain opt-in. Promotion requires a real-data A/B with a
quality gain, no security regression and a documented cost budget.

## 3. Real embedding-provider validation

Profiles, fingerprints and fallback are implemented. Quality, latency and
failure behavior with real EmbeddingGemma 2 and Qwen3-Embedding-4B endpoints
still need a recorded run.

## 4. Institutional memory level in the Studio assistant

`memory_inspect` covers project, workspace, vertex and all. The `institution`
level needs an explicit service status and MCP/token configuration.

## 5. Multi-process maintenance coordination

Out of scope for memory v1. The current worker runs inside the project process.
