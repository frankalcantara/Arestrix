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
    return g1, g2, g3, g4


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = json.loads(self.rfile.read(length) or b"{}")
        system = body.get("messages", [{}])[0].get("content", "")
        with self.server.log_lock:
            with self.server.log_path.open("a", encoding="utf-8") as f:
                f.write(system + "\n===SYSTEM-END===\n")
        data = json.dumps({
            "choices": [{"message": {"role": "assistant", "content": "OK"}, "finish_reason": "stop"}],
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


def db_rows(project, sql, params=()):
    path = memory_db(project)
    if not path.exists():
        return []
    with sqlite3.connect(path) as conn:
        return conn.execute(sql, params).fetchall()


def main():
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memoria-"))
    project = work / "projeto"
    project.mkdir(parents=True)
    log_file = work / "systems.log"
    g1, g2, g3, g4 = make_graphs()
    (work / "graph1.json").write_text(json.dumps(g1, indent=2), encoding="utf-8")
    (work / "graph2.json").write_text(json.dumps(g2, indent=2), encoding="utf-8")
    (work / "graph3.json").write_text(json.dumps(g3, indent=2), encoding="utf-8")
    (work / "graph4.json").write_text(json.dumps(g4, indent=2), encoding="utf-8")

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
        usage = db_rows(project, "SELECT COUNT(*), COALESCE(SUM(calls),0), COALESCE(SUM(prompt_tokens),0), "
                                 "COALESCE(SUM(memory_context_chars),0) FROM memory_usage")
        check(usage and usage[0][0] >= 1, "o consumo de API foi registrado em memory_usage")
        check(usage and usage[0][1] >= 1 and usage[0][3] > 0, "a chamada de API registrou contexto de memoria")
        outcome = db_rows(project, "SELECT COALESCE(SUM(outcome='success'),0), COALESCE(SUM(retries),0) FROM memory_usage")
        check(outcome and outcome[0][0] >= 1, "a execucao bem-sucedida foi registrada com outcome=success")
        hits = db_rows(project, "SELECT COALESCE(SUM(injected),0), COALESCE(SUM(used),0) FROM memory_hit")
        check(hits and hits[0][0] >= 1, "as memorias injetadas foram registradas em memory_hit")
        episodes = db_rows(project, "SELECT kind, metadata FROM memory_episode WHERE kind='agent_success'")
        check(episodes and '"outcome":"success"' in episodes[0][1], "o episodio de sucesso foi registrado com outcome")

        text1 = log_file.read_text(encoding="utf-8") if log_file.exists() else ""
        check(RULE in text1, "a licao apareceu no system prompt")
        check(BLACKBOARD_VALUE in text1, "a chave autorizada do Blackboard apareceu no prompt")
        check(HIDDEN_RULE not in text1, "licao de outro dominio nao foi injetada")
        check(HIDDEN_KEY_VALUE not in text1, "chave nao autorizada do Blackboard nao foi injetada")
        check(TEST_KEY not in out1, "a chave de teste nao aparece na saida")

        print("\n== Memoria de projeto: segunda execucao (carrega do disco) ==")
        p2 = run_graph(EXE, work / "graph2.json", project, env)
        out2 = (p2.stdout or "") + (p2.stderr or "")
        check(p2.returncode == 0, "segunda execucao terminou com codigo 0")
        text2 = log_file.read_text(encoding="utf-8") if log_file.exists() else ""
        check(text2.count("===SYSTEM-END===") >= 2, "o agente recebeu um segundo system prompt")
        check(text2.count(RULE) >= 2, "a licao foi carregada do disco na segunda execucao")
        check(text2.count(BLACKBOARD_VALUE) >= 2, "o Blackboard foi carregado do banco na segunda execucao")
        check("[MEMORIA] contexto injetado" in out2, "o runtime injetou memoria tambem na segunda execucao")
        usage2 = db_rows(project, "SELECT COUNT(*), COALESCE(SUM(calls),0), COALESCE(SUM(prompt_tokens),0) FROM memory_usage")
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
        ab = db_rows(project, "SELECT COUNT(*), COALESCE(SUM(memory_enabled),0), COALESCE(SUM(outcome='success'),0) "
                              "FROM memory_usage WHERE memory_enabled=0")
        check(ab and ab[0][0] >= 1 and ab[0][1] == 0 and ab[0][2] >= 1,
              "o mesmo grafo foi registrado como memoria OFF com outcome=success")

        print("\n== Memoria de projeto: falha de agente gera licao automatica ==")
        p3 = run_graph(EXE, work / "graph3.json", project, env)
        out3 = (p3.stdout or "") + (p3.stderr or "")
        check(p3.returncode != 0, "a execucao com modelo ausente falhou como esperado")
        check("modelo nao configurado" in out3, "a falha veio do agente sem modelo")
        failure_lessons = db_rows(project, "SELECT e.metadata FROM memory_edge e WHERE e.relation='HAS_LESSON' AND e.metadata LIKE '%agent_failure%'")
        failure_text = " ".join(row[0] for row in failure_lessons)
        check("Falha no agente Agente_Com_Memoria" in failure_text, "a falha foi gravada como licao automatica")
        check('"agent_failure"' in failure_text, "a licao foi classificada como falha de agente")

        print("\n== Memoria desligada com metricas (comparacao de consumo) ==")
        env_off = dict(env)
        env_off["ARESTRIX_MEMORY_METRICS"] = "1"
        p4 = run_graph(EXE, work / "graph4.json", project, env_off)
        out4 = (p4.stdout or "") + (p4.stderr or "")
        check(p4.returncode == 0, "a execucao com memoria desligada terminou com codigo 0")
        check("[MEMORIA] contexto injetado" not in out4, "o agente nao recebeu contexto de memoria")
        off = db_rows(project, "SELECT COUNT(*), COALESCE(SUM(prompt_tokens),0), COALESCE(SUM(memory_enabled),0) "
                               "FROM memory_usage WHERE memory_enabled=0")
        check(off and off[0][0] >= 1, "o consumo da execucao sem memoria foi registrado")
        check(off and off[0][1] > 0 and off[0][2] == 0, "a execucao sem memoria tem tokens e memoria_enabled=0")
    finally:
        server.shutdown()
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print(f"\n{len(failures)} verificacao(oes) falharam:")
        for f in failures:
            print("  -", f)
        sys.exit(1)
    print("\nMemoria de projeto executada com sucesso ponta a ponta.")


if __name__ == "__main__":
    main()
