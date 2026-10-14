#!/usr/bin/env python3
"""Teste ponta a ponta dos projetos de examples/ contra um servidor LLM falso. NAO usa chaves reais nem a rede.

Para cada cenario: copia o exemplo para uma pasta temporaria, ajusta o ambiente (somente chaves de TESTE),
roda `arestrix-cli run`, confere o caminho percorrido e os arquivos gerados e apaga a copia.
Os exemplos versionados nunca sao modificados.

Uso: python scripts/e2e_examples.py [caminho-do-arestrix-cli]
"""
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
MOCK = ROOT / "CppCore" / "tests" / "e2e" / "mock_llm_examples.py"
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")
TEST_KEY = "test-key-examples"
# Chaves de provedores que um teste NUNCA pode herdar do ambiente do usuario.
REAL_KEY_VARS = ["OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "JEV_API_KEY", "DEEPSEEK_API_KEY",
                 "LOCAL_LLM_API_KEY"]

failures = []


def check(cond, msg):
    print(("  OK    " if cond else "  FALHA ") + msg)
    if not cond:
        failures.append(msg)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run_example(name, ticket=None, with_key=True, timeout=1500):
    """Roda um exemplo numa copia temporaria. Devolve (saida, pasta-do-projeto, eventos-do-mock, codigo)."""
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-ex-"))
    project = work / "projeto"
    shutil.copytree(EXAMPLES / name, project)
    log_file = work / "mock.log"
    port = free_port()
    mock = subprocess.Popen([sys.executable, str(MOCK), str(port), str(log_file)], stdout=subprocess.PIPE, text=True)
    mock.stdout.readline()
    try:
        env = {k: v for k, v in os.environ.items() if k not in REAL_KEY_VARS and not k.startswith("ARESTRIX_EXAMPLE")}
        env.update({"ARESTRIX_HOME": str(ROOT), "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
                    "ARESTRIX_PYTHON": sys.executable})
        if with_key:
            env["DEEPSEEK_API_KEY"] = TEST_KEY
        if ticket:
            env["ARESTRIX_EXAMPLE_TICKET"] = ticket
        proc = subprocess.run([str(EXE), "run", str(project / "pipelines" / "main.json"), "--root", str(project)],
                              env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
        out = proc.stdout + proc.stderr
        events = []
        if log_file.exists():
            events = [json.loads(l) for l in log_file.read_text(encoding="utf-8").splitlines() if l.strip()]
        return out, project, events, proc.returncode, work, mock
    except Exception:
        mock.terminate()
        shutil.rmtree(work, ignore_errors=True)
        raise


def cleanup(work, mock):
    mock.terminate()
    shutil.rmtree(work, ignore_errors=True)


def no_secret_leak(out, label):
    check(TEST_KEY not in out and not re.search(r"\bsk-[A-Za-z0-9_-]{16,}", out), f"{label}: a chave de API nao aparece na saida")


def scenario_01():
    print("\n== Exemplo 1: agente de SVG (tutorial) ==")
    out, project, events, code, work, mock = run_example("01-svg-agente")
    try:
        check(code == 0, "terminou com codigo 0")
        check("JEV REPROVOU" in out and "JEV APROVOU" in out, "laco de autocorrecao: reprovou e depois aprovou")
        check("[Entrega] SVG aprovado" in out, "vertice de entrega executado")
        svg = project / "workspace" / "scene.svg"
        check(svg.exists() and "feDropShadow" not in svg.read_text(encoding="utf-8"), "SVG final sem sombra em texto")
        chats = [e for e in events if e["event"] == "chat"]
        check(chats and all(e["model"] == "deepseek-flash" for e in chats), "o modelo veio dos Padroes Globais do projeto (deepseek-flash)")
        check(not any(e["event"] == "401" for e in events), "nenhuma chamada sem chave valida")
        no_secret_leak(out, "ex1")
    finally:
        cleanup(work, mock)


def support_checks(out, project, events, path, label):
    check(f"caminho: {path}" in out and f"(caminho: {path})" in out, f"{label}: triagem e entrega pelo caminho '{path}'")
    routes = [e for e in events if e["event"] == "route"]
    check(routes and routes[0]["chosen"] == path, f"{label}: o modelo recebeu as opcoes e escolheu '{path}'")
    check(routes and "revisao_humana" in routes[0]["options"], f"{label}: a opcao default foi oferecida ao modelo")
    check((project / "workspace" / "resposta.md").exists(), f"{label}: workspace/resposta.md criado")
    chats = [e for e in events if e["event"] == "chat"]
    check("[MCP Consultar_Cliente] clientes.lookup_customer ok" in out, f"{label}: o vertice MCP consultou o cadastro de clientes")
    cadastro = project / ".arestrix" / "out" / "consultar_cliente.txt"
    check(cadastro.exists() and '"plano"' in cadastro.read_text(encoding="utf-8"), f"{label}: o resultado do MCP ficou disponivel para o proximo vertice")
    check(any(e["reply"] == "tool_call:read" for e in chats), f"{label}: o agente leu os dados que o vertice MCP gravou")
    check(not any(e["event"] == "401" for e in events), f"{label}: nenhuma chamada sem chave valida")
    no_secret_leak(out, label)


def scenario_02_urgente():
    print("\n== Exemplo 2: chamado urgente (agente + decisao por IA + MCP) ==")
    out, project, events, code, work, mock = run_example("02-triagem-de-suporte", "chamado_urgente.txt")
    try:
        check(code == 0, "terminou com codigo 0")
        support_checks(out, project, events, "urgente", "urgente")
        log = project / "output" / "escalonamentos.jsonl"
        check(log.exists() and "ESC-0001" in log.read_text(encoding="utf-8"), "o servidor MCP registrou o escalonamento ESC-0001")
        chats = [e for e in events if e["event"] == "chat"]
        check(any("mcp__clientes__registrar_escalonamento" in e["tools"] for e in chats), "o agente recebeu a ferramenta MCP como tool (alem do vertice MCP)")
        check(not any("mcp__clientes__lookup_customer" in e["tools"] for e in chats), "o agente leitor NAO recebe a ferramenta de consulta (isso e do vertice MCP)")
        check("ESC-0001" in (project / "workspace" / "resposta.md").read_text(encoding="utf-8"), "a resposta cita o protocolo devolvido pelo MCP")
        check("ana.souza@example.com" in log.read_text(encoding="utf-8"), "o e-mail do cliente chegou ao MCP")
    finally:
        cleanup(work, mock)


def scenario_02_normal():
    print("\n== Exemplo 2: chamado normal (redator, revisor, laco de qualidade por palavras) ==")
    out, project, events, code, work, mock = run_example("02-triagem-de-suporte", "chamado_normal.txt")
    try:
        check(code == 0, "terminou com codigo 0")
        support_checks(out, project, events, "normal", "normal")
        check(out.count("[Switch Controle_de_Qualidade] caminho: ajustar") == 1, "o controle de qualidade pediu ajuste uma vez")
        check("[Switch Controle_de_Qualidade] caminho: aprovado" in out, "e aprovou depois da correcao")
        check(out.count("[Agent Agente_Redator] concluido") == 2, "o redator rodou duas vezes (laco de feedback)")
        resposta = (project / "workspace" / "resposta.md").read_text(encoding="utf-8")
        check("garantido" not in resposta, "a resposta final nao tem a promessa indevida")
    finally:
        cleanup(work, mock)


def scenario_02_duvida():
    print("\n== Exemplo 2: duvida simples (FAQ) ==")
    out, project, events, code, work, mock = run_example("02-triagem-de-suporte", "chamado_duvida.txt")
    try:
        check(code == 0, "terminou com codigo 0")
        support_checks(out, project, events, "duvida", "duvida")
        check("Exportar" in (project / "workspace" / "resposta.md").read_text(encoding="utf-8"), "a resposta usa a base de conhecimento")
    finally:
        cleanup(work, mock)


def scenario_02_sem_chave():
    print("\n== Exemplo 2 sem chave de API: deve falhar com mensagem clara ==")
    out, project, events, code, work, mock = run_example("02-triagem-de-suporte", "chamado_normal.txt", with_key=False)
    try:
        check(code != 0, "o pipeline falha (codigo diferente de 0)")
        check("DEEPSEEK_API_KEY" in out, "a mensagem diz qual variavel de ambiente definir")
        check(not any(e["event"] == "chat" for e in events), "nenhuma chamada ao modelo foi feita")
    finally:
        cleanup(work, mock)


def scenario_03_duvida():
    print("\n== Exemplo 3: projeto de projetos, ramo 'duvida' (FAQ + SVG animado) ==")
    out, project, events, code, work, mock = run_example("03-atendimento-ilustrado", "chamado_duvida.txt")
    try:
        check(code == 0, "terminou com codigo 0")
        check("[Switch Triagem_de_Urgencia] caminho: duvida" in out, "a triagem escolheu 'duvida'")
        check("JEV REPROVOU" in out and "JEV APROVOU" in out, "o fluxo de SVG (exemplo 1) rodou com autocorrecao")
        check("explicacao animada em workspace/scene.svg (caminho: duvida)" in out, "entrega final do exemplo 3")
        svg = project / "workspace" / "scene.svg"
        check(svg.exists() and "feDropShadow" not in svg.read_text(encoding="utf-8"), "SVG final aprovado")
        check((project / "workspace" / "resposta.md").exists(), "a resposta escrita tambem foi gerada")
        no_secret_leak(out, "ex3-duvida")
    finally:
        cleanup(work, mock)


def scenario_03_urgente():
    print("\n== Exemplo 3: ramo 'urgente' (reaproveita o fluxo do exemplo 2 com MCP) ==")
    out, project, events, code, work, mock = run_example("03-atendimento-ilustrado", "chamado_urgente.txt")
    try:
        check(code == 0, "terminou com codigo 0")
        check("[Switch Triagem_de_Urgencia] caminho: urgente" in out, "a triagem escolheu 'urgente'")
        log = project / "output" / "escalonamentos.jsonl"
        check(log.exists() and "ESC-0001" in log.read_text(encoding="utf-8"), "escalonamento registrado via MCP")
        check("[Agent Agente_Gerador_DeepSeek]" not in out and "svg_gerador" not in out, "o fluxo de SVG nao rodou neste ramo")
    finally:
        cleanup(work, mock)


def scenario_secrets_guard():
    print("\n== Garantia de seguranca dos exemplos ==")
    node = shutil.which("node")
    if not node:
        print("  (node ausente: verificacao de segredos ignorada)")
        return
    r = subprocess.run([node, str(ROOT / "scripts" / "check-secrets.mjs")], capture_output=True, text=True, cwd=ROOT)
    check(r.returncode == 0, "check-secrets: nenhum segredo, dado pessoal ou artefato gerado em examples/")
    for ex in sorted(p for p in EXAMPLES.iterdir() if p.is_dir()):
        manifest = json.loads((ex / "arestrix.project.json").read_text(encoding="utf-8"))
        check(manifest["rootPath"] == "." and manifest["environment"] == {}, f"{ex.name}: manifesto portatil e sem variaveis de ambiente")
        check(not (ex / "workspace").exists() and not (ex / "output").exists() and not (ex / ".arestrix").exists(),
              f"{ex.name}: o teste nao deixou artefatos na pasta versionada")


def main():
    for fn in (scenario_01, scenario_02_urgente, scenario_02_normal, scenario_02_duvida, scenario_02_sem_chave,
               scenario_03_duvida, scenario_03_urgente, scenario_secrets_guard):
        fn()
    if failures:
        print(f"\n{len(failures)} verificacao(oes) falharam:")
        for f in failures:
            print("  -", f)
        sys.exit(1)
    print("\nTodos os exemplos executaram com sucesso ponta a ponta.")


if __name__ == "__main__":
    main()
