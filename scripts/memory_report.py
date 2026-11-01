#!/usr/bin/env python3
"""Relatório de consumo de API registrado pela memória do Arestrix.

Lê `.arestrix/memory/memory.db` e mostra tokens, chamadas, retries, sucesso/falha,
contexto injetado e memórias injetadas/usadas por agente. Use-o para comparar
execuções com a memória ligada e desligada no mesmo grafo.

Uso:
  python scripts/memory_report.py <raiz-do-projeto>
  python scripts/memory_report.py <raiz-do-projeto> --json
"""
import argparse
import json
import pathlib
import sqlite3
import sys


def open_db(root: pathlib.Path):
    db = root / ".arestrix" / "memory" / "memory.db"
    if not db.exists():
        raise SystemExit(f"Banco de memória não encontrado: {db}")
    return db


def fetch(db: pathlib.Path):
    with sqlite3.connect(db) as conn:
        total = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(completion_tokens),0), "
            "COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), COALESCE(AVG(latency_ms),0), "
            "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(memory_enabled),0), "
            "COALESCE(SUM(retries),0), "
            "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) FROM memory_usage"
        ).fetchone()
        per_node = conn.execute(
            "SELECT COALESCE(node_id,'(sem no)'), COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), "
            "COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0), "
            "COALESCE(SUM(retries),0), "
            "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
            "FROM memory_usage GROUP BY node_id ORDER BY SUM(total_tokens) DESC"
        ).fetchall()
        per_mode = conn.execute(
            "SELECT memory_enabled, COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), "
            "COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0), "
            "COALESCE(SUM(retries),0), "
            "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) "
            "FROM memory_usage GROUP BY memory_enabled ORDER BY memory_enabled DESC"
        ).fetchall()
        hits = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(injected),0), COALESCE(SUM(used),0), "
            "COALESCE(SUM(CASE WHEN used=1 AND injected=0 THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN used=1 AND injected=1 THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) FROM memory_hit"
        ).fetchone()
        try:
            embeddings = conn.execute(
                "SELECT COUNT(*), COUNT(DISTINCT model), COALESCE(SUM(dim),0) FROM memory_embedding"
            ).fetchone()
        except sqlite3.OperationalError:
            embeddings = (0, 0, 0)
    return total, per_node, per_mode, hits, embeddings


