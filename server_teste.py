#!/usr/bin/env python3
"""
NodeForge - Servidor de Teste, Auto Build & Run e Validador de Ambiente
Este script:
1. Executa a suíte de testes unitários (Vitest).
2. Serve a interface WebGL do NodeForge (Three.js).
3. Provê a API de compilação automática no Windows com MSVC (cl.exe C++20) e execução do binário.
"""

import sys
import os
import subprocess
import time
import json
import urllib.request
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
import threading

# Garante suporte adequado a UTF-8 no console do Windows
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

PORT = 3000
HOST = "localhost"
PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
DIST_DIR = os.path.join(PROJECT_DIR, "dist")
INCLUDE_DIR = os.path.join(PROJECT_DIR, "include")
WORKSPACE_RUN_DIR = os.path.join(PROJECT_DIR, "workspace_run")

# Cores ANSI
GREEN = "\033[92m"
CYAN = "\033[96m"
YELLOW = "\033[93m"
RED = "\033[91m"
BOLD = "\033[1m"
RESET = "\033[0m"


def print_banner():
    print(f"\n{CYAN}{BOLD}{'=' * 68}")
    print("   ⚡ NodeForge - Visual Taskflow C++20 Studio")
    print("   Auto Build & Run Orchestrator (MSVC cl.exe / Make / CMake)")
    print(f"{'=' * 68}{RESET}\n")


def find_msvc_vcvarsall():
    """Localiza o script vcvarsall.bat do Visual Studio usando vswhere."""
    if sys.platform != "win32":
        return None

    vswhere_path = os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe")
    if os.path.exists(vswhere_path):
        try:
            res = subprocess.run(
                [vswhere_path, "-latest", "-find", "**/vcvarsall.bat"],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace"
            )
            for line in res.stdout.splitlines():
                line = line.strip()
                if line.endswith("vcvarsall.bat") and os.path.exists(line):
                    return line
        except Exception:
            pass

    # Caminhos conhecidos de fallback
    common_paths = [
        r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat"
    ]
    for p in common_paths:
        if os.path.exists(p):
            return p
    return None


def run_unit_tests():
    print(f"{CYAN}>>> [1/3] Executando testes unitarios com Vitest...{RESET}")
    npm_cmd = "npm.cmd" if sys.platform == "win32" else "npm"

    try:
        res = subprocess.run(
            [npm_cmd, "run", "test"],
            cwd=PROJECT_DIR,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace"
        )

        if res.returncode == 0:
            print(f"{GREEN}[OK] Todos os testes unitarios passaram com sucesso!{RESET}")
            for line in res.stdout.splitlines():
                if "Tests" in line or "passed" in line:
                    print(f"   {GREEN}{line.strip()}{RESET}")
            return True
        else:
            print(f"{RED}[FALHA] Erro nos testes unitarios:{RESET}")
            print(res.stderr or res.stdout)
            return False
    except Exception as e:
        print(f"{RED}Erro ao executar npm test: {e}{RESET}")
        return False


def ensure_build():
    index_file = os.path.join(DIST_DIR, "index.html")
    if not os.path.exists(index_file):
        print(f"\n{YELLOW}>>> Build de producao nao encontrado. Gerando bundle...{RESET}")
        npm_cmd = "npm.cmd" if sys.platform == "win32" else "npm"
        res = subprocess.run([npm_cmd, "run", "build"], cwd=PROJECT_DIR)
        if res.returncode != 0:
            print(f"{RED}[FALHA] Nao foi possivel compilar o bundle.{RESET}")
            sys.exit(1)
        print(f"{GREEN}[OK] Bundle compilado com sucesso.{RESET}")


