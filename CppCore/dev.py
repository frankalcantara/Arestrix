#!/usr/bin/env python3
"""Build e orquestracao de desenvolvimento do Arestrix Studio (Windows e Linux).

Este script unifica o ciclo de vida completo de desenvolvimento e compilacao do Arestrix:
1. Verificacao e inicializacao automatica de submodulos Git (third_party/).
2. Verificacao de tipos TypeScript (tsc) e empacotamento do frontend (Vite singlefile).
3. Sincronizacao de artefatos web para CppCore/dist.
4. Deteccao de toolchain C++20 (MSVC vswhere/vcvars64 no Windows; GCC/Clang no Linux).
5. Configuracao CMake com flags de hardening, protecoes de seguranca e analise estatica.
6. Compilacao concorrente dos binarios nativos (arestrix, arestrix-cli, arestrix-gateway, arestrix_tests).
7. Execucao automatizada dos testes nativos C++ via CTest (nao inclui o Vitest do frontend).
8. Inicializacao do Studio na area de trabalho.

Exemplos de uso comuns:
    python CppCore/dev.py                        # Compila frontend + backend e abre o Studio
    python CppCore/dev.py --no-run               # Apenas compila (frontend e backend)
    python CppCore/dev.py --no-frontend          # Apenas compila o backend C++
    python CppCore/dev.py --test                 # Compila e roda os testes nativos C++ (CTest)
    python CppCore/dev.py --test --no-frontend   # Roda testes C++ pulando build do frontend
    python CppCore/dev.py --setup                # Apenas clona e inicializa os submodulos Git
    python CppCore/dev.py --asan                 # Compila com AddressSanitizer ativado
    python CppCore/dev.py --clang-tidy           # Compila executando analise estatica Clang-Tidy
    python CppCore/dev.py --cppcheck             # Compila executando analise estatica Cppcheck
    python CppCore/dev.py --no-hardening         # Desativa as protecoes de memoria estritas

Gate completo recomendado (dev.py cobre build + CTest, nao o Vitest):
    npm test
    python CppCore/dev.py --no-run --test
    npm run check:version
    npm run check:local-assets
    npm run check:secrets

Pre-requisitos do sistema:
    - Git no PATH do sistema operacional.
    - Node.js >= 18 e npm (para o frontend TypeScript/Vite).
    - CMake >= 3.20 (o embutido no Visual Studio ou instalado no sistema operacional).
    - Compilador C++20:
        * Windows: Visual Studio 2019/2022 com a carga 'Desenvolvimento para Desktop com C++'.
        * Linux: GCC >= 11 (g++) ou Clang >= 13 (clang++), com suporte a ISO C++20 e curl.
"""

import argparse
import os
import pathlib
import platform
import shutil
import subprocess
import sys

# Caminhos absolutos ancorados na raiz do projeto
ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
IS_WINDOWS = os.name == "nt"

# Submodulos essenciais localizados em third_party/
SUBMODULES = [
    "third_party/taskflow",  # Motor de grafos de tarefas concorrentes ISO C++20
    "third_party/simdjson",  # Parser JSON de ultra-alto desempenho com instrucoes SIMD
    "third_party/webui"      # Camada leve de integracao entre backend C++ e janela Web
]


def run(cmd, **kw):
    """Executa um comando no terminal do sistema operacional imprimindo a linha de comando invocada."""
    print("[cmd]", cmd if isinstance(cmd, str) else " ".join(map(str, cmd)))
    return subprocess.run(cmd, **kw)


def ensure_submodules():
    """Garante que todos os submodulos Git em third_party/ foram clonados e estao disponiveis.
    
    Verifica se cada diretorio declarado em SUBMODULES possui arquivos. Caso algum esteja
    vazio, executa 'git submodule update --init --depth 1' para clonar apenas a revisao necessaria.
    """
    missing = [s for s in SUBMODULES if not any((ROOT / s).glob("*"))]
    if not missing:
        return
    print("[Setup] Inicializando submodulos ausentes:", ", ".join(missing))
    r = run(["git", "submodule", "update", "--init", "--depth", "1", *missing], cwd=ROOT)
    if r.returncode != 0:
        sys.exit("[Erro] Falha ao inicializar os submodulos (git submodule update --init).")


