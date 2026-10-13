#!/usr/bin/env python3
"""Servidor MCP de demonstracao (stdio, JSON-RPC 2.0 por linha): cadastro ficticio de clientes.

Nao usa rede nem segredos. Ferramentas:
  lookup_customer(email)                          consulta data/clientes.json
  registrar_escalonamento(email, resumo, severidade)   grava uma linha em output/escalonamentos.jsonl

Troque este servidor por um MCP real do seu sistema (CRM, helpdesk...) editando .workspace/mcps/clientes.json.
Se o seu servidor precisar de credenciais, passe-as por variaveis de ambiente referenciadas como ${NOME} no "env".
"""
import json
import os
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
TOOLS = [
    {"name": "lookup_customer", "description": "Consulta um cliente pelo e-mail e devolve plano, tempo de casa e chamados abertos.",
     "inputSchema": {"type": "object", "properties": {"email": {"type": "string", "description": "E-mail do cliente"}}, "required": ["email"]}},
    {"name": "registrar_escalonamento", "description": "Registra o escalonamento de um chamado urgente e devolve o numero de protocolo.",
     "inputSchema": {"type": "object", "properties": {
         "email": {"type": "string"}, "resumo": {"type": "string"}, "severidade": {"type": "string", "enum": ["media", "alta", "critica"]}},
         "required": ["email", "resumo"]}},
]


def lookup(args):
    email = str(args.get("email", "")).strip().lower()
    for c in json.loads((BASE / "data" / "clientes.json").read_text(encoding="utf-8"))["clientes"]:
        if c["email"].lower() == email:
            return json.dumps(c, ensure_ascii=False)
    return json.dumps({"encontrado": False, "email": email}, ensure_ascii=False)


def escalate(args):
    out = BASE / "output"
    out.mkdir(exist_ok=True)
    log = out / "escalonamentos.jsonl"
    n = sum(1 for _ in log.open(encoding="utf-8")) + 1 if log.exists() else 1
    protocolo = f"ESC-{n:04d}"
    record = {"protocolo": protocolo, "email": args.get("email", ""), "resumo": args.get("resumo", ""),
              "severidade": args.get("severidade", "alta")}
    with log.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\n")
    return json.dumps({"protocolo": protocolo, "status": "registrado"}, ensure_ascii=False)


def reply(msg_id, result):
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": msg_id, "result": result}) + "\n")
    sys.stdout.flush()


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    msg = json.loads(line)
    method = msg.get("method")
    if method == "initialize":
        reply(msg["id"], {"protocolVersion": "2025-03-26", "capabilities": {"tools": {}},
                          "serverInfo": {"name": "clientes-demo", "version": "1.0"}})
    elif method == "tools/list":
        reply(msg["id"], {"tools": TOOLS})
    elif method == "tools/call":
        name = msg["params"]["name"]
        args = msg["params"].get("arguments") or {}
        try:
            text = {"lookup_customer": lookup, "registrar_escalonamento": escalate}[name](args)
            reply(msg["id"], {"content": [{"type": "text", "text": text}], "isError": False})
        except Exception as exc:  # noqa: BLE001 - o agente recebe o erro como texto
            reply(msg["id"], {"content": [{"type": "text", "text": f"erro: {exc}"}], "isError": True})
