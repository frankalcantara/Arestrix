#!/usr/bin/env python3
"""Servidor MCP minimo (stdio, JSON-RPC 2.0 por linha) usado nos testes de C++."""
import json
import sys


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
                          "serverInfo": {"name": "fake", "version": "1"}})
    elif method == "tools/list":
        reply(msg["id"], {"tools": [{"name": "echo", "description": "Devolve o texto",
                                     "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}}]})
    elif method == "tools/call":
        text = msg["params"]["arguments"].get("text", "")
        reply(msg["id"], {"content": [{"type": "text", "text": "eco: " + text}], "isError": False})
