# Example 2 — Support triage (agents, decision vertices and MCP)

[Português](README.pt-BR.md) · [All examples](../README.md)

A support ticket is read, classified and answered. Nothing here is code you must write: the paths are chosen by an AI model
(and by keywords), and a customer database is reached through an MCP server.

```
Receive ─► Consult customer (MCP vertex) ─► Reader (agent) ─► Triage (switch, AI decides) ─┬─ urgente ─► Escalator (agent + MCP) ─► Delivery
                                                               ├─ normal ──► Writer ─► Reviewer ─► Quality (switch, keywords) ─┬─ aprovado ─► Delivery
                                                               │                ▲                                              ├─ ajustar ─┘ (loop)
                                                               │                └──────────────────────────────────────────────┘
                                                               ├─ duvida ──► FAQ (agent) ─► Delivery
                                                               └─ revisao_humana ─► Human review (also reached from Quality)
```

| Vertex | What it shows |
| :--- | :--- |
| Consultar_Cliente | An **MCP vertex**: a direct, deterministic call to `lookup_customer` (no AI), using the e-mail that *Receber_Chamado* extracted. The result goes to `workspace/cliente_dados.json`. |
| Agente_Leitor | An agent that reads the ticket and the customer data and writes the summary. Its final answer feeds the triage. |
| Triagem_de_Urgencia | A **switch in "AI model decides" mode**: the model reads the reader's answer and picks `urgente`, `normal`, `duvida` or the default. |
| Agente_Escalonador | An agent that gets an MCP server as **tools** and decides to call `registrar_escalonamento` (it writes `output/escalonamentos.jsonl`). The two ways to use MCP, side by side. |
| Redator → Revisor → Controle_de_Qualidade | A **feedback loop**: the reviewer writes `STATUS: AJUSTAR` or `APROVADO`; a **switch in "keywords" mode** (no AI) sends the writer back until approved. |
| Agente_FAQ | An agent answering from `base/faq.md`. |
| Revisao_Humana | The switch default: when nothing fits, a person takes over. |

**Needs:** a model + its API key, and **Python** (it runs the demo MCP server in `mcp/clientes_server.py`; set `ARESTRIX_PYTHON`
if your command is not `python`). Pick the ticket with `ARESTRIX_EXAMPLE_TICKET=chamado_urgente.txt` (or `_normal`, `_duvida`).

**MCP:** the server is configured in `.workspace/mcps/clientes.json` and each agent lists the servers it may use. The demo
server is fictional (`data/clientes.json`, only `@example.com` addresses). To use your real system, point `command`/`args` at
your MCP server; if it needs credentials, pass them as `"env": {"TOKEN": "${MY_TOKEN}"}`: the value is read from **your**
environment variable and never stored in the file.

**Bring your own agents:** rewrite the skills in `skills/`, edit the switch guidance in the Inspector (the *instruction* field
of **Triagem_de_Urgencia**), change the keywords of **Controle_de_Qualidade**, add cases and branches.
