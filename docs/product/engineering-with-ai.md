# Engineering with AI agents

> **Applies to:** Arestrix Beta 2.3 (`2.3.0-beta.1`) · Last reviewed: 2026-10-19

Arestrix was built through a continuous collaboration between human software engineering and
multiple AI coding and reasoning models. This page records that process for transparency; it is not
a claim that AI-generated code is exempt from review.

## How the collaboration worked

- Human direction defined the product scope, architecture, security boundaries and acceptance
  criteria.
- AI agents contributed to refactoring, UI implementation, test generation, documentation and
  security review.
- Every change was reviewed, compiled and tested locally before being accepted.
- Secret scanning, license checks and the test suites are independent of the model that produced a
  change.

## Models used

| Provider | Models | Main contributions |
| :--- | :--- | :--- |
| Anthropic | Claude 3.5 Sonnet, Claude 3.7 Sonnet | Large-scale refactoring, architectural consistency and UI precision. |
| OpenAI | o1, o3-mini, GPT-4o | Task decomposition, Taskflow DAG scheduling and security hardening. |
| Google DeepMind | Gemini 2.0 Flash, Gemini 2.0 Pro | Automated testing, workflow generation and tool-use orchestration. |
| DeepSeek | DeepSeek-R1, DeepSeek-V3 | Memory efficiency analysis, low-level data structures and validation of graph execution algorithms. |

Model names and versions are historical; the project does not depend on a specific provider at
runtime. See [Getting started](../en/getting-started.md) for configuring a model.

## Limits

AI assistance does not replace code review, threat modeling or reproducible tests. Security-
sensitive areas (credential handling, sandboxing, integrity verification and packaging) require
explicit human review and are covered by the [security policy](../../SECURITY.md).
