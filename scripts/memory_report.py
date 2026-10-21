#!/usr/bin/env python3
"""Relatório de consumo de API registrado pela memória do Arestrix.

Lê `.arestrix/memory/memory.db` e mostra os tokens e chamadas por agente, além do
tamanho do contexto de memória injetado. Use-o para comparar execuções com a
memória ligada e desligada.

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
            "COALESCE(SUM(memory_context_chars),0), COALESCE(SUM(memory_enabled),0) FROM memory_usage"
        ).fetchone()
        per_node = conn.execute(
            "SELECT COALESCE(node_id,'(sem no)'), COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), "
            "COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0) "
            "FROM memory_usage GROUP BY node_id ORDER BY SUM(total_tokens) DESC"
        ).fetchall()
        per_mode = conn.execute(
            "SELECT memory_enabled, COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), COALESCE(SUM(calls),0), "
            "COALESCE(AVG(latency_ms),0), COALESCE(SUM(memory_context_chars),0) "
            "FROM memory_usage GROUP BY memory_enabled ORDER BY memory_enabled DESC"
        ).fetchall()
    return total, per_node, per_mode


def main():
    parser = argparse.ArgumentParser(description="Consumo de API da memória do Arestrix")
    parser.add_argument("root", help="raiz do projeto (ou caminho do memory.db)")
    parser.add_argument("--json", action="store_true", help="saída JSON")
    args = parser.parse_args()

    root = pathlib.Path(args.root).resolve()
    db = root if root.suffix == ".db" else open_db(root)
    total, per_node, per_mode = fetch(db)

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
        "modes": [
            {
                "memoryEnabled": row[0], "runs": row[1], "promptTokens": row[2],
                "completionTokens": row[3], "totalTokens": row[4],
                "calls": row[5], "avgLatencyMs": row[6], "memoryContextChars": row[7],
            }
            for row in per_mode
        ],
        "nodes": [
            {
                "nodeId": row[0], "runs": row[1], "promptTokens": row[2],
                "completionTokens": row[3], "totalTokens": row[4],
                "calls": row[5], "avgLatencyMs": row[6], "memoryContextChars": row[7],
            }
            for row in per_node
        ],
    }

    if args.json:
        print(json.dumps(data, indent=2, ensure_ascii=False))
        return

    print(f"Banco: {data['database']}")
    print(f"Execuções: {data['runs']} | chamadas: {data['calls']} | "
          f"tokens: {data['totalTokens']} (prompt {data['promptTokens']} / saída {data['completionTokens']})")
    print(f"Contexto de memória: {data['memoryContextChars']} caracteres | "
          f"execuções com memória: {data['memoryEnabledRuns']} | latência média: {data['avgLatencyMs']:.0f} ms")
    print()
    print("Por modo:")
    for m in data["modes"]:
        label = "memória ON " if m["memoryEnabled"] else "memória OFF"
        print(f"  {label}: runs={m['runs']} calls={m['calls']} tokens={m['totalTokens']} "
              f"(prompt {m['promptTokens']} / saída {m['completionTokens']}) ctx={m['memoryContextChars']}")
    print()
    print(f"{'Agente':32} {'runs':>5} {'prompt':>10} {'saída':>10} {'total':>10} {'chamadas':>9} {'ctx':>8}")
    for n in data["nodes"]:
        print(f"{n['nodeId'][:32]:32} {n['runs']:5d} {n['promptTokens']:10d} {n['completionTokens']:10d} "
              f"{n['totalTokens']:10d} {n['calls']:9d} {n['memoryContextChars']:8d}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
