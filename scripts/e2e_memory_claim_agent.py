#!/usr/bin/env python3
"""Ponta a ponta do agente de extracao de afirmacoes escolhido no projeto.

Monta um projeto temporario com um agente no pipeline, registra esse agente no
Studio, aponta `memory.claimExtraction.agent` para ele e executa
`arestrix-cli memory claims extract` contra um provedor HTTP falso. Verifica que
o pedido chegou ao agente escolhido e que a proposta foi gravada.

Uso:
  python scripts/e2e_memory_claim_agent.py [--keep]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROJECT_ID = "claim-agent-e2e"
SCOPE_ID = "project:" + PROJECT_ID
NOW = "2026-11-11T00:00:00Z"
TEST_KEY = "test-key-claim-agent"
PASSAGE_ID = "psg_claim_agent_e2e"
PASSAGE_TEXT = "Lia usa tea."

failures: list[str] = []
seen_requests: list[dict] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        candidate = ROOT / name
        if candidate.exists():
            return candidate
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args) -> None:  # silencio no console
        pass

    def do_POST(self) -> None:  # noqa: N802
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) or b"{}"
        try:
            body = json.loads(raw)
        except json.JSONDecodeError:
            body = {}
        seen_requests.append(body)
        content = json.dumps({
            "claims": [{
                "subject": "Lia",
                "predicate": "MENTIONS",
                "object": "tea",
                "quote": "Lia usa tea",
            }]
        }, ensure_ascii=False)
        payload = {
            "choices": [{
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop",
            }],
            "usage": {"prompt_tokens": 12, "completion_tokens": 8},
        }
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def write_project(project: pathlib.Path) -> None:
    manifest = {
        "projectId": PROJECT_ID,
        "name": "Claim Agent E2E",
        "globalDefaults": {
            "memory": {
                "enabled": True,
                "claimExtraction": {
                    "enabled": True,
                    "agent": {
                        "id": "claims",
                        "graph": "pipelines/main.json",
                        "node": "claims",
                    },
                },
            }
        },
    }
    (project / "arestrix.project.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    pipeline = {
        "version": "1.0.0",
        "metadata": {
            "projectId": PROJECT_ID,
            "graphName": "ClaimAgentE2E",
            "globalDefaults": {
                "model": {"provider": "deepseek", "model": "deepseek-chat"},
                "memory": {"enabled": False},
            },
        },
        "nodes": [{
            "id": "claims",
            "label": "Agente de Claims",
            "type": "agent",
            "position": {"x": 0, "y": 0},
            "inputs": [],
            "outputs": [],
            "agentSettings": {
                "inheritModel": False,
                "model": {"provider": "deepseek", "model": "deepseek-chat"},
                "inheritMemory": False,
                "memory": {"enabled": False},
                "inheritCodeMode": False,
                "codeMode": {"allowedTools": [], "customScripts": []},
            },
        }],
        "edges": [],
    }
    pipeline_dir = project / "pipelines"
    pipeline_dir.mkdir()
    (pipeline_dir / "main.json").write_text(
        json.dumps(pipeline, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def seed_knowledge(db_path: pathlib.Path) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
            "VALUES(?,?,?,?,?,?)", (SCOPE_ID, "project", None, PROJECT_ID, NOW, "{}"))
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
            "VALUES(?,?,0)", (SCOPE_ID, SCOPE_ID))
        for node_id, name in (("node-lia", "Lia"), ("node-tea", "tea")):
            conn.execute(
                "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,"
                "aliases_json,created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
                (node_id, SCOPE_ID, "entity", name, "[]", NOW, NOW, "{}"))
            conn.execute("DELETE FROM memory_node_fts WHERE node_id=?", (node_id,))
            conn.execute(
                "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)",
                (node_id, name, "", "{}"))
        conn.execute(
            "INSERT OR REPLACE INTO memory_source(id,scope_id,uri,content_hash,kind,size,"
            "first_seen,last_seen,status) VALUES(?,?,?,?,?,?,?,?,?)",
            ("src-claim-agent", SCOPE_ID, "fixture://claim-agent", sha256_text(PASSAGE_TEXT),
             "text", len(PASSAGE_TEXT), NOW, NOW, "active"))
        conn.execute(
            "INSERT OR REPLACE INTO memory_passage(id,source_id,scope_id,ordinal,start,end,"
            "text,text_hash,tokens,status,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)",
            (PASSAGE_ID, "src-claim-agent", SCOPE_ID, 0, 0, len(PASSAGE_TEXT),
             PASSAGE_TEXT, sha256_text(PASSAGE_TEXT), 4, "active", NOW))
        conn.commit()
    finally:
        conn.close()


def seed_telemetry(db_path: pathlib.Path) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
            "VALUES(?,?,?,?,?,?)", (SCOPE_ID, "project", None, PROJECT_ID, NOW, "{}"))
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
            "VALUES(?,?,0)", (SCOPE_ID, SCOPE_ID))
        conn.execute(
            "INSERT OR REPLACE INTO memory_hit(id,scope_id,run_id,node_id,memory_id,kind,"
            "injected,consulted,used,outcome,ab_arm,ab_repetition,created_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
            ("hit-claim-agent", SCOPE_ID, "run-claim-agent", "claims", PASSAGE_ID,
             "passage", 1, 1, 1, "success", "", 0, NOW))
        conn.execute(
            "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,episode_id,"
            "validator_id,validator_version,ok,score,reasons_json,artifact_hash,ab_arm,"
            "ab_repetition,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,0,?)",
            ("val-claim-agent", SCOPE_ID, "run-claim-agent", "claims", "episode-claim-agent",
             "fixture", "1", 1, 1.0, "[]", None, "", NOW))
        conn.commit()
    finally:
        conn.close()


def run(cli: pathlib.Path, args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(cli), *args], env=env, capture_output=True, text=True,
        encoding="utf-8", errors="replace", timeout=300)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    cli = cli_path()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-claim-agent-"))
    project = work / "projeto"
    project.mkdir()
    server: HTTPServer | None = None
    try:
        write_project(project)
        port = free_port()
        server = HTTPServer(("127.0.0.1", port), MockHandler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        env = {k: v for k, v in os.environ.items()
               if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                            "GROQ_API_KEY", "JEV_API_KEY")}
        env.update({
            "ARESTRIX_HOME": str(ROOT),
            "ARESTRIX_DISABLE_USER_ENV": "1",
            "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
            "DEEPSEEK_API_KEY": TEST_KEY,
        })

        init = run(cli, ["memory", "state", "list", "--root", str(project), "--json"], env)
        if init.returncode != 0:
            raise RuntimeError("falha ao inicializar a memoria: " + (init.stdout or "") +
                               (init.stderr or ""))
        seed_knowledge(project / ".arestrix" / "memory" / "memory.db")
        opened = run(cli, ["memory", "bridges", "detect", "--root", str(project), "--json"], env)
        if opened.returncode != 0:
            raise RuntimeError("falha ao abrir a telemetria: " + (opened.stdout or "") +
                               (opened.stderr or ""))
        seed_telemetry(project / ".arestrix" / "telemetry" / "telemetry.db")

        result = run(cli, ["memory", "claims", "extract", "--root", str(project), "--json"], env)
        output = (result.stdout or "") + (result.stderr or "")
        if result.returncode != 0:
            raise RuntimeError("claims extract falhou: " + output)
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError as exc:
            raise RuntimeError("saida nao e JSON puro: " + output) from exc

        check(payload.get("ok") is True, "claims extract nao retornou ok")
        check(payload.get("candidates") == 1, f"candidatos inesperados: {payload.get('candidates')}")
        check(payload.get("extracted") == 1, f"extraidos inesperados: {payload.get('extracted')}")
        check(payload.get("proposed") == 1, f"propostas inesperadas: {payload.get('proposed')}")
        check(payload.get("failed") == 0, f"falhas inesperadas: {payload.get('failed')}")
        check(payload.get("agent", {}).get("id") == "claims",
              "o agente configurado nao foi o escolhido")

        check(len(seen_requests) == 1, f"pedidos ao agente: {len(seen_requests)}")
        if seen_requests:
            messages = seen_requests[0].get("messages", [])
            system = messages[0].get("content", "") if messages else ""
            user = "\n".join(
                str(message.get("content", "")) for message in messages
                if message.get("role") == "user")
            check("Catalogo:" in system and "Schema:" in system,
                  "o pedido nao levou catalogo/schema ao agente")
            check("Lia usa tea" in user, "o trecho nao chegou ao agente")

        conn = sqlite3.connect(project / ".arestrix" / "memory" / "memory.db")
        try:
            proposals = conn.execute(
                "SELECT kind,payload_json FROM memory_proposal WHERE scope_id=?",
                (SCOPE_ID,)).fetchall()
        finally:
            conn.close()
        check(len(proposals) == 1 and proposals[0][0] == "claim",
              f"proposta de claim ausente: {proposals}")
        if proposals:
            claim = json.loads(proposals[0][1])
            check(claim.get("subject") == "Lia" and claim.get("object") == "tea",
                  f"claim inesperada: {claim}")
            check(claim.get("quote") == "Lia usa tea", "citacao literal perdida")

        print(f"  candidatos={payload.get('candidates')} extraidos={payload.get('extracted')} "
              f"propostas={payload.get('proposed')} falhas={payload.get('failed')}")
        print(f"  agente={payload.get('agent')} pedidos={len(seen_requests)}")
    except Exception as exc:  # noqa: BLE001
        failures.append(str(exc))
    finally:
        if server is not None:
            server.shutdown()
        if args.keep:
            print(f"  diretorio preservado: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

    if failures:
        print("\nFalhas do e2e do agente de claims:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Agente de claims executado ponta a ponta com sucesso.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
