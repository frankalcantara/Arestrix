#!/usr/bin/env python3
"""Ponta a ponta do executor A/B da etapa E9.

Monta um projeto temporario, semeia o conhecimento curado, inicia um provedor
LLM deterministico e roda o piloto de 20 tarefas x 3 repeticoes x 2 bracos.
Verifica o relatorio pareado, a marca de braco na telemetria, a injecao do
brief apenas no braco ON e a estabilidade do banco congelado.

Uso:
  python scripts/e2e_memory_ab.py [--keep]
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
import unicodedata
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-ab"
SUITE = ROOT / "tests" / "fixtures" / "memory-ab-v1.json"
PROJECT_ID = "memoria-ab"
SCOPE_ID = "project:" + PROJECT_ID
SEED_NOW = "2026-10-01T00:00:00Z"
TEST_KEY = "test-key-memory-ab"

ARTIFACT = (
    "# Artefato do piloto A/B\n"
    "STATUS_OK\n\n"
    "Este arquivo e produzido pelo vertice Gerador_AB em toda execucao do\n"
    "piloto. O conteudo e deterministico para que o validador de artefato\n"
    "possa aprovar a tarefa sem depender de um provedor real. A memoria do\n"
    "projeto entra apenas no braco ON e o banco congelado nunca e alterado\n"
    "durante a medicao. O texto tem tamanho suficiente para satisfazer a\n"
    "regra de tamanho minimo do vertice.\n"
)

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def normalize_comparable(value: str) -> str:
    folded = unicodedata.normalize("NFKD", value)
    folded = "".join(ch for ch in folded if not unicodedata.combining(ch))
    out: list[str] = []
    space = True
    for ch in folded:
        if ch.isascii() and ch.isalpha():
            ch = ch.lower()
        if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
            out.append(ch)
            space = False
        elif not space:
            out.append(" ")
            space = True
    return "".join(out).strip()


def free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def add_node(conn: sqlite3.Connection, node_id: str, kind: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,"
        "created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
        (node_id, SCOPE_ID, kind, name, "[]", SEED_NOW, SEED_NOW, "{}"),
    )
    conn.execute("DELETE FROM memory_node_fts WHERE node_id=?", (node_id,))
    conn.execute(
        "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)",
        (node_id, name, "", "{}"),
    )
    conn.execute(
        "INSERT OR IGNORE INTO memory_alias(scope_id,alias,node_id,source) VALUES(?,?,?,?)",
        (SCOPE_ID, normalize_comparable(name), node_id, "canonical"),
    )


def add_evidence(conn: sqlite3.Connection, target_kind: str, target_id: str, summary: str) -> None:
    conn.execute(
        "INSERT OR REPLACE INTO memory_evidence(id,scope_id,target_kind,target_id,action,"
        "episode_id,run_id,node_id,input_hash,validator_id,validator_version,ok,"
        "artifact_hash,excerpt_hash,summary,created_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        ("evidence-" + target_id, SCOPE_ID, target_kind, target_id, "create",
         None, None, None, None, "ab-seed", "1", 1, None,
         sha256_text(target_kind + "|" + target_id + "|" + summary), summary, SEED_NOW),
    )


def add_edge(conn: sqlite3.Connection, edge_id: str, src: str, dst: str,
             relation: str, confidence: float, metadata: dict) -> None:
    edge_hash = sha256_text("|".join([SCOPE_ID, src, dst, relation, "", SEED_NOW, ""]))
    conn.execute(
        "INSERT OR REPLACE INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,"
        "valid_from,valid_to,recorded_at,confidence,status,superseded_by,source_kind,"
        "source_ref,evidence_hash,edge_hash,metadata,last_used,success_count,failure_count,"
        "utility,dormant_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (edge_id, SCOPE_ID, src, dst, relation, None, SEED_NOW, None, SEED_NOW,
         confidence, "active", None, "ab_seed", "tests/fixtures/memory-ab",
         None, edge_hash, json.dumps(metadata, ensure_ascii=False), None, 0, 0, 0.5, None),
    )


def seed_knowledge(db_path: pathlib.Path, knowledge: dict) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
            "VALUES(?,?,?,?,?,?)",
            (SCOPE_ID, "project", None, PROJECT_ID, SEED_NOW, "{}"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
            "VALUES(?,?,0)", (SCOPE_ID, SCOPE_ID),
        )
        for lesson in knowledge["lessons"]:
            domain_id = "ab-domain-" + normalize_comparable(lesson["domain"]).replace(" ", "-")
            rule_id = "ab-rule-" + lesson["id"]
            add_node(conn, domain_id, "domain", lesson["domain"])
            add_node(conn, rule_id, "rule", lesson["rule"])
            add_edge(conn, lesson["id"], domain_id, rule_id, "HAS_LESSON",
                     float(lesson["confidence"]),
                     {"domain": lesson["domain"], "triggerError": lesson["trigger"],
                      "lessonRule": lesson["rule"], "sourceItem": "ab_seed"})
            add_evidence(conn, "lesson", lesson["id"],
                         "Licao (" + lesson["domain"] + "): " + lesson["rule"])
        for fact in knowledge.get("facts", []):
            src_id = "ab-entity-" + normalize_comparable(fact["source"]).replace(" ", "-")
            dst_id = "ab-entity-" + normalize_comparable(fact["target"]).replace(" ", "-")
            add_node(conn, src_id, "entity", fact["source"])
            add_node(conn, dst_id, "entity", fact["target"])
            add_edge(conn, fact["id"], src_id, dst_id, fact["relation"],
                     float(fact["confidence"]), {"domain": fact.get("domain", "ab")})
            add_evidence(conn, "fact", fact["id"],
                         fact["relation"] + " " + fact["source"] + " -> " + fact["target"])
        playbook = knowledge["playbook"]
        conn.execute(
            "INSERT OR REPLACE INTO memory_playbook(id,scope_id,task_type,title,steps_json,"
            "preconditions_json,success_count,failure_count,confidence,status,source_ref,"
            "evidence_json,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (playbook["id"], SCOPE_ID, playbook["taskType"], playbook["title"],
             json.dumps(playbook["steps"], ensure_ascii=False),
             json.dumps(playbook.get("preconditions", {}), ensure_ascii=False),
             3, 0, float(playbook["confidence"]), playbook.get("status", "active"),
             "tests/fixtures/memory-ab/knowledge.json",
             json.dumps([{"kind": "curated", "ref": "knowledge.json"}], ensure_ascii=False),
             SEED_NOW, SEED_NOW),
        )
        add_evidence(conn, "playbook", playbook["id"],
                     "Playbook (" + playbook["taskType"] + "): " + playbook["title"])
        conn.commit()
    finally:
        conn.close()


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args) -> None:
        pass

    def do_POST(self) -> None:  # noqa: N802
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        messages = body.get("messages", [])
        system = messages[0].get("content", "") if messages else ""
        if not isinstance(system, str):
            system = ""
        has_tool_result = any(msg.get("role") == "tool" for msg in messages)
        user = ""
        for message in reversed(messages):
            if message.get("role") == "user" and isinstance(message.get("content"), str):
                user = message["content"]
                break

        if not has_tool_result:
            reply = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call_ab_write",
                    "type": "function",
                    "function": {
                        "name": "write",
                        "arguments": json.dumps(
                            {"path": "artefatos/pipeline.md", "content": ARTIFACT},
                            ensure_ascii=False,
                        ),
                    },
                }],
            }
            finish = "tool_calls"
        else:
            reply = {
                "role": "assistant",
                "content": "STATUS: APROVADO. Regra aplicada [MEMO:ab-lesson-1].",
            }
            finish = "stop"

        prompt_chars = sum(len(json.dumps(msg, ensure_ascii=False)) for msg in messages)
        completion_chars = len(json.dumps(reply, ensure_ascii=False))
        payload = {
            "choices": [{"message": reply, "finish_reason": finish}],
            "usage": {"prompt_tokens": max(1, prompt_chars // 4),
                      "completion_tokens": max(1, completion_chars // 4)},
        }
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

        with self.server.log_lock:  # type: ignore[attr-defined]
            self.server.system_prompts.append(system)  # type: ignore[attr-defined]
            self.server.user_prompts.append(user)  # type: ignore[attr-defined]


def run(cli: pathlib.Path, args: list[str], env: dict[str, str], timeout: int = 1800) -> subprocess.CompletedProcess:
    return subprocess.run([str(cli), *args], env=env, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", default=str(ROOT / ("arestrix-cli.exe" if os.name == "nt"
                                                     else "arestrix-cli")))
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-ab-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    shutil.copy2(FIXTURE / "arestrix.project.json", project / "arestrix.project.json")
    graph = work / "pipeline.json"
    shutil.copy2(FIXTURE / "pipeline.json", graph)
    knowledge = json.loads((FIXTURE / "knowledge.json").read_text(encoding="utf-8"))

    port = free_port()
    server = HTTPServer(("127.0.0.1", port), MockHandler)
    server.log_lock = threading.Lock()  # type: ignore[attr-defined]
    server.system_prompts = []  # type: ignore[attr-defined]
    server.user_prompts = []  # type: ignore[attr-defined]
    threading.Thread(target=server.serve_forever, daemon=True).start()

    env = {k: v for k, v in os.environ.items()
           if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                        "GROQ_API_KEY", "JEV_API_KEY")}
    env.update({
        "ARESTRIX_HOME": str(ROOT),
        "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
        "DEEPSEEK_API_KEY": TEST_KEY,
    })

    try:
        init = run(cli, ["memory", "state", "list", "--root", str(project), "--json"], env, 120)
        if init.returncode != 0:
            raise RuntimeError("nao foi possivel inicializar a memoria:\n"
                               + (init.stdout or "") + (init.stderr or ""))
        memory_db = project / ".arestrix" / "memory" / "memory.db"
        seed_knowledge(memory_db, knowledge)
        frozen_before = sha256_file(memory_db)

        result = run(cli, [
            "memory", "ab", "run",
            "--suite", str(SUITE),
            "--pipeline", str(graph),
            "--node", "gerador",
            "--repetitions", "3",
            "--seed", "7",
            "--root", str(project),
            "--json",
        ], env, 1800)
        output = (result.stdout or "") + (result.stderr or "")
        if result.returncode != 0:
            raise RuntimeError(f"executor A/B falhou (codigo {result.returncode}):\n{output}")

        report = json.loads(result.stdout)
        check(report.get("ok") is True, "o relatorio A/B nao ficou ok")
        check(int(report.get("primary", {}).get("n", 0)) == 20,
              "o relatorio nao tem 20 tarefas pareadas")
        check(int(report.get("infraEvents", -1)) == 0,
              "o piloto deterministico nao deveria ter falha de infraestrutura")
        check(report.get("invalidTasks") == [], "nenhuma tarefa deveria ficar sem validador")
        check(report.get("knowledgeUnchanged") is True,
              "o hash do conhecimento congelado mudou durante a medicao")
        check(report.get("frozenFingerprint") == report.get("frozenFingerprintAfter"),
              "fingerprint antes e depois diferem")

        frozen = pathlib.Path(str(report.get("frozenPath", "")))
        check(frozen.is_file(), "o banco congelado nao foi gravado")
        if frozen.is_file():
            check(sha256_file(frozen) == report.get("frozenFingerprint"),
                  "o arquivo congelado mudou depois da medicao")
        check(sha256_file(memory_db) == frozen_before,
              "o banco de conhecimento original mudou durante a medicao")

        measurement = pathlib.Path(str(report.get("measurementDb", "")))
        check(measurement.is_file(), "o banco de medicao nao foi gravado")
        if measurement.is_file():
            conn = sqlite3.connect(f"file:{measurement}?mode=ro", uri=True)
            try:
                arms = dict(conn.execute(
                    "SELECT ab_arm, COUNT(*) FROM memory_usage "
                    "WHERE ab_arm IN ('on','off') GROUP BY ab_arm").fetchall())
                check(arms.get("on") == 60 and arms.get("off") == 60,
                      f"telemetria nao marcou 60/60 execucoes por braco: {arms}")
                validation = conn.execute(
                    "SELECT COUNT(*), COALESCE(SUM(ok),0) FROM memory_validation").fetchone()
                check(int(validation[0]) == 120,
                      "o piloto deveria gravar 120 vereditos de artefato")
                check(int(validation[1]) == 120,
                      "todos os artefatos do piloto deveriam ser aprovados")
                repetitions = dict(conn.execute(
                    "SELECT ab_repetition, COUNT(*) FROM memory_usage "
                    "WHERE ab_arm='on' GROUP BY ab_repetition").fetchall())
                check(sorted(repetitions.values()) == [20, 20, 20],
                      f"as repeticoes nao foram marcadas: {repetitions}")
            finally:
                conn.close()

        recurrence = run(cli, [
            "memory", "report", "--recurrence", "--root", str(project),
            "--db", str(measurement), "--json",
        ], env, 120)
        if recurrence.returncode != 0:
            raise RuntimeError("relatorio de recorrencia falhou:\n"
                               + (recurrence.stdout or "") + (recurrence.stderr or ""))
        marco = json.loads(recurrence.stdout)
        check(marco.get("decision") == "continue_full",
              f"o piloto deveria decidir continue_full, decidiu {marco.get('decision')}")
        check(int(marco.get("taskTypeCount", 0)) == 1, "o piloto deveria ter um task_type")
        task = marco["taskTypes"][0]
        check(int(task.get("executions", 0)) == 120, "o marco deveria somar 120 execucoes")
        check(int(task.get("distinctInputs", 0)) == 20, "o marco deveria ver 20 entradas distintas")
        check(float(task.get("citationRate", 0)) == 0.5, "a citacao deveria ser 50% no piloto")
        check(float(task.get("reuseRate", 0)) > 0.2, "a taxa de reuso deveria passar do limiar")
        check(len(task.get("intentPairs", [])) == 1, "o template unico deveria gerar um par de intencao")

        conn = sqlite3.connect(f"file:{measurement}?mode=ro", uri=True)
        try:
            traces = conn.execute(
                "SELECT trace_id,run_id,node_id,steps_json,delivered_json,knowledge_version "
                "FROM memory_trace ORDER BY rowid").fetchall()
        finally:
            conn.close()
        check(len(traces) == 60, f"o piloto deveria ter 60 tracos ON, tem {len(traces)}")
        if traces:
            trace_id, trace_run, trace_node, steps_json, delivered_json, version = traces[0]
            steps = json.loads(steps_json)
            delivered = json.loads(delivered_json)
            check(int(version) > 0, "o traco nao aponta para a versao do conhecimento")
            check(any(step.get("kind") == "brief" for step in steps),
                  "o traco nao registrou o passo do brief")
            check(any(candidate.get("delivered") for step in steps
                      for candidate in step.get("candidates", [])),
                  "o traco nao marcou nenhum candidato entregue")
            check(any(item == "ab-lesson-1" for item in delivered),
                  "a licao entregue nao chegou ao traco")
            check(all(len(step.get("candidates", [])) <= 50 for step in steps),
                  "um passo excedeu 50 candidatos")
            trace_cli = run(cli, [
                "memory", "trace", "show", trace_run, trace_node,
                "--root", str(project), "--db", str(measurement), "--json",
            ], env, 120)
            check(trace_cli.returncode == 0,
                  "memory trace show falhou: " + (trace_cli.stdout or "") + (trace_cli.stderr or ""))
            if trace_cli.returncode == 0:
                shown = json.loads(trace_cli.stdout)
                check(shown.get("traceId") == trace_id,
                      "memory trace show devolveu outro trace")

        marker = "[DADOS DE MEMORIA DO PROJETO"
        with_memory = sum(1 for prompt in server.system_prompts if marker in prompt)
        without_memory = sum(1 for prompt in server.system_prompts if marker not in prompt)
        check(with_memory == 120, f"o braco ON deveria receber brief nas 120 chamadas, recebeu {with_memory}")
        check(without_memory == 120, f"o braco OFF nao deveria receber brief, recebeu {without_memory}")
        check(any("A/B suite task (data):" in prompt for prompt in server.user_prompts)
              or any("Tarefa do conjunto A/B" in prompt for prompt in server.user_prompts),
              "a entrada da tarefa nao chegou ao agente")

        print(f"  tarefas=20 repeticoes=3 bracos=2 execucoes={report.get('observations')}")
        print(f"  aprovacao ON={report['primary']['on']:.2f} OFF={report['primary']['off']:.2f} "
              f"delta={report['primary']['delta']:.2f} "
              f"IC95=[{report['primary']['ciLow']:.2f},{report['primary']['ciHigh']:.2f}]")
        print(f"  conhecimento congelado inalterado: {report.get('knowledgeUnchanged')}")
    except Exception as exc:  # noqa: BLE001
        failures.append(str(exc))
    finally:
        server.shutdown()
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"  diretorio preservado: {work}")

    if failures:
        print("\nFalhas do e2e A/B:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print("Executor A/B executado com sucesso ponta a ponta.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
