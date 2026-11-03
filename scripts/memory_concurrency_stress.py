#!/usr/bin/env python3
"""Estresse de concorrencia da memoria (Fase 0).

Dispara varios processos do CLI promovendo propostas conflitantes do mesmo
predicado funcional e roda o doctor no fim. Sobre o codigo atual (sem WriteTxn),
o teste deve reproduzir pelo menos uma sobreposicao funcional; depois da Fase 1,
deve terminar com zero.

Uso:
  python scripts/memory_concurrency_stress.py
  python scripts/memory_concurrency_stress.py --processes 8 --proposals 500
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
from concurrent.futures import ThreadPoolExecutor

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-v4"
PROJECT_ID = "memory-v4"
SCOPE = "project:" + PROJECT_ID


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        path = ROOT / name
        if path.exists():
            return path
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def prepare(project: pathlib.Path, proposals: int) -> None:
    db = project / ".arestrix" / "memory" / "memory.db"
    db.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(FIXTURE / "arestrix.project.json", project / "arestrix.project.json") \
        if (FIXTURE / "arestrix.project.json").exists() else None
    conn = sqlite3.connect(db)
    try:
        conn.executescript((FIXTURE / "schema.sql").read_text(encoding="utf-8"))
        conn.executescript((FIXTURE / "seed-knowledge.sql").read_text(encoding="utf-8"))
        for i in range(proposals):
            target = "e_sugar" if i % 2 == 0 else "e_honey"
            payload = json.dumps({
                "subject": "Lia", "subjectId": "e_lia", "predicate": "PREFERS",
                "object": "sugar" if i % 2 == 0 else "honey", "objectId": target,
                "value": "", "unit": "", "polarity": "positive",
                "claim": True, "knownPredicate": True,
            }, ensure_ascii=False)
            conn.execute(
                "INSERT INTO memory_proposal(id,scope_id,kind,payload_json,source_episode_id,"
                "evidence_json,confidence,status,created_at) VALUES(?,?,?,?,?,?,?,?,?)",
                (f"p_{i:04d}", SCOPE, "claim", payload, "ep_T1",
                 json.dumps([{"kind": "fixture"}]), 0.8, "pending", "2026-01-01T00:00:00Z"))
        conn.commit()
    finally:
        conn.close()


def accept(cli: pathlib.Path, project: pathlib.Path, ids: list[str]) -> tuple[list[float], int]:
    durations: list[float] = []
    failures = 0
    env = dict(os.environ)
    env["ARESTRIX_HOME"] = str(ROOT)
    for proposal_id in ids:
        started = time.monotonic()
        proc = subprocess.run(
            [str(cli), "memory", "proposals", "accept", proposal_id,
             "--root", str(project), "--json"],
            cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=120)
        durations.append(time.monotonic() - started)
        if proc.returncode != 0:
            failures += 1
    return durations, failures


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, int(round((len(ordered) - 1) * p))))
    return ordered[index]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", default=str(cli_path()))
    parser.add_argument("--processes", type=int, default=8)
    parser.add_argument("--proposals", type=int, default=500)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    cli = pathlib.Path(args.cli).resolve()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-stress-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    try:
        prepare(project, args.proposals)
        ids = [f"p_{i:04d}" for i in range(args.proposals)]
        buckets = [ids[i::max(1, args.processes)] for i in range(max(1, args.processes))]
        started = time.monotonic()
        with ThreadPoolExecutor(max_workers=len(buckets)) as pool:
            results = list(pool.map(lambda bucket: accept(cli, project, bucket), buckets))
        elapsed = time.monotonic() - started
        durations = [d for part in results for d in part[0]]
        failures = sum(part[1] for part in results)
        doctor = subprocess.run(
            [str(cli), "memory", "doctor", "--root", str(project), "--json"],
            cwd=ROOT, capture_output=True, text=True, encoding="utf-8",
            errors="replace", timeout=120)
        try:
            doctor_json = json.loads(doctor.stdout or "{}")
        except json.JSONDecodeError:
            doctor_json = {}
        overlaps = 0
        for check in doctor_json.get("checks", []):
            if check.get("id") == "functional_overlap":
                overlaps = int(check.get("violations", 0))
        report = {
            "processes": args.processes,
            "proposals": args.proposals,
            "elapsedSeconds": round(elapsed, 2),
            "failures": failures,
            "latencySeconds": {
                "p50": round(percentile(durations, 0.50), 4),
                "p95": round(percentile(durations, 0.95), 4),
            },
            "functionalOverlapViolations": overlaps,
            "doctorExitCode": doctor.returncode,
        }
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 0 if doctor.returncode == 0 and overlaps == 0 else 1
    finally:
        if args.keep:
            print(f"temporario: {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
