#!/usr/bin/env python3
"""Teste ponta a ponta da memoria de projeto (Fases 0-2): persistencia, injecao, ferramentas e metricas.

Cria um projeto temporario com dois grafos:

  graph1: um no estatico semeia uma licao semantica e uma chave do Blackboard;
          um agente com memoria habilitada recebe esse contexto no system prompt.
  graph2: apenas o agente, sem o no de semeadura. A licao e a chave devem ser
          carregadas do disco e injetadas novamente.

O servidor LLM falso registra o system prompt de cada chamada. O teste confere:
  - persistencia no banco .arestrix/memory/memory.db;
  - injecao do contexto no prompt;
  - filtros allowedKeys e domains;
  - carga do banco na segunda execucao;
  - metricas de consumo de API (memory_usage);
  - registro automatico de falha de agente.

Uso: python scripts/e2e_memory.py [caminho-do-arestrix-cli]
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
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
import copy

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")
TEST_KEY = "test-key-memory"
RULE = "REGRA_PERSISTENTE_XYZ"
RULE2 = "REGRA_SECUNDARIA_SEARCH"
HIDDEN_RULE = "NAO_DEVE_APARECER"
BLACKBOARD_VALUE = "valor-do-blackboard"
HIDDEN_KEY_VALUE = "valor-ignorado"

failures = []


def check(cond, msg):
    print(("  OK    " if cond else "  FALHA ") + msg)
    if not cond:
        failures.append(msg)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def port(pid, name, direction, typ="flow"):
    return {"id": pid, "name": name, "direction": direction, "type": typ}


def graph(nodes, edges):
    return {
        "version": "1.0.0",
        "metadata": {
            "projectId": "memoria-teste",
            "graphName": "MemoriaTeste",
            "description": "teste ponta a ponta da memoria de projeto",
            "targetStandard": "C++20",
            "compilerConfig": {
                "compiler": "msvc", "cppStandard": "C++20", "buildSystem": "cmake",
                "taskflowIncludePath": "third_party/taskflow", "outputBinaryName": "memoria_teste",
                "enableOpenMP": False, "enableCuda": False, "extraFlags": [],
            },
            "version": "1.0.0",
            "globalDefaults": {
                "model": {"provider": "deepseek", "model": "deepseek-flash", "skillPath": "", "promptTemplate": ""},
                "sandbox": {"isolationLevel": "host_direct", "network": "egress_llm_only",
                            "maxMemoryMB": 512, "timeoutSeconds": 120, "readOnlyMounts": [], "readWriteMounts": []},
                "codeMode": {"allowedTools": [], "customScripts": []},
                "memory": {"enabled": False, "persistent": False, "readBlackboard": False,
                           "writeBlackboard": False, "semanticLessons": False, "recordLessons": False,
                           "allowedKeys": [], "domains": []},
            },
        },
        "nodes": nodes,
        "edges": edges,
    }


def make_graphs():
    seed = {
        "id": "semear", "label": "Semear_Licao", "type": "static", "position": {"x": 0, "y": 0},
        "inputs": [port("semear_in", "in", "input")],
        "outputs": [port("semear_out", "out", "output")],
        "parameters": {},
        "customLambdaBody": (
            f'memory.recordLesson(projectId, "teste", "manual", "{RULE}", "seed");\n'
            f'memory.recordLesson(projectId, "teste", "manual", "{RULE2}", "seed");\n'
            f'memory.recordLesson(projectId, "outro", "manual", "{HIDDEN_RULE}", "seed");\n'
            f'memory.setWorking(projectId, "pipeline", "resumo", taskforge::Json("{BLACKBOARD_VALUE}"));\n'
            f'memory.setWorking(projectId, "pipeline", "ignorado", taskforge::Json("{HIDDEN_KEY_VALUE}"));\n'
            'std::cout << "[SEED] licao e blackboard gravados\\n";'
        ),
    }
    agent = {
        "id": "agente", "label": "Agente_Com_Memoria", "type": "agent", "position": {"x": 300, "y": 0},
        "inputs": [port("agente_in", "in", "input")],
        "outputs": [port("agente_out", "out", "output")],
        "parameters": {},
        "customLambdaBody": "",
        "agentSettings": {
            "inheritSandbox": True,
            "inheritModel": False,
            "model": {"provider": "deepseek", "model": "deepseek-flash", "skillPath": "",
                      "promptTemplate": "Responda apenas OK."},
            "inheritCodeMode": True,
            "inheritMemory": False,
            "memory": {
                "enabled": True, "persistent": True, "projectId": "memoria-teste",
                "readBlackboard": True, "writeBlackboard": True,
                "semanticLessons": True, "recordLessons": True,
                "allowedKeys": ["resumo"], "domains": ["teste"],
            },
        },
    }
    g1 = graph([seed, agent], [{"id": "e1", "sourceNodeId": "semear", "sourcePortId": "semear_out",
                                "targetNodeId": "agente", "targetPortId": "agente_in"}])
    g2 = graph([agent], [])
    fail_agent = copy.deepcopy(agent)
    fail_agent["agentSettings"]["model"]["model"] = ""
    g3 = graph([fail_agent], [])
    off_agent = copy.deepcopy(agent)
    off_agent["agentSettings"]["memory"]["enabled"] = False
    g4 = graph([off_agent], [])
    g5 = graph([copy.deepcopy(agent)], [])
    g5["metadata"]["globalDefaults"]["memory"].update({"measurementMode": "memory_off", "recordMetrics": True})

    def validation_agent(node_id, label, valid_model=True):
        node = copy.deepcopy(agent)
        node["id"] = node_id
        node["label"] = label
        node["inputs"] = [port(node_id + "_in", "in", "input")]
        node["outputs"] = [port(node_id + "_out", "out", "output")]
        node["agentSettings"]["model"]["promptTemplate"] = "Leia o artefato e responda OK."
        if not valid_model:
            node["agentSettings"]["model"]["model"] = ""
        node["agentSettings"]["inheritCodeMode"] = False
        node["agentSettings"]["codeMode"] = {"allowedTools": ["read"], "customScripts": []}
        node["agentSettings"]["memory"].update({
            "mode": "learn", "theme": "teste", "domains": ["teste"],
            "taskType": "validacao_artefato", "contextBudgetTokens": 800,
        })
        node["validationRules"] = [{
            "targetPathPattern": "artefatos/pipeline.md",
            "minSizeBytes": 200,
            "contentPattern": "STATUS_OK",
        }]
        return node

    def write_artifact(node_id, marker):
        content = marker + "\\n" + ("x" * 400)
        return {
            "id": node_id, "label": "Prepara_Artefato", "type": "static",
            "position": {"x": 0, "y": 0},
            "inputs": [port(node_id + "_in", "in", "input")],
            "outputs": [port(node_id + "_out", "out", "output")],
            "parameters": {},
            "customLambdaBody": (
                'std::filesystem::create_directories("artefatos");\n'
                f'{{ std::ofstream arx_f("artefatos/pipeline.md", std::ios::binary); '
                f'arx_f << "{content}"; }}\n'
                'std::cout << "[PREP] artefato gravado\\n";'
            ),
        }

    def validation_graph(prep_id, marker):
        prep = write_artifact(prep_id, marker)
        node = validation_agent("validador", "Agente_Com_Regras")
        return graph([prep, node], [{
            "id": prep_id + "_edge", "sourceNodeId": prep_id, "sourcePortId": prep_id + "_out",
            "targetNodeId": "validador", "targetPortId": "validador_in",
        }])

    g6 = validation_graph("prepara_ok", "STATUS_OK")
    g7 = validation_graph("prepara_ruim", "SEM_MARCADOR")
    g8 = graph([validation_agent("validador_falha", "Agente_Com_Regras_Falha", valid_model=False)], [])

    search_agent = copy.deepcopy(agent)
    search_agent["id"] = "buscador"
    search_agent["label"] = "Agente_Busca_Cita"
    search_agent["inputs"] = [port("buscador_in", "in", "input")]
    search_agent["outputs"] = [port("buscador_out", "out", "output")]
    search_agent["agentSettings"]["model"]["promptTemplate"] = "Busque o tema REGRA e resuma."
    search_agent["agentSettings"]["inheritCodeMode"] = False
    search_agent["agentSettings"]["codeMode"] = {"allowedTools": [], "customScripts": []}
    search_agent["agentSettings"]["memory"].update({
        "mode": "consult", "theme": "teste", "domains": ["teste"],
        "taskType": "busca_cita", "contextBudgetTokens": 0,
    })
    g9 = graph([search_agent], [])
    return g1, g2, g3, g4, g5, g6, g7, g8, g9


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        messages = body.get("messages", [])
        system = messages[0].get("content", "") if messages else ""
        with self.server.log_lock:
            with self.server.log_path.open("a", encoding="utf-8") as f:
                f.write(system + "\n===SYSTEM-END===\n")
        match = re.search(r'no "([^"]+)" de um pipeline', system)
        label = match.group(1) if match else ""
        has_tool_result = any(msg.get("role") == "tool" for msg in messages)
        if "Agente_Com_Regras" in label and not has_tool_result:
            message = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call_read_artifact",
                    "type": "function",
                    "function": {
                        "name": "read",
                        "arguments": json.dumps({"path": "artefatos/pipeline.md"}),
                    },
                }],
            }
            finish = "tool_calls"
        elif "Agente_Busca_Cita" in label and not has_tool_result:
            message = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call_search_memory",
                    "type": "function",
                    "function": {
                        "name": "memory_search",
                        "arguments": json.dumps({"query": "REGRA", "limit": 5}),
                    },
                }],
            }
            finish = "tool_calls"
        elif "Agente_Busca_Cita" in label:
            tool_text = ""
            for item in reversed(messages):
                if item.get("role") == "tool":
                    tool_text = item.get("content", "") or ""
                    break
            ids = [item for item in re.findall(r"\[MEMO:([^\]]+)\]", tool_text)
                   if item and not item.startswith("<")]
            cited = ids[0] if ids else "sem-id"
            message = {
                "role": "assistant",
                "content": f"Resultado da busca [MEMO:{cited}] [MEMO:citacao-invalida-e2e]",
            }
            finish = "stop"
        else:
            message = {"role": "assistant", "content": "OK"}
            finish = "stop"
        data = json.dumps({
            "choices": [{"message": message, "finish_reason": finish}],
            "usage": {"prompt_tokens": 1, "completion_tokens": 1},
        }).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def run_graph(exe, graph_file, project, env):
    return subprocess.run([str(exe), "run", str(graph_file), "--root", str(project)],
                          env=env, capture_output=True, text=True, encoding="utf-8",
                          errors="replace", timeout=900)


def memory_db(project):
    return project / ".arestrix" / "memory" / "memory.db"


def telemetry_db(project):
    return project / ".arestrix" / "telemetry" / "telemetry.db"


def rows_at(path, sql, params=()):
    if not path.exists():
        return []
    with sqlite3.connect(path) as conn:
        return conn.execute(sql, params).fetchall()


def db_rows(project, sql, params=()):
    return rows_at(memory_db(project), sql, params)


def telemetry_rows(project, sql, params=()):
    return rows_at(telemetry_db(project), sql, params)


def main():
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memoria-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    log_file = work / "systems.log"
    g1, g2, g3, g4, g5, g6, g7, g8, g9 = make_graphs()
    for index, item in enumerate((g1, g2, g3, g4, g5, g6, g7, g8, g9), start=1):
        (work / f"graph{index}.json").write_text(json.dumps(item, indent=2), encoding="utf-8")

    port_num = free_port()
    server = HTTPServer(("127.0.0.1", port_num), MockHandler)
    server.log_path = log_file
    server.log_lock = threading.Lock()
    threading.Thread(target=server.serve_forever, daemon=True).start()

    env = {k: v for k, v in os.environ.items()
           if k not in ("OPENAI_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "JEV_API_KEY")}
    env.update({
        "ARESTRIX_HOME": str(ROOT),
        "DEEPSEEK_BASE_URL": f"http://127.0.0.1:{port_num}",
        "DEEPSEEK_API_KEY": TEST_KEY,
    })

    try:
        print("== Memoria de projeto: primeira execucao (semeia e injeta) ==")
        p1 = run_graph(EXE, work / "graph1.json", project, env)
        out1 = (p1.stdout or "") + (p1.stderr or "")
        check(p1.returncode == 0, "primeira execucao terminou com codigo 0")
        check("[MEMORIA] contexto injetado" in out1, "o runtime anunciou a injecao de memoria")
        check(memory_db(project).exists(), "o banco SQLite de memoria foi criado")
        lessons = db_rows(project, "SELECT r.canonical_name FROM memory_edge e JOIN memory_node r ON r.id=e.dst_id "
                                   "WHERE e.relation='HAS_LESSON' AND e.status='active'")
        check(any(RULE in row[0] for row in lessons), "a licao foi persistida no banco")
        working = db_rows(project, "SELECT value_json FROM memory_working WHERE item_id='pipeline' AND key='resumo'")
        check(any(BLACKBOARD_VALUE in row[0] for row in working), "o Blackboard foi persistido no banco")
        usage = telemetry_rows(project, "SELECT COUNT(*), COALESCE(SUM(calls),0), COALESCE(SUM(prompt_tokens),0), "
                                        "COALESCE(SUM(memory_context_chars),0) FROM memory_usage")
        check(usage and usage[0][0] >= 1, "o consumo de API foi registrado em memory_usage")
        check(usage and usage[0][1] >= 1 and usage[0][3] > 0, "a chamada de API registrou contexto de memoria")
        outcome = telemetry_rows(project, "SELECT COALESCE(SUM(outcome='success'),0), COALESCE(SUM(retries),0) FROM memory_usage")
        check(outcome and outcome[0][0] >= 1, "a execucao bem-sucedida foi registrada com outcome=success")
        hits = telemetry_rows(project, "SELECT COALESCE(SUM(injected),0), COALESCE(SUM(used),0) FROM memory_hit")
        check(hits and hits[0][0] >= 1, "as memorias injetadas foram registradas em memory_hit")
        episodes = telemetry_rows(project, "SELECT kind, metadata FROM memory_episode WHERE kind='agent_success'")
        check(episodes and '"outcome":"success"' in episodes[0][1], "o episodio de sucesso foi registrado com outcome")

        text1 = log_file.read_text(encoding="utf-8") if log_file.exists() else ""
        check(RULE in text1, "a licao apareceu no system prompt")
        check(HIDDEN_RULE not in text1, "licao de outro dominio nao foi injetada")
        check(HIDDEN_KEY_VALUE not in text1, "chave nao autorizada nao foi injetada")
        check(TEST_KEY not in out1, "a chave de teste nao aparece na saida")

        print("\n== Memoria de projeto: segunda execucao (carrega do disco) ==")
        p2 = run_graph(EXE, work / "graph2.json", project, env)
        out2 = (p2.stdout or "") + (p2.stderr or "")
        check(p2.returncode == 0, "segunda execucao terminou com codigo 0")
        text2 = log_file.read_text(encoding="utf-8") if log_file.exists() else ""
        check(text2.count("===SYSTEM-END===") >= 2, "o agente recebeu um segundo system prompt")
        check(text2.count(RULE) >= 2, "a licao foi carregada do disco na segunda execucao")
        check("[MEMORIA] contexto injetado" in out2, "o runtime injetou memoria tambem na segunda execucao")
        usage2 = telemetry_rows(project, "SELECT COUNT(*), COALESCE(SUM(calls),0), COALESCE(SUM(prompt_tokens),0) FROM memory_usage")
        check(usage2 and usage2[0][0] >= 2, "a segunda execucao tambem registrou consumo de API")
        check(usage2 and usage2[0][1] >= 2, "as chamadas de API foram acumuladas no banco")

        print("\n== A/B no mesmo grafo: memoria ON (acima) vs OFF (agora) ==")
        env_ab = dict(env)
        env_ab["ARESTRIX_MEMORY_DISABLE"] = "1"
        env_ab["ARESTRIX_MEMORY_METRICS"] = "1"
        p_ab = run_graph(EXE, work / "graph2.json", project, env_ab)
        out_ab = (p_ab.stdout or "") + (p_ab.stderr or "")
        check(p_ab.returncode == 0, "a execucao A/B OFF terminou com codigo 0")
        check("[MEMORIA] contexto injetado" not in out_ab, "ARESTRIX_MEMORY_DISABLE removeu o contexto")
        ab = telemetry_rows(project, "SELECT COUNT(*), COALESCE(SUM(memory_enabled),0), COALESCE(SUM(outcome='success'),0) "
                                     "FROM memory_usage WHERE memory_enabled=0")
        check(ab and ab[0][0] >= 1 and ab[0][1] == 0 and ab[0][2] >= 1,
              "o mesmo grafo foi registrado como memoria OFF com outcome=success")

        print("\n== A/B configurado no projeto (opcao da UI), sem variavel de ambiente ==")
        p_ui = run_graph(EXE, work / "graph5.json", project, env)
        out_ui = (p_ui.stdout or "") + (p_ui.stderr or "")
        check(p_ui.returncode == 0, "a execucao A/B configurada no projeto terminou com codigo 0")
        check("[MEMORIA] contexto injetado" not in out_ui, "measurementMode=memory_off removeu o contexto")
        ui_off = telemetry_rows(project, "SELECT COUNT(*), COALESCE(SUM(memory_enabled),0) "
                                         "FROM memory_usage WHERE memory_enabled=0 AND outcome='success'")
        check(ui_off and ui_off[0][0] >= 1 and ui_off[0][1] == 0,
              "measurementMode=memory_off foi registrado como memoria OFF")

        print("\n== Memoria de projeto: falha de agente gera licao automatica ==")
        p3 = run_graph(EXE, work / "graph3.json", project, env)
        out3 = (p3.stdout or "") + (p3.stderr or "")
        check(p3.returncode != 0, "a execucao com modelo ausente falhou como esperado")
        check("modelo nao configurado" in out3, "a falha veio do agente sem modelo")
        failure_proposals = db_rows(
            project,
            "SELECT payload_json FROM memory_proposal WHERE kind='lesson' AND status='pending'")
        failure_text = " ".join(row[0] for row in failure_proposals)
        check("Falha no agente Agente_Com_Memoria" in failure_text,
              "a falha foi gravada como proposta de licao")
        check("agent_failure" in failure_text, "a proposta foi classificada como falha de agente")

        print("\n== E1: regras de artefato, veredito e gate de playbooks ==")
        env_val = dict(env)
        for item_id in ("val-a", "val-b", "val-c"):
            env_val["ARESTRIX_ITEM_ID"] = item_id
            p_val = run_graph(EXE, work / "graph6.json", project, env_val)
            out_val = (p_val.stdout or "") + (p_val.stderr or "")
            check(p_val.returncode == 0, f"execucao validada {item_id} terminou com codigo 0")

        validations = telemetry_rows(
            project,
            "SELECT v.ok,v.node_id,v.episode_id,e.input_hash,v.validator_id,v.validator_version "
            "FROM memory_validation v JOIN memory_episode e ON e.id=v.episode_id "
            "WHERE v.node_id='validador' ORDER BY v.created_at")
        check(len(validations) == 3, "cada execucao com regras gravou um veredito")
        check(all(row[0] == 1 for row in validations), "os tres vereditos foram aprovados")
        check(len({row[3] for row in validations}) == 3, "os vereditos apontam entradas distintas")
        check(all(row[4] == "artifact-rules" for row in validations), "o validador e artifact-rules")
        check(all(row[5] for row in validations), "a versao do validador foi gravada")
        playbooks = db_rows(
            project,
            "SELECT status FROM memory_playbook WHERE task_type='validacao_artefato'")
        check(len(playbooks) == 1 and playbooks[0][0] == "active",
              "o gate promoveu o playbook com tres entradas aprovadas")

        p_rep = subprocess.run(
            [str(EXE), "memory", "report", "--root", str(project), "--json"],
            capture_output=True, text=True, timeout=60)
        try:
            report = json.loads(p_rep.stdout)
        except Exception:
            report = {}
        check(p_rep.returncode == 0, "memory report terminou com codigo 0")
        check(int(report.get("validations", {}).get("total", 0)) >= 3,
              "o relatorio contou os vereditos")
        entry = next((item for item in report.get("taskTypes", [])
                      if item.get("taskType") == "validacao_artefato"), None)
        check(entry is not None and abs(float(entry.get("approvalRate", 0)) - 1.0) < 1e-9,
              "o relatorio trouxe a taxa de aprovacao por task_type")

        p_fail = run_graph(EXE, work / "graph8.json", project, env)
        check(p_fail.returncode != 0, "a execucao de infraestrutura falhou como esperado")
        failed_validations = telemetry_rows(
            project, "SELECT COUNT(*) FROM memory_validation WHERE node_id='validador_falha'")
        check(failed_validations and failed_validations[0][0] == 0,
              "falha antes do agente nao gravou veredito")

        for item_id in ("rej-a", "rej-b"):
            env_val["ARESTRIX_ITEM_ID"] = item_id
            p_rej = run_graph(EXE, work / "graph7.json", project, env_val)
            check(p_rej.returncode == 0, f"execucao reprovada {item_id} terminou com codigo 0")
        rejected = telemetry_rows(
            project,
            "SELECT COUNT(*) FROM memory_validation WHERE node_id='validador' AND ok=0")
        check(rejected and rejected[0][0] >= 2, "as reprovacoes gravaram vereditos ok=0")
        demoted = db_rows(
            project,
            "SELECT COUNT(*) FROM memory_playbook WHERE task_type='validacao_artefato' AND status='active'")
        check(demoted and demoted[0][0] == 0,
              "duas reprovacoes seguidas despromoveram o playbook")

        print("\n== E2: consultado distinto de citado ==")
        env_search = dict(env)
        env_search["ARESTRIX_ITEM_ID"] = "busca-1"
        p_search = run_graph(EXE, work / "graph9.json", project, env_search)
        check(p_search.returncode == 0, "a execucao com busca terminou com codigo 0")
        search_hits = telemetry_rows(
            project,
            "SELECT memory_id, consulted, used FROM memory_hit "
            "WHERE node_id='buscador' AND injected=0 ORDER BY memory_id")
        check(len(search_hits) >= 2, "a busca registrou os resultados consultados")
        check(all(row[1] == 1 for row in search_hits),
              "todo resultado de busca fica consulted=1")
        cited = [row for row in search_hits if row[2] == 1]
        uncited = [row for row in search_hits if row[2] == 0]
        check(len(cited) == 1, "apenas o resultado citado fica used=1")
        check(len(uncited) >= 1, "o resultado nao citado continua used=0")
        invalid = telemetry_rows(
            project,
            "SELECT COALESCE(SUM(invalid_citations),0) FROM memory_usage WHERE node_id='buscador'")
        check(invalid and invalid[0][0] >= 1,
              "citacao de id inexistente foi ignorada e contada")
        expected_cited = db_rows(
            project, "SELECT success_count FROM memory_edge WHERE id=?", (cited[0][0],))
        expected_uncited = db_rows(
            project, "SELECT success_count FROM memory_edge WHERE id=?", (uncited[0][0],))
        check(expected_cited and expected_cited[0][0] >= 1,
              "o item citado foi reforcado")
        check(expected_uncited and expected_uncited[0][0] == 0,
              "o item apenas consultado nao foi reforcado")
        p_rep2 = subprocess.run(
            [str(EXE), "memory", "report", "--root", str(project), "--json"],
            capture_output=True, text=True, timeout=60)
        try:
            report2 = json.loads(p_rep2.stdout)
        except Exception:
            report2 = {}
        check(p_rep2.returncode == 0, "memory report pos-E2 terminou com codigo 0")
        check(str(report2.get("schemaVersion", {}).get("knowledge", "")) == "2",
              "o relatorio expoe a versao 2 do esquema")
        check(int(report2.get("invalidCitations", 0)) >= 1,
              "o relatorio conta as citacoes invalidas")
        check(int(report2.get("hits", {}).get("citedFromSearch", 0)) >= 1,
              "o relatorio separa citacoes vindas da busca")

        print("\n== Memoria desligada com metricas (comparacao de consumo) ==")
        env_off = dict(env)
        env_off["ARESTRIX_MEMORY_METRICS"] = "1"
        p4 = run_graph(EXE, work / "graph4.json", project, env_off)
        out4 = (p4.stdout or "") + (p4.stderr or "")
        check(p4.returncode == 0, "a execucao com memoria desligada terminou com codigo 0")
        check("[MEMORIA] contexto injetado" not in out4, "o agente nao recebeu contexto de memoria")
        off = telemetry_rows(project, "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(memory_enabled),0) "
                                      "FROM memory_usage WHERE memory_enabled=0")
        check(off and off[0][0] >= 1, "o consumo da execucao sem memoria foi registrado")
        check(off and off[0][1] > 0 and off[0][2] == 0, "a execucao sem memoria tem tokens e memoria_enabled=0")

        print("\n== Embeddings desligados por padrao ==")
        (project / "arestrix.project.json").write_text(
            json.dumps({"projectId": "memoria-teste"}), encoding="utf-8")
        p_emb = subprocess.run(
            [str(EXE), "memory", "embeddings", "status", "--root", str(project), "--json"],
            capture_output=True, text=True, timeout=60)
        check(p_emb.returncode == 0, "memory embeddings status terminou com codigo 0")
        try:
            emb_status = json.loads(p_emb.stdout)
        except Exception:
            emb_status = {}
        check(emb_status.get("model") == "embeddinggemma-2",
              "o modelo de embeddings pre-configurado e o EmbeddingGemma 2")
        check(int(emb_status.get("dim", 0)) == 768,
              "o EmbeddingGemma 2 usa 768 dimensoes")
        check(emb_status.get("active") is False, "embeddings continuam desligados por padrao")
        check(int(emb_status.get("indexed", 0)) == 0, "nenhum vetor foi indexado com embeddings desligados")

        print("\n== Consolidacao deterministica ==")
        p_cons = subprocess.run(
            [str(EXE), "memory", "consolidate", "--root", str(project), "--json"],
            capture_output=True, text=True, timeout=60)
        check(p_cons.returncode == 0, "memory consolidate terminou com codigo 0")
        try:
            cons = json.loads(p_cons.stdout)
        except Exception:
            cons = {}
        check(cons.get("ok") is True, "a consolidacao reportou ok")
        check("duplicates" in cons and "contradictions" in cons and "dormant" in cons,
              "o relatorio de consolidacao trouxe os contadores")

        print("\n== Workspace e promocao explicita ==")
        workspace_id = "e2e-workspace"
        p_ws = subprocess.run(
            [str(EXE), "memory", "workspace", "init", "--root", str(project),
             "--workspace-id", workspace_id, "--json"],
            capture_output=True, text=True, timeout=60)
        check(p_ws.returncode == 0, "memory workspace init terminou com codigo 0")
        lessons_for_ws = db_rows(
            project,
            "SELECT id FROM memory_edge WHERE relation='HAS_LESSON' AND status='active' LIMIT 1")
        check(bool(lessons_for_ws), "ha uma licao ativa para promover")
        if lessons_for_ws:
            p_prom = subprocess.run(
                [str(EXE), "memory", "promote", "--root", str(project),
                 "--workspace-id", workspace_id, "--kind", "lesson",
                 "--id", lessons_for_ws[0][0], "--evidence", "e2e promocao", "--json"],
                capture_output=True, text=True, timeout=60)
            check(p_prom.returncode == 0, "memory promote terminou com codigo 0")
        p_ws_status = subprocess.run(
            [str(EXE), "memory", "workspace", "status", "--root", str(project),
             "--workspace-id", workspace_id, "--json"],
            capture_output=True, text=True, timeout=60)
        try:
            ws_status = json.loads(p_ws_status.stdout)
        except Exception:
            ws_status = {}
        check(p_ws_status.returncode == 0, "memory workspace status terminou com codigo 0")
        check(ws_status.get("configured") is True, "o workspace ficou configurado")
        check(int(ws_status.get("lessons", 0)) >= 1, "a licao foi promovida ao workspace")
    finally:
        server.shutdown()
        if os.environ.get("ARESTRIX_E2E_KEEP") == "1":
            print(f"\nProjeto preservado em {work}")
        else:
            shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"\n{len(failures)} verificacao(oes) falharam:")
        for f in failures:
            print("  -", f)
        sys.exit(1)
    print("\nMemoria de projeto executada com sucesso ponta a ponta.")


if __name__ == "__main__":
    main()
