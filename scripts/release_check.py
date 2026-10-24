#!/usr/bin/env python3
"""Gate reproduzivel de release do Arestrix.

Roda o ciclo canonico (build completo, CTest, Vitest, checks, gateway smoke e,
opcionalmente, E2E) e gera o manifesto/SHA256SUMS/SBOM em `release/`.

Uso:
  python scripts/release_check.py
  python scripts/release_check.py --skip-e2e
  python scripts/release_check.py --allow-dirty --output release-local

O gate deve rodar em uma arvore limpa para um release oficial. Artefatos gerados
em `release/` sao ignorados pelo Git.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def display_path(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except (OSError, ValueError):
        return path.name


def display_command(command: list[str]) -> list[str]:
    shown = list(command)
    if shown:
        shown[0] = "python"
    for index, item in enumerate(shown):
        try:
            shown[index] = Path(item).resolve().relative_to(ROOT).as_posix()
        except (OSError, ValueError):
            pass
    return shown


def run_step(name: str, command: list[str], report: dict) -> bool:
    print(f"\n[release] {name}: {' '.join(command)}", flush=True)
    started = time.monotonic()
    result = subprocess.run(command, cwd=ROOT)
    duration_ms = int((time.monotonic() - started) * 1000)
    step = {
        "name": name,
        "command": display_command(command),
        "exitCode": result.returncode,
        "durationMs": duration_ms,
    }
    report["steps"].append(step)
    print(f"[release] {name}: {'OK' if result.returncode == 0 else 'FALHOU'} ({duration_ms} ms)", flush=True)
    return result.returncode == 0


def write_report(output: Path, report: dict) -> None:
    output.mkdir(parents=True, exist_ok=True)
    (output / "release-report.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Gate de release do Arestrix")
    parser.add_argument("--output", default="release", help="diretorio de saida (padrao: release)")
    parser.add_argument("--skip-e2e", action="store_true", help="pula tutorial/exemplos ponta a ponta")
    parser.add_argument("--allow-dirty", action="store_true", help="permite arvore Git suja (uso local)")
    parser.add_argument("--artifact", action="append", default=[], help="artefato adicional para o manifesto")
    args = parser.parse_args()

    output = (ROOT / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output)
    dirty = bool(git("status", "--porcelain"))
    report: dict = {
        "schemaVersion": 1,
        "product": "Arestrix",
        "version": (ROOT / "version.txt").read_text(encoding="utf-8").strip(),
        "commit": git("rev-parse", "HEAD"),
        "startedAt": now_iso(),
        "finishedAt": "",
        "status": "running",
        "dirty": dirty,
        "output": display_path(output),
        "steps": [],
        "manifest": "",
    }

    if dirty and not args.allow_dirty:
        report["status"] = "failed"
        report["error"] = "arvore Git suja; use --allow-dirty apenas para testes locais"
        report["finishedAt"] = now_iso()
        write_report(output, report)
        print("[release] FALHOU: arvore Git suja. Use --allow-dirty para testes locais.", file=sys.stderr)
        return 1

    command = [sys.executable, str(ROOT / "CppCore" / "dev.py"), "--full"]
    if not args.skip_e2e:
        command.append("--e2e")
    step_name = "build-tests-checks" if args.skip_e2e else "build-tests-checks-e2e"
    if not run_step(step_name, command, report):
        report["status"] = "failed"
        report["finishedAt"] = now_iso()
        write_report(output, report)
        return 1

    dirty_after = bool(git("status", "--porcelain"))
    report["dirtyAfter"] = dirty_after
    if dirty_after and not args.allow_dirty:
        report["status"] = "failed"
        report["error"] = "o gate deixou a arvore Git suja; um teste escreveu em arquivo versionado"
        report["finishedAt"] = now_iso()
        write_report(output, report)
        print("[release] FALHOU: o gate deixou a arvore Git suja.", file=sys.stderr)
        return 1

    manifest_command = [sys.executable, str(ROOT / "scripts" / "release_manifest.py"), "--output", str(output)]
    for artifact in args.artifact:
        manifest_command += ["--artifact", artifact]
    if not run_step("release-manifest", manifest_command, report):
        report["status"] = "failed"
        report["finishedAt"] = now_iso()
        write_report(output, report)
        return 1

    report["status"] = "passed"
    report["finishedAt"] = now_iso()
    report["manifest"] = display_path(output / "release-manifest.json")
    write_report(output, report)
    print(f"\n[release] APROVADO: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
