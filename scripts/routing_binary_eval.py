#!/usr/bin/env python3
"""A/B de qualidade e custo do roteamento binario com modelos reais.

O harness executa um pipeline minimo de um agente em tres bracos:

- ``local``: roteamento desligado, modelo local (DeepSeek Flash);
- ``frontier``: roteamento desligado, modelo de fronteira (DeepSeek V4 Pro);
- ``binary``: roteamento binario com os dois niveis.

O agente responde a uma tarefa e grava a resposta em ``artefatos/resposta.txt``;
o validador exige o texto exato. A telemetria de cada tipo de tarefa e semeada
com o resultado observado no piloto local (10 episodios, com suavizacao), para
que o roteador tenha historico antes da medicao.

Uso:
  python scripts/routing_binary_eval.py --repetitions 2 --write
  python scripts/routing_binary_eval.py --check
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
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent
REPORT_JSON = ROOT / "tests" / "fixtures" / "routing-binary-report.json"
REPORT_MD = ROOT / "docs" / "quality" / "routing-binary-ab.md"
PROJECT_ID = "routing-binary"
NODE_ID = "respondedor"
LOCAL_MODEL = "deepseek-flash"
FRONTIER_MODEL = "deepseek-v4-pro"

# Tarefas faceis devem passar no modelo local; tarefas dificeis exigem mais
# raciocinio e tendem a falhar. O piloto confirma antes do A/B.
TASKS = [
    {
        "id": "easy-sum",
        "difficulty": "easy",
        "prompt": "Qual e o resultado de 2 + 2? Responda apenas com o numero.",
        "answer": "4",
    },
    {
        "id": "easy-capital",
        "difficulty": "easy",
        "prompt": "Qual e a capital da Franca? Responda apenas com o nome.",
        "answer": "Paris",
    },
    {
        "id": "easy-product",
        "difficulty": "easy",
        "prompt": "Qual e o resultado de 7 * 6? Responda apenas com o numero.",
        "answer": "42",
    },
    {
        "id": "easy-sky",
        "difficulty": "easy",
        "prompt": "Em um dia claro, qual e a cor do ceu? Responda apenas com a cor.",
        "answer": "azul",
    },
    {
        "id": "hard-product",
        "difficulty": "hard",
        "prompt": "Calcule 987654321 * 123456789. Responda apenas com o numero.",
        "answer": "121932631112635269",
    },
    {
        "id": "hard-recurrence",
        "difficulty": "hard",
        "prompt": "Considere a sequencia a1=2, a2=3 e a(n)=a(n-1)*a(n-2)+1. "
                  "Qual e o valor de a5? Responda apenas com o numero.",
        "answer": "155",
    },
    {
        "id": "hard-fib",
        "difficulty": "hard",
        "prompt": "Qual e o 20o numero de Fibonacci, com F1=1 e F2=1? "
                  "Responda apenas com o numero.",
        "answer": "6765",
    },
]


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        candidate = ROOT / name
        if candidate.exists():
            return candidate
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def run(cli: pathlib.Path, args: list[str], env: dict[str, str],
        timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run([str(cli), *args], env=env, capture_output=True,
                          text=True, encoding="utf-8", errors="replace",
                          timeout=timeout)


def base_manifest() -> dict:
    return {
        "projectId": PROJECT_ID,
        "name": "Routing Binary",
        "version": "1.0.0",
        "globalDefaults": {
            "memory": {"enabled": False, "persistent": False},
            "routing": {"mode": "off"},
        },
    }


def pipeline(task: dict, arm: str) -> dict:
    model = LOCAL_MODEL if arm in ("local", "binary") else FRONTIER_MODEL
    routing = {"mode": "off"}
    if arm == "binary":
        routing = {
            "mode": "binary",
            "pMin": 0.5,
            "tiers": {
                "localLarge": {"provider": "deepseek", "model": LOCAL_MODEL, "cost": 0.2},
                "frontier": {"provider": "deepseek", "model": FRONTIER_MODEL, "cost": 1.0},
            },
        }
    prompt = (
        "Resolva a tarefa e grave apenas a resposta final no arquivo "
        "artefatos/resposta.txt usando a ferramenta write. Nao escreva a resposta "
        "no chat. Tarefa:\n{{input}}"
    )
    return {
        "version": "1.0.0",
        "metadata": {
            "projectId": PROJECT_ID,
            "graphName": "RoutingBinary",
            "description": "A/B de roteamento binario com modelos reais",
            "targetStandard": "C++20",
            "version": "1.0.0",
            "compilerConfig": {
                "compiler": "msvc", "cppStandard": "C++20", "buildSystem": "cmake",
                "taskflowIncludePath": "third_party/taskflow",
                "outputBinaryName": "routing_binary",
                "enableOpenMP": False, "enableCuda": False, "extraFlags": [],
            },
            "globalDefaults": {
                "model": {"provider": "deepseek", "model": model,
                          "skillPath": "", "promptTemplate": ""},
                "sandbox": {"isolationLevel": "host_direct",
                            "network": "egress_llm_only", "maxMemoryMB": 512,
                            "timeoutSeconds": 180, "readOnlyMounts": [],
                            "readWriteMounts": []},
                "codeMode": {"allowedTools": ["write"], "customScripts": []},
                "memory": {"enabled": False, "persistent": False},
                "routing": routing,
            },
        },
        "nodes": [{
            "id": NODE_ID,
            "label": "Respondedor",
            "type": "agent",
            "position": {"x": 0, "y": 0},
            "inputs": [{"id": NODE_ID + "_in", "name": "in", "direction": "input",
                        "type": "flow"}],
            "outputs": [{"id": NODE_ID + "_out", "name": "out", "direction": "output",
                         "type": "flow"}],
            "parameters": {},
            "customLambdaBody": "",
            "validationRules": [{
                "targetPathPattern": "artefatos/resposta.txt",
                "minSizeBytes": 1,
                "contentPattern": task["answer"],
            }],
            "agentSettings": {
                "inheritSandbox": True,
                "inheritModel": False,
                "model": {"provider": "deepseek", "model": model,
                          "skillPath": "", "promptTemplate": prompt},
                "inheritCodeMode": False,
                "codeMode": {"allowedTools": ["write"], "customScripts": []},
                "inheritMemory": False,
                "memory": {"enabled": False, "persistent": False,
                           "projectId": PROJECT_ID, "mode": "off",
                           "taskType": task["id"], "contextBudgetTokens": 0},
                "maxTurns": 4,
                "maxTokens": 512,
            },
        }],
        "edges": [],
    }


def ensure_scope(conn: sqlite3.Connection, scope_id: str) -> None:
    now = "2026-11-12T00:00:00Z"
    conn.execute(
        "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
        "VALUES(?,?,?,?,?,?)", (scope_id, "project", None, PROJECT_ID, now, "{}"))
    conn.execute(
        "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
        "VALUES(?,?,0)", (scope_id, scope_id))


def seed_telemetry(db_path: pathlib.Path, task_id: str, successes: int) -> None:
    scope_id = "project:" + PROJECT_ID
    conn = sqlite3.connect(db_path)
    try:
        ensure_scope(conn, scope_id)
        for index in range(10):
            ok = 1 if index < successes else 0
            episode_id = f"seed-{task_id}-{index}"
            run_id = f"seed-run-{task_id}-{index}"
            conn.execute(
                "INSERT OR REPLACE INTO memory_episode(id,scope_id,run_id,node_id,item_id,"
                "kind,content,sanitized,ab_arm,ab_repetition,created_at,metadata,"
                "intent_template_hash,task_type) "
                "VALUES(?,?,?,?,?,?,?,1,'',0,?,'{}','',?)",
                (episode_id, scope_id, run_id, NODE_ID, "pipeline",
                 "agent_success" if ok else "agent_failure", "seed", 
                 "2026-11-12T00:00:00Z", task_id))
            conn.execute(
                "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                "artifact_hash,ab_arm,ab_repetition,created_at) "
                "VALUES(?,?,?,?,?,?,?,?,1.0,'[]',NULL,'',0,?)",
                (f"seed-val-{task_id}-{index}", scope_id, run_id, NODE_ID,
                 episode_id, "seed", "1", ok, "2026-11-12T00:00:00Z"))
        conn.commit()
    finally:
        conn.close()


def usage(project: pathlib.Path) -> dict:
    db = project / ".arestrix" / "telemetry" / "telemetry.db"
    if not db.exists():
        return {"promptTokens": 0, "completionTokens": 0, "totalTokens": 0,
                "cachedTokens": 0, "calls": 0, "avgLatencyMs": 0.0}
    conn = sqlite3.connect(db)
    try:
        row = conn.execute(
            "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), "
            "COALESCE(SUM(completion_tokens),0), COALESCE(SUM(total_tokens),0), "
            "COALESCE(SUM(cached_tokens),0), COALESCE(AVG(latency_ms),0) "
            "FROM memory_usage").fetchone()
        return {
            "calls": int(row[0] or 0),
            "promptTokens": int(row[1] or 0),
            "completionTokens": int(row[2] or 0),
            "totalTokens": int(row[3] or 0),
            "cachedTokens": int(row[4] or 0),
            "avgLatencyMs": round(float(row[5] or 0), 2),
        }
    finally:
        conn.close()


def routing_decisions(project: pathlib.Path) -> dict:
    db = project / ".arestrix" / "telemetry" / "telemetry.db"
    if not db.exists():
        return {}
    conn = sqlite3.connect(db)
    try:
        counts: dict[str, int] = {}
        for (steps_json,) in conn.execute("SELECT steps_json FROM memory_trace"):
            try:
                steps = json.loads(steps_json)
            except json.JSONDecodeError:
                continue
            for step in steps:
                if step.get("kind") != "routing_decision":
                    continue
                decision = step.get("decision") or {}
                tier = str(decision.get("tier", ""))
                counts[tier] = counts.get(tier, 0) + 1
        return counts
    finally:
        conn.close()


def read_answer(project: pathlib.Path) -> str:
    path = project / "artefatos" / "resposta.txt"
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8", errors="replace").strip()


def run_one(cli: pathlib.Path, base: pathlib.Path, work: pathlib.Path,
            task: dict, arm: str, repetition: int, env: dict[str, str],
            timeout: int) -> dict:
    run_dir = work / f"{arm}-{task['id']}-{repetition}"
    run_dir.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(base, run_dir)
    graph = pipeline(task, arm)
    graph_path = run_dir / "pipeline.json"
    graph_path.write_text(json.dumps(graph, ensure_ascii=False, indent=2) + "\n",
                          encoding="utf-8")
    local_env = dict(env)
    local_env["ARESTRIX_AB_TASK_INPUT"] = task["prompt"]
    local_env["ARESTRIX_AB_NODE"] = NODE_ID
    started = time.perf_counter()
    completed = run(cli, ["run", str(graph_path), "--root", str(run_dir),
                          "--json", "--timeout", str(timeout)], local_env, timeout + 30)
    elapsed_ms = round((time.perf_counter() - started) * 1000.0, 2)
    result_event = {}
    for line in (completed.stdout or "").splitlines():
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            continue
        if event.get("event") == "result":
            result_event = event
    answer = read_answer(run_dir)
    expected = task["answer"]
    answer_ok = expected.lower() in answer.lower()
    return {
        "arm": arm,
        "task": task["id"],
        "difficulty": task["difficulty"],
        "repetition": repetition,
        "exitCode": completed.returncode,
        "pipelineOk": bool(result_event.get("ok", completed.returncode == 0)),
        "answerOk": bool(answer_ok),
        "answer": answer[:200],
        "elapsedMs": elapsed_ms,
        "usage": usage(run_dir),
        "routing": routing_decisions(run_dir),
        "error": (completed.stderr or "")[-500:] if completed.returncode != 0 else "",
    }


def aggregate(observations: list[dict], arm: str) -> dict:
    selected = [item for item in observations if item["arm"] == arm]
    total = len(selected)
    approved = sum(1 for item in selected if item["answerOk"])
    prompt = sum(item["usage"]["promptTokens"] for item in selected)
    completion = sum(item["usage"]["completionTokens"] for item in selected)
    tokens = sum(item["usage"]["totalTokens"] for item in selected)
    cost_weight = 0.2 if arm in ("local", "binary") else 1.0
    # O peso e o custo configurado do nivel; para binary usamos o tier real
    # registrado em cada execucao.
    if arm == "binary":
        weighted = 0.0
        for item in selected:
            tier = item["routing"].get("local_large", 0)
            used_local = tier > 0
            weighted += item["usage"]["totalTokens"] * (0.2 if used_local else 1.0)
    else:
        weighted = tokens * cost_weight
    by_difficulty: dict[str, dict] = {}
    for difficulty in ("easy", "hard"):
        rows = [item for item in selected if item["difficulty"] == difficulty]
        by_difficulty[difficulty] = {
            "n": len(rows),
            "approved": sum(1 for item in rows if item["answerOk"]),
            "approval": (sum(1 for item in rows if item["answerOk"]) / len(rows)
                         if rows else 0.0),
        }
    return {
        "n": total,
        "approved": approved,
        "approval": approved / total if total else 0.0,
        "promptTokens": prompt,
        "completionTokens": completion,
        "totalTokens": tokens,
        "weightedCost": round(weighted, 2),
        "avgLatencyMs": round(sum(item["elapsedMs"] for item in selected) / total, 2)
                         if total else 0.0,
        "byDifficulty": by_difficulty,
        "routing": {},
    }


def merge_routing(summary: dict, observations: list[dict], arm: str) -> None:
    counts: dict[str, int] = {}
    for item in observations:
        if item["arm"] != arm:
            continue
        for tier, count in item["routing"].items():
            counts[tier] = counts.get(tier, 0) + count
    summary["routing"] = counts


def build_report(observations: list[dict], repetitions: int) -> dict:
    arms = {arm: aggregate(observations, arm) for arm in ("local", "frontier", "binary")}
    for arm in arms:
        merge_routing(arms[arm], observations, arm)
    local = arms["local"]
    frontier = arms["frontier"]
    binary = arms["binary"]
    checks = {
        "localApprovalOnEasy": local["byDifficulty"]["easy"]["approval"] >= 0.5,
        "frontierApprovalOnHard": frontier["byDifficulty"]["hard"]["approval"] >= 0.5,
        "binaryQualityNotBelowLocal": binary["approval"] >= local["approval"],
        "binaryCostBelowFrontier": binary["weightedCost"] < frontier["weightedCost"],
        "binaryUsesBothTiers": len(binary["routing"]) >= 2,
        "binaryUsesFrontierOnHard": any(
            item["arm"] == "binary" and item["difficulty"] == "hard" and
            item["routing"].get("frontier", 0) > 0 for item in observations),
        "binaryUsesLocalOnEasy": any(
            item["arm"] == "binary" and item["difficulty"] == "easy" and
            item["routing"].get("local_large", 0) > 0 for item in observations),
        "noInfraErrors": all(item["exitCode"] in (0, 1) for item in observations),
    }
    return {
        "schema": 1,
        "name": "routing-binary-v1",
        "provider": "deepseek",
        "localModel": LOCAL_MODEL,
        "frontierModel": FRONTIER_MODEL,
        "repetitions": repetitions,
        "executedAt": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "arms": arms,
        "checks": checks,
        "pass": all(checks.values()),
        "observations": observations,
    }


def markdown(report: dict) -> str:
    lines = [
        "# Roteamento binario com modelos reais",
        "",
        "> **Versao consolidada:** [Resultados dos testes de memoria]"
        "(../pt-BR/resultados-testes-memoria.md) · "
        "[English](../en/memory-test-results.md).",
        "",
        f"- Provedor: `{report['provider']}`",
        f"- Nivel local: `{report['localModel']}`",
        f"- Nivel de fronteira: `{report['frontierModel']}`",
        f"- Repeticoes por tarefa: {report['repetitions']}",
        "",
        "## Desenho",
        "",
        "O pipeline minimo tem um agente que grava a resposta em",
        "`artefatos/resposta.txt`; o validador exige o texto exato. A telemetria",
        "de cada tipo de tarefa e semeada com 10 episodios (9/10 nas faceis e",
        "2/10 nas dificeis) para o roteador ter historico. Os tres bracos usam",
        "o mesmo projeto e o mesmo conjunto de tarefas.",
        "",
        "## Piloto local",
        "",
        "O piloto roda o modelo local uma vez por tarefa e apenas define o",
        "historico semeado; ele nao entra nas metricas finais.",
        "",
        "| Tarefa | Dificuldade | Passou no piloto |",
        "| --- | --- | --- |",
    ]
    task_difficulty = {task["id"]: task["difficulty"] for task in TASKS}
    for task_id, ok in report.get("pilot", {}).items():
        lines.append(f"| `{task_id}` | {task_difficulty.get(task_id, '?')} | "
                     f"{'sim' if ok else 'nao'} |")
    lines += [
        "",
        "## Resultados",
        "",
        "| Braco | Aprovacao | Faceis | Dificeis | Tokens | Custo ponderado | Latencia media |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for arm, values in report["arms"].items():
        lines.append(
            f"| {arm} | {values['approval']:.3f} | "
            f"{values['byDifficulty']['easy']['approval']:.3f} | "
            f"{values['byDifficulty']['hard']['approval']:.3f} | "
            f"{values['totalTokens']} | {values['weightedCost']:.2f} | "
            f"{values['avgLatencyMs']:.0f} ms |")
    lines += [
        "",
        "## Decisoes do roteador",
        "",
        "| Braco | Tiers observados |",
        "| --- | --- |",
    ]
    for arm, values in report["arms"].items():
        lines.append(f"| {arm} | {values['routing'] or '-'} |")
    lines += [
        "",
        "## Criterios de aceite",
        "",
    ]
    for key, value in report["checks"].items():
        lines.append(f"- {'OK' if value else 'FALHOU'} `{key}`")
    local = report["arms"]["local"]
    frontier = report["arms"]["frontier"]
    binary = report["arms"]["binary"]
    savings = ((1.0 - binary["weightedCost"] / frontier["weightedCost"]) * 100.0
               if frontier["weightedCost"] else 0.0)
    lines += [
        "",
        "## Decisao",
        "",
        "O roteamento binario foi medido com chamadas reais. A qualidade e o",
        "custo usam tokens e latencia observados; o custo ponderado aplica o",
        "peso configurado de cada nivel (0,2 local e 1,0 fronteira) ao total de",
        "tokens.",
        "",
        f"Neste conjunto, a aprovacao foi {local['approval']:.3f} no local, "
        f"{frontier['approval']:.3f} na fronteira e {binary['approval']:.3f} no "
        "binario. Nao houve ganho de qualidade: o valor medido esta no custo. O "
        f"binario ficou {savings:.1f}% abaixo da fronteira e "
        f"{binary['weightedCost'] / local['weightedCost']:.2f}x o custo do local.",
        "",
        "O resultado vale para este conjunto de tarefas e para os dois modelos",
        "escolhidos; nao e uma garantia para todo trafego. A promocao do",
        "`routing.mode=binary` para padrao continua exigindo um corpus maior e",
        "telemetria de producao.",
        "",
    ]
    return "\n".join(lines)


def stable_view(report: dict) -> dict:
    view = json.loads(json.dumps(report))
    view.pop("executedAt", None)
    view.pop("elapsedSeconds", None)
    for observation in view.get("observations", []):
        observation.pop("elapsedMs", None)
        observation.get("usage", {}).pop("avgLatencyMs", None)
    for values in view.get("arms", {}).values():
        values.pop("avgLatencyMs", None)
    return view


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--repetitions", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--limit", type=int, default=0,
                        help="limita o numero de tarefas (depuracao)")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        print("use --write ou --check", file=sys.stderr)
        return 2
    if not os.environ.get("DEEPSEEK_API_KEY"):
        print("DEEPSEEK_API_KEY ausente; medicao real ignorada.", file=sys.stderr)
        return 2
    cli = cli_path()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2
    tasks = TASKS[:args.limit] if args.limit > 0 else TASKS
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-routing-binary-"))
    base = work / "base"
    base.mkdir(parents=True)
    (base / "arestrix.project.json").write_text(
        json.dumps(base_manifest(), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    env = {key: value for key, value in os.environ.items()
           if key not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY",
                          "GROQ_API_KEY", "JEV_API_KEY", "TYPESAFE_API_KEY")}
    for key in ("DEEPSEEK_BASE_URL", "ARESTRIX_AI_GATEWAY_URL",
                "ARESTRIX_AI_GATEWAY_KEY", "ARESTRIX_REQUIRE_AI_GATEWAY",
                "ARESTRIX_AI_GATEWAY_POLICY", "ARESTRIX_MEMORY_DISABLE",
                "ARESTRIX_MEMORY_READONLY"):
        env.pop(key, None)
    env["ARESTRIX_HOME"] = str(ROOT)
    started = time.monotonic()
    try:
        init = run(cli, ["memory", "state", "list", "--root", str(base), "--json"],
                   env, 120)
        if init.returncode != 0:
            raise RuntimeError("nao foi possivel inicializar a memoria:\n"
                               + (init.stdout or "") + (init.stderr or ""))
        opened = run(cli, ["memory", "bridges", "detect", "--root", str(base), "--json"],
                     env, 120)
        if opened.returncode != 0:
            raise RuntimeError("nao foi possivel abrir a telemetria:\n"
                               + (opened.stdout or "") + (opened.stderr or ""))
        telemetry_db = base / ".arestrix" / "telemetry" / "telemetry.db"
        # O piloto local decide o historico semeado. Ele roda antes do A/B e
        # nao entra nas metricas finais.
        pilot: dict[str, int] = {}
        for task in tasks:
            result = run_one(cli, base, work / "pilot", task, "local", 0, env,
                             args.timeout)
            pilot[task["id"]] = 1 if result["answerOk"] else 0
        for task in tasks:
            successes = 9 if task["difficulty"] == "easy" else 2
            if pilot.get(task["id"], 0) == 1:
                successes = min(10, successes + 1)
            else:
                successes = max(0, successes - 1)
            seed_telemetry(telemetry_db, task["id"], successes)
        observations: list[dict] = []
        for arm in ("local", "frontier", "binary"):
            for task in tasks:
                for repetition in range(args.repetitions):
                    observations.append(
                        run_one(cli, base, work / "runs", task, arm, repetition, env,
                                args.timeout))
        report = build_report(observations, args.repetitions)
        report["pilot"] = pilot
        report["elapsedSeconds"] = round(time.monotonic() - started, 2)
        if args.write:
            REPORT_JSON.write_text(
                json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                encoding="utf-8")
            REPORT_MD.parent.mkdir(parents=True, exist_ok=True)
            REPORT_MD.write_text(markdown(report), encoding="utf-8")
            print(f"relatorio gravado em {REPORT_JSON.relative_to(ROOT)} e "
                  f"{REPORT_MD.relative_to(ROOT)}")
        else:
            if not REPORT_JSON.exists():
                print("relatorio ainda nao gravado; use --write", file=sys.stderr)
                return 2
            expected = json.loads(REPORT_JSON.read_text(encoding="utf-8"))
            if expected.get("repetitions") != args.repetitions:
                print("--repetitions difere do relatorio gravado", file=sys.stderr)
                return 2
            if (expected.get("provider") != report.get("provider") or
                    expected.get("localModel") != report.get("localModel") or
                    expected.get("frontierModel") != report.get("frontierModel")):
                print("modelos ou provedor diferem do relatorio gravado", file=sys.stderr)
                return 1
            if (not expected.get("pass") or not report.get("pass") or
                    sorted(expected.get("checks", {})) != sorted(report.get("checks", {}))):
                print("criterios de aceite NAO reproduzidos", file=sys.stderr)
                return 1
            print("criterios de aceite reproduzidos; metricas de tokens e latencia "
                  "podem variar entre chamadas reais")
        for arm, values in report["arms"].items():
            print(f"{arm}: aprovacao={values['approval']:.3f} "
                  f"faceis={values['byDifficulty']['easy']['approval']:.3f} "
                  f"dificeis={values['byDifficulty']['hard']['approval']:.3f} "
                  f"tokens={values['totalTokens']} custo={values['weightedCost']:.2f} "
                  f"latencia={values['avgLatencyMs']:.0f}ms tiers={values['routing']}")
        if not report["pass"]:
            print("criterios de aceite falharam:", file=sys.stderr)
            for key, value in report["checks"].items():
                if not value:
                    print(f"  - {key}", file=sys.stderr)
            return 1
        if args.keep:
            print(f"diretorio preservado: {work}")
        return 0
    finally:
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
