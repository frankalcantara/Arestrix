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
LESSON_ID = "deepseek-long-lesson"
COMPACTION = {
    "enabled": True,
    "keepRecentMessages": 4,
    "minChars": 200,
    "maxSummaryChars": 1200,
    "maxSummaryTokens": 300,
    "maxItems": 12,
}
PREFIX_EPOCHS = {
    "enabled": True,
    "cacheTtlSeconds": 0,
    "maxPending": 1,
    "minIntervalSeconds": 0,
}

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
                "totalTokens": 0, "briefChars": 0, "cachedTokens": 0,
                "cacheWriteTokens": 0, "prefixHashes": [], "prefixHashCount": 0,
                "prefixHashesByNode": {}, "prefixIdentitiesByNode": {},
                "prefixEpochsByNode": {}, "injected": 0, "consulted": 0,
                "used": 0, "episodes": 0}
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    try:
        usage = conn.execute(
            "SELECT COALESCE(SUM(calls),0),COALESCE(SUM(prompt_tokens),0),"
            "COALESCE(SUM(completion_tokens),0),COALESCE(SUM(total_tokens),0),"
            "COALESCE(SUM(memory_context_chars),0),COALESCE(SUM(cached_tokens),0),"
            "COALESCE(SUM(cache_write_tokens),0) FROM memory_usage"
        ).fetchone()
        hits = conn.execute(
            "SELECT COALESCE(SUM(injected),0),COALESCE(SUM(consulted),0),"
            "COALESCE(SUM(used),0) FROM memory_hit"
        ).fetchone()
        episodes = conn.execute("SELECT COUNT(*) FROM memory_episode").fetchone()[0]
        compaction = {}
        try:
            row = conn.execute(
                "SELECT metadata FROM memory_episode ORDER BY created_at DESC LIMIT 1"
            ).fetchone()
            if row and row[0]:
                compaction = json.loads(row[0]).get("contextCompaction", {}) or {}
        except (sqlite3.Error, json.JSONDecodeError):
            compaction = {}
        compaction_trace = None
        prefix_hashes = []
        prefix_hashes_by_node = {}
        prefix_identities_by_node = {}
        prefix_epochs_by_node = {}
        try:
            for node_id, steps_json in conn.execute(
                    "SELECT node_id,steps_json FROM memory_trace"):
                for step in json.loads(steps_json or "[]"):
                    if not isinstance(step, dict):
                        continue
                    if step.get("kind") == "context_compaction":
                        compaction_trace = step
                    if step.get("kind") == "prefix":
                        prefix_hash = step.get("prefixHash", "")
                        if prefix_hash:
                            prefix_hashes.append(prefix_hash)
                            prefix_hashes_by_node.setdefault(node_id, [])
                            if prefix_hash not in prefix_hashes_by_node[node_id]:
                                prefix_hashes_by_node[node_id].append(prefix_hash)
                        prefix_identity = step.get("prefixIdentity", "")
                        if prefix_identity:
                            prefix_identities_by_node.setdefault(node_id, [])
                            if prefix_identity not in prefix_identities_by_node[node_id]:
                                prefix_identities_by_node[node_id].append(prefix_identity)
                        prefix_epochs_by_node.setdefault(node_id, set()).add(
                            int(step.get("prefixEpoch", 0) or 0))
        except (sqlite3.Error, json.JSONDecodeError):
            compaction_trace = None
            prefix_hashes = []
            prefix_hashes_by_node = {}
            prefix_identities_by_node = {}
            prefix_epochs_by_node = {}
        return {
            "calls": int(usage[0] or 0),
            "promptTokens": int(usage[1] or 0),
            "completionTokens": int(usage[2] or 0),
            "totalTokens": int(usage[3] or 0),
            "briefChars": int(usage[4] or 0),
            "cachedTokens": int(usage[5] or 0),
            "cacheWriteTokens": int(usage[6] or 0),
            "prefixHashes": prefix_hashes,
            "prefixHashCount": len(prefix_hashes),
            "prefixHashesByNode": prefix_hashes_by_node,
            "prefixIdentitiesByNode": prefix_identities_by_node,
            "prefixEpochsByNode": {
                node: sorted(epochs) for node, epochs in prefix_epochs_by_node.items()},
            "injected": int(hits[0] or 0),
            "consulted": int(hits[1] or 0),
            "used": int(hits[2] or 0),
            "episodes": int(episodes or 0),
            "contextCompaction": compaction,
            "contextCompactionTrace": compaction_trace,
        }
    finally:
        conn.close()


