#!/usr/bin/env python3
"""Teste ponta a ponta de docs/pt-BR/tutorials/svg-agent.md.

Fluxo: grafo do tutorial -> `arestrix run` (gera C++, compila com Taskflow, executa) ->
agentes falam com um servidor LLM falso -> laco de auto-correcao -> SVG final aprovado.

Uso: python scripts/e2e_tutorial.py [caminho-do-arestrix]
"""
import json
import os
import pathlib
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "CppCore" / "tests" / "e2e" / "svg-agent"
MOCK = ROOT / "CppCore" / "tests" / "e2e" / "mock_llm_server.py"
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")

failures = []


def check(cond, msg):
    print(("  OK   " if cond else "  FALHA ") + msg)
    if not cond:
        failures.append(msg)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    subprocess.run([sys.executable, str(ROOT / "scripts" / "build_tutorial_fixture.py")], check=True, stdout=subprocess.DEVNULL)

    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-e2e-"))
    project = work / "projeto"
    shutil.copytree(FIXTURE, project)
    log_file = work / "mock.log"
    port = free_port()

    mock = subprocess.Popen([sys.executable, str(MOCK), str(port), str(log_file)], stdout=subprocess.PIPE, text=True)
    mock.stdout.readline()   # aguarda "ouvindo"
    try:
        env = dict(os.environ)
        env.update({
            "ARESTRIX_HOME": str(ROOT),
            "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
            "DEEPSEEK_API_KEY": "test-key-deepseek",
            "JEV_BASE_URL": f"http://127.0.0.1:{port}",
            "JEV_API_KEY": "test-key-jev",
        })
        t0 = time.time()
        proc = subprocess.run([str(EXE), "run", str(project / "graph.json"), "--root", str(project)],
                              env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=1500)
        elapsed = time.time() - t0
        out = proc.stdout + proc.stderr
        print(out[-6000:])
        print(f"\n--- verificacoes ({elapsed:.0f}s) ---")

        check(proc.returncode == 0, "arestrix run terminou com codigo 0")
        check("JEV REPROVOU" in out, "1a rodada: auditor reprovou (violacao tipografica detectada pelo Linter)")
        check("JEV APROVOU" in out, "2a rodada: auditor aprovou apos a auto-correcao")
        check("[OUTPUT: Entrega_SVG_Final]" in out and "[OUTPUT_DELIVERY]" in out, "vertice de saida registrou o entregavel")
        check("[Thread A - VIOLA" in out, "Thread A (linter) detectou o feDropShadow")
        check(out.count("[Thread A] Iniciando") == 2 and out.count("[Thread B] Iniciando") == 2, "fork paralelo executou 2 vezes (A e B)")

        svg = project / "workspace" / "scene.svg"
        check(svg.exists() and "feDropShadow" not in svg.read_text(encoding="utf-8"), "SVG final sem sombra em texto")
        export = project / "exports" / "scene.svg"
        check(export.exists() and export.read_text(encoding="utf-8") == svg.read_text(encoding="utf-8"),
              "vertice de saida copiou workspace/scene.svg para exports/scene.svg")
        decision = (project / "workspace" / "audit_decision.txt")
        check(decision.exists() and "STATUS: APROVADO" in decision.read_text(encoding="utf-8"), "audit_decision.txt = APROVADO")
        check("agent:telemetry" in out and '"tool_start"' in out, "telemetria de agentes (tool_start) emitida")
        check('"rate_limit"' in out, "429 tratado: evento rate_limit emitido e requisicao repetida")

        events = [json.loads(l) for l in log_file.read_text(encoding="utf-8").splitlines() if l.strip()]
        chats = [e for e in events if e["event"] == "chat"]
        check(any(e["event"] == "429" for e in events), "servidor falso devolveu 429 uma vez")
        check(not any(e["event"] == "401" for e in events), "nenhuma chamada sem chave de API valida")
        check(all(e["model"] in ("deepseek-flash", "jev-visual-evaluator") for e in chats), "modelos por no respeitados (deepseek-flash / jev-visual-evaluator)")
        check(all(set(e["tools"]) == {"write", "read", "validate_svg"} for e in chats), "somente ferramentas autorizadas oferecidas")
    finally:
        mock.terminate()
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"\n{len(failures)} verificacao(oes) falharam")
        sys.exit(1)
    print("\nTutorial executado com sucesso ponta a ponta.")


main()
