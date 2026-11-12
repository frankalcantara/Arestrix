# Memory test results

> **Applies to:** Arestrix 2.8 (`2.8.0`) · **Last reviewed:** 2026-11-11
>
> **Scope:** results from the memory campaigns — baseline, regression
> evaluation, live A/B for parity and value, the memory policy gate
> (`memory.gate`) and the advanced switches (`pathServing`, `traversal`,
> `bridges` and `routing.mode`).
>
> **Details:** [`memory-gate-ab.md`](../quality/memory-gate-ab.md) and
> [`memory-advanced-ab.md`](../quality/memory-advanced-ab.md).
>
> **Operational pending items and limitations:** [`pending.md`](pending.md).

## 1. Executive summary

Memory delivers measurable value, but it costs more tokens and latency when the
agent decides to consult it on its own. `memory.gate` reduces that cost without
losing accuracy in the pilot. The advanced switches work as mechanisms, but the
defaults stay off until they are measured on real projects and with the
production model.

| Campaign | What it measures | Recorded result | Main limitation |
| :--- | :--- | :--- | :--- |
| E0 baseline | determinism, brief, hits and telemetry | 3 runs, 9/9 successes, 7,314 tokens, 0 invalid citations | simulated provider; no model-quality measurement |
| E20 evaluation | regression across variants | 8 cases, 5 variants, all complete | the variants produced identical metrics; they do not exercise the switches |
| Live A/B — parity | cost when memory is not required | approval 1.000 vs 1.000; 2.58x tokens; 1.69x latency | 20 curated tasks; no customer data |
| Live A/B — value | accuracy when memory is required | approval 1.000 vs 0.000; delta 1.000; 2.67x tokens; 1.62x latency | binary effect by validator design |
| `memory.gate` | reduce searches and cost without losing accuracy | Jev: 1.48x/1.58x tokens and 1.08x/1.09x latency, approval 1.000 | 3 repetitions; curated data |
| Advanced switches | paths, traversal, bridges and routing | path 1.000/1.000; traversal 1.000/0.500; 1 bridge and 0 edges; coherent routing | curated corpus; traversal precision is still 0.500 |

## 2. Baseline and regression

The E0 baseline uses a deterministic provider and a frozen fixture with five
lessons, three facts and one playbook. Over 3 graph runs:

| Metric | Result |
| :--- | ---: |
| Graph runs | 3 |
| Agent runs | 9 |
| Model calls | 15 |
| Total tokens | 7,314 |
| Injected brief | 6,795 characters |
| Injected / consulted / used hits | 39 / 21 / 6 |
| Invalid citations | 0 |
| Agent successes | 9/9 |

The E20 evaluation compared `baseline_lexical`, `hybrid_fallback`,
`facets_traversal`, `paths_serving` and `routing_shadow` over 8 positive and
negative cases. All completed, but the five variants produced the same counters
(2,438 tokens, 5 calls, 13 injected items, 2 used, 0 invalid citations). This is
a limitation of the E20 corpus: it verifies that turning a switch off does not
change the baseline, but it does **not** exercise paths, traversal or bridges.
That is why the advanced corpus in section 5 was created.

## 3. Live A/B for parity and value

A/B v2 uses 20 tasks, 3 repetitions and 2 arms over frozen knowledge: 120
executions per suite. The agent was `deepseek-chat`; there was no infrastructure
failure, invalid task or change to the frozen knowledge.

### 3.1 Parity

The validator does not depend on memory; the suite measures cost and approval
regression.

| Metric | OFF | ON | ON/OFF |
| :--- | ---: | ---: | ---: |
| Approval | 60/60 (1.000) | 60/60 (1.000) | delta 0 (95% CI [0, 0]) |
| Total tokens | 146,220 | 377,378 | **2.58x** |
| Prompt tokens | 99,428 | 298,588 | — |
| Completion tokens | 46,792 | 78,790 | — |
| Average turns | 2.00 | 3.52 | — |
| Average latency | 6.46 s | 10.95 s | **1.69x** |

