#!/usr/bin/env python3
"""Servidor LLM falso para o teste do Assistente do Studio. Fala os dois protocolos que o ModelClient usa:
OpenAI (/chat/completions) e Anthropic (/v1/messages). Nao e um modelo: segue um roteiro fixo.

  * Sem resultado de ferramenta no historico -> pede add_node (agente "Pesquisador").
  * Com resultado de ferramenta -> responde com texto final citando o id criado.
Exige a chave de teste; sem ela responde 401. Grava cada requisicao (JSON por linha) no arquivo de log.

Uso: mock_assistant_llm.py <porta> <arquivo-de-log>
"""
import http.server
import json
import socketserver
import sys

PORT = int(sys.argv[1])
LOG = sys.argv[2]
KEY = "test-key-assistant"


def last_tool_result(body, anthropic):
    for m in reversed(body.get("messages", [])):
        if not anthropic and m.get("role") == "tool":
            return m.get("content", "")
        if anthropic and m.get("role") == "user" and isinstance(m.get("content"), list):
            for block in m["content"]:
                if block.get("type") == "tool_result":
                    return block.get("content", "")
    return None


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, payload):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        self._send(200, {"ok": True})

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
        anthropic = self.path.endswith("/v1/messages")
        auth = self.headers.get("x-api-key") if anthropic else (self.headers.get("Authorization") or "").replace("Bearer ", "")
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps({"path": self.path, "body": body}) + "\n")
        if auth != KEY:
            return self._send(401, {"error": "bad key"})

        result = last_tool_result(body, anthropic)
        if result is None:
            args = {"type": "agent", "label": "Pesquisador"}
            if anthropic:
                return self._send(200, {"stop_reason": "tool_use", "usage": {"input_tokens": 10, "output_tokens": 5},
                                        "content": [{"type": "tool_use", "id": "tu1", "name": "add_node", "input": args}]})
            return self._send(200, {"usage": {"prompt_tokens": 10, "completion_tokens": 5},
                                    "choices": [{"finish_reason": "tool_calls", "message": {
                                        "role": "assistant", "content": None,
                                        "tool_calls": [{"id": "call1", "type": "function",
                                                        "function": {"name": "add_node", "arguments": json.dumps(args)}}]}}]})
        node_id = json.loads(result).get("id", "?") if result.startswith("{") else "?"
        text = "Criei o vertice " + node_id
        if anthropic:
            return self._send(200, {"stop_reason": "end_turn", "usage": {"input_tokens": 20, "output_tokens": 6},
                                    "content": [{"type": "text", "text": text}]})
        return self._send(200, {"usage": {"prompt_tokens": 20, "completion_tokens": 6},
                                "choices": [{"finish_reason": "stop", "message": {"role": "assistant", "content": text}}]})


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


Server(("127.0.0.1", PORT), Handler).serve_forever()