def recurrence(db: pathlib.Path, since: str, min_verdict_rate: float,
               min_citation_rate: float, min_executions: int,
               min_distinct_inputs: int, min_reuse_rate: float) -> dict:
    if "telemetry" in db.parts:
        telemetry = db
    elif db.name == "memory.db" and db.parent.name == "memory" and db.parent.parent.name == ".arestrix":
        candidate = db.parent.parent / "telemetry" / "telemetry.db"
        telemetry = candidate if candidate.exists() else db
    else:
        telemetry = db
    conn = sqlite3.connect(f"file:{telemetry}?mode=ro", uri=True)
    try:
        row = conn.execute(
            "SELECT id FROM memory_scope WHERE kind='project' ORDER BY rowid LIMIT 1"
        ).fetchone()
        scope_id = row[0] if row else "project:default"
        verdicts = {}
        for run_id, node_id, ok in conn.execute(
                "SELECT run_id,COALESCE(node_id,''),ok FROM memory_validation "
                "WHERE scope_id=? ORDER BY rowid", (scope_id,)):
            verdicts[(run_id, node_id)] = (True, bool(ok))
        tokens = {
            (run_id, node_id): total
            for run_id, node_id, total in conn.execute(
                "SELECT run_id,COALESCE(node_id,''),COALESCE(SUM(total_tokens),0) "
                "FROM memory_usage WHERE scope_id=? GROUP BY run_id,node_id", (scope_id,))
        }
        citations = {}
        for run_id, node_id, memory_id in conn.execute(
                "SELECT run_id,COALESCE(node_id,''),memory_id FROM memory_hit "
                "WHERE scope_id=? AND used=1 ORDER BY rowid", (scope_id,)):
            citations.setdefault((run_id, node_id), set()).add(memory_id)
        sql = ("SELECT run_id,COALESCE(node_id,''),COALESCE(task_type,''),"
               "COALESCE(input_hash,''),COALESCE(intent_template_hash,'') "
               "FROM memory_episode WHERE scope_id=? AND kind LIKE 'agent_%'")
        params = [scope_id]
        if since:
            sql += " AND created_at>=?"
            params.append(since)
        sql += " ORDER BY rowid"
        episodes = conn.execute(sql, params).fetchall()
    finally:
        conn.close()

    stats = {}
    for run_id, node_id, task_type, input_hash, intent_hash in episodes:
        task_type = task_type or "(sem task_type)"
        row = stats.setdefault(task_type, {
            "executions": 0, "verdicts": 0, "approved": 0, "cited": 0,
            "reused": 0, "tokens": 0, "inputs": set(), "intents": {},
            "seen": set(),
        })
        row["executions"] += 1
        if input_hash:
            row["inputs"].add(input_hash)
        row["intents"][intent_hash] = row["intents"].get(intent_hash, 0) + 1
        row["tokens"] += tokens.get((run_id, node_id), 0)
        verdict = verdicts.get((run_id, node_id))
        if verdict:
            row["verdicts"] += 1
            if verdict[1]:
                row["approved"] += 1
        cited = citations.get((run_id, node_id), set())
        if cited:
            row["cited"] += 1
            if row["seen"].intersection(cited):
                row["reused"] += 1
            row["seen"].update(cited)

    task_types = []
    all_ok = bool(stats)
    verdict_citation_ok = bool(stats)
    for task_type, row in sorted(stats.items()):
        executions = row["executions"]
        verdict_rate = row["verdicts"] / executions if executions else 0.0
        approval_rate = row["approved"] / row["verdicts"] if row["verdicts"] else 0.0
        citation_rate = row["cited"] / executions if executions else 0.0
        reuse_rate = row["reused"] / executions if executions else 0.0
        checks = {
            "verdict": verdict_rate >= min_verdict_rate,
            "citation": citation_rate >= min_citation_rate,
            "recurrence": executions >= min_executions and len(row["inputs"]) >= min_distinct_inputs,
            "reuse": reuse_rate >= min_reuse_rate,
        }
        ok = all(checks.values())
        all_ok = all_ok and ok
        verdict_citation_ok = verdict_citation_ok and checks["verdict"] and checks["citation"]
        task_types.append({
            "taskType": task_type,
            "executions": executions,
            "executionsWithVerdict": row["verdicts"],
            "verdictRate": verdict_rate,
            "approved": row["approved"],
            "approvalRate": approval_rate,
            "distinctInputs": len(row["inputs"]),
            "citedExecutions": row["cited"],
            "citationRate": citation_rate,
            "reusedExecutions": row["reused"],
            "reuseRate": reuse_rate,
            "tokens": row["tokens"],
            "tokensPerExecution": row["tokens"] / executions if executions else 0.0,
            "intentPairs": [
                {"intentTemplateHash": key, "executions": value}
                for key, value in sorted(row["intents"].items())
            ],
            "checks": checks,
            "ok": ok,
        })

    if not stats:
        decision = "insufficient_data"
        reason = "sem episodios de agente no periodo"
    elif all_ok:
        decision = "continue_full"
        reason = "todos os limiares atingidos; o bloco C completo esta justificado"
    elif verdict_citation_ok:
        decision = "continue_base_knowledge"
        reason = "veredito e citacao atingidos, mas recorrencia ou reuso abaixo; siga E11-E14 e adie caminhos"
    else:
        decision = "fix_validators"
        reason = "veredito ou citacao abaixo do limiar; corrija validadores e formato de saida antes de seguir"
    return {
        "scopeId": scope_id,
        "since": since,
        "thresholds": {
            "minVerdictRate": min_verdict_rate,
            "minCitationRate": min_citation_rate,
            "minExecutions": min_executions,
            "minDistinctInputs": min_distinct_inputs,
            "minReuseRate": min_reuse_rate,
        },
        "taskTypes": task_types,
        "taskTypeCount": len(task_types),
        "decision": decision,
        "decisionReason": reason,
        "ok": True,
        "database": str(telemetry),
    }