The higher ON cost comes from the agent's own `memory_search` calls (20/20
traces in the 20-execution trial, 1.8 searches per trace on average), not from
the brief: the injected context averages ~451 characters.

### 3.2 Value

The validator requires the `AB-7731` token, which exists only in a curated
lesson.

| Metric | OFF | ON | ON/OFF |
| :--- | ---: | ---: | ---: |
| Approval | 0/60 (0.000) | 60/60 (1.000) | delta 1.000 (95% CI [1.000, 1.000]) |
| Total tokens | 151,539 | 405,161 | **2.67x** |
| Average latency | 7.12 s | 11.56 s | **1.62x** |

The lesson is effectively used, but the effect is binary by validator design.
The value A/B measures lesson delivery, not advanced retrieval quality.

## 4. Memory policy gate (`memory.gate`)

The gate decides between `none`, `brief`, `search` and `learn` before the agent
loop. The 3-repetition confirmation (2026-11-12) used 20 tasks × 2 arms × 3
repetitions = 120 executions per suite, with 0 infrastructure failures, 0 invalid
tasks and unchanged frozen knowledge.

| Scenario | ON approval | ON/OFF tokens | ON/OFF latency | ON total | OFF total |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, parity | 1.000 | 1.48x | 1.08x | 215,381 (173,453 agent + 41,928 gate) | 145,623 |
| Jev, value | **1.000 vs 0.000** | 1.58x | 1.09x | 238,059 (193,311 agent + 44,748 gate) | 151,129 |

The historical 1-repetition pilot with DeepSeek and the deterministic gate
remains in `docs/quality/memory-gate-ab.md`; the table above is the Jev
confirmation. The gate chose 39 `brief`, 18 `none` and 3 `search` in parity, and
57 `brief` + 3 `search` in value. Average gate latency was 271.6 ms in parity and
420.3 ms in value. The approval delta was 0 in parity (95% CI [0, 0]) and 1.000
in value (95% CI [1.000, 1.000]).

The validation floor was necessary: in the `fail-fonte` case the model chose
`none`, but the validator required `AB-7731`, present only in the brief; the
floor promoted the decision to `brief` and preserved accuracy.

## 5. Advanced switches

The `memory-advanced-v1` corpus has 10 tasks, 7 variants and 3 repetitions (210
deterministic measurements), with no model call. Each task runs on a copy of the
frozen database.

| Scenario | Recall | Precision | Brief | Reading |
| :--- | ---: | ---: | ---: | :--- |
| `pathServing` on a path | 1.000 | 1.000 | 422 chars | baseline 0.000; expired path is not served |
| `traversal` on a three-hop chain | 1.000 | 0.500 | 736 chars | baseline 0.000; no-anchor query abstains; `local` edge is excluded |
| `all_on` — path | 1.000 | 1.000 | 422 chars | path served before traversal |
| `all_on` — traversal | 1.000 | 0.500 | 736 chars | traversal only when no path covers the query |

- **Bridges:** 1 correct hypothesis (`Guia`, lenses `contraste`/`tipografia`),
  with the expected claimIds and **0 edges created**. Same-lens and
  incompatible-access cases produced no proposal.
- **`shadow` routing:** the decision was recorded, but the executed tier stayed
  `current`, including with no history.
- **`binary` routing:** chose `local_large` at high confidence, `frontier` at low
  confidence and `frontier` with no history.
- **Harness latency:** ~44–82 ms per local query, without a model.

The measurement found and fixed two defects: traversal did not resolve anchors
from natural-language prompts (it now tokenizes the query when the whole phrase
is not an alias), and `shadow` switched the executed tier when there was no
history (it now keeps the current tier and only records the suggestion).

## 6. Integration and release tests

| Check | Result |
| :--- | :--- |
| C++ suite | passed, including the anchor and router tests |
| Vitest | 630/630 |
| End-to-end tutorial | passed |
| Examples 01–14 | 14/14 passed |
| Advanced corpus (`--check`) | reproduced |
| Claim agent (registration + extraction) | e2e with a fake provider: 1 candidate, 1 extracted, 1 proposal, 0 failures |
| `check:secrets` | no secret or personal data |
| `check:version` | 2.8.0 consistent |

