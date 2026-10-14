#!/usr/bin/env python3
"""Servidor MCP de teste (stdio) que expoe o PID do processo e registra cada inicializacao.

Serve para provar que o Arestrix reaproveita UM processo por servidor em vez de abrir um por chamada.
  ARX_MCP_COUNT_FILE   arquivo onde cada inicializacao do processo grava uma linha
Ferramentas: pid() -> PID do processo; echo(text); die() -> encerra o processo sem responder (simula queda do servidor)
"""
import json
import os
import sys

count_file = os.environ.get("ARX_MCP_COUNT_FILE")
if count_file:
    with open(count_file, "a", encoding="utf-8") as f:
        f.write(f"start {os.getpid()}\n")

TOOLS = [
    {"name": "pid", "description": "PID do processo do servidor", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "echo", "description": "Devolve o texto", "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}},
    {"name": "die", "description": "Encerra o servidor sem responder", "inputSchema": {"type": "object", "properties": {}}},
]


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
        reply(msg["id"], {"protocolVersion": "2025-03-26", "capabilities": {"tools": {}}, "serverInfo": {"name": "counting", "version": "1"}})
    elif method == "tools/list":
        reply(msg["id"], {"tools": TOOLS})
    elif method == "tools/call":
        name = msg["params"]["name"]
        args = msg["params"].get("arguments") or {}
        if name == "die":
            os._exit(3)
        text = str(os.getpid()) if name == "pid" else "eco: " + str(args.get("text", ""))
        reply(msg["id"], {"content": [{"type": "text", "text": text}], "isError": False})
