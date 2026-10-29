#!/usr/bin/env python3
"""Cria um pacote portatil reproduzivel do Arestrix.

O pacote preserva o layout esperado pelo Studio e pelo BuildService:

  arestrix(.exe), arestrix-cli(.exe), arestrix-gateway(.exe), arestrix-memory(.exe)
  dist/                         UI compilada
  CppCore/src/                  headers/fontes usados ao compilar pipelines
  CppCore/runtime/              catalogo e manifesto do runtime local
  runtime/lib/                  biblioteca nativa do runtime
  third_party/taskflow/         headers do Taskflow
  third_party/simdjson/         headers do simdjson
  examples/, docs/              exemplos e documentacao
  .agents/skills/               skills de desenvolvimento, incluindo arestrix-skill-guard
  package-manifest.json, SHA256SUMS, SBOM.json

Requer que o gate de release (ou ao menos release_manifest.py) tenha gerado
release/SBOM.json. O compilador C++ do sistema ainda e necessario para compilar
pipelines; o pacote nao inclui MSVC/GCC.

Uso:
  python scripts/package_release.py
  python scripts/package_release.py --installer
  python scripts/package_release.py --format both --allow-dirty
  python scripts/package_release.py --skip-smoke --keep-stage

O instalador Windows (--installer) usa o Inno Setup 6 e e gerado a partir do
mesmo staging do pacote portatil; requer ISCC.exe no PATH (ou variavel ISCC).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run_git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def ignore_generated(directory: str, names: list[str]) -> set[str]:
    ignored = {"__pycache__", ".git", "node_modules", "build", ".arestrix", ".workspace"}
    return {name for name in names if name in ignored or name.endswith((".pyc", ".obj", ".pdb", ".log"))}


def copy_tree(source: Path, target: Path) -> None:
    if not source.is_dir():
        raise SystemExit(f"[package] diretorio ausente: {source}")
    shutil.copytree(source, target, ignore=ignore_generated)


def find_binary(base: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    path = ROOT / "build" / (base + suffix)
    if not path.is_file():
        raise SystemExit(f"[package] binario ausente: {path}; rode o gate de release primeiro.")
    return path


def core_library() -> Path:
    candidates = [ROOT / "build" / "arestrix_core.lib", ROOT / "build" / "libarestrix_core.a"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit("[package] biblioteca nativa do runtime ausente; rode o gate de release primeiro.")


def platform_slug() -> tuple[str, str]:
    system = platform.system().lower()
    if system == "windows":
        name = "windows"
    elif system == "darwin":
        name = "macos"
    else:
        name = "linux"
    machine = platform.machine().lower()
    arch = "x64" if machine in ("amd64", "x86_64") else ("arm64" if machine in ("arm64", "aarch64") else machine)
    return name, arch


def find_iscc() -> Path:
    """Localiza o compilador do Inno Setup 6 (ISCC.exe)."""
    env_path = os.environ.get("ISCC") or os.environ.get("INNO_SETUP_ISCC")
    if env_path and Path(env_path).is_file():
        return Path(env_path)

    for executable in ("ISCC", "iscc"):
        found = shutil.which(executable)
        if found:
            return Path(found)

    roots = [
        os.environ.get("ProgramFiles"),
        os.environ.get("ProgramFiles(x86)"),
        os.environ.get("LOCALAPPDATA") and str(Path(os.environ["LOCALAPPDATA"]) / "Programs"),
    ]
    for root in roots:
        if not root:
            continue
        candidate = Path(root) / "Inno Setup 6" / "ISCC.exe"
        if candidate.is_file():
            return candidate

    raise SystemExit(
        "[installer] Inno Setup 6 nao encontrado. Instale o Inno Setup 6 ou defina ISCC "
        "(caminho completo para ISCC.exe)."
    )


def numeric_version(version: str) -> str:
    """Converte SemVer em uma versao numerica de 4 partes para o metadata do setup."""
    match = re.match(r"^(\d+)\.(\d+)\.(\d+)", version)
    if not match:
        raise SystemExit(f"[installer] versao invalida para o Inno Setup: {version}")
    return f"{match.group(1)}.{match.group(2)}.{match.group(3)}.0"


def build_windows_installer(stage: Path, release_dir: Path, version: str, arch: str) -> Path:
    """Compila o instalador Inno Setup a partir do staging ja validado."""
    if os.name != "nt":
        raise SystemExit("[installer] o instalador Inno Setup so pode ser gerado no Windows.")

    script = ROOT / "installer" / "arestrix.iss"
    icon = ROOT / "installer" / "arestrix.ico"
    for required in (script, icon):
        if not required.is_file():
            raise SystemExit(f"[installer] arquivo ausente: {required}")

    output_base = f"arestrix-{version}-windows-{arch}-setup"
    iscc = find_iscc()
    command = [
        str(iscc),
        f"/DAppVersion={version}",
        f"/DAppVersionNumeric={numeric_version(version)}",
        f"/DAppArch={arch}",
        f"/DStageDir={stage}",
        f"/DOutputDir={release_dir}",
        f"/DOutputBaseFilename={output_base}",
        f"/DIconFile={icon}",
        str(script),
    ]
    print("[installer]", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr[-5000:], file=sys.stderr)
        raise SystemExit(f"[installer] ISCC falhou (codigo {result.returncode}).")

    target = release_dir / f"{output_base}.exe"
    if not target.is_file():
        raise SystemExit(f"[installer] saida esperada nao encontrada: {target}")

    if os.environ.get("ARESTRIX_SIGN_PFX") or os.environ.get("ARESTRIX_SIGN_THUMBPRINT"):
        sign_script = ROOT / "scripts" / "sign-windows.ps1"
        shell = shutil.which("pwsh") or shutil.which("powershell")
        if not shell:
            raise SystemExit("[installer] assinatura solicitada, mas pwsh/powershell nao encontrado.")
        subprocess.run(
            [shell, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(sign_script), "-Path", str(target)],
            cwd=ROOT,
            check=True,
        )

    print(f"[installer] {target} ({target.stat().st_size} bytes)")
    return target


def write_portable_readme(stage: Path, product_version: str, system: str) -> None:
    if system == "windows":
        lines = [
            f"# Arestrix {product_version} - portable package",
            "",
            "Run the Studio:",
            "",
            "```bat",
            "start-arestrix.bat",
            "```",
            "",
            "CLI:",
            "",
            "```bat",
            "arestrix-cli.exe version",
            "arestrix-cli.exe project validate --project examples\\01-svg-agente",
            "```",
            "",
            "The folder is the `ARESTRIX_HOME`. Compiling pipelines additionally requires MSVC Build Tools",
            "(Desktop development with C++) and the Visual C++ Redistributable on the target machine.",
            "Optional prose compression uses ONNX Runtime (bundled) and DirectML on capable GPUs;",
            "the Kompress model is downloaded on demand from Studio.",
            "The managed local runtime is downloaded to the user data folder when selected.",
        ]
    else:
        lines = [
            f"# Arestrix {product_version} - portable package",
            "",
            "Run the Studio:",
            "",
            "```bash",
            "chmod +x arestrix start-arestrix.sh",
            "./start-arestrix.sh",
            "```",
            "",
            "CLI:",
            "",
            "```bash",
            "chmod +x arestrix-cli",
            "./arestrix-cli version",
            "./arestrix-cli project validate --project examples/01-svg-agente",
            "```",
            "",
            "The folder is the `ARESTRIX_HOME`. Compiling pipelines additionally requires g++ >= 11 or",
            "clang++ >= 14 and libcurl development headers. The managed local runtime is downloaded to",
            "the user data folder when selected.",
        ]
    lines += [
        "",
        "## Integrity",
        "",
        "- `package-manifest.json` lists every file with SHA-256 and size.",
        "- `SHA256SUMS` can be checked with `sha256sum -c SHA256SUMS`.",
        "- `SBOM.json` is the component inventory from the release gate.",
        "",
        "No API keys or user data are included.",
        "",
    ]
    (stage / "README-PORTABLE.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_launchers(stage: Path, system: str) -> None:
    if system == "windows":
        (stage / "start-arestrix.bat").write_text(
            "@echo off\r\n"
            "set \"ARESTRIX_HOME=%~dp0\"\r\n"
            "cd /d \"%~dp0\"\r\n"
            "start \"\" \"%~dp0arestrix.exe\"\r\n",
            encoding="utf-8",
            newline="",
        )
    else:
        script = stage / "start-arestrix.sh"
        script.write_text(
            "#!/usr/bin/env sh\n"
            "set -eu\n"
            "HERE=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
            "export ARESTRIX_HOME=\"$HERE\"\n"
            "exec \"$HERE/arestrix\" \"$@\"\n",
            encoding="utf-8",
            newline="\n",
        )
        script.chmod(0o755)


def file_inventory(stage: Path, exclude: set[str]) -> list[dict]:
    files: list[dict] = []
    for path in sorted(p for p in stage.rglob("*") if p.is_file()):
        relative = path.relative_to(stage).as_posix()
        if relative in exclude:
            continue
        files.append({"path": relative, "sha256": sha256(path), "sizeBytes": path.stat().st_size})
    return files


def write_manifests(stage: Path, product_version: str, commit: str, dirty: bool, system: str, arch: str) -> None:
    files = file_inventory(stage, {"package-manifest.json", "SHA256SUMS"})
    manifest = {
        "schemaVersion": 1,
        "product": "Arestrix",
        "version": product_version,
        "commit": commit,
        "dirty": dirty,
        "platform": {"system": system, "arch": arch},
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "files": files,
        "sbomFile": "SBOM.json",
    }
    manifest_path = stage / "package-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    sums = [(item["sha256"], item["path"]) for item in files]
    sums.append((sha256(manifest_path), "package-manifest.json"))
    (stage / "SHA256SUMS").write_text(
        "".join(f"{digest}  {relative}\n" for digest, relative in sorted(sums, key=lambda item: item[1])),
        encoding="utf-8",
        newline="\n",
    )


ONNX_RUNTIME_FILES = ("onnxruntime.dll", "onnxruntime_providers_shared.dll", "DirectML.dll")


def _onnx_search_dirs() -> list[Path]:
    dirs: list[Path] = []
    root = os.environ.get("ONNXRUNTIME_ROOT")
    if root:
        dirs += [Path(root) / "bin", Path(root) / "runtimes" / "win-x64" / "native"]
    directml = os.environ.get("ARESTRIX_DIRECTML_BIN")
    if directml:
        dirs.append(Path(directml))
    dirs.append(ROOT / "build")
    return dirs


def copy_onnx_runtime(stage: Path, require_onnx: bool, require_directml: bool) -> None:
    dirs = _onnx_search_dirs()
    required = ["onnxruntime.dll"] if (require_onnx or require_directml) else []
    if require_directml:
        required += ["onnxruntime_providers_shared.dll", "DirectML.dll"]

    copied: list[str] = []
    for name in ONNX_RUNTIME_FILES:
        source = next((directory / name for directory in dirs if (directory / name).is_file()), None)
        if source is None:
            if name in required:
                raise SystemExit(
                    f"[package] {name} ausente; rode scripts/fetch_onnxruntime.py e defina "
                    "ONNXRUNTIME_ROOT."
                )
            continue
        shutil.copy2(source, stage / name)
        copied.append(name)

    root = os.environ.get("ONNXRUNTIME_ROOT")
    if root:
        for candidate in (Path(root) / "lib" / "onnxruntime.lib",
                          Path(root) / "runtimes" / "win-x64" / "native" / "onnxruntime.lib"):
            if candidate.is_file():
                target = stage / "runtime" / "lib"
                target.mkdir(parents=True, exist_ok=True)
                shutil.copy2(candidate, target / "onnxruntime.lib")
                copied.append("runtime/lib/onnxruntime.lib")
                break

    license_dir = Path(root) / "licenses" if root else None
    if license_dir and license_dir.is_dir():
        target = stage / "licenses"
        target.mkdir(exist_ok=True)
        for item in sorted(license_dir.iterdir()):
            if item.is_file():
                shutil.copy2(item, target / item.name)
    elif required:
        raise SystemExit(
            "[package] licencas do ONNX Runtime/DirectML ausentes em "
            f"{license_dir or 'ONNXRUNTIME_ROOT'}; rode scripts/fetch_onnxruntime.py."
        )

    if require_onnx and not (stage / "runtime" / "lib" / "onnxruntime.lib").is_file():
        raise SystemExit(
            "[package] onnxruntime.lib ausente em runtime/lib; o pipeline compilado precisaria "
            "de ONNX e nao teria a import lib."
        )

    if copied:
        print(f"[package] runtime ONNX: {', '.join(copied)}")


def smoke_test(stage: Path, system: str, require_onnx: bool = False, require_directml: bool = False) -> None:
    binary = stage / ("arestrix-cli.exe" if system == "windows" else "arestrix-cli")
    memory = stage / ("arestrix-memory.exe" if system == "windows" else "arestrix-memory")
    for command in (
        [str(binary), "version"],
        [str(binary), "project", "validate", "--project", str(stage / "examples" / "01-svg-agente"), "--json"],
        [str(memory), "version"],
    ):
        result = subprocess.run(command, cwd=stage, capture_output=True, text=True, timeout=120)
        if result.returncode != 0:
            print(result.stdout[-3000:])
            print(result.stderr[-3000:], file=sys.stderr)
            raise SystemExit(f"[package] smoke test falhou: {' '.join(command)}")

    if require_onnx:
        result = subprocess.run(
            [str(binary), "compress", "--status", "--json"],
            cwd=stage, capture_output=True, text=True, timeout=120
        )
        if result.returncode != 0:
            print(result.stderr[-3000:], file=sys.stderr)
            raise SystemExit("[package] smoke test do ONNX falhou ao consultar --status")
        try:
            status = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise SystemExit(f"[package] --status nao devolveu JSON: {error}") from error
        if not status.get("onnx"):
            raise SystemExit("[package] binario empacotado sem ONNX; build de release incorreto")
        if require_directml and not status.get("dmlBuild"):
            raise SystemExit("[package] binario empacotado sem DirectML; build de release incorreto")
        print(f"[package] ONNX ok (dmlBuild={bool(status.get('dmlBuild'))})")


def create_archive(stage: Path, output: Path, archive_format: str) -> Path:
    output.parent.mkdir(parents=True, exist_ok=True)
    if archive_format == "zip":
        target = Path(str(output) + ".zip")
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    archive.write(path, (stage.name + "/" + path.relative_to(stage).as_posix()))
        return target
    target = Path(str(output) + ".tar.gz")
    with tarfile.open(target, "w:gz") as archive:
        archive.add(stage, arcname=stage.name)
    return target


def main() -> int:
    parser = argparse.ArgumentParser(description="Cria pacote portatil do Arestrix")
    parser.add_argument("--output", default="release", help="diretorio de saida (padrao: release)")
    parser.add_argument("--format", choices=["zip", "tar.gz", "both"], default=None,
                        help="formato do arquivo (padrao: zip no Windows, tar.gz nos demais)")
    parser.add_argument("--installer", action="store_true",
                        help="gera o instalador Windows com Inno Setup a partir do mesmo staging")
    parser.add_argument("--allow-dirty", action="store_true", help="permite arvore Git suja (uso local)")
    parser.add_argument("--skip-smoke", action="store_true", help="pula o smoke test dos binarios")
    parser.add_argument("--require-onnx", action="store_true",
                        help="exige ONNX Runtime no pacote (build de release)")
    parser.add_argument("--require-directml", action="store_true",
                        help="exige DirectML no pacote (implica --require-onnx)")
    parser.add_argument("--keep-stage", action="store_true", help="mantem a pasta staging para inspecao")
    args = parser.parse_args()

    system, arch = platform_slug()
    version = (ROOT / "version.txt").read_text(encoding="utf-8").strip()
    commit = run_git("rev-parse", "HEAD")
    dirty = bool(run_git("status", "--porcelain"))
    if dirty and not args.allow_dirty:
        raise SystemExit("[package] arvore Git suja; use --allow-dirty apenas para testes locais.")

    release_dir = (ROOT / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output)
    sbom = release_dir / "SBOM.json"
    if not sbom.is_file():
        raise SystemExit(f"[package] {sbom} ausente; rode scripts/release_check.py ou release_manifest.py primeiro.")
    try:
        sbom_commit = json.loads(sbom.read_text(encoding="utf-8")).get("commit", "")
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"[package] SBOM invalido: {error}")
    if sbom_commit != commit:
        raise SystemExit(
            "[package] o SBOM nao corresponde ao HEAD "
            f"({sbom_commit or 'ausente'} != {commit}); rode scripts/release_check.py no commit atual."
        )

    dist = ROOT / "dist"
    if not (dist / "index.html").is_file():
        raise SystemExit("[package] dist/index.html ausente; rode npm run build primeiro.")

    stage_parent = Path(tempfile.mkdtemp(prefix="arestrix-package-", dir=str(release_dir if release_dir.is_dir() else ROOT)))
    stage = stage_parent / f"arestrix-{version}-{system}-{arch}"
    stage.mkdir(parents=True)
    try:
        for base in ("arestrix", "arestrix-cli", "arestrix-gateway", "arestrix-memory"):
            shutil.copy2(find_binary(base), stage / find_binary(base).name)

        require_onnx = bool(args.require_onnx or args.require_directml)
        copy_onnx_runtime(stage, require_onnx, bool(args.require_directml))

        copy_tree(dist, stage / "dist")
        copy_tree(ROOT / "CppCore" / "src", stage / "CppCore" / "src")
        copy_tree(ROOT / "CppCore" / "runtime", stage / "CppCore" / "runtime")
        copy_tree(ROOT / "third_party" / "taskflow", stage / "third_party" / "taskflow")
        copy_tree(ROOT / "third_party" / "simdjson" / "singleheader", stage / "third_party" / "simdjson" / "singleheader")
        for license_name in ("LICENSE", "LICENSE-MIT"):
            license_path = ROOT / "third_party" / "simdjson" / license_name
            if license_path.is_file():
                shutil.copy2(license_path, stage / "third_party" / "simdjson" / license_name)
        copy_tree(ROOT / "examples", stage / "examples")
        copy_tree(ROOT / ".agents", stage / ".agents")
        copy_tree(ROOT / "docs" / "en", stage / "docs" / "en")
        copy_tree(ROOT / "docs" / "pt-BR", stage / "docs" / "pt-BR")

        (stage / "runtime" / "lib").mkdir(parents=True, exist_ok=True)
        shutil.copy2(core_library(), stage / "runtime" / "lib" / core_library().name)
        shutil.copy2(sbom, stage / "SBOM.json")
        for name in ("README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CHANGELOG.md", "CODE_SIGNING.md", "PRIVACY.md", "version.txt"):
            shutil.copy2(ROOT / name, stage / name)

        write_portable_readme(stage, version, system)
        write_launchers(stage, system)
        if not args.skip_smoke:
            smoke_test(stage, system, require_onnx, bool(args.require_directml))
            shutil.rmtree(stage / ".arestrix", ignore_errors=True)
            shutil.rmtree(stage / "workspace", ignore_errors=True)
        write_manifests(stage, version, commit, dirty, system, arch)

        installer = build_windows_installer(stage, release_dir, version, arch) if args.installer else None

        formats = [args.format] if args.format and args.format != "both" else (
            ["zip", "tar.gz"] if args.format == "both" else (["zip"] if system == "windows" else ["tar.gz"])
        )
        base = release_dir / f"arestrix-{version}-{system}-{arch}"
        archives = [create_archive(stage, base, fmt) for fmt in formats]
        for archive in archives:
            digest = sha256(archive)
            Path(str(archive) + ".sha256").write_text(
                f"{digest}  {archive.name}\n", encoding="utf-8", newline="\n"
            )
            print(f"[package] {archive} ({archive.stat().st_size} bytes, sha256={digest})")
        print(f"[package] {len((stage / 'SHA256SUMS').read_text(encoding='utf-8').splitlines())} arquivo(s) no pacote")
        if installer:
            digest = sha256(installer)
            Path(str(installer) + ".sha256").write_text(
                f"{digest}  {installer.name}\n", encoding="utf-8", newline="\n"
            )
            print(f"[installer] {installer} (sha256={digest})")
        return 0
    finally:
        if not args.keep_stage:
            shutil.rmtree(stage_parent, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
