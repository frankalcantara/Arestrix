# A/B v2 value evidence (2026-11-11)

Memory-dependent run of the versioned A/B suite against a real provider.

- **Design:** the validator requires the `AB-7731` token, which exists only in a
  curated lesson seeded into the frozen knowledge.
- **Provider/model:** DeepSeek `deepseek-chat`
- **Run:** 20 tasks x 3 repetitions x 2 arms (120 executions)
- **Summary:** [`../memory-ab-live-value-report.json`](../memory-ab-live-value-report.json)
- **Reproduce:** `py -3 scripts/e2e_memory_ab_live.py --repetitions 3 --memory-dependent`

## Result

| Metric | OFF | ON |
| :--- | ---: | ---: |
| Approval | 0/60 (0.000) | 60/60 (1.000) |
| Prompt tokens | 101,494 | 326,612 |
| Cached prompt tokens | 39,808 | 241,152 |
| Completion tokens | 50,045 | 78,549 |
| Mean turns | 2.00 | 3.70 |
| Mean latency | 7.12 s | 11.56 s |

The approval delta is 1.000 (95% CI [1.000, 1.000]). The ON arm delivers the
curated lesson and the agent uses it; the OFF arm cannot know the token. This
measures delivery and use of curated memory, not generalized answer quality or
the advanced retrieval switches.
