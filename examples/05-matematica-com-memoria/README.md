# Example 5 — Mathematics with memory and web research

This example shows the flow that motivated the MCP research vertex: **search the web for sources,
write a mathematics text, and record what was learned in project memory**.

## Flow

```text
Semear_Memoria
      |
      v
Pesquisar_Fontes (Keenable MCP: search_web_pages)
      |
      v
Redigir_Texto_Matematico (agent + project memory)
      |
      v
Guardar_Licao (lesson + Blackboard)
      |
      v
Entrega_Texto (exports/texto-matematica.md)
```

1. `Semear_Memoria` records an initial lesson: every mathematical claim must cite a source or
   justify the step. It also stores the topic in the Blackboard.
2. `Pesquisar_Fontes` is an **MCP vertex** (no model call). It reads `samples/tema.md` and calls
   `search_web_pages` on the `keenable` server, writing the result to `workspace/fontes.json`.
3. `Redigir_Texto_Matematico` receives the memory brief and reads `workspace/fontes.json`. The skill
   requires an explanation, a demonstration, a worked example and real references.
4. `Guardar_Licao` records a review lesson and updates the Blackboard in SQLite.
5. `Entrega_Texto` copies the Markdown to `exports/texto-matematica.md`.

## Keenable

The MCP server lives in `.workspace/mcps/keenable.json`:

```json
{
  "id": "keenable",
  "name": "Keenable Web Search",
  "transport": "http",
  "url": "https://api.keenable.ai/mcp",
  "headers": { "X-API-Key": "${KEENABLE_API}" },
  "isEnabled": true
}
```

The key is **never** stored in the repository. Set the environment variable before running:

```powershell
$env:KEENABLE_API = "your-key"
```

In the UI, **Global Defaults → Credentials** can also store `KEENABLE_API` in the user environment;
the project file still keeps only the reference.

Without the variable, the MCP vertex fails explicitly (`env_var_unset`) instead of continuing
without research. The tools used are `search_web_pages` and, optionally, `fetch_page_content`.

## Run

```powershell
arestrix-cli run examples/05-matematica-com-memoria/pipelines/main.json `
  --root examples/05-matematica-com-memoria
```

The agent uses the model from Global Defaults (DeepSeek by default) and needs the matching API key.
To inspect memory and API consumption:

```powershell
arestrix-cli memory report --project examples/05-matematica-com-memoria
```

## What to look for

- `[MCP Pesquisar_Fontes] keenable.search_web_pages ok` in the console.
- `workspace/fontes.json` with the search results.
- `[MEMORIA] contexto injetado em Redigir_Texto_Matematico`.
- `workspace/texto-matematica.md` and `exports/texto-matematica.md`.
- `revisao_anterior` and `pos_redacao` lessons in `.arestrix/memory/memory.db`.

## Limitations

- Writing quality depends on the model and the returned sources.
- The example does not automatically verify every URL; the skill only forbids inventing sources.
- Memory scope is the project. Workspace and institution scopes arrive in later phases.
