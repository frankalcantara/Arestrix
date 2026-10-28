# Arestrix examples

[Português](README.pt-BR.md) · [Documentation](../docs/README.md)

Thirteen ready-to-run projects, from simple to complex. Each one is a normal Arestrix project folder: open it in the Studio,
choose your model, set your API key, press **F5**.

| # | Project | What it shows | Needs |
| :--- | :--- | :--- | :--- |
| 1 | [`01-svg-agente`](01-svg-agente) | The tutorial: two agents, parallel checks (fork/join), and a self-correction loop decided in C++. | A model. |
| 2 | [`02-triagem-de-suporte`](02-triagem-de-suporte) | Support-ticket triage: agents, **decision vertices** (an AI model picks the path; keyword rules close a quality loop) and an **MCP** (an MCP vertex and an agent using MCP tools). | A model, Python. |
| 3 | [`03-atendimento-ilustrado`](03-atendimento-ilustrado) | A "project of projects": example 2 followed, on the FAQ branch, by example 1 (the answer gets an animated SVG). | A model, Python. |
| 4 | [`04-memoria`](04-memoria) | Project memory: a semantic lesson and the Blackboard are persisted, loaded and injected into the prompt; the answer becomes an artifact. | A model. |
| 5 | [`05-matematica-com-memoria`](05-matematica-com-memoria) | Web research through the Keenable MCP vertex, followed by a mathematics text written with project memory and delivered as Markdown. | A model, `KEENABLE_API`. |
| 6 | [`06-chamada-agente`](06-chamada-agente) | A coordinator agent declares a **call** to a specialist agent (dashed edge) and receives its answer synchronously; the result is delivered as an artifact. | A model. |
| 7 | [`07-interacao`](07-interacao) | A human **interaction** vertex with a safe form; `--answers` feeds a scripted response for CI/headless runs, and the answer is delivered as an artifact. | Nothing (scripted answers). |
| 8 | [`08-formulario-gerado`](08-formulario-gerado) | A model generates the form schema at runtime from markdown context and an artifact; the runtime validates it and audits the generated schema. | A model (generator). |
| 9 | [`09-chamadas-paralelas`](09-chamadas-paralelas) | A coordinator requests two declared specialists in the same turn; the Agent loop runs the `call_agent` calls in parallel and consolidates both answers. | A model. |
| 10 | [`10-subflow-chamavel`](10-subflow-chamavel) | A coordinator calls a **subflow** with its own nested graph; the compiler emits it as a native function and the runtime passes the input to the child agents. | A model. |
| 11 | [`11-call-vertex`](11-call-vertex) | A **Call** vertex executes a declared target inside the flow, writes the answer to `.arestrix/out` and hands it to the next vertex. | A model. |
| 12 | [`12-chamadas-assincronas`](12-chamadas-assincronas) | A coordinator starts `call_agent` with `async: true`, gets a pending `callId` and checks the result with `call_result`. | A model. |
| 13 | [`13-trilhas-independentes`](13-trilhas-independentes) | Two paths with no shared vertex: badges, path counter, write-conflict warning and both ways to propose a junction. | Nada. |

Examples 3 and 5 are **generated** by `python scripts/build_examples.py`, so the shared pieces do not drift apart.
Vertices cannot yet execute a nested graph, so the composition is flattened into a single pipeline.

All thirteen end in **output vertices**: the produced file is copied to `exports/` and registered in the **Artifacts** tab,
with a viewer per format (SVG, Markdown, spreadsheets, PDF and JSON).

## Run an example

```bash
python scripts/copy_example.py 02-triagem-de-suporte C:/my-projects/triage     # copy it OUTSIDE the repository
```

1. In the Studio: **File → Open** and pick that folder.
2. **Global Defaults → Provider & Model**: choose your provider and model. The examples use DeepSeek by default, but any
   provider works because every agent inherits the project's model. Save your API key in the same screen: it goes to
   **your user environment variables**, never to the project.
3. Press **F5**.

Without the Studio: `arestrix-cli run pipelines/main.json --root <the copied folder>`.
See [`arestrix-cli`](../docs/en/arestrix-cli.md) for build, `gen`, CI and troubleshooting.

Examples 2 and 3 read a support ticket. Pick one of `samples/` with the `ARESTRIX_EXAMPLE_TICKET` environment variable
(`chamado_urgente.txt`, `chamado_normal.txt` or `chamado_duvida.txt`), or put your own text in `workspace/ticket.txt`.

Use **your own agents**: each agent vertex points to a `skills/*.md` file (its instructions). Edit them, add vertices, change
the model per vertex (**Model** button), or replace the demo MCP server in `.workspace/mcps/` with your real one.

## Test them without spending anything

```bash
python scripts/e2e_examples.py
```

Runs every example against a scripted fake LLM server, with test keys only: no network, no cost. It checks the paths taken,
the MCP calls, the loops, that no real key is used or printed, and that the examples contain no secret.

## Secrets: how they are kept out of Git

The examples **never contain a key**. The rules, and what enforces them:

- **API keys live only in your user environment variables** (`DEEPSEEK_API_KEY`, `OPENAI_API_KEY`...). Project files may only
  name a variable (`DEEPSEEK_API_KEY`) or reference it as `${NAME}`. MCP server configs follow the same rule for their `env`.
- **Copy an example before using it** (`scripts/copy_example.py`). Opening it inside the repository makes the Studio write
  agent outputs and your local paths into versioned folders.
- `examples/.gitignore` keeps generated artifacts (`workspace/`, `output/`, `storage/`, `.arestrix/`), `.env` files, keys and
  databases out of Git.
- `scripts/check-secrets.mjs` scans for key formats (OpenAI/DeepSeek/Anthropic, Google, GitHub, AWS, private keys, JWT...),
  literal values assigned to `api_key`/`token`/`password` fields, user paths, personal e-mails and forbidden files, and
  checks that manifests use `rootPath: "."` and an empty `environment`. It never prints a full secret.
- A **pre-commit hook** runs it on everything you stage. Enable it once per clone: `npm run hooks:install`.
- `npm test` fails if any file under `examples/` is flagged.

If you ever type a real key into a file, **revoke it at the provider**; deleting the file is not enough once it was committed.

## Trust

A project can run code on your machine: custom C++ lambdas and MCP server commands. The agents' own tools are confined to the
project folder, but MCP servers run as ordinary processes of your user. Open projects only from sources you trust.
