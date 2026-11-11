#!/usr/bin/env python3
"""E2E deterministico de remocao de memoria no meio de uma execucao.

Exercita os cenarios F4-T5 e F4-T10 sem chamar um provedor real:

* a primeira requisicao recebe o brief com uma licao e pede ``memory_search``;
* o resultado da busca entra no historico com o marcador ``[MEMO:...]``;
* durante a segunda requisicao outro processo rejeita a licao e grava a linha
  em ``memory_removal_log``;
* a terceira requisicao nao pode conter a licao no system prompt nem no
  historico, e o trace precisa registrar a reconstrucao e a publicacao de uma
  epoca nova.

Uso:
  python scripts/e2e_memory_removal.py
  python scripts/e2e_memory_removal.py --keep
"""
from __future__ import annotations

import argparse
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
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-deepseek-long"
REPORT = ROOT / "tests" / "fixtures" / "memory-removal-report.json"
PROJECT_ID = "memoria-removal-e2e"
SCOPE_ID = "project:" + PROJECT_ID
LESSON_ID = "deepseek-long-lesson"
LESSON_TEXT = "ORION-42"
FACET_ID = "facet_removal_e2e"
PATH_ID = "path_removal_e2e"
FACET_TEXT = "A faceta do projeto confirma ORION-42."
PATH_TEXT = "O caminho do projeto leva a ORION-42."