def build_frontend():
    """Compila e empacota o frontend do Studio em TypeScript/Vite.
    
    Etapas executadas:
    1. Instala dependencias com 'npm install' caso node_modules/ nao exista.
    2. Executa 'npm run build':
       - 'tsc': Valida a tipagem de todo o codigo TypeScript sem emitir erros.
       - 'vite build': Empacota o frontend em arquivo unico (.html) e bundle estavel.
       - 'node -e': Copia a pasta 'dist/' gerada diretamente para 'CppCore/dist/'.
    """
    print("[Frontend] Compilando interface TypeScript e empacotando com Vite...")
    if not (ROOT / "node_modules").exists():
        print("[Frontend] node_modules nao encontrado. Executando npm install...")
        run("npm install", cwd=ROOT, shell=True, check=True)
    r = run("npm run build", cwd=ROOT, shell=True)
    if r.returncode != 0:
        sys.exit("[Erro] Falha ao compilar o frontend (erros no tsc ou vite build).")


def find_vcvars():
    """Localiza o ambiente de compilacao do Microsoft Visual Studio no Windows.
    
    Utiliza a ferramenta oficial 'vswhere.exe' da Microsoft para consultar a instalacao
    mais recente do Visual Studio contendo o componente 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64'.
    
    Retorna:
        tuple: (caminho_vcvars64, caminho_base_vs, caminho_instalador) ou (None, None, None).
    """
    candidates = []
    for env in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(env)
        if base:
            candidates.append(pathlib.Path(base) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe")
    for vswhere in candidates:
        if vswhere.exists():
            out = subprocess.run([
                str(vswhere), "-latest", "-products", "*", "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath"
            ], capture_output=True, text=True).stdout.strip()
            if out:
                vcvars = pathlib.Path(out) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
                if vcvars.exists():
                    return vcvars, pathlib.Path(out), vswhere.parent
    return None, None, None


def build_backend(run_tests: bool, hardening: bool = True, asan: bool = False, clang_tidy: bool = False, cppcheck: bool = False):
    """Configura via CMake e compila os alvos nativos C++20 com as opcoes de seguranca solicitadas.
    
    Parametros:
        run_tests (bool): Se True, invoca o ctest apos a compilacao para rodar os testes unitarios.
        hardening (bool): Se True, ativa protecoes de memoria (GS, CFG, SDL, ASLR, DEP no MSVC;
                          stack-protector-strong, _FORTIFY_SOURCE=2, RELRO no GCC/Clang).
        asan (bool): Se True, ativa o AddressSanitizer para deteccao em tempo de execucao de
                     use-after-free, estouros de buffer e vazamentos de memoria.
        clang_tidy (bool): Se True, integra a analise estatica do Clang-Tidy (.clang-tidy) ao build.
        cppcheck (bool): Se True, integra a analise estatica comportamental do Cppcheck ao build.
    """
    cfg = [
        "-S", str(ROOT),
        "-B", str(BUILD),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DARESTRIX_ENABLE_HARDENING={'ON' if hardening else 'OFF'}",
        f"-DARESTRIX_ENABLE_ASAN={'ON' if asan else 'OFF'}",
        f"-DARESTRIX_ENABLE_CLANG_TIDY={'ON' if clang_tidy else 'OFF'}",
        f"-DARESTRIX_ENABLE_CPPCHECK={'ON' if cppcheck else 'OFF'}"
    ]

    if IS_WINDOWS:
        # 1. Localiza o Visual Studio e o script de inicializacao das variaveis de ambiente
        vcvars, vs_path, installer = find_vcvars()
        if not vcvars:
            sys.exit("[Erro] Visual Studio com 'Desenvolvimento para Desktop com C++' nao encontrado.")

        # 2. Localiza ferramentas CMake e gerador Ninja disponiveis no ambiente
        cmake = shutil.which("cmake") or str(vs_path / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
        ninja = shutil.which("ninja") or str(vs_path / "Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe")
        gen = ["-G", "Ninja"] if pathlib.Path(ninja).exists() else ["-G", "NMake Makefiles"]

        # 3. Prepara a chamada encapsulada com vcvars64.bat para garantir ambiente x64 puro
        env_setup = f'set "PATH=%PATH%;{installer};{pathlib.Path(ninja).parent}" && call "{vcvars}" >nul'
        steps = [
            f'"{cmake}" ' + " ".join(f'"{a}"' for a in cfg + gen),
            f'"{cmake}" --build "{BUILD}"'
        ]

        # 4. Adiciona execucao de testes via ctest.exe se solicitado
        if run_tests:
            ctest = pathlib.Path(cmake).with_name("ctest.exe")
            steps.append(f'"{ctest}" --test-dir "{BUILD}" --output-on-failure')

        # 5. Executa os passos de configuracao e compilacao
        for s in steps:
            r = run(f'cmd /c "{env_setup} && {s}"', shell=True, cwd=ROOT)
            if r.returncode != 0:
                sys.exit("[Erro] Falha na compilacao do backend C++ no ambiente MSVC.")
    else:
        # Ambiente Linux / macOS
        cmake = shutil.which("cmake")
        if not cmake:
            sys.exit("[Erro] CMake nao encontrado no sistema (instale via apt/dnf/brew).")

        gen = ["-G", "Ninja"] if shutil.which("ninja") else []
        for cmd in ([cmake, *cfg, *gen], [cmake, "--build", str(BUILD), "-j"]):
            if run(cmd, cwd=ROOT).returncode != 0:
                sys.exit("[Erro] Falha na compilacao do backend C++.")

        # Executa suite de testes se solicitado
        if run_tests:
            if run(["ctest", "--test-dir", str(BUILD), "--output-on-failure"], cwd=ROOT).returncode != 0:
                sys.exit("[Erro] Testes de C++ falharam no ctest.")

    print("[Build] Backend C++ compilado com sucesso.")


def main():
    """Ponto de entrada principal do orquestrador de desenvolvimento."""
    ap = argparse.ArgumentParser(
        description="Arestrix Build & Dev Runner - Orquestrador de Compilacao e Seguranca C++20",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Exemplos:
  python CppCore/dev.py                      Compila frontend + backend e inicia o Studio
  python CppCore/dev.py --no-run             Apenas compila sem iniciar o executavel
  python CppCore/dev.py --test --no-frontend Compila e roda os testes C++20 via CTest
  python CppCore/dev.py --asan               Compila com AddressSanitizer ativado
  python CppCore/dev.py --clang-tidy         Compila executando analise estatica Clang-Tidy
        """
    )
    ap.add_argument("--no-run", action="store_true", help="Apenas compila os artefatos sem iniciar o Arestrix Studio")
    ap.add_argument("--no-frontend", action="store_true", help="Pula a etapa de compilacao do frontend (TypeScript/Vite)")
    ap.add_argument("--setup", action="store_true", help="Apenas inicializa e atualiza os submodulos Git em third_party/")
    ap.add_argument("--test", action="store_true", help="Executa a suite de testes nativos C++20 (CTest); o Vitest e rodado com npm test")
    ap.add_argument("--hardening", action="store_true", default=True, help="Ativa flags de seguranca de memoria e mitigacao de exploracao (padrao: ativado)")
    ap.add_argument("--no-hardening", dest="hardening", action="store_false", help="Desativa flags estritas de hardening de memoria")
    ap.add_argument("--asan", action="store_true", help="Ativa instrumentacao AddressSanitizer (deteccao de use-after-free e buffer overflows)")
    ap.add_argument("--clang-tidy", action="store_true", help="Ativa analise estatica Clang-Tidy integrada ao processo de compilacao")
    ap.add_argument("--cppcheck", action="store_true", help="Ativa analise estatica Cppcheck integrada ao processo de compilacao")
    args = ap.parse_args()

    # 1. Garante que dependencias externas e submodulos estao presentes
    ensure_submodules()
    if args.setup:
        print("[Setup] Submodulos verificados com sucesso.")
        return

    # 2. Compila e valida o frontend TypeScript (se nao desativado por flag)
    if not args.no_frontend:
        build_frontend()

    # 3. Configura e compila o backend C++20 com as protecoes selecionadas
    build_backend(
        run_tests=args.test,
        hardening=args.hardening,
        asan=args.asan,
        clang_tidy=args.clang_tidy,
        cppcheck=args.cppcheck
    )

    # 4. Se solicitado apenas teste ou sem execucao, encerra aqui
    if args.no_run or args.test:
        return

    # 5. Inicia o Arestrix Studio nativo
    exe = ROOT / ("arestrix.exe" if IS_WINDOWS else "arestrix")
    print("[Run] Iniciando o Arestrix Studio:", exe)
    subprocess.run([str(exe)], cwd=ROOT)


if __name__ == "__main__":
    main()
