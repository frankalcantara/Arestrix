#!/usr/bin/env python3
"""Servidor LLM falso (API compativel com OpenAI /chat/completions) para testar os projetos de examples/ SEM chaves reais.

Nao e um modelo: segue um roteiro deterministico por papel (identificado por um marcador no prompt de sistema da skill).
Aceita somente a chave de teste "test-key-examples" (nunca uma chave real).

Papeis: leitor, escalonador, redator, revisor, faq, SVG (gerador/auditor) e as decisoes do vertice switch.
Uso: mock_llm_examples.py <porta> <arquivo-de-log>
"""
import http.server
import json
import re
import socketserver
import sys
import threading

PORT = int(sys.argv[1])
LOG = sys.argv[2]
API_KEYS = {"Bearer test-key-examples"}

BAD_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">'
    '<defs><filter id="glow"><feDropShadow dx="0" dy="0" stdDeviation="2"/></filter></defs>'
    '<rect id="box" x="20" y="20" width="120" height="60" rx="8" fill="#6366f1">'
    '<animate id="appear" attributeName="opacity" from="0" to="1" begin="0s" dur="1s" fill="freeze"/></rect>'
    '<text x="30" y="55" fill="#ffffff" font-weight="700" filter="url(#glow)">Passo 1</text></svg>'
)
GOOD_SVG = (
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 200">'
    '<rect id="box" x="20" y="20" width="120" height="60" rx="8" fill="#6366f1">'
    '<animate id="appear" attributeName="opacity" from="0" to="1" begin="0s" dur="1s" fill="freeze"/></rect>'
    '<rect x="150" y="20" width="120" height="60" rx="8" fill="#10b981" opacity="0">'
    '<animate attributeName="opacity" from="0" to="1" begin="appear.end + 0.2s" dur="1s" fill="freeze"/></rect>'
    '<text x="30" y="55" fill="#ffffff" font-weight="700">Passo 1</text></svg>'
)

lock = threading.Lock()
state = {"first_429_done": False}


def log(event):
    with lock:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(event, ensure_ascii=False) + "\n")


def tool_call(call_id, name, args):
    return {"role": "assistant", "content": None, "tool_calls": [
        {"id": call_id, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}]}


def tool_calls(calls):
    return {"role": "assistant", "content": None, "tool_calls": [
        {"id": call_id, "type": "function", "function": {"name": name, "arguments": json.dumps(args)}}
        for call_id, name, args in calls]}


def text(content):
    return {"role": "assistant", "content": content}


def route(user):
    """Decisao do vertice switch (modo IA): le as opcoes e o texto e escolhe um rotulo."""
    options = re.findall(r"^- ([^\n(]+?)(?:\s{2,}\(.*\))?$", user.split("Text:")[0], re.M)
    options = [o.strip() for o in options]
    body = user.split("<<<", 1)[1].rsplit(">>>", 1)[0].lower() if "<<<" in user else user.lower()
    if any(k in body for k in ("fora do ar", "cobrada duas", "cobranca", "cobrança")):
        wanted = "urgente"
    elif any(k in body for k in ("como faco", "como faço", "como exporto", "como posso")):
        wanted = "duvida"
    else:
        wanted = "normal"
    chosen = wanted if wanted in options else (options[-1] if options else "")
    log({"event": "route", "options": options, "chosen": chosen})
    return text(chosen)


