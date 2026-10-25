#!/usr/bin/env python3
"""Gera manifesto de release, SHA256SUMS e SBOM do Arestrix.

Uso:
  python scripts/release_manifest.py
  python scripts/release_manifest.py --output release
  python scripts/release_manifest.py --artifact build/arestrix.exe --artifact CppCore/dist/index.html

O diretorio de saida e ignorado pelo Git. Nenhum segredo e lido ou gravado.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

NATIVE_COMPONENTS = {
    "third_party/taskflow": ("taskflow", "MIT"),
    "third_party/simdjson": ("simdjson", "Apache-2.0"),
    "third_party/FastWebUI": ("FastWebUI", "MIT"),
    "third_party/i18next": ("i18next", "MIT"),
    "third_party/monaco-editor": ("monaco-editor", "MIT"),
    "third_party/SkillSpector": ("SkillSpector", "Apache-2.0"),
    "third_party/security-audit-skill": ("Cloudflare security-audit-skill", "MIT"),
    "third_party/fonts/inter": ("Inter font", "OFL-1.1"),
    "third_party/sqlite": ("SQLite", "blessing"),
    "third_party/httplib": ("cpp-httplib", "MIT"),
}

DEFAULT_ARTIFACTS = [
    "build/arestrix.exe",
    "build/arestrix",
    "build/arestrix-cli.exe",
    "build/arestrix-cli",
    "build/arestrix-gateway.exe",
    "build/arestrix-gateway",
    "build/arestrix-memory.exe",
    "build/arestrix-memory",
    "build/arestrix_core.lib",
    "build/libarestrix_core.a",
]


def run_git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def git_commit() -> str:
    return run_git("rev-parse", "HEAD")


def git_dirty() -> bool:
    return bool(run_git("status", "--porcelain"))


def version() -> str:
    return (ROOT / "version.txt").read_text(encoding="utf-8").strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def add_artifact(path: Path, artifacts: dict[str, dict]) -> None:
    if not path.is_file():
        return
    try:
        relative = path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        relative = path.as_posix()
    if relative in artifacts:
        return
    artifacts[relative] = {
        "path": relative,
        "sha256": sha256(path),
        "sizeBytes": path.stat().st_size,
    }


def discover_artifacts(extra: list[str]) -> list[dict]:
    artifacts: dict[str, dict] = {}
    for item in DEFAULT_ARTIFACTS:
        add_artifact(ROOT / item, artifacts)
    for item in extra:
        add_artifact(Path(item) if Path(item).is_absolute() else ROOT / item, artifacts)

    dist = ROOT / "CppCore" / "dist"
    if dist.is_dir():
        for path in sorted(p for p in dist.rglob("*") if p.is_file()):
            add_artifact(path, artifacts)

    runtime = ROOT / "CppCore" / "runtime"
    if runtime.is_dir():
        for path in sorted(p for p in runtime.glob("*.json") if p.is_file()):
            add_artifact(path, artifacts)

    for name in ("version.txt", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md"):
        add_artifact(ROOT / name, artifacts)

    if not artifacts:
        raise SystemExit("[release] nenhum artefato encontrado; rode o build antes.")
    return [artifacts[key] for key in sorted(artifacts)]


def native_sbom() -> list[dict]:
    components: list[dict] = []
    for relative, (name, license_name) in NATIVE_COMPONENTS.items():
        path = ROOT / relative
        if not path.exists():
            continue
        commit = run_git("-C", str(path), "rev-parse", "HEAD")
        components.append({
            "type": "native",
            "name": name,
            "path": relative,
            "version": commit or "unknown",
            "license": license_name,
        })
    return components


def npm_sbom() -> list[dict]:
    lock_path = ROOT / "package-lock.json"
    if not lock_path.exists():
        return []
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    packages = lock.get("packages", {})
    components: list[dict] = []
    for key in sorted(packages):
        if not key.startswith("node_modules/"):
            continue
        value = packages[key]
        name = key.rsplit("node_modules/", 1)[-1]
        components.append({
            "type": "npm",
            "name": name,
            "path": key,
            "version": value.get("version", "unknown"),
            "license": value.get("license", "UNKNOWN"),
            "dev": bool(value.get("dev", False)),
        })
    return components


def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Gera manifesto de release do Arestrix")
    parser.add_argument("--output", default="release", help="diretorio de saida (padrao: release)")
    parser.add_argument("--artifact", action="append", default=[], help="artefato adicional")
    args = parser.parse_args()

    output = (ROOT / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    generated_at = datetime.now(timezone.utc).isoformat()
    product_version = version()
    commit = git_commit()
    artifacts = discover_artifacts(args.artifact)

    checksums = output / "SHA256SUMS"
    checksums.write_text(
        "".join(f"{artifact['sha256']}  {artifact['path']}\n" for artifact in artifacts),
        encoding="utf-8",
    )

    components = native_sbom() + npm_sbom()
    sbom = {
        "schemaVersion": 1,
        "product": "Arestrix",
        "version": product_version,
        "commit": commit,
        "generatedAt": generated_at,
        "components": components,
    }
    write_json(output / "SBOM.json", sbom)

    manifest = {
        "schemaVersion": 1,
        "product": "Arestrix",
        "version": product_version,
        "commit": commit,
        "generatedAt": generated_at,
        "dirty": git_dirty(),
        "platform": {
            "system": platform.system(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "artifacts": artifacts,
        "checksumsFile": "SHA256SUMS",
        "sbomFile": "SBOM.json",
        "sbomComponents": len(components),
    }
    write_json(output / "release-manifest.json", manifest)
    print(f"[release] {len(artifacts)} artefato(s), {len(components)} componente(s) no SBOM")
    print(f"[release] saida: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