sys.path.insert(0, str(ROOT / "scripts"))
import memory_baseline  # noqa: E402


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        path = ROOT / name
        if path.exists():
            return path
    return ROOT / ("arestrix-cli.exe" if sys.platform == "win32" else "arestrix-cli")


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def initialize_project(cli: pathlib.Path, project: pathlib.Path) -> None:
    data = json.loads((FIXTURE / "arestrix.project.json").read_text(encoding="utf-8"))
    data["projectId"] = PROJECT_ID
    (project / "arestrix.project.json").write_text(
        json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result = subprocess.run(
        [str(cli), "memory", "state", "list", "--root", str(project), "--json"],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError("falha ao inicializar o projeto: " + result.stderr[-1000:])


def seed_removal_artifacts(db_path: pathlib.Path) -> None:
    now = "2026-11-06T00:00:00Z"
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,"
            "created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
            ("node_removal_e2e", SCOPE_ID, "entity", "ORION", "[]", now, now, "{}"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_lens(id,scope_id,name,description,version,"
            "created_at,updated_at) VALUES(?,?,?,?,?,?,?)",
            ("lens_removal_e2e", SCOPE_ID, "removal", "Lente do e2e", 1, now, now),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_facet(id,scope_id,entity_id,lens_id,revision,"
            "description,supporting_claims_json,claims_hash,status,embedding_fingerprint,"
            "created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            (FACET_ID, SCOPE_ID, "node_removal_e2e", "lens_removal_e2e", 1,
             FACET_TEXT, json.dumps([LESSON_ID]), "facet-hash", "active", "",
             now, now),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_path_schema(id,scope_id,signature,created_at) "
            "VALUES(?,?,?,?)",
            ("psch_removal_e2e", SCOPE_ID, "removal-signature", now),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path(id,scope_id,revision,schema_id,task_type,"
            "state,intent_sample,access_json,created_at,updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?)",
            (PATH_ID, SCOPE_ID, 1, "psch_removal_e2e", "long_memory", "active",
             "", "{}", now, now),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path_stats(path_id,success,failure,last_tau,"
            "uses,distinct_inputs) VALUES(?,?,?,?,?,?)",
            (PATH_ID, 9, 0, 0, 9, 9),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path_claim(path_id,ordinal,claim_id,"
            "claim_revision) VALUES(?,?,?,?)",
            (PATH_ID, 0, LESSON_ID, ""),
        )
        conn.commit()
    finally:
        conn.close()


def make_graph(path: pathlib.Path) -> None:
    graph = json.loads((FIXTURE / "pipeline.json").read_text(encoding="utf-8"))
    graph["metadata"]["projectId"] = PROJECT_ID
    graph["metadata"]["graphName"] = "MemoriaRemocaoE2E"
    global_memory = graph["metadata"]["globalDefaults"].setdefault("memory", {})
    global_memory["lenses"] = True
    global_memory["pathServing"] = {
        "enabled": True, "minUtility": 0.0, "minUses": 1, "maxPaths": 2,
    }
    node = graph["nodes"][0]
    node["id"] = "agente_removal"
    node["label"] = "Agente_Removal"
    node["outputs"][0]["id"] = "agente_removal_out_0"
    node["inputs"][0]["id"] = "agente_removal_in_0"
    settings = node["agentSettings"]
    settings["model"]["promptTemplate"] = (
        "Consulte a memoria e produza um artefato curto. "
        "Use as ferramentas quando necessario."
    )
    settings["codeMode"]["allowedTools"] = ["write", "memory_search"]
    memory = settings["memory"]
    memory["projectId"] = PROJECT_ID
    memory["theme"] = "long"
    memory["taskType"] = "long_memory"
    memory["contextBudgetTokens"] = 1200
    path.write_text(json.dumps(graph, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")


def reject_artifacts(db_path: pathlib.Path) -> None:
    now = "2026-11-06T00:00:00Z"
    for attempt in range(20):
        try:
            conn = sqlite3.connect(db_path, timeout=10)
            try:
                conn.execute("PRAGMA busy_timeout=10000")
                conn.execute(
                    "UPDATE memory_edge SET status='rejected',retracted_at=? "
                    "WHERE scope_id=? AND id=? AND relation='HAS_LESSON'",
                    (now, SCOPE_ID, LESSON_ID),
                )
                conn.execute(
                    "UPDATE memory_facet SET status='retired',updated_at=? "
                    "WHERE scope_id=? AND id=?",
                    (now, SCOPE_ID, FACET_ID),
                )
                conn.execute(
                    "UPDATE memory_path SET state='retired',updated_at=? "
                    "WHERE scope_id=? AND id=?",
                    (now, SCOPE_ID, PATH_ID),
                )
                for kind, item_id in (("lesson", LESSON_ID),
                                      ("facet", FACET_ID),
                                      ("path", PATH_ID)):
                    conn.execute(
                        "INSERT INTO memory_removal_log"
                        "(scope_id,item_kind,item_id,reason,created_at) VALUES(?,?,?,?,?)",
                        (SCOPE_ID, kind, item_id, "e2e-removal", now),
                    )
                conn.commit()
                return
            finally:
                conn.close()
        except sqlite3.OperationalError:
            if attempt == 19:
                raise
            time.sleep(0.1)


def tool_call(call_id: str, name: str, args: dict) -> dict:
    return {
        "role": "assistant",
        "content": None,
        "tool_calls": [{
            "id": call_id,
            "type": "function",
            "function": {"name": name, "arguments": json.dumps(args)},
        }],
    }


def text(content: str) -> dict:
    return {"role": "assistant", "content": content}


class MockState:
    def __init__(self, db_path: pathlib.Path) -> None:
        self.db_path = db_path
        self.requests: list[dict] = []
        self.lock = threading.Lock()
        self.removed = False

    def decide(self, index: int) -> dict:
        if index == 0:
            return tool_call("c1", "memory_search", {"query": "ORION"})
        if index == 1:
            with self.lock:
                if not self.removed:
                    reject_artifacts(self.db_path)
                    self.removed = True
            return tool_call("c2", "write",
                             {"path": "artefatos/step1.txt", "content": "STEP 1"})
        if index == 2:
            return tool_call("c3", "write",
                             {"path": "artefatos/step2.txt", "content": "STEP 2"})
        return text("FIM")


def run_mock(state: MockState, port: int) -> HTTPServer:
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args) -> None:  # silencio
            pass

        def _send(self, code: int, payload: dict) -> None:
            data = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_POST(self) -> None:  # noqa: N802
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length) or b"{}")
            with state.lock:
                index = len(state.requests)
                state.requests.append(body)
            reply = state.decide(index)
            self._send(200, {
                "id": "mock-removal",
                "object": "chat.completion",
                "model": body.get("model", "deepseek-chat"),
                "choices": [{
                    "index": 0,
                    "message": reply,
                    "finish_reason": "tool_calls" if reply.get("tool_calls") else "stop",
                }],
                "usage": {"prompt_tokens": 100, "completion_tokens": 20,
                          "total_tokens": 120},
            })

    server = HTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def request_text(body: dict) -> str:
    parts = [str(body.get("system", ""))]
    for message in body.get("messages", []):
        content = message.get("content")
        if isinstance(content, str):
            parts.append(content)
        elif isinstance(content, list):
            for item in content:
                if isinstance(item, dict) and isinstance(item.get("text"), str):
                    parts.append(item["text"])
        for tool_call in message.get("tool_calls", []) or []:
            parts.append(json.dumps(tool_call, ensure_ascii=False))
    return "\n".join(parts)


def inspect_trace(telemetry_db: pathlib.Path, memory_db: pathlib.Path) -> dict:
    conn = sqlite3.connect(f"file:{telemetry_db}?mode=ro", uri=True)
    try:
        rebuild = None
        for (steps_json,) in conn.execute("SELECT steps_json FROM memory_trace"):
            for step in json.loads(steps_json or "[]"):
                if isinstance(step, dict) and step.get("kind") == "memory_removal_rebuild":
                    rebuild = step
    finally:
        conn.close()
    conn = sqlite3.connect(f"file:{memory_db}?mode=ro", uri=True)
    try:
        epochs = conn.execute(
            "SELECT epoch,prefix_key,content_hash,item_ids_json FROM memory_prefix_epoch "
            "WHERE scope_id=?", (SCOPE_ID,)
        ).fetchall()
    finally:
        conn.close()
    return {"rebuild": rebuild, "epochs": epochs}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=str(cli_path()))
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--no-write-report", action="store_true")
    args = parser.parse_args()

    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-removal-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    graph = work / "pipeline.json"
    try:
        initialize_project(cli, project)
        make_graph(graph)
        memory_db = project / ".arestrix" / "memory" / "memory.db"
        knowledge = json.loads((FIXTURE / "knowledge.json").read_text(encoding="utf-8"))
        knowledge["scope"] = SCOPE_ID
        old_fixture = memory_baseline.FIXTURE
        old_project = memory_baseline.PROJECT_ID
        old_scope = memory_baseline.SCOPE_ID
        try:
            memory_baseline.FIXTURE = FIXTURE
            memory_baseline.PROJECT_ID = PROJECT_ID
            memory_baseline.SCOPE_ID = SCOPE_ID
            memory_baseline.seed_knowledge(memory_db, knowledge)
        finally:
            memory_baseline.FIXTURE = old_fixture
            memory_baseline.PROJECT_ID = old_project
            memory_baseline.SCOPE_ID = old_scope
        seed_removal_artifacts(memory_db)

        port = free_port()
        state = MockState(memory_db)
        server = run_mock(state, port)
        env = {k: v for k, v in os.environ.items()
               if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                            "GROQ_API_KEY", "JEV_API_KEY")}
        env["DEEPSEEK_BASE_URL"] = f"http://127.0.0.1:{port}"
        env["DEEPSEEK_API_KEY"] = "test-key-memory-removal"
        env["ARESTRIX_HOME"] = str(ROOT)
        try:
            result = memory_baseline.run_pipeline(cli, graph, project, env,
                                                  timeout=args.timeout)
        finally:
            server.shutdown()
            server.server_close()

        telemetry_db = project / ".arestrix" / "telemetry" / "telemetry.db"
        trace = inspect_trace(telemetry_db, memory_db)
        first_request = request_text(state.requests[0]) if state.requests else ""
        later = [request_text(body) for body in state.requests[2:]]
        leaked = [text for text in later
                  if LESSON_ID in text or LESSON_TEXT in text or
                  FACET_ID in text or PATH_ID in text or FACET_TEXT in text or
                  "[MEMO:" + LESSON_ID in text]
        rebuild = trace["rebuild"] or {}
        removed_ids = [item.get("id", "") for item in rebuild.get("removed", [])
                       if isinstance(item, dict)]
        epoch_rows = trace["epochs"]
        checks = {
            "exitZero": result.returncode == 0,
            "atLeastThreeRequests": len(state.requests) >= 3,
            "firstRequestHasLesson": LESSON_ID in first_request,
            "firstRequestHasFacet": FACET_ID in first_request,
            "firstRequestHasPath": PATH_ID in first_request,
            "laterRequestsHaveNoLesson": not leaked,
            "traceHasRebuild": bool(rebuild),
            "rebuildMatchesLesson": LESSON_ID in removed_ids,
            "rebuildMatchesFacet": FACET_ID in removed_ids,
            "rebuildMatchesPath": PATH_ID in removed_ids,
            "rebuildSourceIsBrief": rebuild.get("deliveredIdsSource") == "brief_info",
            "epochPublished": bool(rebuild.get("prefixEpochPublished")),
            "epochRecorded": bool(epoch_rows),
            "epochWithoutLesson": all(LESSON_ID not in (row[3] or "")
                                      for row in epoch_rows),
        }
        report = {
            "project": PROJECT_ID,
            "lessonId": LESSON_ID,
            "requests": len(state.requests),
            "leaks": leaked,
            "rebuild": rebuild,
            "epochs": [{"epoch": row[0], "prefixKey": row[1],
                        "contentHash": row[2], "itemIds": row[3]}
                       for row in epoch_rows],
            "checks": checks,
            "pass": all(checks.values()),
            "stdoutTail": (result.stdout or "")[-2000:],
            "stderrTail": (result.stderr or "")[-2000:],
        }
        if not args.no_write_report:
            REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                              encoding="utf-8")
        print(json.dumps({"pass": report["pass"], "checks": checks,
                          "requests": len(state.requests), "leaks": leaked,
                          "rebuild": rebuild, "epochs": report["epochs"]},
                         ensure_ascii=False, indent=2))
        if args.keep:
            print(f"temporario: {work}")
        return 0 if report["pass"] else 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
