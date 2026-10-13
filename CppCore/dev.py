#!/usr/bin/env python3
"""Build e execucao do Arestrix Studio (Windows e Linux).

    python CppCore/dev.py              # frontend + backend (CMake) + abre o Studio
    python CppCore/dev.py --no-run     # apenas compila
    python CppCore/dev.py --test       # compila e roda os testes de C++ (ctest)
    python CppCore/dev.py --setup      # apenas inicializa os submodulos (third_party/)
    python CppCore/dev.py --no-frontend

Pre-requisitos: git, Node.js, CMake >= 3.20 (o do Visual Studio serve), um compilador C++20
(MSVC no Windows; g++ >= 11 ou clang++ no Linux) e, no Linux, curl (usado pelo runtime dos agentes).
"""
import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
IS_WINDOWS = os.name == "nt"
SUBMODULES = ["third_party/taskflow", "third_party/simdjson", "third_party/webui"]


def run(cmd, **kw):
    print("[cmd]", cmd if isinstance(cmd, str) else " ".join(map(str, cmd)))
    return subprocess.run(cmd, **kw)


def ensure_submodules():
    missing = [s for s in SUBMODULES if not any((ROOT / s).glob("*"))]
    if not missing:
        return
    print("[Setup] Inicializando submodulos:", ", ".join(missing))
    r = run(["git", "submodule", "update", "--init", "--depth", "1", *missing], cwd=ROOT)
    if r.returncode != 0:
        sys.exit("[Erro] Falha ao inicializar os submodulos (git submodule update --init).")


def build_frontend():
    print("[Frontend] npm run build")
    if not (ROOT / "node_modules").exists():
        run("npm install", cwd=ROOT, shell=True, check=True)
    r = run("npm run build", cwd=ROOT, shell=True)
    if r.returncode != 0:
        sys.exit("[Erro] Falha ao compilar o frontend.")


def find_vcvars():
    candidates = []
    for env in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(env)
        if base:
            candidates.append(pathlib.Path(base) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe")
    for vswhere in candidates:
        if vswhere.exists():
            out = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                                  "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                 capture_output=True, text=True).stdout.strip()
            if out:
                vcvars = pathlib.Path(out) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
                if vcvars.exists():
                    return vcvars, pathlib.Path(out), vswhere.parent
    return None, None, None


def build_backend(run_tests: bool):
    cfg = ["-S", str(ROOT), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release"]
    if IS_WINDOWS:
        vcvars, vs_path, installer = find_vcvars()
        if not vcvars:
            sys.exit("[Erro] Visual Studio com 'Desenvolvimento para Desktop com C++' nao encontrado.")
        cmake = shutil.which("cmake") or str(vs_path / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
        ninja = shutil.which("ninja") or str(vs_path / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe")
        gen = ["-G", "Ninja"] if pathlib.Path(ninja).exists() else ["-G", "NMake Makefiles"]
        env_setup = f'set "PATH=%PATH%;{installer};{pathlib.Path(ninja).parent}" && call "{vcvars}" >nul'
        steps = [f'"{cmake}" ' + " ".join(f'"{a}"' for a in cfg + gen), f'"{cmake}" --build "{BUILD}"']
        if run_tests:
            ctest = pathlib.Path(cmake).with_name("ctest.exe")
            steps.append(f'"{ctest}" --test-dir "{BUILD}" --output-on-failure')
        for s in steps:
            r = run(f'cmd /c "{env_setup} && {s}"', shell=True, cwd=ROOT)
            if r.returncode != 0:
                sys.exit("[Erro] Falha na compilacao do backend C++.")
    else:
        cmake = shutil.which("cmake")
        if not cmake:
            sys.exit("[Erro] CMake nao encontrado (sudo apt install cmake).")
        gen = ["-G", "Ninja"] if shutil.which("ninja") else []
        for cmd in ([cmake, *cfg, *gen], [cmake, "--build", str(BUILD), "-j"]):
            if run(cmd, cwd=ROOT).returncode != 0:
                sys.exit("[Erro] Falha na compilacao do backend C++.")
        if run_tests:
            if run(["ctest", "--test-dir", str(BUILD), "--output-on-failure"], cwd=ROOT).returncode != 0:
                sys.exit("[Erro] Testes de C++ falharam.")
    print("[Build] Backend compilado com sucesso.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-run", action="store_true")
    ap.add_argument("--no-frontend", action="store_true")
    ap.add_argument("--setup", action="store_true")
    ap.add_argument("--test", action="store_true")
    args = ap.parse_args()

    ensure_submodules()
    if args.setup:
        return
    if not args.no_frontend:
        build_frontend()
    build_backend(run_tests=args.test)
    if args.no_run or args.test:
        return
    exe = ROOT / ("arestrix.exe" if IS_WINDOWS else "arestrix")
    print("[Run] Abrindo o Arestrix Studio:", exe)
    subprocess.run([str(exe)], cwd=ROOT)


if __name__ == "__main__":
    main()