def main():
    parser = argparse.ArgumentParser(description="Consumo de API da memória do Arestrix")
    parser.add_argument("root", help="raiz do projeto (ou caminho do memory.db)")
    parser.add_argument("--json", action="store_true", help="saída JSON")
    parser.add_argument("--recurrence", action="store_true",
                        help="marco de recorrencia por task_type")
    parser.add_argument("--since", default="", help="inicio ISO (ex.: 2026-10-01T00:00:00Z)")
    parser.add_argument("--min-verdict-rate", type=float, default=0.70)
    parser.add_argument("--min-citation-rate", type=float, default=0.30)
    parser.add_argument("--min-executions", type=int, default=5)
    parser.add_argument("--min-distinct-inputs", type=int, default=3)
    parser.add_argument("--min-reuse-rate", type=float, default=0.20)
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    db = root if root.suffix == ".db" else open_db(root)
    if args.recurrence:
        data = recurrence(db, args.since, args.min_verdict_rate, args.min_citation_rate,
                          args.min_executions, args.min_distinct_inputs, args.min_reuse_rate)
        if args.json:
            print(json.dumps(data, indent=2, ensure_ascii=False))
            return
        print(f"Banco: {data['database']}")
        print(f"Marco de recorrencia: {data['decision']}")
        print(f"Motivo: {data['decisionReason']}")
        for task in data["taskTypes"]:
            print(f"  {task['taskType']}: execucoes={task['executions']} "
                  f"veredito={task['verdictRate']:.0%} aprovacao={task['approvalRate']:.0%} "
                  f"inputs={task['distinctInputs']} citacao={task['citationRate']:.0%} "
                  f"reuso={task['reuseRate']:.0%} tokens/exec={task['tokensPerExecution']:.1f} "
                  f"{'OK' if task['ok'] else 'abaixo'}")
        return
    total, per_node, per_mode, hits, embeddings = fetch(db)

    data = {
        "database": str(db),
        "runs": total[0],
        "promptTokens": total[1],
        "completionTokens": total[2],
        "totalTokens": total[3],
        "calls": total[4],
        "avgLatencyMs": total[5],
        "memoryContextChars": total[6],
        "memoryEnabledRuns": total[7],
        "retries": total[8],
        "successRuns": total[9],
        "failureRuns": total[10],
        "hits": {
            "total": hits[0], "injected": hits[1], "used": hits[2],
            "consulted": hits[3], "usedInjected": hits[4],
            "success": hits[5], "failure": hits[6],
        },
        "embeddings": {
            "vectors": embeddings[0], "models": embeddings[1], "dimensions": embeddings[2],
        },
        "modes": [
            {
                "memoryEnabled": row[0], "runs": row[1], "promptTokens": row[2],
                "completionTokens": row[3], "totalTokens": row[4],
                "calls": row[5], "avgLatencyMs": row[6], "memoryContextChars": row[7],
                "retries": row[8], "successRuns": row[9], "failureRuns": row[10],
            }
            for row in per_mode
        ],
        "nodes": [
            {
                "nodeId": row[0], "runs": row[1], "promptTokens": row[2],
                "completionTokens": row[3], "totalTokens": row[4],
                "calls": row[5], "avgLatencyMs": row[6], "memoryContextChars": row[7],
                "retries": row[8], "successRuns": row[9], "failureRuns": row[10],
            }
            for row in per_node
        ],
    }

    if args.json:
        print(json.dumps(data, indent=2, ensure_ascii=False))
        return

    print(f"Banco: {data['database']}")
    print(f"Execuções: {data['runs']} | sucesso: {data['successRuns']} | falha: {data['failureRuns']} | "
          f"chamadas: {data['calls']} | retries: {data['retries']}")
    print(f"Tokens: {data['totalTokens']} (prompt {data['promptTokens']} / saída {data['completionTokens']})")
    print(f"Contexto de memória: {data['memoryContextChars']} caracteres | "
          f"execuções com memória: {data['memoryEnabledRuns']} | latência média: {data['avgLatencyMs']:.0f} ms")
    print(f"Memória injetada: {data['hits']['injected']} | usada do brief: {data['hits']['usedInjected']} | "
          f"consultada via busca: {data['hits']['consulted']} | "
          f"em sucesso: {data['hits']['success']} | em falha: {data['hits']['failure']}")
    print(f"Embeddings: {data['embeddings']['vectors']} vetores | "
          f"{data['embeddings']['models']} modelo(s) | soma de dimensões: {data['embeddings']['dimensions']}")
    print()
    print("Por modo:")
    for m in data["modes"]:
        label = "memória ON " if m["memoryEnabled"] else "memória OFF"
        print(f"  {label}: runs={m['runs']} ok={m['successRuns']} fail={m['failureRuns']} "
              f"calls={m['calls']} retries={m['retries']} tokens={m['totalTokens']} "
              f"(prompt {m['promptTokens']} / saída {m['completionTokens']}) ctx={m['memoryContextChars']}")
    print()
    print(f"{'Agente':32} {'runs':>5} {'ok':>4} {'fail':>4} {'retry':>5} {'prompt':>10} "
          f"{'saída':>10} {'total':>10} {'chamadas':>9} {'ctx':>8}")
    for n in data["nodes"]:
        print(f"{n['nodeId'][:32]:32} {n['runs']:5d} {n['successRuns']:4d} {n['failureRuns']:4d} "
              f"{n['retries']:5d} {n['promptTokens']:10d} {n['completionTokens']:10d} "
              f"{n['totalTokens']:10d} {n['calls']:9d} {n['memoryContextChars']:8d}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
