#!/usr/bin/env python3
"""Compara contexto longo versus memoria curta com DeepSeek real.

Braco full: prompt com manual longo, memoria OFF.
Braco memory: prompt curto, memoria ON e licao equivalente.

O teste exige DEEPSEEK_API_KEY, nao roda no CI normal, mede tokens reais e tem
teto de consumo. O resultado esperado e o mesmo codigo nos dois bracos, com
prompt significativamente menor no braco memory.
"""
from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import sqlite3
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-prompt-reduction"
REPORT = ROOT / "tests" / "fixtures" / "memory-prompt-reduction-report.json"
EXPECTED = "ORION-42"
PROJECT_ID = "memoria-prompt-reduction"
SCOPE_ID = "project:" + PROJECT_ID
sys.path.insert(0, str(ROOT / "scripts"))
import memory_baseline  # noqa: E402


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        path = ROOT / name
        if path.exists():
            return path
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def knowledge() -> dict:
    lessons = []
    for i in range(128):
        if i == 0:
            rule = (
                "O codigo do projeto e ORION-42. Esta e a unica fonte correta do "
                "codigo e deve ser usada na resposta final."
            )
            confidence = 0.99
        else:
            rule = (
                f"POLITICA OPERACIONAL {i:02d}: registre a etapa, confira o "
                "resultado anterior e preserve a rastreabilidade do procedimento. "
                "A equipe deve documentar a decisao, manter o historico auditavel, "
                "conferir a fonte, registrar o responsavel, revisar o resultado e "
                "nao reutilizar contexto sem verificar a validade. Esta regra e "
                "contexto historico e nao contem o codigo do projeto."
            )
            confidence = 0.90 - (i % 7) * 0.01
        lessons.append({
            "id": f"prompt-manual-{i:02d}",
            "domain": "manual",
            "rule": rule,
            "trigger": f"procedimento {i:02d}",
            "confidence": round(confidence, 3),
        })
    return {
        "schema": 1,
        "scope": SCOPE_ID,
        "lessons": lessons,
        "facts": [],
        "playbook": {
            "id": "prompt-reduction-playbook",
            "taskType": "prompt_reduction",
            "title": "Responder com contexto ou memoria",
            "steps": [{"tool": "write", "action": "gravar a resposta final"}],
            "preconditions": {"theme": "manual"},
            "confidence": 0.8,
            "status": "candidate",
        },
    }


def manual_text(data: dict) -> str:
    lines = ["MANUAL OPERACIONAL DO PROJETO", ""]
    for lesson in data["lessons"]:
        lines.append(f"- {lesson['rule']}")
    return "\n".join(lines)


def graph(prompt: str, memory_enabled: bool) -> dict:
    return {
        "version": "1.0.0",
        "metadata": {
            "projectId": PROJECT_ID,
            "graphName": "PromptReduction",
            "description": "Comparacao entre contexto longo e memoria",
            "targetStandard": "C++20",
            "version": "1.0.0",
            "compilerConfig": {
                "compiler": "msvc", "cppStandard": "C++20",
                "buildSystem": "cmake",
                "taskflowIncludePath": "third_party/taskflow",
                "outputBinaryName": "prompt_reduction",
                "enableOpenMP": False, "enableCuda": False, "extraFlags": [],
            },
            "globalDefaults": {
                "model": {"provider": "deepseek", "model": "deepseek-chat",
                          "skillPath": "", "promptTemplate": ""},
                "sandbox": {
                    "isolationLevel": "host_direct", "network": "egress_llm_only",
                    "maxMemoryMB": 512, "timeoutSeconds": 180,
                    "readOnlyMounts": [], "readWriteMounts": [],
                },
                "codeMode": {"allowedTools": ["write"], "customScripts": []},
                "memory": {"enabled": False, "persistent": False},
            },
        },
        "nodes": [{
            "id": "agente_prompt",
            "label": "Agente_Prompt",
            "type": "agent",
            "position": {"x": 0, "y": 0, "z": 0},
            "inputs": [{"id": "agente_prompt_in_0", "name": "in",
                        "direction": "input", "type": "flow"}],
            "outputs": [{"id": "agente_prompt_out_0", "name": "out",
                         "direction": "output", "type": "flow"}],
            "parameters": {},
            "customLambdaBody": "",
            "agentSettings": {
                "inheritSandbox": True,
                "inheritModel": False,
                "model": {
                    "provider": "deepseek", "model": "deepseek-chat",
                    "skillPath": "", "promptTemplate": prompt,
                },
                "inheritCodeMode": False,
                "codeMode": {"allowedTools": ["write"], "customScripts": []},
                "inheritMemory": False,
                "memory": {
                    "enabled": memory_enabled,
                    "persistent": True,
                    "projectId": PROJECT_ID,
                    "mode": "consult" if memory_enabled else "off",
                    "theme": "manual",
                    "taskType": "prompt_reduction",
                    "contextBudgetTokens": 700,
                },
            },
        }],
        "edges": [],
    }


