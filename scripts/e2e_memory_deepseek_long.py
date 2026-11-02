#!/usr/bin/env python3
"""Teste real e controlado da memoria com DeepSeek.

Executa o mesmo grafo duas vezes, com a memoria ligada e desligada, usando a
API real da DeepSeek. O agente faz seis gravacoes, uma busca de memoria e uma
resposta final; o teste mede chamadas, tokens, brief, hits e o codigo final.

O teste NAO roda no CI normal e exige DEEPSEEK_API_KEY. Ele tem um teto de
tokens para evitar consumo inesperado da conta.

Uso:
  python scripts/e2e_memory_deepseek_long.py
  python scripts/e2e_memory_deepseek_long.py --keep
  python scripts/e2e_memory_deepseek_long.py --max-total-tokens 30000
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-deepseek-long"
REPORT = ROOT / "tests" / "fixtures" / "memory-deepseek-long-report.json"
EXPECTED_CODE = "ORION-42"

sys.path.insert(0, str(ROOT / "scripts"))
import memory_baseline  # noqa: E402


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        path = ROOT / name
        if path.exists():
            return path
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def read_usage(project: pathlib.Path) -> dict:
    db = project / ".arestrix" / "telemetry" / "telemetry.db"
    if not db.exists():
        return {"calls": 0, "promptTokens": 0, "completionTokens": 0,
                "totalTokens": 0, "briefChars": 0, "injected": 0, "consulted": 0,
                "used": 0, "episodes": 0}
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    try:
        usage = conn.execute(
            "SELECT COALESCE(SUM(calls),0),COALESCE(SUM(prompt_tokens),0),"
            "COALESCE(SUM(completion_tokens),0),COALESCE(SUM(total_tokens),0),"
            "COALESCE(SUM(memory_context_chars),0) FROM memory_usage"
        ).fetchone()
        hits = conn.execute(
            "SELECT COALESCE(SUM(injected),0),COALESCE(SUM(consulted),0),"
            "COALESCE(SUM(used),0) FROM memory_hit"
        ).fetchone()
        episodes = conn.execute("SELECT COUNT(*) FROM memory_episode").fetchone()[0]
        return {
            "calls": int(usage[0] or 0),
            "promptTokens": int(usage[1] or 0),
            "completionTokens": int(usage[2] or 0),
            "totalTokens": int(usage[3] or 0),
            "briefChars": int(usage[4] or 0),
            "injected": int(hits[0] or 0),
            "consulted": int(hits[1] or 0),
            "used": int(hits[2] or 0),
            "episodes": int(episodes or 0),
        }
    finally:
        conn.close()


def run_arm(cli: pathlib.Path, arm: str, disabled: bool, keep: bool,
            timeout: int) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"arestrix-deepseek-{arm}-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    graph = work / "pipeline.json"
    shutil.copy2(FIXTURE / "pipeline.json", graph)
    old_fixture = memory_baseline.FIXTURE
    old_project = memory_baseline.PROJECT_ID
    old_scope = memory_baseline.SCOPE_ID
    try:
        memory_baseline.FIXTURE = FIXTURE
        memory_baseline.PROJECT_ID = "memoria-deepseek-long"
        memory_baseline.SCOPE_ID = "project:" + memory_baseline.PROJECT_ID
        memory_baseline.initialize_project(cli, project)
        memory_db = project / ".arestrix" / "memory" / "memory.db"
        knowledge = json.loads((FIXTURE / "knowledge.json").read_text(encoding="utf-8"))
        memory_baseline.seed_knowledge(memory_db, knowledge)

        env = {k: v for k, v in os.environ.items()
               if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                            "GROQ_API_KEY", "JEV_API_KEY")}
        env["ARESTRIX_HOME"] = str(ROOT)
        if disabled:
            env["ARESTRIX_MEMORY_DISABLE"] = "1"
        else:
            env.pop("ARESTRIX_MEMORY_DISABLE", None)

        started = time.monotonic()
        result = memory_baseline.run_pipeline(cli, graph, project, env, timeout=timeout)
        elapsed = time.monotonic() - started
        final_path = project / "artefatos" / "final.txt"
        final_text = ""
        if final_path.exists():
            final_text = final_path.read_text(encoding="utf-8", errors="replace").strip()
        steps = sorted(project.glob("artefatos/step*.txt"))
        usage = read_usage(project)
        report = {
            "arm": arm,
            "disabled": disabled,
            "exitCode": result.returncode,
            "elapsedSeconds": round(elapsed, 2),
            "final": final_text,
            "stepsWritten": len(steps),
            "ok": result.returncode == 0,
            "usage": usage,
            "stdoutTail": (result.stdout or "")[-2000:],
            "stderrTail": (result.stderr or "")[-2000:],
        }
        return report, (work if keep else None)
    finally:
        memory_baseline.FIXTURE = old_fixture
        memory_baseline.PROJECT_ID = old_project
        memory_baseline.SCOPE_ID = old_scope
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", default=str(cli_path()))
    parser.add_argument("--max-total-tokens", type=int, default=30000,
                        help="aborta depois da execucao se ON+OFF ultrapassar este teto")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--no-write-report", action="store_true")
    args = parser.parse_args()

    if not os.environ.get("DEEPSEEK_API_KEY"):
        print("DEEPSEEK_API_KEY ausente; teste real ignorado.", file=sys.stderr)
        return 2
    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2

    print(f"DeepSeek real: {FIXTURE / 'pipeline.json'}")
    on, on_dir = run_arm(cli, "on", False, args.keep, args.timeout)
    off, off_dir = run_arm(cli, "off", True, args.keep, args.timeout)
    total_tokens = on["usage"]["totalTokens"] + off["usage"]["totalTokens"]
    checks = {
        "onExitZero": on["exitCode"] == 0,
        "offExitZero": off["exitCode"] == 0,
        "onHasSixSteps": on["stepsWritten"] >= 6,
        "offHasSixSteps": off["stepsWritten"] >= 6,
        "onLongEnough": on["usage"]["calls"] >= 6,
        "onBriefInjected": on["usage"]["briefChars"] > 0,
        "offBriefEmpty": off["usage"]["briefChars"] == 0,
        "onHasCode": EXPECTED_CODE in on["final"],
        "offHasNoCode": EXPECTED_CODE not in off["final"],
        "withinTokenBudget": total_tokens <= args.max_total_tokens,
    }
    report = {
        "provider": "deepseek",
        "model": "deepseek-chat",
        "fixture": str(FIXTURE.relative_to(ROOT)),
        "expectedCode": EXPECTED_CODE,
        "maxTotalTokens": args.max_total_tokens,
        "totalTokens": total_tokens,
        "checks": checks,
        "pass": all(checks.values()),
        "on": on,
        "off": off,
    }
    if not args.no_write_report:
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
    print(json.dumps({"pass": report["pass"], "checks": checks,
                      "tokens": total_tokens,
                      "onFinal": on["final"], "offFinal": off["final"]},
                     ensure_ascii=False, indent=2))
    if args.keep:
        if on_dir:
            print(f"ON temporario: {on_dir}")
        if off_dir:
            print(f"OFF temporario: {off_dir}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