## 7. Remaining limitations

1. **Live A/B with curated data.** The 20 tasks and frozen knowledge do not
   replace real customer projects; the results show direction, not
   generalisation.
2. **Variable provider latency.** The Jev gate is confirmed with 3 repetitions,
   but the DeepSeek agent still varies; tokens and latency can change with the
   chosen provider.
3. **Binary value.** The value validator requires a token that exists only in the
   curated lesson; this proves delivery, but does not measure quality gradation.
4. **Routing without a quality A/B.** `routing.mode=binary` was validated as a
   decision mechanism, not as model selection with real cost and quality.
5. **Traversal precision.** The three-hop chain had recall 1.000 and precision
   0.500: the pack includes claims related to the same anchor beyond the target.
   This is acceptable as context, but still needs ranking or pruning work.
6. **Path cold start.** `pathServing` depends on paths consolidated from approved
   traces; new projects have no paths until usage accumulates.
7. **Bridges are hypotheses.** The detector creates no edges; promotion is manual
   and requires human review.
8. **Real embeddings.** Profiles and fingerprints exist, but quality and latency
   validation with real EmbeddingGemma 2 and Qwen3-Embedding-4B models is still
   pending; lexical fallback remains the safe behaviour.
9. **Claim-extraction agent UI.** Registration/listing, the Properties selector,
   per-project saving, CLI extraction and manual Studio validation are complete.
   Validation found and fixed a saved-target comparison bug caused by JSON key
   order.

10. **Single machine.** Multi-machine synchronisation and multi-worker
    coordination remain out of scope for this release.
11. **Monetary cost.** Reports measure tokens and latency, not dollar price or
    infrastructure cost.
12. **The gate is not proof of correctness.** The `memory_gate` step is the
    source of the effective decision; `memory_hit` may record brief injection
    before the gate. The gate reduces cost, but does not replace validation and
    A/B.

## 8. Reproduction

```powershell
# Live A/B for parity and value (requires provider and key)
py -3 scripts/e2e_memory_ab_live.py --repetitions 3
py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent

# Gate with Jev (the agent is still DeepSeek)
py -3 scripts/e2e_memory_ab_live.py --repetitions 1 --gate-mode enforce `
  --gate-provider jev --gate-model jev-latest
py -3 scripts/e2e_memory_ab_live.py --repetitions 1 --memory-dependent `
  --gate-mode enforce --gate-provider jev --gate-model jev-latest

# Advanced switches (deterministic, no model)
py -3 scripts/memory_advanced_eval.py --write --runs 3
py -3 scripts/memory_advanced_eval.py --check --runs 3
```

## 9. Evidence

| Evidence | File |
| :--- | :--- |
| E0 baseline | `tests/fixtures/memory-baseline/baseline.json` |
| E20 evaluation | `tests/fixtures/memory-eval-v1-report.json` |
| Live A/B — parity | `tests/fixtures/memory-ab-live-report.json` |
| Live A/B — value | `tests/fixtures/memory-ab-live-value-report.json` |
| DeepSeek gate — parity | `tests/fixtures/memory-ab-live-gate-enforce-deepseek-report.json` |
| DeepSeek gate — value | `tests/fixtures/memory-ab-live-value-gate-enforce-deepseek-report.json` |
| Jev gate — parity | `tests/fixtures/memory-ab-live-gate-enforce-jev-report.json` |
| Jev gate — value | `tests/fixtures/memory-ab-live-value-gate-enforce-jev-report.json` |
| Advanced switches | `tests/fixtures/memory-advanced-report.json` |
| Advanced corpus | `tests/fixtures/memory-advanced-v1.json` |
| Gate report | `docs/quality/memory-gate-ab.md` |
| Switch report | `docs/quality/memory-advanced-ab.md` |