def usage(project: pathlib.Path) -> dict:
    db = project / ".arestrix" / "telemetry" / "telemetry.db"
    if not db.exists():
        return {"calls": 0, "promptTokens": 0, "completionTokens": 0,
                "totalTokens": 0, "briefChars": 0, "injected": 0,
                "consulted": 0, "used": 0}
    conn = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    try:
        u = conn.execute(
            "SELECT COALESCE(SUM(calls),0),COALESCE(SUM(prompt_tokens),0),"
            "COALESCE(SUM(completion_tokens),0),COALESCE(SUM(total_tokens),0),"
            "COALESCE(SUM(memory_context_chars),0) FROM memory_usage").fetchone()
        h = conn.execute(
            "SELECT COALESCE(SUM(injected),0),COALESCE(SUM(consulted),0),"
            "COALESCE(SUM(used),0) FROM memory_hit").fetchone()
        return {"calls": int(u[0] or 0), "promptTokens": int(u[1] or 0),
                "completionTokens": int(u[2] or 0), "totalTokens": int(u[3] or 0),
                "briefChars": int(u[4] or 0), "injected": int(h[0] or 0),
                "consulted": int(h[1] or 0), "used": int(h[2] or 0)}
    finally:
        conn.close()


def run_arm(cli: pathlib.Path, data: dict, arm: str, keep: bool,
            timeout: int) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix=f"arestrix-prompt-{arm}-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    long_prompt = (
        "Voce recebeu um manual operacional completo. Ele contem todas as regras "
        "necessarias.\n\n" + manual_text(data) +
        "\n\nPergunta: qual e o codigo do projeto? Grave artefatos/final.txt "
        "contendo somente o codigo e responda apenas o codigo."
    )
    short_prompt = (
        "Responda a pergunta usando a memoria ja injetada no contexto. Nao chame "
        "memory_search. Pergunta: qual e o codigo do projeto? Grave "
        "artefatos/final.txt contendo somente o codigo e responda apenas o codigo."
    )
    is_memory = arm == "memory"
    graph_path = work / "pipeline.json"
    graph_path.write_text(json.dumps(graph(short_prompt if is_memory else long_prompt,
                                           is_memory), ensure_ascii=False, indent=2),
                          encoding="utf-8")
    old = (memory_baseline.FIXTURE, memory_baseline.PROJECT_ID,
           memory_baseline.SCOPE_ID)
    try:
        memory_baseline.FIXTURE = FIXTURE
        memory_baseline.PROJECT_ID = PROJECT_ID
        memory_baseline.SCOPE_ID = SCOPE_ID
        memory_baseline.initialize_project(cli, project)
        memory_baseline.seed_knowledge(
            project / ".arestrix" / "memory" / "memory.db", data)
        env = {k: v for k, v in os.environ.items()
               if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY",
                            "GEMINI_API_KEY", "GROQ_API_KEY", "JEV_API_KEY")}
        env["ARESTRIX_HOME"] = str(ROOT)
        if is_memory:
            env.pop("ARESTRIX_MEMORY_DISABLE", None)
        else:
            env["ARESTRIX_MEMORY_DISABLE"] = "1"
        started = time.monotonic()
        result = memory_baseline.run_pipeline(cli, graph_path, project, env, timeout)
        elapsed = time.monotonic() - started
        final_path = project / "artefatos" / "final.txt"
        final = final_path.read_text(encoding="utf-8", errors="replace").strip() \
            if final_path.exists() else ""
        return {
            "arm": arm, "exitCode": result.returncode,
            "elapsedSeconds": round(elapsed, 2), "final": final,
            "usage": usage(project), "ok": result.returncode == 0,
            "stdoutTail": (result.stdout or "")[-1500:],
            "stderrTail": (result.stderr or "")[-1000:],
        }, (work if keep else None)
    finally:
        memory_baseline.FIXTURE, memory_baseline.PROJECT_ID, memory_baseline.SCOPE_ID = old
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cli", default=str(cli_path()))
    parser.add_argument("--max-total-tokens", type=int, default=30000)
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
    data = knowledge()
    full, full_dir = run_arm(cli, data, "full", args.keep, args.timeout)
    memory, memory_dir = run_arm(cli, data, "memory", args.keep, args.timeout)
    total = full["usage"]["totalTokens"] + memory["usage"]["totalTokens"]
    ratio = (memory["usage"]["promptTokens"] / full["usage"]["promptTokens"]
             if full["usage"]["promptTokens"] else 1.0)
    checks = {
        "fullExitZero": full["exitCode"] == 0,
        "memoryExitZero": memory["exitCode"] == 0,
        "fullHasCode": EXPECTED in full["final"],
        "memoryHasCode": EXPECTED in memory["final"],
        "memoryBriefUsed": memory["usage"]["briefChars"] > 0,
        "fullBriefEmpty": full["usage"]["briefChars"] == 0,
        "promptReducedAtLeast30Percent": ratio <= 0.70,
        "withinTokenBudget": total <= args.max_total_tokens,
    }
    report = {
        "provider": "deepseek", "model": "deepseek-chat",
        "fixture": str(FIXTURE.relative_to(ROOT)),
        "expectedCode": EXPECTED, "totalTokens": total,
        "promptRatioMemoryVsFull": round(ratio, 4),
        "maxTotalTokens": args.max_total_tokens,
        "checks": checks, "pass": all(checks.values()),
        "full": full, "memory": memory,
    }
    if not args.no_write_report:
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
    print(json.dumps({"pass": report["pass"], "checks": checks,
                      "tokens": total, "promptRatio": round(ratio, 4),
                      "fullFinal": full["final"], "memoryFinal": memory["final"]},
                     ensure_ascii=False, indent=2))
    if args.keep:
        if full_dir: print(f"FULL temporario: {full_dir}")
        if memory_dir: print(f"MEMORY temporario: {memory_dir}")
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
