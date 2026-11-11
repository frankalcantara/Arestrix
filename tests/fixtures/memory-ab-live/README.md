# A/B v2 live evidence (2026-11-11)

Run of the versioned A/B suite against a real provider.

- **Suite:** `tests/fixtures/memory-ab-v1.json` (12 relevant, 4 control, 4 known failure)
- **Pipeline:** `tests/fixtures/memory-ab/pipeline.json` with the live model variant
- **Provider/model:** DeepSeek `deepseek-chat`
- **Design:** 20 tasks x 3 repetitions x 2 arms (120 executions), frozen knowledge
- **Summary:** [`../memory-ab-live-report.json`](../memory-ab-live-report.json)
- **Reproduce:** `py -3 scripts/e2e_memory_ab_live.py`

## Files

| File | Contents |
| :--- | :--- |
| `observations-*.json` | Raw per-execution observations (`runId`, `episodeId`, `arm`, `ok`, tokens, cached tokens, turns, latency, error class). |
| `trace-diagnostic.json` | ON-arm trace analysis from the 20-execution pilot that preceded the full run: every trace issued at least one `memory_search` (1.8 on average). |

## Result

Approval was tied at 1.000 in both arms (60/60), with no infrastructure failures
and unchanged frozen knowledge. The ON arm used 2.58x total tokens and 1.69x
latency. The gap comes from memory searches the agent issues on its own, not
from the brief (~451 characters on average).

The pilot validator does not depend on memory, so this run measures approval
parity and cost, not the value of memory. The memory-dependent follow-up is in
[`../memory-ab-live-value/`](../memory-ab-live-value/): ON 60/60 versus OFF 0/60.
