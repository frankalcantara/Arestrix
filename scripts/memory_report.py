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
            "COALESCE(SUM(CASE WHEN outcome='success' THEN 1 ELSE 0 END),0), "
            "COALESCE(SUM(CASE WHEN outcome='failure' THEN 1 ELSE 0 END),0) FROM memory_hit"
        ).fetchone()
    return total, per_node, per_mode, hits


def main():
    parser = argparse.ArgumentParser(description="Consumo de API da memória do Arestrix")
    parser.add_argument("root", help="raiz do projeto (ou caminho do memory.db)")
    parser.add_argument("--json", action="store_true", help="saída JSON")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    db = root if root.suffix == ".db" else open_db(root)
    total, per_node, per_mode, hits = fetch(db)

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
            "success": hits[3], "failure": hits[4],
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
    print(f"Memória injetada: {data['hits']['injected']} | usada/consultada: {data['hits']['used']} | "
          f"em sucesso: {data['hits']['success']} | em falha: {data['hits']['failure']}")
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
