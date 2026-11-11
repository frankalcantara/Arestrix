#!/usr/bin/env python3
"""Executa o piloto A/B v2 contra um provedor real.

Diferente de scripts/e2e_memory_ab.py, que usa um provedor deterministico para
validar o harness no CI, este script envia as 20 tarefas do conjunto
tests/fixtures/memory-ab-v1.json ao provedor real configurado no ambiente
(DeepSeek por padrao). O banco de conhecimento e congelado antes da medicao e o
braco OFF nao recebe brief; o braco ON recebe o brief curado.

O resultado e gravado em tests/fixtures/memory-ab-live-report.json e as
observacoes brutas ficam em tests/fixtures/memory-ab-live/.

Uso:
  python scripts/e2e_memory_ab_live.py
  python scripts/e2e_memory_ab_live.py --repetitions 1          # ensaio curto
  python scripts/e2e_memory_ab_live.py --model deepseek-chat --keep
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
from collections import Counter
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from e2e_memory_ab import (  # noqa: E402
    FIXTURE,
    PROJECT_ID,
    SUITE,
    seed_knowledge,
    sha256_file,
)

REPORT_BASE = "memory-ab-live"


def report_paths(gate_mode: str, memory_dependent: bool,
                 gate_provider: str = "") -> tuple[pathlib.Path, pathlib.Path]:
    base = REPORT_BASE + ("-value" if memory_dependent else "")
    if gate_mode != "off":
        base += f"-gate-{gate_mode}"
        if gate_provider:
            base += f"-{gate_provider}"
    return ROOT / "tests" / "fixtures" / f"{base}-report.json", ROOT / "tests" / "fixtures" / base

PROMPT_TEMPLATE = (
    "Gere o artefato da tarefa e escreva em artefatos/pipeline.md. "
    "O arquivo deve ter no minimo 400 bytes e conter o marcador STATUS_OK."
)

VALUE_PROMPT_TEMPLATE = (
    "Gere o artefato da tarefa e escreva em artefatos/pipeline.md com no minimo 400 bytes. "
    "Inclua o marcador STATUS_OK e o codigo de conformidade do projeto."
)

VALUE_LESSON = {
    "id": "ab-lesson-token",
    "domain": "ab",
    "trigger": "conformidade",
    "rule": "Inclua o codigo de conformidade AB-7731 no artefato.",
    "confidence": 0.97,
}


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        candidate = ROOT / name
        if candidate.is_file():
            return candidate
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def run(cli: pathlib.Path, args: list[str], env: dict[str, str],
        timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run([str(cli), *args], env=env, capture_output=True,
                          text=True, encoding="utf-8", errors="replace",
                          timeout=timeout)


def live_pipeline(model: str, memory_dependent: bool = False,
                  gate: dict | None = None) -> dict:
    data = json.loads((FIXTURE / "pipeline.json").read_text(encoding="utf-8"))
    for node in data.get("nodes", []):
        if node.get("type") != "agent":
            continue
        model_settings = node.setdefault("agentSettings", {}).setdefault("model", {})
        model_settings["model"] = model
        model_settings["promptTemplate"] = (
            VALUE_PROMPT_TEMPLATE if memory_dependent else PROMPT_TEMPLATE)
        if gate:
            memory_settings = node.setdefault("agentSettings", {}).setdefault("memory", {})
            memory_settings["gate"] = gate
        if memory_dependent:
            for rule in node.get("validationRules", []):
                rule["contentPattern"] = "AB-7731"
    return data


def arm_metrics(measurement: pathlib.Path) -> dict:
    conn = sqlite3.connect(f"file:{measurement}?mode=ro", uri=True)
    try:
        usage = {}
        for arm, rows, prompt, completion, total, latency, chars, cached, cache_write in conn.execute(
            "SELECT ab_arm, COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), "
            "COALESCE(AVG(latency_ms),0), COALESCE(AVG(memory_context_chars),0), "
            "COALESCE(SUM(cached_tokens),0), COALESCE(SUM(cache_write_tokens),0) "
            "FROM memory_usage WHERE ab_arm IN ('on','off') GROUP BY ab_arm"
        ):
            usage[arm] = {
                "executions": int(rows),
                "promptTokens": int(prompt),
                "completionTokens": int(completion),
                "totalTokens": int(total),
                "cachedTokens": int(cached),
                "cacheWriteTokens": int(cache_write),
                "avgLatencyMs": round(float(latency), 2),
                "avgMemoryContextChars": round(float(chars), 2),
            }
        validation = {}
        for arm, rows, ok in conn.execute(
            "SELECT ab_arm, COUNT(*), COALESCE(SUM(ok),0) FROM memory_validation "
            "WHERE ab_arm IN ('on','off') GROUP BY ab_arm"
        ):
            validation[arm] = {"verdicts": int(rows), "approved": int(ok)}
        errors = {}
        for arm, error_class, rows in conn.execute(
            "SELECT ab_arm, error_class, COUNT(*) FROM memory_episode "
            "WHERE ab_arm IN ('on','off') AND error_class IS NOT NULL "
            "AND error_class <> '' GROUP BY ab_arm, error_class"
        ):
            errors.setdefault(arm, {})[error_class] = int(rows)
        return {"usage": usage, "validation": validation, "errors": errors}
    finally:
        conn.close()


def gate_metrics(measurement: pathlib.Path) -> dict:
    conn = sqlite3.connect(f"file:{measurement}?mode=ro", uri=True)
    try:
        decisions = Counter()
        sources = Counter()
        in_tokens = 0
        out_tokens = 0
        cached_tokens = 0
        latencies = []
        for (steps_json,) in conn.execute("SELECT steps_json FROM memory_trace"):
            for step in json.loads(steps_json):
                if step.get("kind") != "memory_gate":
                    continue
                decision = step.get("decision") or {}
                decisions[str(decision.get("useMemory", ""))] += 1
                sources[str(decision.get("source", ""))] += 1
                in_tokens += int(decision.get("modelInTokens", 0) or 0)
                out_tokens += int(decision.get("modelOutTokens", 0) or 0)
                cached_tokens += int(decision.get("modelCachedTokens", 0) or 0)
                latencies.append(int(decision.get("latencyMs", 0) or 0))
        return {
            "decisions": dict(decisions),
            "sources": dict(sources),
            "modelInTokens": in_tokens,
            "modelOutTokens": out_tokens,
            "modelCachedTokens": cached_tokens,
            "avgLatencyMs": round(sum(latencies) / len(latencies), 1) if latencies else 0,
            "maxLatencyMs": max(latencies) if latencies else 0,
        }
    finally:
        conn.close()


def copy_evidence(keys: list[str], target: pathlib.Path) -> dict:
    target.mkdir(parents=True, exist_ok=True)
    for old in target.glob("observations-*.json"):
        old.unlink()
    for old in target.glob("measurement-*.db"):
        old.unlink()
    copied = {}
    for key in keys:
        source = pathlib.Path(key)
        if source.is_file():
            destination = target / source.name
            shutil.copy2(source, destination)
            copied[source.name] = str(destination.relative_to(ROOT)).replace("\\", "/")
    return copied


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", default=str(cli_path()))
    parser.add_argument("--model", default="deepseek-chat")
    parser.add_argument("--provider", default="deepseek",
                        help="rotulo do provedor no relatorio; o pipeline usa deepseek")
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--memory-dependent", action="store_true",
                        help="validador exige o token AB-7731, presente apenas no conhecimento curado")
    parser.add_argument("--gate-mode", choices=["off", "shadow", "enforce"], default="off",
                        help="porteiro de memoria: off, shadow (registra) ou enforce")
    parser.add_argument("--gate-provider", default="",
                        help="provedor do gate (vazio = deterministico; ex.: jev, local, deepseek)")
    parser.add_argument("--gate-model", default="",
                        help="modelo do gate (ex.: system-one, deepseek-chat)")
    parser.add_argument("--gate-max-searches", type=int, default=2,
                        help="teto de memory_search quando o gate decide buscar")
    parser.add_argument("--timeout", type=int, default=7200,
                        help="teto total do subprocesso do executor A/B")
    parser.add_argument("--max-total-tokens", type=int, default=400000,
                        help="marca o relatorio como reprovado acima deste teto de tokens")
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--no-write-report", action="store_true")
    args = parser.parse_args()

    if not os.environ.get("DEEPSEEK_API_KEY"):
        print("DEEPSEEK_API_KEY ausente; medicao real ignorada.", file=sys.stderr)
        return 2
    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2
    if not (1 <= args.repetitions <= 20):
        print("--repetitions deve ficar entre 1 e 20.", file=sys.stderr)
        return 2

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-ab-live-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    shutil.copy2(FIXTURE / "arestrix.project.json", project / "arestrix.project.json")
    gate = None
    if args.gate_mode != "off":
        gate = {
            "mode": args.gate_mode,
            "provider": args.gate_provider,
            "model": args.gate_model,
            "maxSearches": max(0, args.gate_max_searches),
            "minConfidence": 0.6,
            "fallback": "brief",
            "maxBriefChars": 1200,
        }
    graph = work / "pipeline.json"
    graph.write_text(json.dumps(live_pipeline(args.model, args.memory_dependent, gate),
                               ensure_ascii=False, indent=2),
                     encoding="utf-8")
    knowledge = json.loads((FIXTURE / "knowledge.json").read_text(encoding="utf-8"))
    if args.memory_dependent:
        knowledge["lessons"].append(VALUE_LESSON)
    report_path, evidence_dir = report_paths(
        args.gate_mode, args.memory_dependent, args.gate_provider)

    env = {k: v for k, v in os.environ.items()
           if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                        "GROQ_API_KEY", "JEV_API_KEY", "MISTRAL_API_KEY")}
    if args.gate_provider == "jev" and os.environ.get("JEV_API_KEY"):
        env["JEV_API_KEY"] = os.environ["JEV_API_KEY"]
    for key in ("DEEPSEEK_BASE_URL", "ARESTRIX_AI_GATEWAY_URL",
                "ARESTRIX_AI_GATEWAY_KEY", "ARESTRIX_REQUIRE_AI_GATEWAY",
                "ARESTRIX_AI_GATEWAY_POLICY", "ARESTRIX_MEMORY_DISABLE",
                "ARESTRIX_MEMORY_READONLY"):
        env.pop(key, None)
    env["ARESTRIX_HOME"] = str(ROOT)

    started = time.monotonic()
    try:
        init = run(cli, ["memory", "state", "list", "--root", str(project), "--json"],
                   env, 120)
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
            "--repetitions", str(args.repetitions),
            "--seed", str(args.seed),
            "--root", str(project),
            "--json",
        ], env, args.timeout)
        output = (result.stdout or "") + (result.stderr or "")
        if result.returncode != 0:
            raise RuntimeError(f"executor A/B falhou (codigo {result.returncode}):\n{output}")
        report = json.loads(result.stdout)

        measurement = pathlib.Path(str(report.get("measurementDb", "")))
        expected = 20 * args.repetitions
        arms = arm_metrics(measurement) if measurement.is_file() else {
            "usage": {}, "validation": {}, "errors": {}}
        checks = {
            "exitZero": True,
            "reportOk": report.get("ok") is True,
            "twentyTasks": int(report.get("primary", {}).get("n", 0)) == 20,
            "knowledgeUnchanged": report.get("knowledgeUnchanged") is True,
            "frozenFingerprintStable":
                report.get("frozenFingerprint") == report.get("frozenFingerprintAfter"),
            "originalDbUnchanged": sha256_file(memory_db) == frozen_before,
            "onExecutions": arms["usage"].get("on", {}).get("executions") == expected,
            "offExecutions": arms["usage"].get("off", {}).get("executions") == expected,
            "realPromptTokens": arms["usage"].get("on", {}).get("promptTokens", 0) > 0
                                and arms["usage"].get("off", {}).get("promptTokens", 0) > 0,
        }
        total_tokens = sum(arm.get("totalTokens", 0)
                           for arm in arms["usage"].values())
        checks["withinTokenBudget"] = total_tokens <= args.max_total_tokens

        evidence_keys = [str(report.get("observationsPath", ""))]
        if args.gate_mode != "off":
            evidence_keys.append(str(report.get("measurementDb", "")))
        evidence = copy_evidence(evidence_keys, evidence_dir)
        gate_stats = gate_metrics(measurement) \
            if args.gate_mode != "off" and measurement.is_file() else {}
        total_with_gate = total_tokens + int(gate_stats.get("modelInTokens", 0)) \
            + int(gate_stats.get("modelOutTokens", 0))
        summary = {
            "provider": args.provider,
            "model": args.model,
            "design": "memory-dependent" if args.memory_dependent else "parity-and-cost",
            "gate": gate,
            "gateMode": args.gate_mode,
            "suite": str(SUITE.relative_to(ROOT)).replace("\\", "/"),
            "pipeline": "tests/fixtures/memory-ab (variante live)",
            "promptTemplate": VALUE_PROMPT_TEMPLATE if args.memory_dependent else PROMPT_TEMPLATE,
            "repetitions": args.repetitions,
            "seed": args.seed,
            "executedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "elapsedSeconds": round(time.monotonic() - started, 2),
            "totalTokens": total_tokens,
            "totalWithGateTokens": total_with_gate,
            "arms": arms,
            "gateStats": gate_stats,
            "evidence": evidence,
            "report": report,
            "checks": checks,
            "pass": all(checks.values()),
        }
        if args.memory_dependent:
            summary["valueObserved"] = report["primary"]["delta"] > 0
        if not args.no_write_report:
            report_path.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
                                   encoding="utf-8")

        print(f"tarefas={report['primary']['n']} reps={args.repetitions} "
              f"execucoes={report.get('observations')} tokens={total_tokens}")
        print(f"aprovacao ON={report['primary']['on']:.3f} "
              f"OFF={report['primary']['off']:.3f} "
              f"delta={report['primary']['delta']:.3f} "
              f"IC95=[{report['primary']['ciLow']:.3f},{report['primary']['ciHigh']:.3f}]")
        print(f"infra={report.get('infraEvents')} invalidas={len(report.get('invalidTasks', []))} "
              f"conhecimento_inalterado={report.get('knowledgeUnchanged')}")
        for arm, values in arms["usage"].items():
            print(f"  {arm}: {values['executions']} execucoes, "
                  f"{values['promptTokens']} prompt, {values['completionTokens']} completion, "
                  f"{values.get('cachedTokens', 0)} cached, "
                  f"{values['avgLatencyMs']:.0f} ms")
        if args.gate_mode != "off":
            print(f"gate={args.gate_mode} provider={args.gate_provider or 'deterministic'} "
                  f"decisoes={summary['gateStats'].get('decisions')} "
                  f"gate_tokens={summary['gateStats'].get('modelInTokens', 0)}+"
                  f"{summary['gateStats'].get('modelOutTokens', 0)} "
                  f"gate_latency={summary['gateStats'].get('avgLatencyMs', 0)}ms "
                  f"total_com_gate={summary['totalWithGateTokens']}")
        on_agent = arms["usage"].get("on", {})
        off_agent = arms["usage"].get("off", {})
        gate_tokens_total = (int(summary["gateStats"].get("modelInTokens", 0))
                             + int(summary["gateStats"].get("modelOutTokens", 0)))
        on_total = int(on_agent.get("totalTokens", 0)) + gate_tokens_total
        off_total = int(off_agent.get("totalTokens", 0))
        ratio = (on_total / off_total) if off_total else 0.0
        on_latency = (float(on_agent.get("avgLatencyMs", 0))
                      + float(summary["gateStats"].get("avgLatencyMs", 0))) / 1000.0
        off_latency = float(off_agent.get("avgLatencyMs", 0)) / 1000.0
        print(f"RESUMO: {summary['design']} | aprovacao ON {report['primary']['on']:.3f} / "
              f"OFF {report['primary']['off']:.3f} | tokens ON {on_total} / OFF {off_total} "
              f"({ratio:.2f}x) | latencia {on_latency:.2f}s / {off_latency:.2f}s")
        if args.memory_dependent:
            print(f"valor_observado={summary['valueObserved']} "
                  f"(delta de aprovacao {report['primary']['delta']:+.3f})")
        print(f"pass={summary['pass']} relatorio={report_path.relative_to(ROOT)}")
        return 0 if summary["pass"] else 1
    except Exception as exc:  # noqa: BLE001
        print(f"falha na medicao A/B live: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)
        else:
            print(f"diretorio preservado: {work}")


if __name__ == "__main__":
    raise SystemExit(main())
