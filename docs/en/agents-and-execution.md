# Agents and execution

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-21

[Português](../pt-BR/agentes-e-execucao.md) · [Index](../README.md)

## Build & Run (F5)

1. The graph is validated and sent to the C++ backend.
2. The backend generates a Taskflow `main.cpp`, compiles it with MSVC (Windows) or g++/clang++ (Linux)
   against `arestrix_core`, and **runs it inside the project folder**. An unchanged graph reuses the
   compiled binary.
3. Logs and agent telemetry stream live to the console and to badges on the vertices.

`F5` or `Ctrl+Enter` runs, `Shift+F5` stops. The same engine is available without the interface:

```bash
arestrix-cli gen graph.json                    # print the generated C++
arestrix-cli run graph.json --root <project>   # generate, compile and run
```

See [`arestrix-cli`](arestrix-cli.md) for requirements, exit codes, CI and troubleshooting.

## What an agent vertex does

An agent vertex runs a real **tool-use loop** against its configured provider:

- The **skill** (`SKILL.md`) is the system prompt; the vertex prompt template is the task.
- The model can call tools: `read`, `write`, `edit`, `bash`, `validate_svg`, plus the tools of the MCP servers the agent declares (including the [native Arestrix tools](workspaces-memory-skills-mcp.md#native-tools-no-install-no-process))
  (see [MCP servers](workspaces-memory-skills-mcp.md#mcp-servers)). The built-in ones are confined
  to the project folder.
- The loop continues until the model stops calling tools. Skills can add more tools and MCP servers.

### Agent-to-agent calls

An agent can call another agent **declared in the graph** through a dashed **call edge**. The target
is callable-only (it has no flow edges) and runs synchronously inside the caller's task; it is not a
scheduled Taskflow task.

- The runtime exposes `call_agent` with an **enum of declared targets**; the model cannot invent a
  target.
- The call receives an explicit `input` payload and returns a JSON tool message with `callId`,
  `target`, `status`, `output` and `depth`.
- The skill guides when to call; the graph authorizes; the runtime enforces `maxCalls` per edge,
  `ARESTRIX_MAX_CALL_DEPTH` (default 3) and `ARESTRIX_MAX_TOTAL_CALLS` (default 50).
- If the model requests several `call_agent` calls in the same turn, the runtime runs them in
  parallel and returns the results in the requested order. A turn mixing calls with other tools stays
  sequential. Set `ARESTRIX_PARALLEL_CALLS=0` to force sequential delegation.
- `call_start` / `call_end` telemetry events carry the call path; the target's final text is also
  written to `.arestrix/out/<target>.txt`.
- The target never receives the caller's full conversation: only the explicit input. This keeps
  cost and prompt-injection surface small.
- A **subflow** target declared with a `childGraph` is compiled as a native function in the same
  executable. Its child graph runs in a nested Taskflow executor; the call input is passed as
  `inputContext` to its agents/interactions and the child output artifact is returned to the caller.
  Nested call edges and nested subflows are rejected in v1.

See [`examples/06-chamada-agente`](../examples/06-chamada-agente) and
[Graph and nodes](graph-and-nodes.md#call-edges-delegation).

### Human interaction vertices

An **interaction** vertex asks a person a question through a declarative form schema. The runtime
builds the form from declared field types only (text, textarea, number, boolean, choice, multi-choice,
date) and never executes HTML or JavaScript produced by a model.

- In the Studio, **Build & Run** opens the form as a modal and sends the answer through the
  `answer_interaction` RPC. The pipeline process is blocked on its stdin/stdout host channel while
  the modal is open. The Runner panel shows **Aguardando resposta** during the wait.
- In an interactive terminal, `arestrix-cli run` prompts field by field; CLI/CI automation uses
  `--answers <file.json>` keyed by the interaction node id. A list of answer objects is consumed in
  order for repeated interactions. Without a TTY and without `--answers`, the CLI fails closed.
- The runtime validates required fields, types and declared choices again before returning the
  answers. The result is written to `.arestrix/out/<nodeId>.txt`/`.json` and can feed the flow or a
  calling agent.
- With `schemaSource: "vertex"`, a model generates the form from the markdown context and artifacts;
  the runtime validates the JSON schema and applies `generator.fallback` (`fail`/`free_text`) when
  generation fails. Model-generated HTML/JavaScript is never rendered.
- Every attempt is audited at `.arestrix/runs/<runId>/interactions/<requestId>.json` (form, answers,
  channel, timestamps, `answeredBy`, generator metadata). Sensitive values stay out of telemetry/logs
  and sensitive text fields are masked in the Studio.
- Timeout, cancel and `fail`/`default`/`abort` policies are enforced; without a channel the vertex
  fails closed.

See [`examples/07-interacao`](../examples/07-interacao) (static form) and
[`examples/08-formulario-gerado`](../examples/08-formulario-gerado) (generated form), plus
[Graph and nodes](graph-and-nodes.md#interaction-vertices).

**Agent language.** At run time the agent is configured in the language of its own constitution: the skill and the prompt
template (the texts that define it). The runtime detects the language and emits everything the agent sees — the system
context block, the default prompt, the built-in and native tool descriptions and schemas, and the error messages returned
to it — in that language (Portuguese or English; Portuguese is the default when the texts give no clear signal). External
MCP tool descriptions are third-party data and pass through unchanged. Console logs and telemetry stay in the interface's
language.

Providers are listed in [Getting started](getting-started.md#api-keys-and-providers).

### The five quick actions

Every vertex has a bar of five buttons (also in the right-click menu):

| Action | Scope | Configures |
| :--- | :--- | :--- |
| Sandbox | Workspace | Isolation level, network policy, memory, timeout, mounts. |
| Model | Project | Provider, model, skill file, prompt template. |
| Tools | Project | Allowed tools and MCP servers. |
| Memory | Project | Blackboard, semantic lessons, persistence. |
| Skills | Workspace / Project | Skills bound to the vertex. |

Settings can be inherited from the **global defaults** (toolbar) or overridden per vertex.

## Sandboxes

| Level | Isolation | Notes |
| :--- | :--- | :--- |
| `host_direct` | Job Object / process group, timeout, memory limit | **Not a security boundary.** Strips `*_API_KEY`, `*_TOKEN`, `*_SECRET` from the child environment. |
| `container` | Docker | `--cap-drop=ALL`, read-only root, no network by default, workspace-only mounts. |
| `microvm` | WSL2 | Shares the host file system; refuses `offline` policies it cannot enforce. |

Network policy per vertex: `offline`, `egress_llm_only` or `full`.

## Safety properties

- A sandbox level that is unavailable **fails closed**. It does not silently fall back to the host unless
  `allowFallbackToHost` is set.
- File tools and the file endpoints exposed to the interface reject paths outside the project: `..`,
  absolute paths, symlinks and NTFS streams.
- No shell is used to start processes (`std::system` is not used).
- A feedback loop cannot spend unlimited money: each agent vertex is capped at 10 runs
  (`ARESTRIX_MAX_AGENT_RUNS`).

## Telemetry

The runtime streams events that the canvas shows as badges on each vertex:

| Event | Meaning |
| :--- | :--- |
| `thinking` | Reasoning in progress, with a running token/byte count. |
| `tool_start` / `tool_end` | A tool call began or finished, with arguments and status. |
| `429 retry` | Rate limit detected (`RESOURCE_EXHAUSTED`, "Rate limit reached"); a countdown shows the scheduled retry. |
| `completed` | The vertex finished. |

## Batch pipelines and resume

For pipelines that process many items (articles, data batches), the **Resume engine** keeps a checkpoint
per item and per stage in `pipeline-state.json`, written atomically.

- **Artifact validation:** a stage is marked complete without calling the API again if its output
  exists, has a minimum size, and matches an expected pattern (for example YAML front matter). This
  is recorded as `recoveredFromArtifact: true`.
- **Serialized bottlenecks:** heavy stages can run one item at a time to respect provider limits while
  later stages run concurrently.
- The **Run** tab has the batch orchestrator: items, concurrency, serialized stages, and a live checkpoint matrix.

## Deliverables and Output Nodes (Artifacts)

Any pipeline designed to generate deliverable products (vector graphics, reports, spreadsheets, models, or datasets) can declare one or more **`output` (Output / Artifact)** nodes.

- **Source and Destination Configuration:** The vertex specifies a `sourcePath` (e.g., `workspace/scene.svg` or `workspace/report.md`) produced by preceding agents, and optionally a `destinationPath` (e.g., `exports/scene.svg`). At execution completion, the deliverable is atomically copied to the final destination folder.
- **Built-in Multi-Format Support:**
  - **SVG:** Interactive vector viewer with zoom controls (+, -, 100%, fit to screen), source code inspection, and crisp typography rendering (no blur/drop-shadow on text).
  - **Spreadsheets (CSV, TSV, XLSX):** Tabular data grid with instant cell search and raw data toggling.
  - **Text / Markdown:** Formatted viewer for technical documents and reports with syntax highlighting and one-click copy.
  - **PDF and JSON:** Structured view with clean indentation and direct system viewer launch.
- **Artifacts Tab in the Bottom Drawer:** Positioned alongside the *Terminal* and *Telemetry* tabs, the *Artifacts* tab displays the real-time count of generated deliverables, lets users inspect visual graphics or code, copy content, and open the `exports/` folder in Windows Explorer with one click.
- **Local Chat Assistant Operation:** The Studio's native chat assistant integrates with the artifact lifecycle through the `list_artifacts` and `get_artifact` tools, allowing the assistant to examine generated deliverables and provide analytical feedback directly to the user.

## Standalone Runner (Phase 1: Autonomous Executable)

The **Standalone Runner** transforms any agent graph into an independent application suitable for standalone distribution or streamlined operation, without requiring operators to launch the full visual graph editor.

### Activation Triggers
1. **Ribbon Button:** In the **Run** tab, the **Distribution** group exposes the **Standalone Runner** button (icon 🚀).
2. **Backstage (File):** In the **Standalone Runner** card on the active project overview page.
3. **Project Manifest Property:** Configured directly in `arestrix.project.json` under `"runner": { "enabled": true, ... }`.

### The Three Operational Views
The Runner provides a modern, high-contrast, clean UI with 3 essential screens for monitoring and operational control:

1. **Progress Screen:**
   - **Real-Time Status Indicator:** Status pill (`Waiting`, `Compiling`, `Running`, `Success`, `Error`, `Stopped`).
   - **Linear Progress Bar:** Smooth visual progress bar tracking workflow completion percentage.
   - **Elapsed Timer:** Real-time run duration timer with millisecond accuracy.
   - **Step Timeline:** Granular tracking of each Taskflow pipeline stage with status badges, start timestamps, and stage execution duration.

2. **Diagnostics Screen:**
   - **Telemetry KPIs:** Live cards monitoring token consumption (Prompt, Completion, Total), injected memory context, tool calls count, 429 rate limit retries, and average latency. Tokens arrive through a `usage` event emitted by the C++ runtime; there is no stdout parsing and no auxiliary Python/JS process.
   - **Agent Thinking Stream:** Live streaming view of agent reasoning and cognitive deliberations during execution loops.
   - **Tool Call Inspection:** Detailed table of invoked tools, arguments passed, execution duration, and success/failure status.
   - **Diagnostic Export:** One-click button to copy complete diagnostic logs in structured JSON format.

3. **Operations Screen:**
   - **Execution Controls:** Start Execution (`F5`), Stop (`Shift+F5`), and Restart.
   - **Configurable Input Parameters:** Dynamically rendered form based on manifest parameters (strings, numbers, booleans), allowing runtime customization without modifying graph definitions.
   - **Artifacts Access:** Direct button to open the generated outputs directory in Windows Explorer.

### Standalone Packaging
The **Export** tab in the Standalone Runner modal coordinates native distribution packaging:
- Target distribution directory (defaults to `dist/runner`).
- Copies the native C++ executable and referenced workspace skills.
- Generates `runner_manifest.json` with metadata, schema, and parameter definitions.
- Generates single-click launcher batch scripts (`run.bat`).

### Zero-Secrets Policy in Executables and Runner Files
Under strict security guidelines, **no secrets, passwords, or API keys are ever stored inside the executable (`.exe`) or any files required for runner operation**:
- **Exclusive OS Environment Resolution:** The compiled C++ executable resolves API keys dynamically from the host operating system's environment variables (e.g., `DEEPSEEK_API_KEY`, `OPENAI_API_KEY`). If the environment variable is not defined on the target machine, the runner fails closed with an informative message.
- **Fail-Closed Pre-Export Audit:** Both frontend and C++ backend perform strict automated scanning before export. Any detection of real API key formats (`sk-...`, `AIza...`, GitHub tokens, JWTs, private keys, or password assignments) aborts packaging immediately.
- **Prohibited Files Exclusion:** Local credential files (`.env`, `.env.*`, `credentials.json`, `*.key`, `*.pem`, `id_rsa`) are strictly filtered and never copied to distribution folders.
- **Child Process Scrubbing:** The process driver automatically strips all sensitive environment variables (`_API_KEY`, `_TOKEN`, `_SECRET`, `_PASSWORD`) before launching child tool processes on the host.

## Corporate Governance & Packaging (Phase 2: Enterprise IT & Security)

Phase 2 establishes enterprise security foundations, cryptographic integrity enforcement, and deployment packaging certified for IT and InfoSec (SecOps) teams.

### 1. Fail-Closed Cryptographic Package Validation
- **Canonical SHA-256 and HMAC-SHA256 / Ed25519 Hashing:** Every generated runner distribution package is hashed canonically using NIST FIPS 180-4 SHA-256.
- **Embedded & Detached Signatures:** The `runner_manifest.json` file embeds a dedicated `"signature"` section and emits a detached `runner_manifest.json.sig` file.
- **Fail-Closed Execution Policy:** When `"requiredForExecution": true` is enforced, the runner strictly validates digital signature authenticity and payload integrity prior to bootstrapping the pipeline. Any signature tampering, payload modification, or missing corporate issuer blocks startup immediately with an explicit security audit log.

### 2. Native Enterprise Vault Integration (Zero Secrets in Disk & RAM)
Enterprise pipelines and autonomous agents do not store secrets or API tokens on local disk files:
- **Windows Credential Vault (DPAPI):** Native Win32 Credential Manager integration (`CredReadW`, `CredWriteW`, `CredDeleteW`) protected by Windows DPAPI under the `Arestrix/` corporate namespace.
- **Azure Key Vault (REST API / IMDS):** Direct secret retrieval from Azure Key Vault via native HTTPS REST API (`api-version=7.4`), authenticated seamlessly via Azure Instance Metadata Service (IMDS Managed Identity at `169.254.169.254`) or Entra ID Service Principal tokens.
- **Cascading Resolution Engine:** Resolves secrets hierarchically (Azure Key Vault -> Windows Credential Vault -> sanitized OS environment variables).
- **Immediate Memory Purging (`SecureZeroMemory`):** Memory buffers containing tokens, keys, and credentials are scrubbed immediately after use using Win32 `SecureZeroMemory` (`secureWipeMemory`), preventing sensitive tokens from lingering in memory dumps, crash logs, or paging files.

### 3. Enterprise Packaging for Microsoft Intune and SCCM
To facilitate seamless enterprise rollout via Microsoft Endpoint Manager (Intune) and SCCM, the export process automatically generates enterprise packaging artifacts:
- **WiX Toolset Corporate MSI Installer:**
  - Automated `installer.wxs` with deterministic RFC 4122 `UpgradeCode` GUIDs for silent automated upgrades.
  - Automated build scripts `build_msi.bat` and `build_msi.ps1`.
  - Silent deployment support:
    ```cmd
    msiexec.exe /i "Arestrix_Runner.msi" /qn ALLUSERS=1 /l*v "%TEMP%\install.log"
    ```
- **Modern MSIX Package (AppxManifest):**
  - Generates `AppxManifest.xml` featuring the `runFullTrust` restricted capability for enterprise AppLocker/WDAC environments.
  - Packaging script `package_msix.ps1` utilizing `MakeAppx.exe` and `SignTool.exe`.
- **Intune Custom Detection Script (`intune_detection_rule.ps1`):**
  - PowerShell detection rule script verifying physical installation under `%ProgramFiles%\Arestrix\Runners\` as well as Windows Uninstall Registry keys (`HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`), exiting with code `0` when compliant.
- **Cryptographic Manifest (`SHA256SUMS`):**
  - Checksum manifest providing SHA-256 hashes for all bundled distribution files, enabling zero-trust auditing and verification by enterprise EDR and firewall solutions.

## Related

- [Workspaces, memory, skills and MCP](workspaces-memory-skills-mcp.md)
- [Architecture](architecture.md)
