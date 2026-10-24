#!/usr/bin/env python3
"""Teste ponta a ponta dos projetos de examples/ contra um servidor LLM falso. NAO usa chaves reais.
O cenario 5 so acessa a rede (Keenable) quando a variavel KEENABLE_API esta definida; sem ela, ele e ignorado.

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
import sqlite3
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


def run_example(name, ticket=None, with_key=True, timeout=1500, answers=None, mutate=None):
    """Roda um exemplo numa copia temporaria. Devolve (saida, pasta-do-projeto, eventos-do-mock, codigo)."""
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-ex-"))
    project = work / "projeto"
    shutil.copytree(EXAMPLES / name, project)
    if mutate:
        mutate(project)
    log_file = work / "mock.log"
    port = free_port()
    mock = subprocess.Popen([sys.executable, str(MOCK), str(port), str(log_file)], stdout=subprocess.PIPE, text=True)
    mock.stdout.readline()
    try:
        env = {k: v for k, v in os.environ.items() if k not in REAL_KEY_VARS and not k.startswith("ARESTRIX_EXAMPLE")}
        env.update({"ARESTRIX_HOME": str(ROOT), "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port}",
                    "ARESTRIX_PYTHON": sys.executable, "ARESTRIX_DISABLE_USER_ENV": "1"})
        if with_key:
            env["DEEPSEEK_API_KEY"] = TEST_KEY
        if ticket:
            env["ARESTRIX_EXAMPLE_TICKET"] = ticket
        command = [str(EXE), "run", str(project / "pipelines" / "main.json"), "--root", str(project)]
        if answers:
            command.extend(["--answers", str(project / answers)])
        proc = subprocess.run(command, env=env, capture_output=True, text=True,
                              encoding="utf-8", errors="replace", timeout=timeout,
                              stdin=subprocess.DEVNULL)
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
        check("[OUTPUT: Entrega_SVG_Final]" in out and "[OUTPUT_DELIVERY]" in out, "vertice de saida registrou o entregavel")
        svg = project / "workspace" / "scene.svg"
        check(svg.exists() and "feDropShadow" not in svg.read_text(encoding="utf-8"), "SVG final sem sombra em texto")
        export = project / "exports" / "scene.svg"
        check(export.exists() and export.read_text(encoding="utf-8") == svg.read_text(encoding="utf-8"),
              "vertice de saida copiou o SVG para exports/scene.svg")
        check('"destination": "exports/scene.svg"' in out, "o entregavel foi anunciado com origem e destino")
        chats = [e for e in events if e["event"] == "chat"]
        check(chats and all(e["model"] == "deepseek-flash" for e in chats), "o modelo veio dos Padroes Globais do projeto (deepseek-flash)")
        check(not any(e["event"] == "401" for e in events), "nenhuma chamada sem chave valida")
        no_secret_leak(out, "ex1")
    finally:
        cleanup(work, mock)


def support_checks(out, project, events, path, label, artifact):
    check(f"caminho: {path}" in out and f"(caminho: {path})" in out, f"{label}: triagem e entrega pelo caminho '{path}'")
    routes = [e for e in events if e["event"] == "route"]
    check(routes and routes[0]["chosen"] == path, f"{label}: o modelo recebeu as opcoes e escolheu '{path}'")
    check(routes and "revisao_humana" in routes[0]["options"], f"{label}: a opcao default foi oferecida ao modelo")
    resposta = project / "workspace" / "resposta.md"
    check(resposta.exists(), f"{label}: workspace/resposta.md criado")
    exported = project / "exports" / artifact
    check(exported.exists(), f"{label}: vertice de saida exportou exports/{artifact}")
    check(exported.exists() and resposta.exists() and exported.read_bytes() == resposta.read_bytes(),
          f"{label}: o artefato exportado e a resposta escrita")
    check(f'"destination": "exports/{artifact}"' in out, f"{label}: o entregavel foi anunciado com origem e destino")
    chats = [e for e in events if e["event"] == "chat"]
    check("[MCP Extrair_Email] arestrix.dados.regex_extract ok" in out, f"{label}: a ferramenta NATIVA extraiu o e-mail do chamado (sem processo externo)")
    check("[MCP Consultar_Cliente] clientes.lookup_customer ok" in out, f"{label}: o vertice MCP consultou o cadastro de clientes com esse e-mail")
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
        support_checks(out, project, events, "urgente", "urgente", "resposta-urgente.md")
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
        support_checks(out, project, events, "normal", "normal", "resposta-normal.md")
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
        support_checks(out, project, events, "duvida", "duvida", "resposta-duvida.md")
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
        resposta = project / "workspace" / "resposta.md"
        check(resposta.exists(), "a resposta escrita tambem foi gerada")
        resposta_exportada = project / "exports" / "resposta-duvida.md"
        check(resposta_exportada.exists() and resposta_exportada.read_bytes() == resposta.read_bytes(),
              "vertice de saida entregou a resposta escrita antes da ilustracao")
        svg_exportado = project / "exports" / "scene.svg"
        check(svg_exportado.exists() and svg_exportado.read_bytes() == svg.read_bytes(),
              "vertice de saida entregou o SVG animado")
        check(out.count("[OUTPUT_DELIVERY]") == 2, "os dois vertices de saida do ramo registraram entregaveis")
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
        check((project / "exports" / "resposta-urgente.md").exists(), "vertice de saida entregou a resposta urgente")
        check(not (project / "exports" / "scene.svg").exists(), "o fluxo de SVG nao gerou entregavel neste ramo")
        check("[Agent Agente_Gerador_DeepSeek]" not in out and "svg_gerador" not in out, "o fluxo de SVG nao rodou neste ramo")
    finally:
        cleanup(work, mock)


def scenario_04_memoria():
    print("\n== Exemplo 4: memoria de projeto (licao, Blackboard e saida) ==")
    out, project, events, code, work, mock = run_example("04-memoria")
    try:
        check(code == 0, "terminou com codigo 0")
        check("[MEMORIA] contexto injetado em Agente_Com_Memoria" in out, "o runtime injetou a memoria no agente")
        resposta = project / "workspace" / "resposta.txt"
        check(resposta.exists(), "o agente gravou workspace/resposta.txt")
        exportado = project / "exports" / "resposta.md"
        check(exportado.exists() and resposta.exists() and exportado.read_bytes() == resposta.read_bytes(),
              "o vertice de saida entregou a resposta")
        memoria = project / ".arestrix" / "memory" / "memory.db"
        check(memoria.exists(), "o banco SQLite de memoria foi criado")
        if memoria.exists():
            with sqlite3.connect(memoria) as conn:
                lessons = conn.execute("SELECT r.canonical_name FROM memory_edge e JOIN memory_node r ON r.id=e.dst_id "
                                       "WHERE e.relation='HAS_LESSON' AND e.status='active'").fetchall()
                working = conn.execute("SELECT value_json FROM memory_working WHERE item_id='pipeline' AND key='resumo'").fetchall()
                usage = conn.execute("SELECT COUNT(*), COALESCE(SUM(calls),0) FROM memory_usage").fetchone()
            check(any("A resposta deve citar a regra aprendida" in row[0] for row in lessons),
                  "a licao foi persistida no banco")
            check(any("Licao registrada pelo agente no teste" in row[0] for row in lessons),
                  "o agente registrou uma nova licao via memory_record_lesson")
            check(any("chamado de exemplo" in row[0] for row in working),
                  "o Blackboard foi persistido no banco")
            check(usage and usage[0] >= 1 and usage[1] >= 1, "o consumo de API foi registrado no banco")
        else:
            check(False, "a licao foi persistida no banco")
            check(False, "o Blackboard foi persistido no banco")
            check(False, "o consumo de API foi registrado no banco")
        chats = [e for e in events if e["event"] == "chat"]
        system = chats[0].get("system", "") if chats else ""
        check("A resposta deve citar a regra aprendida" in system, "a licao apareceu no system prompt")
        check("chamado de exemplo" in system, "o resumo do Blackboard apareceu no system prompt")
        check(any("memory_search" in e.get("tools", []) for e in chats), "a ferramenta memory_search foi oferecida")
        check(any(e.get("reply") == "tool_call:memory_search" for e in chats), "o agente chamou memory_search")
        check(any(e.get("reply") == "tool_call:memory_record_lesson" for e in chats), "o agente chamou memory_record_lesson")
        check(re.search(r'"type"\s*:\s*"usage"', out) is not None, "o runtime emitiu telemetria de tokens para o Runner")
        no_secret_leak(out, "ex4")
    finally:
        cleanup(work, mock)


def scenario_05_matematica():
    print("\n== Exemplo 5: pesquisa web (Keenable) + matematica com memoria ==")
    if not os.environ.get("KEENABLE_API"):
        print("  (KEENABLE_API ausente: cenario de rede ignorado)")
        return
    out, project, events, code, work, mock = run_example("05-matematica-com-memoria", timeout=2400)
    try:
        check(code == 0, "terminou com codigo 0")
        check("[MCP Pesquisar_Fontes] keenable.search_web_pages ok" in out, "o vertice MCP pesquisou na web")
        fontes = project / "workspace" / "fontes.json"
        check(fontes.exists() and fontes.stat().st_size > 0, "os resultados da pesquisa foram gravados")
        check("[MEMORIA] contexto injetado em Redigir_Texto_Matematico" in out, "a memoria foi injetada no redator")
        texto = project / "workspace" / "texto-matematica.md"
        exportado = project / "exports" / "texto-matematica.md"
        check(texto.exists() and "Pitagoras" in texto.read_text(encoding="utf-8"), "o texto de matematica foi escrito")
        check(exportado.exists() and exportado.read_bytes() == texto.read_bytes(), "o vertice de saida entregou o Markdown")
        chats = [e for e in events if e["event"] == "chat"]
        check(any(e.get("reply") == "tool_call:memory_search" for e in chats), "o redator consultou a memoria")
        memoria = project / ".arestrix" / "memory" / "memory.db"
        check(memoria.exists(), "o banco SQLite de memoria foi criado")
        if memoria.exists():
            with sqlite3.connect(memoria) as conn:
                lessons = conn.execute("SELECT r.canonical_name FROM memory_edge e JOIN memory_node r ON r.id=e.dst_id "
                                       "WHERE e.relation='HAS_LESSON' AND e.status='active'").fetchall()
            check(any("Toda afirmacao matematica" in row[0] for row in lessons), "a licao inicial foi persistida")
            check(any("Antes de publicar" in row[0] for row in lessons), "a licao final foi persistida")
        no_secret_leak(out, "ex5")
        check(os.environ["KEENABLE_API"] not in out, "ex5: a chave Keenable nao aparece na saida")
    finally:
        cleanup(work, mock)


def scenario_06_chamada_agente():
    print("\n== Exemplo 6: chamada entre agentes ==")
    out, project, events, code, work, mock = run_example("06-chamada-agente")
    try:
        check(code == 0, "terminou com codigo 0")
        check("[Agent Coordenador_de_Chamadas] iniciando" in out, "o coordenador executou")
        check("[Agent Especialista_de_Chamadas] iniciando" in out, "o especialista foi chamado")

        resposta = project / "workspace" / "resposta.txt"
        check(resposta.exists(), "workspace/resposta.txt criado")
        if resposta.exists():
            content = resposta.read_text(encoding="utf-8")
            check('"status":"ok"' in content, "o resultado da chamada tem status ok")
            check("Parecer do especialista: aprovado." in content, "o parecer chegou ao coordenador")

        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "vertice de saida exportou a resposta")

        chats = [e for e in events if e["event"] == "chat"]
        coord = [e for e in chats if "COORDENADOR DE CHAMADAS" in e.get("system", "")]
        spec = [e for e in chats if "ESPECIALISTA DE CHAMADAS" in e.get("system", "")]
        check(coord and any("call_agent" in e.get("tools", []) for e in coord),
              "coordenador recebeu a tool call_agent")
        check(spec and all("call_agent" not in e.get("tools", []) for e in spec),
              "especialista nao recebeu call_agent")
        check(any(e.get("reply") == "tool_call:call_agent" for e in coord),
              "coordenador chamou o especialista")

        notification_files = list((project / ".arestrix" / "runs").glob("*/notifications.jsonl"))
        check(bool(notification_files), "a chamada gerou notificacao de execucao")
        if notification_files:
            notifications = notification_files[0].read_text(encoding="utf-8")
            check("call.completed" in notifications, "a notificacao registra a chamada concluida")
            check("especialista" in notifications, "a notificacao identifica o alvo")
            check(TEST_KEY not in notifications, "a notificacao nao contem a chave de API")
        no_secret_leak(out, "ex6")
    finally:
        cleanup(work, mock)


def scenario_07_interacao():
    print("\n== Exemplo 7: interacao humana (--answers) ==")
    out, project, events, code, work, mock = run_example("07-interacao", answers="answers.json")
    try:
        check(code == 0, "terminou com codigo 0")
        check("[Interaction Pergunta_ao_Usuario] respondido" in out, "a interacao foi respondida")

        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "vertice de saida exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Tipo: pj" in content, "a escolha chegou ao fluxo")
            check("Observacao: Cliente empresarial" in content, "o texto chegou ao fluxo")
        check("[OUTPUT_DELIVERY]" in out, "o entregavel foi anunciado")
        audit_files = list((project / ".arestrix" / "runs").glob("*/interactions/*.json"))
        check(bool(audit_files), "auditoria da interacao foi gravada")
        if audit_files:
            audit = json.loads(audit_files[0].read_text(encoding="utf-8"))
            check(audit.get("status") == "ok", "auditoria registrou status ok")
            check(audit.get("channel") == "answers", "auditoria registrou o canal answers")
            check(audit.get("answers", {}).get("tipo") == "pj", "auditoria registrou a resposta")
            check("test-key-examples" not in json.dumps(audit), "auditoria nao contem chave de API")
        no_secret_leak(out, "ex7")
    finally:
        cleanup(work, mock)

    print("  -- sem --answers: falha fechada --")
    out, project, events, code, work, mock = run_example("07-interacao")
    try:
        check(code != 0, "sem canal, o pipeline falha")
        check("nenhum canal de respostas" in out, "a mensagem explica que falta o canal")
        failed_audits = list((project / ".arestrix" / "runs").glob("*/interactions/*.json"))
        check(bool(failed_audits), "auditoria registra a tentativa sem canal")
        if failed_audits:
            audit = json.loads(failed_audits[0].read_text(encoding="utf-8"))
            check(audit.get("status") == "error", "auditoria registra o erro")
    finally:
        cleanup(work, mock)


def scenario_08_formulario_gerado():
    print("\n== Exemplo 8: formulario gerado pelo modelo ==")
    out, project, events, code, work, mock = run_example("08-formulario-gerado", answers="answers.json")
    try:
        check(code == 0, "terminou com codigo 0")
        chats = [e for e in events if e.get("event") == "chat"]
        generator_chats = [e for e in chats if "FORM SCHEMA GENERATOR" in e.get("system", "")]
        check(bool(generator_chats), "o modelo gerador de formulario foi chamado")
        check(any(e.get("user_has_acme") for e in generator_chats),
              "o conteudo do artefato chegou ao modelo gerador")

        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "vertice de saida exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Tipo de cadastro: pj" in content, "a escolha gerada chegou ao fluxo")
            check("Observacao: ACME Ltda" in content, "o texto chegou ao fluxo")

        audit_files = list((project / ".arestrix" / "runs").glob("*/interactions/*.json"))
        check(bool(audit_files), "auditoria do formulario gerado foi gravada")
        if audit_files:
            audit = json.loads(audit_files[0].read_text(encoding="utf-8"))
            check(audit.get("status") == "ok", "auditoria registrou status ok")
            check(audit.get("generated") is True, "auditoria registrou formulario gerado")
            check((audit.get("generator") or {}).get("model") == "deepseek-flash",
                  "auditoria registrou o modelo gerador")
            ids = [f.get("id") for f in (audit.get("form") or {}).get("fields", [])]
            check("tipo" in ids and "obs" in ids, "o schema gerado foi validado e auditado")
        no_secret_leak(out, "ex8")
    finally:
        cleanup(work, mock)

    print("  -- schema invalido: fallback free_text --")
    def configure_fallback(project: pathlib.Path) -> None:
        graph_path = project / "pipelines" / "main.json"
        graph = json.loads(graph_path.read_text(encoding="utf-8"))
        for node in graph.get("nodes", []):
            if node.get("type") != "interaction":
                continue
            settings = node.setdefault("interactionSettings", {})
            settings.setdefault("form", {}).setdefault("context", {})["markdown"] = "INVALID_FORM"
            settings.setdefault("generator", {})["fallback"] = "free_text"
        graph_path.write_text(json.dumps(graph, ensure_ascii=False, indent=2), encoding="utf-8")
        (project / "fallback-answers.json").write_text(
            json.dumps({"pergunta": {"resposta": "Fallback ok"}}), encoding="utf-8")

    out, project, events, code, work, mock = run_example(
        "08-formulario-gerado", answers="fallback-answers.json", mutate=configure_fallback)
    try:
        check(code == 0, "fallback free_text terminou com codigo 0")
        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "fallback exportou a resposta")
        if exported.exists():
            check("Resposta: Fallback ok" in exported.read_text(encoding="utf-8"),
                  "a pergunta livre recebeu a resposta")
        audit_files = list((project / ".arestrix" / "runs").glob("*/interactions/*.json"))
        if audit_files:
            audit = json.loads(audit_files[0].read_text(encoding="utf-8"))
            check(audit.get("generated") is False, "auditoria marca o fallback como nao gerado")
            check((audit.get("generator") or {}).get("usedFallback") is True,
                  "auditoria registra o fallback usado")
            ids = [f.get("id") for f in (audit.get("form") or {}).get("fields", [])]
            check(ids == ["resposta"], "fallback gera a pergunta livre")
    finally:
        cleanup(work, mock)

    print("  -- sem canal: falha fechado antes de chamar o modelo --")
    out, project, events, code, work, mock = run_example("08-formulario-gerado")
    try:
        check(code != 0, "sem canal o pipeline falha")
        check("nenhum canal de respostas" in out, "a mensagem explica que falta o canal")
        check(not any("FORM SCHEMA GENERATOR" in e.get("system", "") for e in events if e.get("event") == "chat"),
              "o modelo gerador nao foi chamado sem canal")
    finally:
        cleanup(work, mock)


def scenario_09_chamadas_paralelas():
    print("\n== Exemplo 9: chamadas paralelas ==")
    out, project, events, code, work, mock = run_example("09-chamadas-paralelas")
    try:
        check(code == 0, "terminou com codigo 0")
        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "vertice de saida exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Parecer A: risco baixo." in content, "o parecer A chegou ao coordenador")
            check("Parecer B: prazo compativel." in content, "o parecer B chegou ao coordenador")

        chats = [e for e in events if e.get("event") == "chat"]
        coord = [e for e in chats if "COORDENADOR PARALELO" in e.get("system", "")]
        check(bool(coord), "o coordenador executou")
        check(any(e.get("requested_tools") == ["call_agent", "call_agent"] for e in coord),
              "o coordenador pediu duas chamadas no mesmo turno")
        check(any("ESPECIALISTA A" in e.get("system", "") for e in chats), "o especialista A foi chamado")
        check(any("ESPECIALISTA B" in e.get("system", "") for e in chats), "o especialista B foi chamado")
        no_secret_leak(out, "ex9")
    finally:
        cleanup(work, mock)


def scenario_10_subflow_chamavel():
    print("\n== Exemplo 10: subflow chamavel ==")
    out, project, events, code, work, mock = run_example("10-subflow-chamavel")
    try:
        check(code == 0, "terminou com codigo 0")
        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "vertice de saida do pai exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Subflow processou: dados do cliente." in content,
                  "o texto do subflow chegou ao coordenador")

        child_export = project / "exports" / "subflow_result.txt"
        check(child_export.exists(), "o grafo interno do subflow entregou seu artefato")
        check((project / ".arestrix" / "build" / "subflows.cpp").exists(),
              "o compilador gerou a funcao nativa do subflow")

        chats = [e for e in events if e.get("event") == "chat"]
        check(any("COORDENADOR SUBFLOW" in e.get("system", "") for e in chats), "o coordenador executou")
        worker_chats = [e for e in chats if "WORKER SUBFLOW" in e.get("system", "")]
        check(bool(worker_chats), "o agente interno do subflow executou")
        check(any(e.get("user_has_subflow_input") for e in worker_chats),
              "o input da chamada chegou ao agente interno do subflow")
        no_secret_leak(out, "ex10")
    finally:
        cleanup(work, mock)


def scenario_11_call_vertex():
    print("\n== Exemplo 11: vertice Call em fluxo ==")
    out, project, events, code, work, mock = run_example("11-call-vertex")
    try:
        check(code == 0, "terminou com codigo 0")
        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "o vertice de saida exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Especialista recebeu: Dados preparados para a chamada." in content,
                  "a resposta do alvo voltou ao fluxo")

        check((project / ".arestrix" / "out" / "chamada.txt").exists(),
              "o vertice Call gravou sua saida para o proximo vertice")

        chats = [e for e in events if e.get("event") == "chat"]
        check(any("PREPARADOR CALL" in e.get("system", "") for e in chats), "o preparador executou")
        specialists = [e for e in chats if "ESPECIALISTA CALL" in e.get("system", "")]
        check(bool(specialists), "o alvo chamado executou")
        check(any(e.get("user_has_call_input") for e in specialists),
              "o input do fluxo chegou ao alvo chamado")
        no_secret_leak(out, "ex11")
    finally:
        cleanup(work, mock)


def scenario_12_chamadas_assincronas():
    print("\n== Exemplo 12: chamadas assincronas ==")
    out, project, events, code, work, mock = run_example("12-chamadas-assincronas")
    try:
        check(code == 0, "terminou com codigo 0")
        exported = project / "exports" / "resposta.txt"
        check(exported.exists(), "o vertice de saida exportou a resposta")
        if exported.exists():
            content = exported.read_text(encoding="utf-8")
            check("Resposta assincrona do especialista." in content,
                  "o resultado do call_result chegou ao coordenador")

        chats = [e for e in events if e.get("event") == "chat"]
        coord = [e for e in chats if "COORDENADOR ASYNC" in e.get("system", "")]
        check(bool(coord), "o coordenador executou")
        check(any(e.get("async_requested") for e in coord), "o coordenador pediu call_agent async")
        check(any("call_result" in e.get("tools", []) for e in coord),
              "o coordenador recebeu a tool call_result")
        check(any("call_result" in e.get("requested_tools", []) for e in coord),
              "o coordenador consultou o resultado")
        check(any("ALVO ASSINCRONO" in e.get("system", "") for e in chats),
              "o alvo assincrono executou")

        notification_files = list((project / ".arestrix" / "runs").glob("*/notifications.jsonl"))
        check(bool(notification_files), "a chamada async gerou notificacao de execucao")
        if notification_files:
            notifications = notification_files[0].read_text(encoding="utf-8")
            check("call.completed" in notifications, "a notificacao da chamada async foi registrada")
        no_secret_leak(out, "ex12")
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
               scenario_03_duvida, scenario_03_urgente, scenario_04_memoria, scenario_05_matematica,
               scenario_06_chamada_agente, scenario_07_interacao, scenario_08_formulario_gerado,
               scenario_09_chamadas_paralelas, scenario_10_subflow_chamavel,
               scenario_11_call_vertex, scenario_12_chamadas_assincronas,
               scenario_secrets_guard):
        fn()
    if failures:
        print(f"\n{len(failures)} verificacao(oes) falharam:")
        for f in failures:
            print("  -", f)
        sys.exit(1)
    print("\nTodos os exemplos executaram com sucesso ponta a ponta.")


if __name__ == "__main__":
    main()