def run_arm(cli: pathlib.Path, arm: str, disabled: bool, keep: bool,
            timeout: int, compaction: dict | None = None,
            prefix_epochs: dict | None = None) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"arestrix-deepseek-{arm}-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    graph = work / "pipeline.json"
    graph_data = json.loads((FIXTURE / "pipeline.json").read_text(encoding="utf-8"))
    if compaction:
        for node in graph_data.get("nodes", []):
            if node.get("type") != "agent":
                continue
            memory = node.setdefault("agentSettings", {}).setdefault("memory", {})
            memory["contextCompaction"] = compaction
            break
    if prefix_epochs:
        for node in graph_data.get("nodes", []):
            if node.get("type") != "agent":
                continue
            memory = node.setdefault("agentSettings", {}).setdefault("memory", {})
            memory["prefixEpochs"] = prefix_epochs
            break
    graph.write_text(json.dumps(graph_data, ensure_ascii=False, indent=2), encoding="utf-8")
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
    parser.add_argument("--max-total-tokens", type=int, default=45000,
                        help="aborta depois da execucao se ON+OFF+COMPACTED ultrapassar este teto")
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--with-epochs", action="store_true",
                        help="executa um quarto braco com memory.prefixEpochs e compara cache/prefix_hash")
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
    compacted, compacted_dir = run_arm(cli, "compacted", False, args.keep,
                                       args.timeout, COMPACTION)
    epochs = None
    epochs_dir = None
    if args.with_epochs:
        epochs, epochs_dir = run_arm(cli, "epochs", False, args.keep,
                                     args.timeout, prefix_epochs=PREFIX_EPOCHS)
    total_tokens = (on["usage"]["totalTokens"] + off["usage"]["totalTokens"] +
                    compacted["usage"]["totalTokens"] +
                    (epochs["usage"]["totalTokens"] if epochs else 0))
    on_prompt = on["usage"]["promptTokens"]
    compacted_prompt = compacted["usage"]["promptTokens"]
    compaction_info = compacted["usage"].get("contextCompaction") or {}
    compaction_trace = compacted["usage"].get("contextCompactionTrace")
    checks = {
        "onExitZero": on["exitCode"] == 0,
        "offExitZero": off["exitCode"] == 0,
        "compactedExitZero": compacted["exitCode"] == 0,
        "onHasSixSteps": on["stepsWritten"] >= 6,
        "offHasSixSteps": off["stepsWritten"] >= 6,
        "compactedHasSixSteps": compacted["stepsWritten"] >= 6,
        "onLongEnough": on["usage"]["calls"] >= 6,
        "compactedLongEnough": compacted["usage"]["calls"] >= 6,
        "onBriefInjected": on["usage"]["briefChars"] > 0,
        "offBriefEmpty": off["usage"]["briefChars"] == 0,
        "compactedBriefInjected": compacted["usage"]["briefChars"] > 0,
        "onHasCode": EXPECTED_CODE in on["final"],
        "offHasNoCode": EXPECTED_CODE not in off["final"],
        "compactedHasCode": EXPECTED_CODE in compacted["final"],
        "compactedApplied": bool(compaction_info.get("applied")),
        "compactedTurns": int(compaction_info.get("turnsCompacted", 0)) > 0,
        "compactedMemoryIds": LESSON_ID in (compaction_info.get("memoryIds") or []),
        "compactedTraceRecordsCompaction": bool(
            compaction_trace and "fallbackReason" in compaction_trace
            and "charsBefore" in compaction_trace),
        "compactedPromptReduced": 0 < compacted_prompt < on_prompt,
        "withinTokenBudget": total_tokens <= args.max_total_tokens,
    }
    cache_ratio = None
    if epochs:
        checks["epochsExitZero"] = epochs["exitCode"] == 0
        checks["epochsHasCode"] = EXPECTED_CODE in epochs["final"]
        checks["epochsHasPrefixObservations"] = bool(epochs["usage"]["prefixHashCount"])
        checks["epochsOneHashPerNode"] = all(
            len(hashes) <= 1
            for hashes in (epochs["usage"].get("prefixHashesByNode") or {}).values())
        checks["epochsOneIdentityPerNode"] = all(
            len(identities) <= 1
            for identities in (epochs["usage"].get("prefixIdentitiesByNode") or {}).values())
        on_cached = int(on["usage"].get("cachedTokens", 0))
        epochs_cached = int(epochs["usage"].get("cachedTokens", 0))
        cache_ratio = (round(epochs_cached / on_cached, 6)
                       if on_cached else None)
    report = {
        "provider": "deepseek",
        "model": "deepseek-chat",
        "fixture": str(FIXTURE.relative_to(ROOT)),
        "expectedCode": EXPECTED_CODE,
        "maxTotalTokens": args.max_total_tokens,
        "totalTokens": total_tokens,
        "promptTokens": {
            "on": on_prompt,
            "compacted": compacted_prompt,
            "reductionPercent": round(
                100.0 * (on_prompt - compacted_prompt) / on_prompt, 2) if on_prompt else 0.0,
        },
        "compactionConfig": COMPACTION,
        "prefixEpochsConfig": PREFIX_EPOCHS if epochs else None,
        "epochsCacheRatio": cache_ratio,
        "checks": checks,
        "pass": all(checks.values()),
        "on": on,
        "off": off,
        "compacted": compacted,
        "epochs": epochs,
    }
    if not args.no_write_report:
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
    print(json.dumps({"pass": report["pass"], "checks": checks,
                      "tokens": total_tokens,
                      "promptTokens": report["promptTokens"],
                      "compaction": compaction_info,
                      "epochsCacheRatio": cache_ratio,
                      "onFinal": on["final"], "offFinal": off["final"],
                      "compactedFinal": compacted["final"],
                      "epochsFinal": epochs["final"] if epochs else None},
                     ensure_ascii=False, indent=2))
    if args.keep:
        if on_dir:
            print(f"ON temporario: {on_dir}")
        if off_dir:
            print(f"OFF temporario: {off_dir}")
        if compacted_dir:
            print(f"COMPACTED temporario: {compacted_dir}")
        if epochs_dir:
            print(f"EPOCHS temporario: {epochs_dir}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
