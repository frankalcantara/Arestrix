#!/usr/bin/env python3
"""Teste ponta a ponta do servico institucional `arestrix-memory`.

Cobre:
  - HTTP REST com Bearer token e papeis viewer/editor/admin;
  - health sem autenticacao, 401 sem token e 403 para papel insuficiente;
  - escrita de licao, busca de contexto e politica institucional;
  - JSON-RPC do MCP em `/mcp` e no modo stdio (`arestrix-memory mcp`).

Uso: python scripts/e2e_memory_institution.py [caminho-do-arestrix-memory]
"""
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / (
    "arestrix-memory.exe" if os.name == "nt" else "arestrix-memory"
)

failures = []


def check(cond, msg):
    print(("  OK    " if cond else "  FALHA ") + msg)
    if not cond:
        failures.append(msg)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def request(url, method="GET", token=None, body=None):
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        raw = error.read().decode("utf-8")
        try:
            return error.code, json.loads(raw)
        except json.JSONDecodeError:
            return error.code, {"raw": raw}


def request_text(url, token=None):
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, headers=headers, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            return response.status, response.read().decode("utf-8")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode("utf-8")


def wait_health(base):
    for _ in range(40):
        try:
            status, body = request(base + "/health")
            if status == 200 and body.get("ok"):
                return True
        except Exception:
            pass
        time.sleep(0.25)
    return False


def main():
    if not EXE.is_file():
        print(f"[e2e] binario ausente: {EXE}")
        return 1

    generated = subprocess.run(
        [str(EXE), "token", "--principal", "qa", "--role", "editor"],
        capture_output=True, text=True, timeout=30,
    )
    generated_json = json.loads(generated.stdout) if generated.returncode == 0 else {}
    check(len(generated_json.get("token", "")) == 64 and generated_json.get("role") == "editor",
          "token gera segredo de 64 hex e papel valido")

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-institution-"))
    tokens_file = work / "tokens.txt"
    tokens_file.write_text(
        "# token principal role\n"
        "viewer-token ana viewer\n"
        "editor-token bruno editor\n"
        "admin-token carla admin\n",
        encoding="utf-8",
    )
    db = work / "institution.db"
    port = free_port()
    base = f"http://127.0.0.1:{port}"
    proc = subprocess.Popen(
        [
            str(EXE), "serve",
            "--db", str(db),
            "--institution", "acme",
            "--host", "127.0.0.1",
            "--port", str(port),
            "--tokens-file", str(tokens_file),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )

    try:
        check(wait_health(base), "servidor responde /health sem token")

        status, body = request(base + "/v1/status")
        check(status == 401 and "error" in body, "status sem token responde 401")

        status, body = request(base + "/v1/status", token="viewer-token")
        check(status == 200 and body.get("role") == "viewer", "viewer le status e papel")
        check(body.get("institutionId") == "acme", "status identifica a instituicao")

        lesson = {
            "domain": "geral",
            "triggerError": "manual",
            "lessonRule": "REGRA-INSTITUCIONAL-E2E",
        }
        status, _ = request(base + "/v1/lessons", "POST", "viewer-token", lesson)
        check(status == 403, "viewer nao escreve licao")
        status, created = request(base + "/v1/lessons", "POST", "editor-token", lesson)
        check(status == 200 and created.get("id"), "editor escreve licao")
        lesson_id = created.get("id", "")

        status, context = request(
            base + "/v1/context", "POST", "viewer-token",
            {"query": "REGRA-INSTITUCIONAL-E2E", "limit": 5},
        )
        check(status == 200 and "REGRA-INSTITUCIONAL-E2E" in context.get("context", ""),
              "viewer recupera a licao institucional")

        policy = {"key": "retention_days", "value": 30, "immutable": True}
        status, _ = request(base + "/v1/policies", "POST", "editor-token", policy)
        check(status == 403, "editor nao altera politica")
        status, _ = request(base + "/v1/policies", "POST", "admin-token", policy)
        check(status == 200, "admin altera politica")

        status, _ = request(
            base + "/mcp", "POST", None,
            {"jsonrpc": "2.0", "id": 0, "method": "initialize"},
        )
        check(status == 401, "MCP HTTP exige token ate no initialize")

        status, mcp = request(
            base + "/mcp", "POST", "admin-token",
            {"jsonrpc": "2.0", "id": 1, "method": "tools/list"},
        )
        tools = mcp.get("result", {}).get("tools", [])
        check(status == 200 and len(tools) == 4, "MCP HTTP lista as quatro ferramentas")
        names = {tool.get("name") for tool in tools}
        check({"memory_institution_context", "memory_institution_write_lesson",
               "memory_institution_status", "memory_institution_set_policy"} <= names,
              "MCP HTTP expoe contexto, licao, status e politica")

        status, metrics_text = request_text(base + "/metrics")
        check(status == 401, "metricas exigem token")
        status, metrics_text = request_text(base + "/metrics", "viewer-token")
        check(status == 200, "viewer le metricas")
        check("arestrix_memory_http_requests_total" in metrics_text and
              "arestrix_memory_rest_requests_total" in metrics_text and
              "arestrix_memory_mcp_requests_total" in metrics_text and
              "arestrix_memory_auth_failures_total" in metrics_text,
              "metricas expoem contadores REST, MCP e falhas de autenticacao")
        check("arestrix_memory_info{version=" in metrics_text,
              "metricas identificam a versao do servico")

        lines = [
            json.dumps({"jsonrpc": "2.0", "id": 10, "method": "initialize"}),
            json.dumps({"jsonrpc": "2.0", "id": 11, "method": "tools/call",
                        "params": {"name": "memory_institution_status", "arguments": {}}}),
        ]
        result = subprocess.run(
            [str(EXE), "mcp", "--db", str(db), "--institution", "acme",
             "--token", "viewer-token", "--role", "viewer"],
            input="\n".join(lines) + "\n",
            capture_output=True,
            text=True,
            timeout=30,
        )
        responses = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
        check(result.returncode == 0 and len(responses) == 2, "MCP stdio responde initialize e tools/call")
        check(responses[0].get("result", {}).get("serverInfo", {}).get("name") == "arestrix-memory",
              "MCP stdio identifica arestrix-memory")
        status_text = responses[1].get("result", {}).get("content", [{}])[0].get("text", "")
        check('"lessons": 1' in status_text and '"role": "viewer"' in status_text,
              "MCP stdio consulta o status institucional")

        print(f"\n[info] banco temporario: {db}")
        print(f"[info] licao criada: {lesson_id}")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        stderr = proc.stderr.read() if proc.stderr else ""
        if stderr.strip():
            print("[server stderr]", stderr.strip())
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"\n{len(failures)} falha(s).")
        return 1
    print("\nServico institucional executado com sucesso ponta a ponta.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