def decide(body):
    msgs = body["messages"]
    system = msgs[0]["content"]
    user = msgs[1]["content"]
    tool_msgs = [m for m in msgs if m["role"] == "tool"]
    step = len(tool_msgs)
    it = int((re.search(r"numero (\d+)", user) or [0, "1"])[1])

    if system.startswith("You are the routing step"):
        return route(user)

    if "LEITOR DE CHAMADOS" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/ticket.txt"})
        ticket = tool_msgs[0]["content"]
        email = (re.search(r"[\w.+-]+@[\w.-]+", ticket) or [""])[0]
        subject = (re.search(r"Assunto:\s*(.+)", ticket) or [0, "(sem assunto)"])[1].strip()
        if step == 1:
            return tool_call("c2", "read", {"path": "workspace/cliente_dados.json"})   # gravado pelo vertice MCP
        customer = json.loads(tool_msgs[1]["content"]) if tool_msgs[1]["content"].startswith("{") else {}
        who = f'{customer.get("nome", "cliente desconhecido")} (plano {customer.get("plano", "?")})'
        compact = " ".join(ticket.split())[:400]
        summary = f"Assunto: {subject}. Cliente: {who}. Chamados abertos: {customer.get('chamados_abertos', '?')}. Pedido: {compact}"
        if step == 2:
            return tool_call("c3", "write", {"path": "workspace/resumo.md", "content": f"# Resumo do chamado\n\n{summary}\n\nE-mail: {email}\n"})
        return text(summary)

    if "ESCALONADOR" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/resumo.md"})
        resumo = tool_msgs[0]["content"]
        email = (re.search(r"[\w.+-]+@[\w.-]+", resumo) or [""])[0]
        if step == 1:
            return tool_call("c2", "mcp__clientes__registrar_escalonamento",
                             {"email": email, "resumo": "Servico fora do ar e cobranca duplicada", "severidade": "alta"})
        protocolo = (re.search(r"ESC-\d+", tool_msgs[1]["content"]) or ["ESC-????"])[0]
        if step == 2:
            return tool_call("c3", "write", {"path": "workspace/resposta.md", "content":
                             f"Ola! Pedimos desculpas pelo transtorno. Seu caso foi escalonado com o protocolo {protocolo} e "
                             f"um especialista entrara em contato.\n"})
        return text(f"Escalonamento registrado: {protocolo}.")

    if "REDATOR DE RESPOSTAS" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/resumo.md"})
        if step == 1:
            return tool_call("c2", "read", {"path": "workspace/revisao.txt"})
        revisao = tool_msgs[1]["content"]
        corrigir = "STATUS: AJUSTAR" in revisao
        if step == 2:
            if corrigir:
                content = "Ola! Recebemos seu pedido de troca de plano. Ela vale a partir do proximo ciclo de cobranca. Qualquer duvida, estamos a disposicao.\n"
            else:
                content = "Ola! Sobre a troca de plano: reembolso garantido em 24 horas e desconto de 50% no primeiro mes. Abracos.\n"
            return tool_call("c3", "write", {"path": "workspace/resposta.md", "content": content})
        return text("Correcao aplicada." if corrigir else "Primeira versao escrita.")

    if "REVISOR DE RESPOSTAS" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/resposta.md"})
        resposta = tool_msgs[0]["content"].lower()
        if step == 1:
            if "garantido" in resposta or "desconto" in resposta:
                parecer = "STATUS: AJUSTAR\n- Remover a promessa de reembolso e de desconto, que nao constam do resumo.\n"
            else:
                parecer = "STATUS: APROVADO\n"
            return tool_call("c2", "write", {"path": "workspace/revisao.txt", "content": parecer})
        return text("Revisao registrada.")

    if "ATENDENTE DE FAQ" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/resumo.md"})
        if step == 1:
            return tool_call("c2", "read", {"path": "base/faq.md"})
        if step == 2:
            return tool_call("c3", "write", {"path": "workspace/resposta.md", "content":
                             "Ola! Para exportar os relatorios, abra Relatorios, escolha o mes e clique em Exportar (padrao PDF; CSV para planilha).\n"})
        return text("Usado o item: Exportar relatorios.")

    if "AGENTE COM MEMORIA" in system:
        if step == 0:
            return tool_call("c1", "memory_search", {"query": "REGRA", "limit": 5})
        if step == 1:
            return tool_call("c2", "memory_record_lesson", {"domain": "memoria", "rule":
                             "Licao registrada pelo agente no teste", "trigger": "teste e2e"})
        if step == 2:
            return tool_call("c3", "write", {"path": "workspace/resposta.txt", "content":
                             "A regra aprendida foi aplicada a resposta.\n"})
        return text("OK")

    if "REDATOR DE MATEMATICA" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/fontes.json"})
        if step == 1:
            return tool_call("c2", "memory_search", {"query": "matematica", "limit": 5})
        if step == 2:
            content = (
                "# Teorema de Pitagoras\n\n"
                "Em um triangulo retangulo, a soma dos quadrados dos catetos e igual ao quadrado da hipotenusa.\n\n"
                "## Demonstracao\n\nA demonstracao usa a decomposicao de um quadrado de lado (a+b) de duas formas.\n\n"
                "## Exemplo\n\nSe os catetos medem 3 e 4, a hipotenusa mede 5.\n\n"
                "## Referencias\n\n- Fontes retornadas em workspace/fontes.json.\n"
            )
            return tool_call("c3", "write", {"path": "workspace/texto-matematica.md", "content": content})
        return text("OK")

    if "SVG Animation Architect" in system:
        if step == 0:
            if it >= 2:
                return tool_call("c1", "read", {"path": "workspace/audit_decision.txt"})
            return tool_call("c1", "write", {"path": "workspace/scene.svg", "content": BAD_SVG})
        if step == 1 and it >= 2:
            return tool_call("c2", "write", {"path": "workspace/scene.svg", "content": GOOD_SVG})
        if step == 1:
            return tool_call("c2", "validate_svg", {"path": "workspace/scene.svg"})
        return text("SVG gravado em workspace/scene.svg (execucao %d)." % it)

    if "JEV Visual Auditor" in system:
        if step == 0:
            return tool_call("c1", "read", {"path": "workspace/report_thread_a.txt"})
        if step == 1:
            return tool_call("c2", "read", {"path": "workspace/report_thread_b.txt"})
        if step == 2:
            return tool_call("c3", "read", {"path": "workspace/scene.svg"})
        if step == 3:
            decision = ("STATUS: REPROVADO\nRemover feDropShadow do texto; usar pilula de fundo."
                        if "VIOLACAO" in tool_msgs[0]["content"] else "STATUS: APROVADO")
            return tool_call("c4", "write", {"path": "workspace/audit_decision.txt", "content": decision})
        return text("Decisao registrada em workspace/audit_decision.txt.")

    if "COORDENADOR DE CHAMADAS" in system:
        if step == 0:
            return tool_call("c1", "call_agent", {"target": "especialista", "input": "preciso de um parecer"})
        if step == 1:
            parecer = tool_msgs[0]["content"]
            return tool_call("c2", "write", {"path": "workspace/resposta.txt", "content": parecer})
        return text("Chamada ao especialista concluida.")

    if "ESPECIALISTA DE CHAMADAS" in system:
        return text("Parecer do especialista: aprovado.")

    if "FORM SCHEMA GENERATOR" in system:
        if "INVALID_FORM" in user:
            return text("isto nao e um schema JSON")
        schema = {
            "title": "Confirme os dados do cliente",
            "description": "Confira as informacoes e escolha o tipo de cadastro.",
            "fields": [
                {
                    "id": "tipo",
                    "type": "choice",
                    "label": "Tipo de cadastro",
                    "required": True,
                    "options": [
                        {"value": "pf", "label": "Pessoa fisica"},
                        {"value": "pj", "label": "Pessoa juridica"}
                    ]
                },
                {
                    "id": "obs",
                    "type": "textarea",
                    "label": "Observacao",
                    "required": False,
                    "maxLength": 500
                }
            ]
        }
        return text(json.dumps(schema, ensure_ascii=False))

    if "COORDENADOR PARALELO" in system:
        if step == 0:
            return tool_calls([
                ("c1", "call_agent", {"target": "especialista_a", "input": "avalie o risco"}),
                ("c2", "call_agent", {"target": "especialista_b", "input": "avalie o prazo"})
            ])
        if step == 2:
            def opinion(message):
                try:
                    parsed = json.loads(message["content"])
                    return parsed.get("output", message["content"])
                except Exception:
                    return message["content"]
            content = "Risco: " + opinion(tool_msgs[0]) + "\nPrazo: " + opinion(tool_msgs[1]) + "\n"
            return tool_call("c3", "write", {"path": "workspace/resposta.txt", "content": content})
        return text("Chamadas paralelas concluidas.")

    if "ESPECIALISTA A" in system:
        return text("Parecer A: risco baixo.")

    if "ESPECIALISTA B" in system:
        return text("Parecer B: prazo compativel.")

    if "COORDENADOR SUBFLOW" in system:
        if step == 0:
            return tool_call("c1", "call_agent", {"target": "subflow_especialista", "input": "processe os dados do cliente"})
        if step == 1:
            try:
                output = json.loads(tool_msgs[0]["content"]).get("output", "")
            except Exception:
                output = tool_msgs[0]["content"]
            return tool_call("c2", "write", {"path": "workspace/resposta.txt", "content": "Subflow: " + output + "\n"})
        return text("Subflow chamado e resultado entregue.")

    if "WORKER SUBFLOW" in system:
        return text("Subflow processou: dados do cliente.")

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
            do_429 = not state["first_429_done"]
            state["first_429_done"] = True
        if do_429:
            log({"event": "429"})
            return self._send(429, {"error": {"message": "Rate limit reached"}}, {"Retry-After": "1"})

        msg = decide(body)
        messages = body.get("messages", [])
        user_content = messages[1].get("content", "") if len(messages) > 1 else ""
        log({"event": "chat", "model": body.get("model"), "tools": sorted(t["function"]["name"] for t in body.get("tools", [])),
             "system": messages[0].get("content", "") if messages else "",
             "user_has_acme": "ACME Ltda" in user_content,
             "user_has_subflow_input": "processe os dados do cliente" in user_content,
             "requested_tools": [t["function"]["name"] for t in msg.get("tool_calls", [])],
             "reply": "tool_call:" + msg["tool_calls"][0]["function"]["name"] if msg.get("tool_calls") else "text"})
        self._send(200, {"choices": [{"message": msg, "finish_reason": "tool_calls" if msg.get("tool_calls") else "stop"}],
                         "usage": {"prompt_tokens": 10, "completion_tokens": 10}})


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    srv = Server(("127.0.0.1", PORT), Handler)
    print("ouvindo", flush=True)
    srv.serve_forever()
