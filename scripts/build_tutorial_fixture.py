#!/usr/bin/env python3
"""Gera o projeto de teste de docs/pt-BR/tutorials/svg-agent.md.

Cria CppCore/tests/e2e/svg-agent/ com:
  - graph.json          grafo exatamente como o tutorial manda montar na interface
  - skills/gerador.md   skill 3.1 (DeepSeek Flash)
  - skills/jev.md       skill 3.4 (JEV)

Os trechos de C++ e de skill sao extraidos do proprio tutorial, para que o teste
acompanhe o documento.
"""
import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
TUTORIAL = (ROOT / "docs" / "pt-BR" / "tutorials" / "svg-agent.md").read_text(encoding="utf-8")
OUT = ROOT / "CppCore" / "tests" / "e2e" / "svg-agent"


def section_block(title_start: str, lang: str) -> str:
    """Primeiro bloco ```lang ... ``` depois do titulo que comeca com title_start."""
    i = TUTORIAL.index(title_start)
    m = re.search(r"```" + lang + r"\n(.*?)\n```", TUTORIAL[i:], re.S)
    assert m, title_start
    return m.group(1)


skill_gen = section_block("### 3.1 Skill do Gerador", "markdown")
lambda_a = section_block("### 3.2 C", "cpp")
lambda_b = section_block("### 3.3 C", "cpp")
skill_jev = section_block("### 3.4 Skill do Auditor", "markdown")
lambda_cond = section_block("### 3.5 C", "cpp")


def port(pid, name, direction, ptype="flow", branch=None):
    p = {"id": pid, "name": name, "direction": direction, "type": ptype}
    if branch is not None:
        p["conditionBranchIndex"] = branch
    return p


def node(nid, label, ntype, x, y, body="", settings=None, condition=False):
    outs = ([port(f"{nid}_out_0", "branch 0", "output", branch=0), port(f"{nid}_out_1", "branch 1", "output", branch=1)]
            if condition else [port(f"{nid}_out_0", "out", "output")])
    n = {
        "id": nid, "label": label, "type": ntype, "position": {"x": x, "y": y},
        "inputs": [port(f"{nid}_in_0", "in", "input")], "outputs": outs,
        "parameters": {}, "customLambdaBody": body,
    }
    if settings is not None:
        n["agentSettings"] = settings
    return n


def edge(src, sport, dst):
    return {
        "id": f"edge_{src}_{sport}_to_{dst}_{dst}_in_0",
        "sourceNodeId": src, "sourcePortId": sport,
        "targetNodeId": dst, "targetPortId": f"{dst}_in_0",
    }


gen = node("gerador", "Agente_Gerador_DeepSeek", "agent", 0, 0, settings={
    "inheritSandbox": True, "inheritModel": False,
    "model": {"provider": "deepseek", "model": "deepseek-flash", "skillPath": "skills/gerador.md"},
    "inheritCodeMode": True, "inheritMemory": True,
})
jev = node("jev", "Agente_Auditor_JEV", "agent", 900, 0, settings={
    "inheritSandbox": True, "inheritModel": False,
    "model": {"provider": "jev", "model": "jev-visual-evaluator", "skillPath": "skills/jev.md"},
    "inheritCodeMode": True, "inheritMemory": True,
})

# Passo 6 do tutorial: o sucesso do decisor termina em um vertice de SAIDA (output).
# O destino da copia vem de outputSettings; o corpo padrao e o mesmo que a interface cria.
entrega_data = port("entrega_in_data", "data_in", "input", "data")
entrega_data["dataType"] = "std::string"
entrega = {
    "id": "entrega", "label": "Entrega_SVG_Final", "type": "output", "position": {"x": 1500, "y": -80},
    "inputs": [port("entrega_in_0", "in", "input"), entrega_data],
    "outputs": [port("entrega_out_0", "out", "output")],
    "parameters": {},
    "customLambdaBody": ("// Coletor de Entrega / Artefato: Entrega_SVG_Final\n"
                         "// Em runtime, este vertice registra e disponibiliza o artefato produzido na aba Artefatos "
                         "e na pasta de saida.\n"
                         "std::cout << \"[OUTPUT: Entrega_SVG_Final] Entregavel registrado com sucesso!\\n\";"),
    "outputSettings": {
        "sourcePath": "workspace/scene.svg",
        "destinationPath": "exports/scene.svg",
        "artifactType": "svg",
        "displayName": "Cena SVG Animada Final",
        "autoOpenPreview": True,
        "description": "Vetor SVG animado e auditado gerado pelo pipeline de agentes",
    },
}

nodes = [
    gen,
    node("linter", "Linter_Tipografia_Estatico", "static", 300, -100, lambda_a),
    node("smil", "Auditor_Timeline_SMIL", "static", 300, 100, lambda_b),
    node("join", "Consolidador_Diagnostico", "static", 600, 0, "std::cout << \"[Join] diagnosticos consolidados\\n\";"),
    jev,
    node("decisor", "Decisor_Qualidade", "condition", 1200, 0, lambda_cond, condition=True),
    entrega,
]

# Sem isFeedbackLoop no retorno do decisor: o gerador de codigo deve detectar o ciclo sozinho,
# mas a UI marca a aresta; o fixture marca como a UI faria.
edges = [
    edge("gerador", "gerador_out_0", "linter"),
    edge("gerador", "gerador_out_0", "smil"),
    edge("linter", "linter_out_0", "join"),
    edge("smil", "smil_out_0", "join"),
    edge("join", "join_out_0", "jev"),
    edge("jev", "jev_out_0", "decisor"),
    edge("decisor", "decisor_out_0", "entrega"),
    edge("decisor", "decisor_out_1", "gerador"),
]
edges[-1]["isFeedbackLoop"] = True

graph = {
    "version": "1.0.0",
    "metadata": {
        "projectId": "svg-agent",
        "graphName": "SVG_Agent_Pipeline",
        "description": "Agente Auditor e Criador de SVGs Animados",
        "targetStandard": "C++20",
        "compilerConfig": {
            "compiler": "msvc", "cppStandard": "C++20", "buildSystem": "cmake",
            "taskflowIncludePath": "third_party/taskflow", "outputBinaryName": "svg_agent",
            "enableOpenMP": False, "enableCuda": False, "extraFlags": [],
        },
        "version": "1.0.0",
        "globalDefaults": {
            "sandbox": {"isolationLevel": "host_direct", "network": "egress_llm_only", "maxMemoryMB": 512,
                        "timeoutSeconds": 120, "readOnlyMounts": [], "readWriteMounts": []},
            "model": {"provider": "deepseek", "model": "deepseek-flash", "skillPath": "", "promptTemplate": ""},
            "codeMode": {"allowedTools": ["write", "read", "validate_svg"], "customScripts": []},
        },
    },
    "nodes": nodes,
    "edges": edges,
}

(OUT / "skills").mkdir(parents=True, exist_ok=True)
(OUT / "graph.json").write_text(json.dumps(graph, indent=2, ensure_ascii=False), encoding="utf-8", newline="\n")
(OUT / "skills" / "gerador.md").write_text(skill_gen + "\n", encoding="utf-8", newline="\n")
(OUT / "skills" / "jev.md").write_text(skill_jev + "\n", encoding="utf-8", newline="\n")
print("Fixture gerado em", OUT)
