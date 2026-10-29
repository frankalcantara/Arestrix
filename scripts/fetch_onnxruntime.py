#!/usr/bin/env python3
"""Baixa e prepara ONNX Runtime DirectML + DirectML para o build de release.

O repositorio nao versiona o runtime (MIT) nem a DLL do DirectML (licenca
Microsoft, redistribuivel em apps). Este script baixa os pacotes NuGet
fixados, confere SHA-256 e monta uma raiz normalizada:

  <saida>/include/onnxruntime_cxx_api.h ...
  <saida>/lib/onnxruntime.lib
  <saida>/bin/onnxruntime.dll
  <saida>/bin/onnxruntime_providers_shared.dll
  <saida>/bin/DirectML.dll
  <saida>/licenses/ONNX-RUNTIME-LICENSE.txt
  <saida>/licenses/DIRECTML-LICENSE.txt
  <saida>/manifest.json

Uso:
  python scripts/fetch_onnxruntime.py --output .deps/onnxruntime-directml
  python scripts/fetch_onnxruntime.py --output "$RUNNER_TEMP/..." --json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

ORT_VERSION = "1.24.4"
DML_VERSION = "1.15.4"

ORT_URL = (
    "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/"
    f"{ORT_VERSION}/microsoft.ml.onnxruntime.directml.{ORT_VERSION}.nupkg"
)
DML_URL = (
    "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/"
    f"{DML_VERSION}/microsoft.ai.directml.{DML_VERSION}.nupkg"
)

# Fixados em 2026-10-29; mudar a versao exige atualizar URL + hash + tamanho.
ORT_SHA256 = "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681"
ORT_SIZE = 12_458_649
DML_SHA256 = "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9"
DML_SIZE = 202_292_617

REQUIRED_FILES = (
    "include/onnxruntime_cxx_api.h",
    "lib/onnxruntime.lib",
    "bin/onnxruntime.dll",
    "bin/onnxruntime_providers_shared.dll",
    "bin/DirectML.dll",
    "licenses/ONNX-RUNTIME-LICENSE.txt",
    "licenses/DIRECTML-LICENSE.txt",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, target: Path, expected_hash: str, expected_size: int, label: str) -> None:
    if target.is_file() and target.stat().st_size == expected_size and sha256(target) == expected_hash:
        print(f"[onnx] {label}: cache local OK")
        return

    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_suffix(target.suffix + ".part")
    print(f"[onnx] baixando {label}: {url}", flush=True)
    try:
        with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as handle:
            total = int(response.headers.get("Content-Length") or expected_size)
            done = 0
            while True:
                chunk = response.read(1024 * 1024)
                if not chunk:
                    break
                handle.write(chunk)
                done += len(chunk)
                if total:
                    print(f"\r[onnx] {label}: {done / 1e6:.1f}/{total / 1e6:.1f} MB", end="", flush=True)
        print("", flush=True)
    except Exception as error:  # noqa: BLE001 - mensagem clara para o pipeline
        partial.unlink(missing_ok=True)
        raise SystemExit(f"[onnx] falha ao baixar {label}: {error}") from error

    actual_size = partial.stat().st_size
    actual_hash = sha256(partial)
    if actual_size != expected_size or actual_hash != expected_hash:
        partial.unlink(missing_ok=True)
        raise SystemExit(
            f"[onnx] {label}: hash/tamanho nao conferem "
            f"(esperado {expected_size}/{expected_hash}, obtido {actual_size}/{actual_hash})"
        )
    partial.replace(target)


def extract_member(archive: zipfile.ZipFile, member: str, target: Path) -> None:
    data = archive.read(member)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def install(output: Path, force: bool) -> dict:
    marker = output / "manifest.json"
    if not force and marker.is_file():
        try:
            manifest = json.loads(marker.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            manifest = {}
        if (
            manifest.get("onnxruntime") == ORT_VERSION
            and manifest.get("directml") == DML_VERSION
            and all((output / relative).is_file() for relative in REQUIRED_FILES)
        ):
            return manifest

    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="onnxruntime-download-") as temporary:
        temp = Path(temporary)
        ort_package = temp / "onnxruntime-directml.nupkg"
        dml_package = temp / "directml.nupkg"
        download(ORT_URL, ort_package, ORT_SHA256, ORT_SIZE, "ONNX Runtime DirectML")
        download(DML_URL, dml_package, DML_SHA256, DML_SIZE, "DirectML")

        with zipfile.ZipFile(ort_package) as archive:
            names = archive.namelist()
            prefix = "build/native/include/"
            for member in names:
                if member.startswith(prefix) and not member.endswith("/"):
                    relative = member[len(prefix):]
                    extract_member(archive, member, output / "include" / relative)
            extract_member(
                archive, "runtimes/win-x64/native/onnxruntime.lib", output / "lib" / "onnxruntime.lib"
            )
            for dll in ("onnxruntime.dll", "onnxruntime_providers_shared.dll"):
                extract_member(
                    archive, f"runtimes/win-x64/native/{dll}", output / "bin" / dll
                )
            if "LICENSE" in names:
                extract_member(archive, "LICENSE", output / "licenses" / "ONNX-RUNTIME-LICENSE.txt")
            if "ThirdPartyNotices.txt" in names:
                extract_member(
                    archive,
                    "ThirdPartyNotices.txt",
                    output / "licenses" / "ONNX-RUNTIME-THIRD-PARTY-NOTICES.txt",
                )

        with zipfile.ZipFile(dml_package) as archive:
            names = archive.namelist()
            extract_member(archive, "bin/x64-win/DirectML.dll", output / "bin" / "DirectML.dll")
            if "LICENSE.txt" in names:
                extract_member(archive, "LICENSE.txt", output / "licenses" / "DIRECTML-LICENSE.txt")
            if "ThirdPartyNotices.txt" in names:
                extract_member(
                    archive,
                    "ThirdPartyNotices.txt",
                    output / "licenses" / "DIRECTML-THIRD-PARTY-NOTICES.txt",
                )

    missing = [relative for relative in REQUIRED_FILES if not (output / relative).is_file()]
    if missing:
        raise SystemExit("[onnx] pacote incompleto; ausente(s): " + ", ".join(missing))

    manifest = {
        "schemaVersion": 1,
        "onnxruntime": ORT_VERSION,
        "directml": DML_VERSION,
        "source": {"onnxruntime": ORT_URL, "directml": DML_URL},
        "sha256": {"onnxruntimePackage": ORT_SHA256, "directmlPackage": DML_SHA256},
        "root": str(output.resolve()),
    }
    marker.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepara ONNX Runtime DirectML para o release")
    parser.add_argument(
        "--output",
        default=str(ROOT / ".deps" / "onnxruntime-directml"),
        help="raiz normalizada de saida (padrao: .deps/onnxruntime-directml)",
    )
    parser.add_argument("--force", action="store_true", help="rebaixa mesmo com cache valido")
    parser.add_argument("--json", action="store_true", help="imprime somente o manifesto final")
    args = parser.parse_args()

    output = Path(args.output)
    manifest = install(output, args.force)
    if args.json:
        print(json.dumps(manifest))
    else:
        print(f"[onnx] pronto: {output.resolve()}")
        print(f"[onnx] ONNX Runtime {ORT_VERSION} + DirectML {DML_VERSION}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