class NodeForgeServerHandler(SimpleHTTPRequestHandler):
    """Handler HTTP unificado: Serve o app SPA e responde à API /api/run-pipeline."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIST_DIR, **kwargs)

    def log_message(self, format, *args):
        # Apenas loga chamadas da API
        if "/api/" in str(args[0]):
            sys.stdout.write(f"   [API] {args[0]} - {args[1]}\n")
            sys.stdout.flush()

    def do_POST(self):
        if self.path == "/api/run-pipeline":
            self.handle_build_and_run()
        else:
            self.send_error(404, "Endpoint not found")

    def handle_build_and_run(self):
        start_time = time.time()
        content_len = int(self.headers.get("Content-Length", 0))
        post_body = self.rfile.read(content_len).decode("utf-8")

        try:
            data = json.loads(post_body)
        except Exception as e:
            self.send_json_response({"success": False, "error": f"JSON invalido: {e}"}, status=400)
            return

        cpp_code = data.get("cppCode", "")
        cmake_code = data.get("cmakeCode", "")
        make_code = data.get("makeCode", "")
        msvc_bat = data.get("msvcBat", "")
        binary_name = data.get("binaryName", "pipeline_app")

        # 1. Cria a pasta workspace_run
        os.makedirs(WORKSPACE_RUN_DIR, exist_ok=True)
        main_cpp_path = os.path.join(WORKSPACE_RUN_DIR, "main.cpp")
        cmake_path = os.path.join(WORKSPACE_RUN_DIR, "CMakeLists.txt")
        makefile_path = os.path.join(WORKSPACE_RUN_DIR, "Makefile")
        msvc_bat_path = os.path.join(WORKSPACE_RUN_DIR, "build_msvc.bat")

        with open(main_cpp_path, "w", encoding="utf-8") as f:
            f.write(cpp_code)
        with open(cmake_path, "w", encoding="utf-8") as f:
            f.write(cmake_code)
        with open(makefile_path, "w", encoding="utf-8") as f:
            f.write(make_code)
        with open(msvc_bat_path, "w", encoding="utf-8") as f:
            f.write(msvc_bat)

        # 2. Localiza o compilador MSVC
        vcvarsall = find_msvc_vcvarsall()
        exe_path = os.path.join(WORKSPACE_RUN_DIR, f"{binary_name}.exe")

        if os.path.exists(exe_path):
            try:
                os.remove(exe_path)
            except Exception:
                pass

        compile_stdout = ""
        run_stdout = ""

        # 3. Compilação automática com MSVC
        if vcvarsall and os.path.exists(vcvarsall):
            compile_cmd = (
                f'call "{vcvarsall}" x64 >nul 2>&1 && '
                f'cl.exe /std:c++20 /EHsc /O2 /W3 /I"{INCLUDE_DIR}" main.cpp /Fe:"{binary_name}.exe"'
            )
            print(f"\n{CYAN}>>> [MSVC] Compilando pipeline C++20 em workspace_run/...{RESET}")
            compile_proc = subprocess.run(
                compile_cmd,
                cwd=WORKSPACE_RUN_DIR,
                shell=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace"
            )

            compile_stdout = (compile_proc.stdout or "") + (compile_proc.stderr or "")

            if compile_proc.returncode != 0 or not os.path.exists(exe_path):
                print(f"{RED}[MSVC] Erro de compilacao.{RESET}")
                elapsed_ms = int((time.time() - start_time) * 1000)
                self.send_json_response({
                    "success": False,
                    "stage": "compile",
                    "stdout": compile_stdout,
                    "stderr": "Falha na compilacao com MSVC cl.exe.",
                    "elapsedMs": elapsed_ms
                })
                return

            print(f"{GREEN}[MSVC] Compilacao concluida com sucesso! Executando binario...{RESET}")

            # 4. Executa o binário gerado
            run_proc = subprocess.run(
                [exe_path],
                cwd=WORKSPACE_RUN_DIR,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30
            )

            run_stdout = (run_proc.stdout or "") + (run_proc.stderr or "")
            elapsed_ms = int((time.time() - start_time) * 1000)

            full_output = f"[COMPILADOR MSVC C++20]\n{compile_stdout.strip()}\n\n[EXECUCAO DOS AGENTES TASKFLOW]\n{run_stdout.strip()}"

            self.send_json_response({
                "success": run_proc.returncode == 0,
                "stage": "run",
                "stdout": full_output,
                "elapsedMs": elapsed_ms
            })
            return

        else:
            # Fallback caso MSVC não seja encontrado
            msg = (
                "Compilador MSVC (vcvarsall.bat / cl.exe) nao foi detectado automaticamente.\n"
                f"Os arquivos C++20 foram criados com sucesso em: {WORKSPACE_RUN_DIR}\n"
                "Voce pode compilar manualmente no Developer PowerShell via:\\n"
                "  cd workspace_run && .\\build_msvc.bat"
            )
            self.send_json_response({
                "success": False,
                "stage": "compile",
                "stdout": msg,
                "stderr": "MSVC nao localizado no PATH.",
                "elapsedMs": int((time.time() - start_time) * 1000)
            })

    def send_json_response(self, data, status=200):
        body = json.dumps(data).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-cache, no-store, must-revalidate')
        self.send_header('Access-Control-Allow-Origin', '*')
        super().end_headers()


def start_http_server(host, port):
    server = HTTPServer((host, port), NodeForgeServerHandler)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    return server


def verify_http_response(url):
    print(f"\n{CYAN}>>> [2/3] Verificando conectividade HTTP do servidor...{RESET}")
    max_retries = 10
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'NodeForgeTestClient/1.0'})
            with urllib.request.urlopen(req, timeout=3) as resp:
                if resp.status == 200:
                    content = resp.read().decode('utf-8', errors='ignore')
                    if "NodeForge" in content or "vite" in content or "app" in content:
                        print(f"{GREEN}[OK] Servidor respondendo em: {url} (HTTP 200 OK){RESET}")
                        return True
        except Exception:
            time.sleep(0.3)
    
    print(f"{RED}[FALHA] Servidor nao respondeu no tempo esperado.{RESET}")
    return False


def main():
    print_banner()

    # 1. Executa testes unitários
    tests_ok = run_unit_tests()
    if not tests_ok:
        print(f"\n{RED}{BOLD}Interrompido: os testes unitarios falharam.{RESET}")
        sys.exit(1)

    # 2. Garante arquivos do build da interface
    ensure_build()

    # 3. Inicia servidor HTTP com API de build e run
    print(f"\n{CYAN}>>> [3/3] Iniciando servidor NodeForge Studio em http://{HOST}:{PORT}...{RESET}")
    
    active_port = PORT
    for offset in range(10):
        try:
            start_http_server(HOST, active_port)
            break
        except OSError:
            active_port = PORT + offset + 1
    
    target_url = f"http://{HOST}:{active_port}"

    # 4. Verifica resposta do servidor
    if verify_http_response(target_url):
        print(f"\n{GREEN}{BOLD}{'=' * 68}")
        print(f"   🚀 NodeForge Studio no ar com Auto Build & Run (MSVC C++20)!")
        print(f"   URL: {target_url}")
        print(f"{'=' * 68}{RESET}\n")

        print(f"{CYAN}>>> Abrindo interface no navegador...{RESET}")
        try:
            webbrowser.open(target_url)
        except Exception:
            pass

        print(f"\n{YELLOW}Pressione Ctrl+C para encerrar o servidor.{RESET}\n")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print(f"\n{CYAN}>>> Servidor finalizado pelo usuario. Ate logo!{RESET}")
            sys.exit(0)


if __name__ == "__main__":
    main()
