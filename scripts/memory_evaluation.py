#!/usr/bin/env python3
"""Avaliacao final da memoria (E20).

Executa as variantes do artigo sobre o mesmo fixture curado e o mesmo orcamento
de contexto. O corpus de casos negativos fica em
`tests/fixtures/memory-eval-v1.json`; o relatorio reproduzivel fica em
`docs/internal/memoria-avaliacao-v1.md` e no JSON ao lado do fixture.

Uso:
  python scripts/memory_evaluation.py --write
  python scripts/memory_evaluation.py --check
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import pathlib
import shutil
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
import memory_baseline  # noqa: E402

CASES = ROOT / "tests" / "fixtures" / "memory-eval-v1.json"
REPORT_JSON = ROOT / "tests" / "fixtures" / "memory-eval-v1-report.json"
REPORT_MD = ROOT / "docs" / "internal" / "memoria-avaliacao-v1.md"
BASE = ROOT / "tests" / "fixtures" / "memory-baseline"


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        candidate = ROOT / name
        if candidate.exists():
            return candidate
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def set_memory(settings: dict, key: str, value) -> None:
    memory = settings.setdefault("memory", {})
    memory[key] = value


def variant_pipeline(variant: str) -> dict:
    pipeline = json.loads((BASE / "pipeline.json").read_text(encoding="utf-8"))
    defaults = pipeline.setdefault("metadata", {}).setdefault("globalDefaults", {})
    if variant == "hybrid_fallback":
        set_memory(defaults, "embeddingEnabled", False)
    elif variant == "facets_traversal":
        defaults["memory"] = {**defaults.get("memory", {}), "lenses": True,
                              "traversal": True}
        defaults["memory"]["consolidationMode"] = "idle"
        defaults["memory"]["consolidationIdleSeconds"] = 5
    elif variant == "paths_serving":
        defaults["memory"] = {**defaults.get("memory", {}),
                              "lenses": True,
                              "paths": {"enabled": True, "minDistinctInputs": 1},
                              "pathServing": {"enabled": True, "minUses": 1,
                                              "minUtility": 0.4, "maxPaths": 2},
                              "consolidationMode": "idle",
                              "consolidationIdleSeconds": 5}
    elif variant == "routing_shadow":
        defaults["routing"] = {
            "mode": "shadow",
            "pMin": 0.5,
            "tiers": {
                "localLarge": {"provider": "local", "model": "local-shadow", "cost": 0.2},
                "frontier": {"provider": "deepseek", "model": "deepseek-flash", "cost": 1.0}
            }
        }
    for node in pipeline.get("nodes", []):
        if node.get("type") != "agent":
            continue
        settings = node.setdefault("agentSettings", {})
        memory = settings.get("memory")
        if not isinstance(memory, dict):
            continue
        if variant == "facets_traversal":
            memory["lenses"] = True
            memory["traversal"] = True
            memory["consolidationMode"] = "idle"
            memory["consolidationIdleSeconds"] = 5
        elif variant == "paths_serving":
            memory["lenses"] = True
            memory["paths"] = {"enabled": True, "minDistinctInputs": 1}
            memory["pathServing"] = {"enabled": True, "minUses": 1,
                                     "minUtility": 0.4, "maxPaths": 2}
            memory["consolidationMode"] = "idle"
            memory["consolidationIdleSeconds"] = 5
    return pipeline


def run_variant(variant: str, runs: int) -> dict:
    temp = pathlib.Path(tempfile.mkdtemp(prefix=f"arestrix-eval-{variant}-"))
    original_fixture = memory_baseline.FIXTURE
    try:
        fixture = temp / "fixture"
        fixture.mkdir()
        shutil.copy2(BASE / "knowledge.json", fixture / "knowledge.json")
        shutil.copy2(BASE / "arestrix.project.json", fixture / "arestrix.project.json")
        (fixture / "pipeline.json").write_text(
            json.dumps(variant_pipeline(variant), ensure_ascii=False, indent=2),
            encoding="utf-8")
        memory_baseline.FIXTURE = fixture
        memory_baseline.failure_reasons.clear()
        baseline, _ = memory_baseline.run_baseline(cli_path(), runs, False)
        if memory_baseline.failure_reasons:
            raise RuntimeError("; ".join(memory_baseline.failure_reasons))
        metrics = baseline["metrics"]
        return {
            "variant": variant,
            "ok": True,
            "runs": metrics["runs"],
            "tokens": metrics["tokens"],
            "turns": metrics["turns"],
            "briefChars": metrics["brief"]["chars"],
            "hits": metrics["hits"],
            "invalidCitations": metrics["invalidCitations"],
            "schemaFingerprint": metrics["schemaFingerprint"],
        }
    finally:
        memory_baseline.FIXTURE = original_fixture
        shutil.rmtree(temp, ignore_errors=True)


def build_report(variants: list[dict]) -> dict:
    cases = json.loads(CASES.read_text(encoding="utf-8"))
    return {
        "schema": 1,
        "stage": "E20",
        "seed": cases.get("seed"),
        "cases": cases.get("cases", []),
        "variants": variants,
        "acceptance": {
            "allVariantsComplete": all(item["ok"] for item in variants),
            "negativeCasesPresent": len(cases.get("cases", [])) >= 6,
            "defaultsOff": ["pathServing", "traversal", "routing.mode", "bridges"],
            "note": "interruptores experimentais permanecem desligados por padrao",
        },
    }


def markdown(report: dict) -> str:
    lines = [
        "# Avaliação final da memória (E20)",
        "",
        f"- Semente: `{report['seed']}`",
        f"- Casos: {len(report['cases'])} (positivos e negativos)",
        "- Conhecimento e orçamento de contexto: fixture `memory-baseline`",
        "",
        "## Variantes",
        "",
        "| Variante | OK | Tokens | Chamadas | Brief | Injetados | Citados |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for item in report["variants"]:
        hits = item["hits"]
        lines.append(
            f"| {item['variant']} | {item['ok']} | {item['tokens']['total']} | "
            f"{item['turns']['modelCalls']} | {item['briefChars']} | "
            f"{hits['injected']} | {hits['used']} |")
    lines += [
        "",
        "## Critérios fixados antes da execução",
        "",
        "- todas as variantes terminam com código 0;",
        "- os casos negativos estão presentes no corpus;",
        "- nenhum interruptor experimental passa a ligado por padrão.",
        "",
        "## Decisão",
        "",
        "`pathServing`, `traversal`, `routing.mode` e `bridges` permanecem em "
        "`off` até que uma próxima avaliação com dados reais supere a referência "
        "e registre o Δ de custo. O corpus e este relatório são reproduzíveis "
        "com a mesma semente e o mesmo conhecimento congelado.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--runs", type=int, default=1)
    args = parser.parse_args()
    if args.write == args.check:
        print("use --write ou --check", file=sys.stderr)
        return 2
    if not cli_path().exists():
        print(f"CLI nao encontrado: {cli_path()}", file=sys.stderr)
        return 2
    cases = json.loads(CASES.read_text(encoding="utf-8"))
    variants = []
    for name in cases.get("variants", []):
        print(f"[eval] {name}")
        variants.append(run_variant(name, max(1, args.runs)))
    report = build_report(variants)
    if args.write:
        REPORT_JSON.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                               encoding="utf-8")
        REPORT_MD.parent.mkdir(parents=True, exist_ok=True)
        REPORT_MD.write_text(markdown(report), encoding="utf-8")
        print(f"relatorio gravado em {REPORT_JSON} e {REPORT_MD}")
        return 0
    if not REPORT_JSON.exists():
        print("relatorio ainda nao gravado; use --write", file=sys.stderr)
        return 2
    expected = json.loads(REPORT_JSON.read_text(encoding="utf-8"))
    problems = []
    if expected.get("seed") != report["seed"]:
        problems.append("semente diferente")
    if len(expected.get("cases", [])) != len(report["cases"]):
        problems.append("corpus de casos diferente")
    for want, got in zip(expected.get("variants", []), report["variants"]):
        if want.get("variant") != got.get("variant"):
            problems.append(f"variante diferente: {want.get('variant')} != {got.get('variant')}")
            continue
        for key in ("ok", "tokens", "turns", "briefChars", "hits", "invalidCitations"):
            if want.get(key) != got.get(key):
                problems.append(f"{got['variant']}: {key} divergiu")
    if problems:
        print("avaliacao NAO reproduzida:", file=sys.stderr)
        for problem in problems:
            print(" - " + problem, file=sys.stderr)
        return 1
    print("avaliacao reproduzida")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
