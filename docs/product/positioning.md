# Positioning

> **Applies to:** Arestrix Beta 2.2 (`2.2.0-beta.1`) · Last reviewed: 2026-10-19

## The problem

Enterprises of all sizes face the same obstacle when moving autonomous AI workflows into
production: **the gap between prototype scripts and software that operations, security and IT can
actually run**.

Common approaches impose runtime liabilities that are difficult to justify in a controlled
environment:

- heavy Node.js or Python runtime dependencies on every endpoint;
- fragile virtual environments and package managers;
- credentials stored in plaintext `.env` files or shared configuration;
- vendor lock-in with multi-tenant SaaS platforms that move data outside the perimeter;
- workflows that cannot be versioned, reviewed or packaged like normal software.

## The Arestrix approach

Arestrix treats an agent pipeline as a **program**, not as a hosted automation:

1. **Design visually.** A canvas inspired by Microsoft Office lets engineers, analysts and
   operations specialists describe tasks, dependencies and feedback loops.
2. **Compile natively.** The graph is translated into C++20 source and compiled with
   [Taskflow](https://github.com/taskflow/taskflow) into parallel task DAGs. The graph is not
   interpreted at runtime.
3. **Distribute as software.** A pipeline can be exported as a standalone runner, with packaging
   support for MSI and MSIX so IT teams can deploy it through their existing Windows management
   tooling.
4. **Keep secrets out of the project.** Credentials are resolved at runtime through Windows DPAPI
   or Azure Key Vault / Managed Identity. Project files contain variable names, never values.
5. **Verify what runs.** Exported packages carry an integrity manifest, and the runner can verify
   it before execution (fail-closed).

## Design principles

| Principle | What it means in practice |
| :--- | :--- |
| **Local-first** | Projects, memory, logs and databases live on the user's machine unless a model or MCP server is explicitly called. |
| **Native by default** | The Studio avoids Electron and the generated program runs as a native binary. |
| **Fail closed** | Security checks deny execution when integrity or confinement cannot be established. |
| **Bilingual by design** | The interface and documentation ship in English and Portuguese; new strings go through the translation catalog. |
| **Reviewable artifacts** | Graphs, generated C++ and packaging metadata are text files that can be diffed and versioned. |

## Who it is for

- Teams that need agent workflows on managed Windows endpoints.
- Engineers who want generated code they can inspect and compile.
- Organizations that cannot send prompts and data to a multi-tenant automation platform.
- Educators and researchers who need a reproducible, local example of agent orchestration.

## Beta scope

Beta 2.2 is a development pre-release. The Studio runs on Windows and Linux, but programs generated
from a graph currently target Windows. APIs, file formats and command-line flags may change before
general availability. See the [changelog](../../CHANGELOG.md) for current limitations.

For a factual comparison with other approaches, see [Comparison](comparison.md).
