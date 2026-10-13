#!/usr/bin/env python3
"""Servidor LLM falso (API compativel com OpenAI /chat/completions) para o teste ponta a ponta
do tutorial do agente de SVG. Nao e um modelo: segue um roteiro deterministico.

Roteiro
  * 1a requisicao de todas: responde 429 + Retry-After (exercita a retentativa do runtime).
  * Gerador ("SVG Animation Architect"):
        execucao 1 -> escreve um SVG COM feDropShadow no texto (viola a regra tipografica);
        execucao 2+ -> le o parecer, escreve um SVG limpo.
  * Auditor ("JEV Visual Auditor"): le os relatorios das threads A/B e grava audit_decision.txt
        (REPROVADO se a Thread A acusou violacao, APROVADO caso contrario).

Uso: mock_llm_server.py <porta> <arquivo-de-log>
"""
import http.server
import json
import re
import socketserver
import sys
import threading

PORT = int(sys.argv[1])
LOG = sys.argv[2]
API_KEYS = {"Bearer test-key-deepseek", "Bearer test-key-jev"}

BAD_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">'
    '<defs><filter id="glow"><feDropShadow dx="0" dy="0" stdDeviation="2"/></filter></defs>'
    '<rect id="box" x="20" y="20" width="120" height="60" rx="8" fill="#6366f1">'
    '<animate id="appear" attributeName="opacity" from="0" to="1" begin="0s" dur="1s" fill="freeze"/></rect>'
    '<text x="30" y="55" fill="#ffffff" font-weight="700" filter="url(#glow)">Etapa 1</text>'
    '</svg>'
)
GOOD_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">'
    '<rect id="box" x="20" y="20" width="120" height="60" rx="8" fill="#6366f1">'
    '<animate id="appear" attributeName="opacity" from="0" to="1" begin="0s" dur="1s" fill="freeze"/></rect>'
    '<rect x="150" y="20" width="120" height="60" rx="8" fill="#10b981" opacity="0">'
    '<animate attributeName="opacity" from="0" to="1" begin="appear.end + 0.2s" dur="1s" fill="freeze"/></rect>'
    '<text x="30" y="55" fill="#ffffff" font-weight="700">Etapa 1</text>'
    '</svg>'
)

lock = threading.Lock()
state = {"requests": 0, "first_429_done": False}


def log(event):
    with lock:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(event, ensure_ascii=False) + "\n")


def tool_call(call_id, name, args):
    return {"role": "assistant", "content": None, "tool_calls": [
        {"id": call_id, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}]}


def text(content, reasoning=None):
    m = {"role": "assistant", "content": content}
    if reasoning:
        m["reasoning_content"] = reasoning
    return m


def decide(body):
    msgs = body["messages"]
    system = msgs[0]["content"]
    user = msgs[1]["content"]
    tool_msgs = [m for m in msgs if m["role"] == "tool"]
    step = len(tool_msgs)
    it = int((re.search(r"numero (\d+)", user) or [0, "1"])[1])

    if "SVG Animation Architect" in system:
        if step == 0:
            if it >= 2:
                return tool_call("c1", "read", {"path": "workspace/audit_decision.txt"})
            return tool_call("c1", "write", {"path": "workspace/scene.svg", "content": BAD_SVG})
        if step == 1 and it >= 2:
            return tool_call("c2", "write", {"path": "workspace/scene.svg", "content": GOOD_SVG})
        if step == 1:
            return tool_call("c2", "validate_svg", {"path": "workspace/scene.svg"})
        return text("SVG gravado em workspace/scene.svg (execucao %d)." % it,
                    reasoning="Planejando a timeline SMIL e a paleta antes de escrever o arquivo.")

    if "JEV Visual Auditor" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/report_thread_a.txt"})
        if step == 1:
            return tool_call("c2", "read", {"path": "workspace/report_thread_b.txt"})
        if step == 2:
            return tool_call("c3", "read", {"path": "workspace/scene.svg"})
        if step == 3:
            report_a = tool_msgs[0]["content"]
            if "VIOLACAO" in report_a:
                decision = "STATUS: REPROVADO\nRemover feDropShadow do texto; usar pilula de fundo."
            else:
                decision = "STATUS: APROVADO"
            return tool_call("c4", "write", {"path": "workspace/audit_decision.txt", "content": decision})
        return text("Decisao registrada em workspace/audit_decision.txt.")

    return text("Sem roteiro para este agente.")


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, payload, headers=None):
        data = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        if self.path != "/chat/completions":
            return self._send(404, {"error": "rota desconhecida " + self.path})
        if self.headers.get("Authorization") not in API_KEYS:
            log({"event": "401"})
            return self._send(401, {"error": "chave invalida"})

        with lock:
            state["requests"] += 1
            do_429 = not state["first_429_done"]
            state["first_429_done"] = True
        if do_429:
            log({"event": "429"})
            return self._send(429, {"error": {"message": "Rate limit reached"}}, {"Retry-After": "1"})

        msg = decide(body)
        log({"event": "chat", "model": body.get("model"), "tools": [t["function"]["name"] for t in body.get("tools", [])],
             "reply": "tool_call:" + msg["tool_calls"][0]["function"]["name"] if msg.get("tool_calls") else "text",
             "has_reasoning_in_history": any("reasoning_content" in m for m in body["messages"])})
        self._send(200, {
            "id": "mock", "object": "chat.completion", "model": body.get("model"),
            "choices": [{"index": 0, "message": msg, "finish_reason": "tool_calls" if msg.get("tool_calls") else "stop"}],
            "usage": {"prompt_tokens": 100, "completion_tokens": 20, "total_tokens": 120}})


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    srv = Server(("127.0.0.1", PORT), Handler)
    print("mock-llm ouvindo em", PORT, flush=True)
    srv.serve_forever()
