#!/usr/bin/env python3
"""Registra a linha de base da Fase 0 (fix5) em um JSON versionado.

Roda a suite C++, o baseline deterministico e o A/B deterministico. Os testes
reais de DeepSeek ja possuem relatorios proprios e entram por referencia, sem
nova chamada de provedor.
"""
from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "tests" / "fixtures" / "memory-fix5-baseline.json"


def run(command: list[str], timeout: int) -> dict:
    started = time.monotonic()
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=timeout)
    output = (result.stdout or "") + (result.stderr or "")
    return {
        "command": command,
        "exitCode": result.returncode,
        "seconds": round(time.monotonic() - started, 2),
        "stdoutTail": output[-4000:],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    exe = ROOT / "build" / ("arestrix_tests.exe" if sys.platform == "win32" else "arestrix_tests")
    report = {
        "schemaVersion": 1,
        "phase": "fix5-phase0",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "version": (ROOT / "version.txt").read_text(encoding="utf-8").strip(),
        "cpp": run([str(exe)], args.timeout) if exe.exists() else {"error": f"ausente: {exe}"},
        "baseline": run([sys.executable, "scripts/memory_baseline.py", "--runs", "3", "--check"],
                        args.timeout),
        "ab": run([sys.executable, "scripts/e2e_memory_ab.py"], args.timeout),
    }
    if isinstance(report["cpp"].get("stdoutTail"), str):
        match = re.search(r"(\d+)/(\d+) verificacoes passaram", report["cpp"]["stdoutTail"])
        if match:
            report["cpp"]["checksPassed"] = int(match.group(1))
            report["cpp"]["checksTotal"] = int(match.group(2))
    phase1 = ROOT / "tests" / "fixtures" / "memory-fix5-concurrency-phase1.json"
    if phase1.exists():
        try:
            report["phase1Concurrency"] = json.loads(phase1.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as ex:
            report["phase1Concurrency"] = {"error": str(ex)}
    for name, path in (
        ("deepseek", ROOT / "tests" / "fixtures" / "memory-deepseek-long-report.json"),
        ("promptReduction", ROOT / "tests" / "fixtures" / "memory-prompt-reduction-report.json"),
    ):
        if path.exists():
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                report[name] = {
                    "report": str(path.relative_to(ROOT)),
                    "pass": data.get("pass"),
                    "totalTokens": data.get("totalTokens"),
                }
            except (OSError, json.JSONDecodeError) as ex:
                report[name] = {"error": str(ex)}
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"linha de base: {OUT.relative_to(ROOT)}")
    return 0 if all(
        step.get("exitCode", 0) == 0 for step in (report["cpp"], report["baseline"], report["ab"])
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
