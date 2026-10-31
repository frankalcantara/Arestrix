#!/usr/bin/env python3
"""Linha de base da memoria do Arestrix (etapa E0 do plano do multigrafo).

Cria um projeto temporario a partir de tests/fixtures/memory-baseline, semeia o
banco de conhecimento com o conjunto curado (cinco licoes, tres fatos e um
playbook ativo), executa o grafo N vezes com um provedor LLM deterministico e
resume os contadores de tokens, turnos, retries, brief, hits e linhas por
tabela de memoria e telemetria.

Uso:
  python scripts/memory_baseline.py --write          # (re)grava baseline.json
  python scripts/memory_baseline.py --check          # compara com baseline.json
  python scripts/memory_baseline.py --runs 5 --write # muda o numero de execucoes

O provedor simulado responde pelo papel do vertice (gerador, auditor, consultor)
e usa apenas dados deterministicos. Nenhuma chamada de rede externa acontece.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unicodedata
from http.server import BaseHTTPRequestHandler, HTTPServer

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-baseline"
DEFAULT_BASELINE = FIXTURE / "baseline.json"
PROJECT_ID = "memoria-baseline"
SCOPE_ID = "project:" + PROJECT_ID
FIXTURE_FILES = ("arestrix.project.json", "pipeline.json", "knowledge.json")
SEED_NOW = "2026-10-01T00:00:00Z"
TEST_KEY = "test-key-memory-baseline"

ARTIFACT = (
    "# Artefato da linha de base\n"
    "STATUS_OK\n\n"
    "Este arquivo e produzido pelo vertice Gerador_Baseline em toda execucao da\n"
    "linha de base. Ele existe para que a etapa E1 possa validar as regras de\n"
    "artefato do vertice (tamanho minimo e padrao de conteudo) sem depender de\n"
    "um provedor real. O conteudo e deterministico de proposito: qualquer mudanca\n"
    "na linha de base deve aparecer como diferenca explicita no baseline.json.\n"
)

failure_reasons: list[str] = []


def check(condition: bool, message: str) -> None:
    if not condition:
        failure_reasons.append(message)


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize_comparable(value: str) -> str:
    """Mesma dobra de MemoryStore::normalizeComparable (ASCII, minusculas)."""
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


# ---------------------------------------------------------------------------
# Banco de conhecimento: semeadura deterministica
# ---------------------------------------------------------------------------
def add_node(conn: sqlite3.Connection, node_id: str, kind: str, name: str,
             metadata: dict | None = None) -> None:
    meta = json.dumps(metadata or {}, ensure_ascii=False)
    conn.execute(
        "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,"
        "created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
        (node_id, SCOPE_ID, kind, name, "[]", SEED_NOW, SEED_NOW, meta),
    )
    conn.execute("DELETE FROM memory_node_fts WHERE node_id=?", (node_id,))
    conn.execute(
        "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)",
        (node_id, name, "", meta),
    )
    conn.execute(
        "INSERT OR IGNORE INTO memory_alias(scope_id,alias,node_id,source) VALUES(?,?,?,?)",
        (SCOPE_ID, normalize_comparable(name), node_id, "canonical"),
    )


def add_edge(conn: sqlite3.Connection, edge_id: str, src: str, dst: str,
             relation: str, confidence: float, metadata: dict) -> None:
    value_json = None
    valid_from = SEED_NOW
    valid_to = None
    edge_hash = sha256_text(
        "|".join([SCOPE_ID, src, dst, relation, "", valid_from, ""])
    )
    conn.execute(
        "INSERT OR REPLACE INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,"
        "valid_from,valid_to,recorded_at,confidence,status,superseded_by,source_kind,"
        "source_ref,evidence_hash,edge_hash,metadata,last_used,success_count,failure_count,"
        "utility,dormant_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (
            edge_id, SCOPE_ID, src, dst, relation, value_json, valid_from, valid_to,
            SEED_NOW, confidence, "active", None, "baseline_seed", "tests/fixtures/memory-baseline",
            None, edge_hash, json.dumps(metadata, ensure_ascii=False), None, 0, 0, 0.5, None,
        ),
    )


def seed_knowledge(db_path: pathlib.Path, knowledge: dict) -> dict:
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
            "VALUES(?,?,0)",
            (SCOPE_ID, SCOPE_ID),
        )

        for lesson in knowledge["lessons"]:
            domain_id = "baseline-domain-" + normalize_comparable(lesson["domain"]).replace(" ", "-")
            rule_id = "baseline-rule-" + lesson["id"]
            add_node(conn, domain_id, "domain", lesson["domain"])
            add_node(conn, rule_id, "rule", lesson["rule"])
            add_edge(
                conn, lesson["id"], domain_id, rule_id, "HAS_LESSON",
                float(lesson["confidence"]),
                {
                    "domain": lesson["domain"],
                    "triggerError": lesson["trigger"],
                    "lessonRule": lesson["rule"],
                    "sourceItem": "baseline_seed",
                },
            )

        for fact in knowledge["facts"]:
            src_id = "baseline-entity-" + normalize_comparable(fact["source"]).replace(" ", "-")
            dst_id = "baseline-entity-" + normalize_comparable(fact["target"]).replace(" ", "-")
            add_node(conn, src_id, "entity", fact["source"])
            add_node(conn, dst_id, "entity", fact["target"])
            add_edge(
                conn, fact["id"], src_id, dst_id, fact["relation"],
                float(fact["confidence"]), {"domain": fact["domain"]},
            )

        playbook = knowledge["playbook"]
        conn.execute(
            "INSERT OR REPLACE INTO memory_playbook(id,scope_id,task_type,title,steps_json,"
            "preconditions_json,success_count,failure_count,confidence,status,source_ref,"
            "evidence_json,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (
                playbook["id"], SCOPE_ID, playbook["taskType"], playbook["title"],
                json.dumps(playbook["steps"], ensure_ascii=False),
                json.dumps(playbook.get("preconditions", {}), ensure_ascii=False),
                3, 0, float(playbook["confidence"]), playbook.get("status", "active"),
                "tests/fixtures/memory-baseline/knowledge.json",
                json.dumps([{"kind": "curated", "ref": "knowledge.json"}], ensure_ascii=False),
                SEED_NOW, SEED_NOW,
            ),
        )
        conn.commit()
    finally:
        conn.close()
    return {
        "lessons": len(knowledge["lessons"]),
        "facts": len(knowledge["facts"]),
        "playbooks": 1,
    }


# ---------------------------------------------------------------------------
# Provedor LLM simulado e deterministico
# ---------------------------------------------------------------------------
class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args) -> None:  # silencio no console
        pass

    def do_POST(self) -> None:  # noqa: N802 (API do http.server)
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        messages = body.get("messages", [])
        system = messages[0].get("content", "") if messages else ""
        if not isinstance(system, str):
            system = ""
        match = re.search(r'no "([^"]+)" (?:de um pipeline|of an agent pipeline)', system)
        label = match.group(1) if match else "desconhecido"
        has_tool_result = any(msg.get("role") == "tool" for msg in messages)

        if "Gerador" in label and not has_tool_result:
            reply = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call_baseline_write",
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
        elif "Consultor" in label and not has_tool_result:
            reply = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call_baseline_search",
                    "type": "function",
                    "function": {
                        "name": "memory_search",
                        "arguments": json.dumps({"query": "baseline", "limit": 10}),
                    },
                }],
            }
            finish = "tool_calls"
        elif "Gerador" in label:
            reply = {
                "role": "assistant",
                "content": "Artefato gerado em artefatos/pipeline.md [MEMO:baseline-lesson-1].",
            }
            finish = "stop"
        elif "Auditor" in label:
            reply = {
                "role": "assistant",
                "content": "STATUS: APROVADO. O artefato contem STATUS_OK e tamanho suficiente.",
            }
            finish = "stop"
        elif "Consultor" in label:
            reply = {
                "role": "assistant",
                "content": "Resumo da memoria do tema baseline [MEMO:baseline-lesson-2].",
            }
            finish = "stop"
        else:
            reply = {"role": "assistant", "content": "OK"}
            finish = "stop"

        prompt_chars = sum(len(json.dumps(msg, ensure_ascii=False)) for msg in messages)
        completion_chars = len(json.dumps(reply, ensure_ascii=False))
        payload = {
            "choices": [{"message": reply, "finish_reason": finish}],
            "usage": {
                "prompt_tokens": max(1, prompt_chars // 4),
                "completion_tokens": max(1, completion_chars // 4),
            },
        }
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

        with self.server.log_lock:  # type: ignore[attr-defined]
            self.server.system_prompts.append(system)  # type: ignore[attr-defined]


# ---------------------------------------------------------------------------
# Execucao
# ---------------------------------------------------------------------------
def initialize_project(cli: pathlib.Path, project: pathlib.Path) -> None:
    shutil.copy2(FIXTURE / "arestrix.project.json", project / "arestrix.project.json")
    result = subprocess.run(
        [str(cli), "memory", "state", "list", "--root", str(project), "--json"],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "nao foi possivel inicializar o banco de memoria: "
            + (result.stdout or "") + (result.stderr or "")
        )


def table_counts(path: pathlib.Path) -> dict[str, int]:
    if not path.exists():
        return {}
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        names = [
            row[0]
            for row in conn.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'memory_%' "
                "ORDER BY name"
            )
        ]
        counts: dict[str, int] = {}
        for name in names:
            if name.startswith("memory_node_fts_"):
                continue  # tabelas internas do FTS5
            counts[name] = conn.execute(f'SELECT COUNT(*) FROM "{name}"').fetchone()[0]
        return counts
    finally:
        conn.close()


def schema_fingerprint(path: pathlib.Path) -> str:
    if not path.exists():
        return ""
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        rows = conn.execute(
            "SELECT type,name,tbl_name,COALESCE(sql,'') FROM sqlite_master "
            "WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name"
        ).fetchall()
        return sha256_text(json.dumps(rows, ensure_ascii=False))[:16]
    finally:
        conn.close()


def scalar(conn: sqlite3.Connection, sql: str, params: tuple = ()) -> int:
    row = conn.execute(sql, params).fetchone()
    return int(row[0] or 0) if row else 0


def table_block(rows: dict[str, int], seeded: dict[str, int]) -> dict[str, dict[str, int]]:
    return {
        "rows": rows,
        "seeded": {name: int(seeded.get(name, 0)) for name in rows},
        "created": {name: int(rows[name] - seeded.get(name, 0)) for name in rows},
    }


def collect_metrics(project: pathlib.Path, runs: int, elapsed_seconds: float,
                    seeded_knowledge: dict[str, int],
                    seeded_telemetry: dict[str, int]) -> dict:
    memory_db = project / ".arestrix" / "memory" / "memory.db"
    telemetry_db = project / ".arestrix" / "telemetry" / "telemetry.db"
    knowledge = table_counts(memory_db)
    telemetry = table_counts(telemetry_db)

    conn = sqlite3.connect(f"file:{telemetry_db}?mode=ro", uri=True)
    try:
        usage = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(calls),0), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), "
            "COALESCE(SUM(cached_tokens),0), COALESCE(SUM(cache_write_tokens),0), "
            "COALESCE(SUM(retries),0), COALESCE(SUM(memory_context_chars),0), "
            "COALESCE(SUM(outcome='success'),0), COALESCE(SUM(outcome='failure'),0), "
            "COALESCE(SUM(invalid_citations),0) "
            "FROM memory_usage"
        ).fetchone()
        hits = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(injected),0), COALESCE(SUM(consulted),0), "
            "COALESCE(SUM(used),0), "
            "COALESCE(SUM(CASE WHEN used=1 AND injected=1 THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN used=1 AND consulted=1 AND injected=0 THEN 1 ELSE 0 END),0) "
            "FROM memory_hit"
        ).fetchone()
        episodes = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(kind='agent_success'),0), "
            "COALESCE(SUM(kind='agent_failure'),0) FROM memory_episode"
        ).fetchone()
    finally:
        conn.close()

    agent_runs = int(usage[0] or 0)
    calls = int(usage[1] or 0)
    brief_chars = int(usage[8] or 0)
    return {
        "runs": runs,
        "elapsedSeconds": round(elapsed_seconds, 3),
        "tokens": {
            "prompt": int(usage[2] or 0),
            "completion": int(usage[3] or 0),
            "total": int(usage[4] or 0),
            "cached": int(usage[5] or 0),
            "cacheWrite": int(usage[6] or 0),
        },
        "turns": {
            "modelCalls": calls,
            "agentRuns": agent_runs,
            "averageTurnsPerAgentRun": round(calls / agent_runs, 4) if agent_runs else 0.0,
        },
        "retries": int(usage[7] or 0),
        "brief": {
            "chars": brief_chars,
            "averageCharsPerAgentRun": round(brief_chars / agent_runs, 4) if agent_runs else 0.0,
        },
        "hits": {
            "rows": int(hits[0] or 0),
            "injected": int(hits[1] or 0),
            "consulted": int(hits[2] or 0),
            "used": int(hits[3] or 0),
            "citedFromBrief": int(hits[4] or 0),
            "citedFromSearch": int(hits[5] or 0),
            "usedFromBrief": int(hits[4] or 0),  # compatibilidade
            "cited": int(hits[3] or 0),
        },
        "invalidCitations": int(usage[11] or 0),
        "episodes": {
            "rows": int(episodes[0] or 0),
            "success": int(episodes[1] or 0),
            "failure": int(episodes[2] or 0),
        },
        "usage": {
            "rows": agent_runs,
            "success": int(usage[9] or 0),
            "failure": int(usage[10] or 0),
        },
        "tables": {
            "knowledge": table_block(knowledge, seeded_knowledge),
            "telemetry": table_block(telemetry, seeded_telemetry),
        },
        "schemaFingerprint": {
            "knowledge": schema_fingerprint(memory_db),
            "telemetry": schema_fingerprint(telemetry_db),
        },
    }


def run_pipeline(cli: pathlib.Path, graph: pathlib.Path, project: pathlib.Path,
                 env: dict[str, str], timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(cli), "run", str(graph), "--root", str(project)],
        env=env, capture_output=True, text=True, encoding="utf-8", errors="replace",
        timeout=timeout,
    )


def run_baseline(cli: pathlib.Path, runs: int, keep: bool) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-baseline-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    graph = work / "pipeline.json"
    shutil.copy2(FIXTURE / "pipeline.json", graph)
    knowledge = json.loads((FIXTURE / "knowledge.json").read_text(encoding="utf-8"))

    port = free_port()
    server = HTTPServer(("127.0.0.1", port), MockHandler)
    server.log_lock = threading.Lock()  # type: ignore[attr-defined]
    server.system_prompts = []  # type: ignore[attr-defined]
    threading.Thread(target=server.serve_forever, daemon=True).start()

    env = {k: v for k, v in os.environ.items()
           if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                        "GROQ_API_KEY", "JEV_API_KEY")}
    env.update({
        "ARESTRIX_HOME": str(ROOT),
        "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
        "DEEPSEEK_API_KEY": TEST_KEY,
    })

    started = time.monotonic()
    try:
        initialize_project(cli, project)
        memory_db = project / ".arestrix" / "memory" / "memory.db"
        telemetry_db = project / ".arestrix" / "telemetry" / "telemetry.db"
        seeded = seed_knowledge(memory_db, knowledge)
        seeded_knowledge = table_counts(memory_db)
        seeded_telemetry = table_counts(telemetry_db)
        print(f"  conjunto curado: {seeded['lessons']} licoes, {seeded['facts']} fatos, "
              f"{seeded['playbooks']} playbook")
        for index in range(1, runs + 1):
            result = run_pipeline(cli, graph, project, env, timeout=900)
            output = (result.stdout or "") + (result.stderr or "")
            if result.returncode != 0:
                raise RuntimeError(f"execucao {index} falhou (codigo {result.returncode}):\n{output}")
            if "[MEMORIA] contexto injetado" not in output:
                raise RuntimeError(f"execucao {index} nao injetou contexto de memoria:\n{output}")
            if not (project / "artefatos" / "pipeline.md").exists():
                raise RuntimeError(f"execucao {index} nao gerou o artefato do gerador")
            print(f"  execucao {index}/{runs} concluida")
        elapsed = time.monotonic() - started

        prompts = "\n".join(server.system_prompts)  # type: ignore[attr-defined]
        check("baseline-lesson-1" in prompts, "a licao baseline-lesson-1 nao entrou no brief")
        check("baseline-lesson-5" not in prompts,
              "a licao de outro tema (baseline-lesson-5) entrou no brief")
        check("baseline-playbook-1" in prompts, "o playbook ativo nao entrou no brief")
        check(TEST_KEY not in prompts, "a chave de teste apareceu no system prompt")

        metrics = collect_metrics(project, runs, elapsed, seeded_knowledge, seeded_telemetry)
        fixture_material = "".join(
            (FIXTURE / name).read_text(encoding="utf-8") for name in FIXTURE_FILES
        )
        baseline = {
            "schema": 1,
            "stage": "E0",
            "version": (ROOT / "version.txt").read_text(encoding="utf-8").strip(),
            "projectId": PROJECT_ID,
            "fixtureHash": sha256_text(fixture_material),
            "fixtureFiles": {name: sha256_file(FIXTURE / name) for name in FIXTURE_FILES},
            "tolerance": {"default": 0, "metrics": {}},
            "metrics": metrics,
        }
        return baseline, (work if keep else None)
    finally:
        server.shutdown()
        if not keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"  diretorio preservado: {work}")


# ---------------------------------------------------------------------------
# Comparacao
# ---------------------------------------------------------------------------
def flatten(value, prefix: str = "") -> dict[str, object]:
    out: dict[str, object] = {}
    if isinstance(value, dict):
        for key, item in value.items():
            out.update(flatten(item, f"{prefix}.{key}" if prefix else key))
    elif isinstance(value, list):
        out[prefix] = json.dumps(value, ensure_ascii=False, sort_keys=True)
    else:
        out[prefix] = value
    return out


def compare(baseline: dict, current: dict) -> list[str]:
    problems: list[str] = []
    expected = flatten(baseline)
    actual = flatten(current)
    tolerance = baseline.get("tolerance", {})
    default_tolerance = float(tolerance.get("default", 0))
    per_metric = tolerance.get("metrics", {})

    for key in sorted(set(expected) | set(actual)):
        if key.startswith("metrics.elapsedSeconds"):
            continue  # tempo nao entra na comparacao
        if key not in actual:
            problems.append(f"metrica ausente na execucao atual: {key} (esperado {expected[key]!r})")
            continue
        if key not in expected:
            problems.append(f"metrica nova sem referencia: {key} = {actual[key]!r}")
            continue
        want, got = expected[key], actual[key]
        if isinstance(want, (int, float)) and isinstance(got, (int, float)) \
                and not isinstance(want, bool) and not isinstance(got, bool):
            allowed = float(per_metric.get(key, default_tolerance))
            if abs(float(got) - float(want)) > allowed:
                problems.append(f"{key}: esperado {want}, obtido {got} (tolerancia {allowed})")
        elif want != got:
            problems.append(f"{key}: esperado {want!r}, obtido {got!r}")
    return problems


def summary(metrics: dict) -> str:
    hits = metrics["hits"]
    return (
        f"execucoes={metrics['runs']} "
        f"tokens={metrics['tokens']['total']} "
        f"chamadas={metrics['turns']['modelCalls']} "
        f"brief_chars={metrics['brief']['chars']} "
        f"hits(inj={hits['injected']}, cons={hits['consulted']}, "
        f"cit_brief={hits['citedFromBrief']}, cit_busca={hits['citedFromSearch']}, "
        f"inv={metrics['invalidCitations']})"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", default=str(ROOT / ("arestrix-cli.exe" if os.name == "nt"
                                                     else "arestrix-cli")))
    parser.add_argument("--runs", type=int, default=3, help="execucoes do grafo (padrao: 3)")
    parser.add_argument("--write", action="store_true", help="grava baseline.json")
    parser.add_argument("--check", action="store_true", help="compara com baseline.json")
    parser.add_argument("--keep", action="store_true", help="preserva o projeto temporario")
    args = parser.parse_args()

    if args.write and args.check:
        print("use --write ou --check, nao os dois", file=sys.stderr)
        return 2
    if not args.write and not args.check:
        print("informe --write (grava a referencia) ou --check (compara com ela)",
              file=sys.stderr)
        return 2
    if args.runs < 1:
        print("--runs deve ser >= 1", file=sys.stderr)
        return 2
    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2

    print(f"Linha de base da memoria: {args.runs} execucao(oes) de {FIXTURE / 'pipeline.json'}")
    baseline, kept = run_baseline(cli, args.runs, args.keep)
    if failure_reasons:
        print("\nVerificacoes de sanidade falharam:", file=sys.stderr)
        for reason in failure_reasons:
            print(f"  - {reason}", file=sys.stderr)
        if kept:
            print(f"  projeto preservado em {kept}", file=sys.stderr)
        return 1

    print(f"  {summary(baseline['metrics'])}")

    if args.write:
        if DEFAULT_BASELINE.exists():
            old = json.loads(DEFAULT_BASELINE.read_text(encoding="utf-8"))
            problems = compare(old, baseline)
            if problems:
                print("\nMudancas explicitas em relacao a linha de base anterior:")
                for problem in problems:
                    print(f"  ~ {problem}")
        DEFAULT_BASELINE.write_text(
            json.dumps(baseline, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
        print(f"  gravado em {DEFAULT_BASELINE.relative_to(ROOT)}")
        return 0

    if not DEFAULT_BASELINE.exists():
        print(f"linha de base ausente: {DEFAULT_BASELINE}", file=sys.stderr)
        return 1
    previous = json.loads(DEFAULT_BASELINE.read_text(encoding="utf-8"))
    if int(previous.get("metrics", {}).get("runs", -1)) != args.runs:
        print(f"numero de execucoes difere: baseline={previous['metrics'].get('runs')} "
              f"atual={args.runs}", file=sys.stderr)
        return 1
    problems = compare(previous, baseline)
    if problems:
        print("\nLinha de base NAO reproduzida:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        if kept:
            print(f"  projeto preservado em {kept}", file=sys.stderr)
        return 1
    print("  linha de base reproduzida (todos os contadores dentro da tolerancia)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
