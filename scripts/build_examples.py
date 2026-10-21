#!/usr/bin/env python3
"""Gera os quatro projetos de exemplo em examples/ (do simples ao complexo).

  01-svg-agente           o tutorial: dois agentes, fork/join, laco de autocorrecao (decisao em C++)
  02-triagem-de-suporte   agentes, agentes de decisao (switch com IA e por palavras) e MCP
  03-atendimento-ilustrado  "projeto de projetos": a triagem (02) seguida do agente de SVG (01)
  04-memoria              licao semantica + Blackboard persistidos e injetados no agente

Os projetos nao contem NENHUM segredo: o modelo vem dos Padroes Globais do projeto (provedor/modelo, sem chave) e as
chaves vivem so em variaveis de ambiente do usuario (DEEPSEEK_API_KEY, OPENAI_API_KEY...). Os caminhos sao relativos
(rootPath ".").

Uso:  python scripts/build_examples.py
Depois confira:  npm run check:secrets
"""
import copy
import json
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "examples"
FIXTURE = ROOT / "CppCore" / "tests" / "e2e" / "svg-agent"
STAMP = "2026-10-13T00:00:00Z"

DEFAULTS = {
    "model": {"provider": "deepseek", "model": "deepseek-flash", "skillPath": "", "promptTemplate": ""},
    "sandbox": {"isolationLevel": "host_direct", "network": "egress_llm_only", "maxMemoryMB": 512, "timeoutSeconds": 180,
                "readOnlyMounts": [], "readWriteMounts": []},
    "codeMode": {"allowedTools": ["read", "write"], "customScripts": []},
    "memory": {"enabled": False, "persistent": False, "readBlackboard": False, "writeBlackboard": False,
               "semanticLessons": False, "recordLessons": False, "allowedKeys": [], "domains": []},
}


def defaults(extra_tools=()):
    d = copy.deepcopy(DEFAULTS)
    d["codeMode"]["allowedTools"] = sorted(set(d["codeMode"]["allowedTools"]) | set(extra_tools))
    return d


# ---------------------------------------------------------------------------------------------- vertices
def port(pid, name, direction, ptype="flow", branch=None):
    p = {"id": pid, "name": name, "direction": direction, "type": ptype}
    if branch is not None:
        p["conditionBranchIndex"] = branch
    return p


def base(nid, label, ntype, x, y, outputs, body=""):
    return {"id": nid, "label": label, "type": ntype, "position": {"x": x, "y": y, "z": 0},
            "inputs": [port(f"{nid}_in_0", "in", "input")], "outputs": outputs,
            "parameters": {}, "customLambdaBody": body}


def static(nid, label, x, y, body, headers=None):
    n = base(nid, label, "static", x, y, [port(f"{nid}_out_0", "out", "output")], body)
    if headers:
        n["requiredHeaders"] = headers
    return n


def output(nid, label, x, y, source_path, destination_path, artifact_type, display_name, body="", description=""):
    """Vertice de Saida / Artefato: entrega (copia) um arquivo produzido pelos vertices anteriores."""
    n = base(nid, label, "output", x, y, [port(f"{nid}_out_0", "out", "output")], body)
    data_in = port(f"{nid}_in_data", "data_in", "input", "data")
    data_in["dataType"] = "std::string"
    n["inputs"].append(data_in)
    n["outputSettings"] = {
        "sourcePath": source_path,
        "destinationPath": destination_path,
        "artifactType": artifact_type,
        "displayName": display_name,
        "autoOpenPreview": True,
        "description": description,
    }
    return n


def agent(nid, label, x, y, skill, tools=None, mcp=None, mcp_tools=None, prompt=""):
    """Agente que herda o provedor/modelo dos Padroes Globais e so define a propria skill."""
    model = {"skillPath": skill}
    if prompt:
        model["promptTemplate"] = prompt
    settings = {"inheritSandbox": True, "inheritModel": False, "model": model,
                "inheritCodeMode": tools is None and mcp is None, "inheritMemory": True}
    if tools is not None or mcp is not None:
        code_mode = {"allowedTools": list(tools or ["read", "write"]), "customScripts": []}
        if mcp:
            code_mode["mcpServers"] = list(mcp)
            code_mode["allowedMcpTools"] = list(mcp_tools or [])
        settings["codeMode"] = code_mode
    n = base(nid, label, "agent", x, y, [port(f"{nid}_out_0", "out", "output")])
    n["agentSettings"] = settings
    return n


def switch(nid, label, x, y, cases, default, mode, source, rules=None, instruction=""):
    outs = [port(f"{nid}_out_{i}", name, "output", branch=i) for i, name in enumerate(cases)]
    outs.append(port(f"{nid}_out_default", default, "output", branch=len(cases)))
    n = base(nid, label, "switch", x, y, outs, "")
    n["agentSettings"] = {"inheritSandbox": True, "inheritModel": True, "inheritCodeMode": True, "inheritMemory": True}
    n["switchSettings"] = {
        "mode": mode, "source": source, "instruction": instruction,
        "rules": {f"{nid}_out_{i}": words for i, words in enumerate(rules or [])},
    }
    return n


def mcp(nid, label, x, y, server, tool, arguments, source, output_file=""):
    """Vertice MCP: chama UMA ferramenta de um servidor do projeto, sem modelo de IA."""
    n = base(nid, label, "mcp", x, y, [port(f"{nid}_out_0", "out", "output")], "")
    n["mcpSettings"] = {"server": server, "tool": tool, "arguments": arguments, "source": source,
                        "output": {"file": output_file} if output_file else {}}
    return n


def condition(nid, label, x, y, body):
    outs = [port(f"{nid}_out_0", "branch 0", "output", branch=0), port(f"{nid}_out_1", "branch 1", "output", branch=1)]
    return base(nid, label, "condition", x, y, outs, body)


def edge(src, src_port_idx, dst, feedback=False):
    """Aresta da porta de saida `src_port_idx` (indice, ou 'default') de src para a entrada de dst."""
    sp = f"{src}_out_default" if src_port_idx == "default" else f"{src}_out_{src_port_idx}"
    e = {"id": f"edge_{src}_{src_port_idx}_to_{dst}", "sourceNodeId": src, "sourcePortId": sp,
         "targetNodeId": dst, "targetPortId": f"{dst}_in_0"}
    if feedback:
        e["isFeedbackLoop"] = True
    return e


def graph(project_id, name, description, nodes, edges, extra_tools=()):
    return {
        "version": "1.0.0",
        "metadata": {
            "projectId": project_id, "graphName": name, "description": description, "targetStandard": "C++20",
            "compilerConfig": {"compiler": "msvc", "cppStandard": "C++20", "buildSystem": "cmake",
                               "taskflowIncludePath": "third_party/taskflow", "outputBinaryName": project_id.replace("-", "_"),
                               "enableOpenMP": False, "enableCuda": False, "extraFlags": []},
            "version": "1.0.0", "globalDefaults": defaults(extra_tools),
        },
        "nodes": nodes, "edges": edges,
    }


def manifest(project_id, name, description, extra_tools=()):
    return {
        "projectId": project_id, "name": name, "description": description, "version": "1.0.0",
        "createdAt": STAMP, "updatedAt": STAMP,
        "rootPath": ".",                                   # portatil: o Studio preenche com a pasta real ao abrir
        "database": {"type": "sqlite", "filePath": "storage/database.sqlite", "url": ""},
        "environment": {},                                  # NUNCA guarde segredos aqui: use variaveis de ambiente do usuario
        "globalDefaults": defaults(extra_tools),
        "pipelines": [{"id": "main", "name": "Pipeline Principal", "file": "pipelines/main.json",
                       "isPrimary": True, "description": description}],
    }


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_text(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.strip("\n") + "\n", encoding="utf-8")


# ---------------------------------------------------------------------------------------------- exemplo 1
def build_01():
    """O tutorial do agente de SVG. O grafo vem do fixture gerado a partir do proprio tutorial."""
    src = json.loads((FIXTURE / "graph.json").read_text(encoding="utf-8"))
    nodes = copy.deepcopy(src["nodes"])
    for n in nodes:
        if n["type"] == "agent":
            # herda o provedor/modelo dos Padroes Globais: o usuario escolhe o dele (a skill continua do vertice)
            skill = n["agentSettings"]["model"]["skillPath"]
            n["agentSettings"]["model"] = {"skillPath": skill}
    g = graph("exemplo-svg-agente", "SVG_Agent_Pipeline", "Agente que cria e audita SVGs animados (tutorial do Arestrix)",
              nodes, copy.deepcopy(src["edges"]), extra_tools=["validate_svg"])
    g["metadata"]["globalDefaults"]["codeMode"]["allowedTools"] = ["write", "read", "validate_svg"]

    d = OUT / "01-svg-agente"
    m = manifest("exemplo-svg-agente", "Exemplo 1 - Agente de SVG", g["metadata"]["description"], extra_tools=["validate_svg"])
    m["globalDefaults"]["codeMode"]["allowedTools"] = ["write", "read", "validate_svg"]
    write_json(d / "arestrix.project.json", m)
    write_json(d / "pipelines" / "main.json", g)
    shutil.copyfile(FIXTURE / "skills" / "gerador.md", d / "skills" / "gerador.md") if (d / "skills").mkdir(parents=True, exist_ok=True) is None else None
    shutil.copyfile(FIXTURE / "skills" / "jev.md", d / "skills" / "jev.md")
    return g


# ---------------------------------------------------------------------------------------------- exemplo 2
RECEBER_BODY = """namespace fs = std::filesystem;
fs::create_directories("workspace");
if (!fs::exists("workspace/ticket.txt")) {
    const char* pick = std::getenv("ARESTRIX_EXAMPLE_TICKET");
    std::string name = (pick && *pick) ? pick : "chamado_normal.txt";
    if (name.find("..") != std::string::npos || name.find('/') != std::string::npos || name.find('\\\\') != std::string::npos)
        throw std::runtime_error("ARESTRIX_EXAMPLE_TICKET deve ser so o nome de um arquivo de samples/");
    fs::copy_file("samples/" + name, "workspace/ticket.txt", fs::copy_options::overwrite_existing);
}
std::cout << "[Receber] chamado em workspace/ticket.txt\\n";"""


def entrega(nid, label, x, y, caminho, destino, nome):
    """Entrega do ramo: registra workspace/resposta.md como artefato Markdown em exports/."""
    body = f'std::cout << "[Entrega] resposta pronta em workspace/resposta.md (caminho: {caminho})\\n";'
    return output(nid, label, x, y, "workspace/resposta.md", f"exports/{destino}", "markdown", nome, body,
                  f"Resposta ao chamado ({caminho})")


def support_nodes():
    """Vertices e arestas do fluxo de triagem. Usado pelo exemplo 2 e, depois, pelo 3."""
    clientes = ["clientes"]
    nodes = [
        static("receber", "Receber_Chamado", 0, 0, RECEBER_BODY, headers=["<filesystem>", "<cstdlib>", "<stdexcept>"]),
        # ferramenta NATIVA do Arestrix (sem servidor, sem processo): acha o e-mail do cliente no texto do chamado
        mcp("extrair_email", "Extrair_Email", 450, 0, "arestrix.dados", "regex_extract",
            {"text": "{{input}}", "pattern": "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z]{2,}"},
            {"kind": "file", "path": "workspace/ticket.txt"}),
        # servidor MCP externo do projeto (.workspace/mcps): consulta o cadastro com o e-mail do vertice anterior
        mcp("consultar_cliente", "Consultar_Cliente", 450, 0, "clientes", "lookup_customer",
            {"email": "{{input.0}}"}, {"kind": "previous"}, "workspace/cliente_dados.json"),
        agent("leitor", "Agente_Leitor_de_Chamados", 900, 0, "skills/leitor.md", tools=["read", "write"]),
        switch("triagem", "Triagem_de_Urgencia", 1350, 0, ["urgente", "normal", "duvida"], "revisao_humana",
               mode="model", source={"kind": "previous"},
               instruction=("Classifique o chamado do cliente. Escolha 'urgente' para servico fora do ar, perda de dinheiro, "
                            "cobranca indevida ou risco legal; 'duvida' para perguntas simples sobre como usar o produto; "
                            "'normal' para os demais pedidos. Se nao for possivel decidir, escolha 'revisao_humana'.")),
        # --- urgente
        agent("escalonador", "Agente_Escalonador", 1800, -430, "skills/escalonador.md", tools=["read", "write"],
              mcp=clientes, mcp_tools=["registrar_escalonamento"]),
        entrega("entrega_urgente", "Entrega_Urgente", 2250, -430, "urgente", "resposta-urgente.md", "Resposta ao chamado urgente"),
        # --- normal: redige, revisa e, se preciso, refaz (laco)
        agent("redator", "Agente_Redator", 1800, 0, "skills/redator.md", tools=["read", "write"]),
        agent("revisor", "Agente_Revisor", 2250, 0, "skills/revisor.md", tools=["read", "write"]),
        switch("qualidade", "Controle_de_Qualidade", 2700, 0, ["ajustar", "aprovado"], "revisao_humana",
               mode="rules", source={"kind": "file", "path": "workspace/revisao.txt"},
               rules=["AJUSTAR, reprovado, corrigir", "APROVADO"]),
        entrega("entrega_normal", "Entrega_Normal", 3150, -90, "normal", "resposta-normal.md", "Resposta ao chamado normal"),
        # --- duvida
        agent("faq", "Agente_FAQ", 1800, 430, "skills/faq.md", tools=["read", "write"]),
        entrega("entrega_faq", "Entrega_FAQ", 2250, 430, "duvida", "resposta-duvida.md", "Resposta à dúvida"),
        # --- ninguem decidiu: pessoa
        static("humano", "Revisao_Humana", 3150, 330,
               'std::cout << "[Humano] caso encaminhado para revisao humana. Leia workspace/ticket.txt e workspace/resumo.md.\\n";'),
    ]
    for n in nodes:                       # abre espaco para o vertice Extrair_Email (uma coluna de 450 px)
        if n["id"] not in ("receber", "extrair_email"):
            n["position"]["x"] += 450
    edges = [
        edge("receber", 0, "extrair_email"), edge("extrair_email", 0, "consultar_cliente"), edge("consultar_cliente", 0, "leitor"), edge("leitor", 0, "triagem"),
        edge("triagem", 0, "escalonador"), edge("escalonador", 0, "entrega_urgente"),
        edge("triagem", 1, "redator"), edge("redator", 0, "revisor"), edge("revisor", 0, "qualidade"),
        edge("qualidade", 0, "redator", feedback=True), edge("qualidade", 1, "entrega_normal"),
        edge("triagem", 2, "faq"), edge("faq", 0, "entrega_faq"),
        edge("triagem", "default", "humano"), edge("qualidade", "default", "humano"),
    ]
    return nodes, edges


SKILLS_02 = {
    "leitor.md": """
---
name: leitor-de-chamados
description: Le o chamado, consulta o cadastro do cliente e resume o caso
---

# Papel: LEITOR DE CHAMADOS

Voce le chamados de suporte e prepara um resumo para o proximo passo.

1. Leia `workspace/ticket.txt` com a ferramenta `read`.
2. Leia `workspace/cliente_dados.json`: o cadastro do cliente, consultado antes por um vertice MCP (nome, plano, chamados abertos).
3. Grave `workspace/resumo.md` com: assunto, nome e plano do cliente, quantos chamados ele tem abertos e o pedido principal.
4. Responda com um resumo de ate 4 linhas (assunto, cliente e plano, tom do cliente, pedido principal). Seja objetivo:
   esta resposta sera lida por um classificador que decide o caminho do chamado.
""",
    "escalonador.md": """
---
name: escalonador
description: Escalona chamados urgentes e escreve a primeira resposta ao cliente
---

# Papel: ESCALONADOR

Voce cuida dos chamados URGENTES.

1. Leia `workspace/resumo.md`.
2. Registre o escalonamento com a ferramenta `mcp__clientes__registrar_escalonamento` (e-mail do cliente, resumo curto do problema
   e severidade `alta`). Guarde o numero de protocolo devolvido.
3. Grave `workspace/resposta.md`: peca desculpas, informe o numero do protocolo e diga que um especialista entrara em contato.
   NAO prometa prazos de reembolso, valores ou compensacoes.
4. Responda com uma linha confirmando o protocolo.
""",
    "redator.md": """
---
name: redator-de-respostas
description: Redige a resposta ao cliente e a refaz quando o revisor pede ajustes
---

# Papel: REDATOR DE RESPOSTAS

Voce escreve respostas cordiais a chamados NORMAIS.

1. Leia `workspace/resumo.md`.
2. Tente ler `workspace/revisao.txt`. Se existir e contiver `STATUS: AJUSTAR`, corrija exatamente o que o revisor apontou.
   Se o arquivo nao existir, e a primeira versao.
3. Grave `workspace/resposta.md` com a resposta ao cliente: cumprimente, responda ao pedido e se despeca.
   NUNCA prometa reembolsos, descontos, prazos ou valores que nao constem do resumo.
4. Responda com uma linha dizendo se foi a primeira versao ou uma correcao.
""",
    "revisor.md": """
---
name: revisor-de-respostas
description: Revisa a resposta ao cliente e decide se esta aprovada
---

# Papel: REVISOR DE RESPOSTAS

Voce revisa a resposta escrita ao cliente.

1. Leia `workspace/resposta.md` e `workspace/resumo.md`.
2. Verifique: tom cordial, resposta ao pedido do cliente e ausencia de promessas indevidas (reembolso, desconto, prazo ou valor).
3. Grave `workspace/revisao.txt`:
   - `STATUS: APROVADO` se estiver tudo certo;
   - `STATUS: AJUSTAR` seguido de uma lista curta do que corrigir.
4. Responda com uma linha repetindo o status.
""",
    "faq.md": """
---
name: atendente-faq
description: Responde duvidas simples com base na base de conhecimento
---

# Papel: ATENDENTE DE FAQ

Voce responde duvidas de uso do produto.

1. Leia `workspace/resumo.md` e a base de conhecimento `base/faq.md`.
2. Grave `workspace/resposta.md` com uma resposta curta e cordial, usando SOMENTE o que estiver na base.
   Se a base nao cobrir a duvida, diga que um atendente retornara.
3. Responda com uma linha dizendo qual item da base foi usado.
""",
}

TICKETS = {
    "chamado_urgente.txt": """
Assunto: Sistema fora do ar e cobranca em duplicidade!!

De: ana.souza@example.com

Ola, desde as 9h o painel nao abre para nenhum dos meus funcionarios e hoje fui cobrada duas vezes no cartao.
Preciso que resolvam isso agora, estou perdendo vendas.

Ana Souza
""",
    "chamado_normal.txt": """
Assunto: Troca de plano

De: bruno.lima@example.com

Boa tarde, gostaria de mudar do plano Basico para o Profissional a partir do proximo mes. Preciso de alguma
documentacao? Obrigado.

Bruno Lima
""",
    "chamado_duvida.txt": """
Assunto: Como exporto meus relatorios?

De: carla.dias@example.com

Oi! Como faco para exportar os relatorios mensais em PDF? Nao achei o botao.

Carla
""",
}

FAQ_MD = """
# Base de conhecimento (exemplo)

## Exportar relatorios
Abra **Relatorios**, escolha o mes e clique em **Exportar**. O formato padrao e PDF; para planilha escolha CSV.

## Trocar de plano
Em **Conta > Plano**, selecione o novo plano. A mudanca vale a partir do proximo ciclo de cobranca.

## Redefinir a senha
Na tela de entrada, clique em **Esqueci minha senha** e siga o link enviado ao seu e-mail.
"""

CLIENTES_JSON = {
    "clientes": [
        {"email": "ana.souza@example.com", "nome": "Ana Souza", "plano": "Profissional", "cliente_desde": "2023-04-10", "chamados_abertos": 1,
         "observacao": "Conta empresarial com 12 usuarios."},
        {"email": "bruno.lima@example.com", "nome": "Bruno Lima", "plano": "Basico", "cliente_desde": "2025-01-22", "chamados_abertos": 0,
         "observacao": ""},
        {"email": "carla.dias@example.com", "nome": "Carla Dias", "plano": "Basico", "cliente_desde": "2025-08-03", "chamados_abertos": 0,
         "observacao": "Primeiro contato com o suporte."},
    ]
}

CLIENTES_SERVER = '''
#!/usr/bin/env python3
"""Servidor MCP de demonstracao (stdio, JSON-RPC 2.0 por linha): cadastro ficticio de clientes.

Nao usa rede nem segredos. Ferramentas:
  lookup_customer(email)                          consulta data/clientes.json
  registrar_escalonamento(email, resumo, severidade)   grava uma linha em output/escalonamentos.jsonl

Troque este servidor por um MCP real do seu sistema (CRM, helpdesk...) editando .workspace/mcps/clientes.json.
Se o seu servidor precisar de credenciais, passe-as por variaveis de ambiente referenciadas como ${NOME} no "env".
"""
import json
import os
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
TOOLS = [
    {"name": "lookup_customer", "description": "Consulta um cliente pelo e-mail e devolve plano, tempo de casa e chamados abertos.",
     "inputSchema": {"type": "object", "properties": {"email": {"type": "string", "description": "E-mail do cliente"}}, "required": ["email"]}},
    {"name": "registrar_escalonamento", "description": "Registra o escalonamento de um chamado urgente e devolve o numero de protocolo.",
     "inputSchema": {"type": "object", "properties": {
         "email": {"type": "string"}, "resumo": {"type": "string"}, "severidade": {"type": "string", "enum": ["media", "alta", "critica"]}},
         "required": ["email", "resumo"]}},
]


def lookup(args):
    email = str(args.get("email", "")).strip().lower()
    for c in json.loads((BASE / "data" / "clientes.json").read_text(encoding="utf-8"))["clientes"]:
        if c["email"].lower() == email:
            return json.dumps(c, ensure_ascii=False)
    return json.dumps({"encontrado": False, "email": email}, ensure_ascii=False)


def escalate(args):
    out = BASE / "output"
    out.mkdir(exist_ok=True)
    log = out / "escalonamentos.jsonl"
    n = sum(1 for _ in log.open(encoding="utf-8")) + 1 if log.exists() else 1
    protocolo = f"ESC-{n:04d}"
    record = {"protocolo": protocolo, "email": args.get("email", ""), "resumo": args.get("resumo", ""),
              "severidade": args.get("severidade", "alta")}
    with log.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False) + "\\n")
    return json.dumps({"protocolo": protocolo, "status": "registrado"}, ensure_ascii=False)


def reply(msg_id, result):
    sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": msg_id, "result": result}) + "\\n")
    sys.stdout.flush()


for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    msg = json.loads(line)
    method = msg.get("method")
    if method == "initialize":
        reply(msg["id"], {"protocolVersion": "2025-03-26", "capabilities": {"tools": {}},
                          "serverInfo": {"name": "clientes-demo", "version": "1.0"}})
    elif method == "tools/list":
        reply(msg["id"], {"tools": TOOLS})
    elif method == "tools/call":
        name = msg["params"]["name"]
        args = msg["params"].get("arguments") or {}
        try:
            text = {"lookup_customer": lookup, "registrar_escalonamento": escalate}[name](args)
            reply(msg["id"], {"content": [{"type": "text", "text": text}], "isError": False})
        except Exception as exc:  # noqa: BLE001 - o agente recebe o erro como texto
            reply(msg["id"], {"content": [{"type": "text", "text": f"erro: {exc}"}], "isError": True})
'''

MCP_CONFIG = {
    "id": "clientes",
    "name": "Cadastro de clientes (demo)",
    "transport": "stdio",
    # ${VAR:-padrao}: usa a variavel de ambiente se existir; senao, o valor padrao. Nunca coloque segredos aqui.
    "command": "${ARESTRIX_PYTHON:-python}",
    "args": ["mcp/clientes_server.py"],
    "env": {},
    "url": "",
    "isEnabled": True,
}


def build_02():
    nodes, edges = support_nodes()
    g = graph("exemplo-triagem-suporte", "Triagem_de_Suporte",
              "Triagem de chamados com agentes, decisao por IA e por palavras, e um servidor MCP", nodes, edges)
    d = OUT / "02-triagem-de-suporte"
    write_json(d / "arestrix.project.json",
               manifest("exemplo-triagem-suporte", "Exemplo 2 - Triagem de Suporte", g["metadata"]["description"]))
    write_json(d / "pipelines" / "main.json", g)
    for name, text in SKILLS_02.items():
        write_text(d / "skills" / name, text)
    for name, text in TICKETS.items():
        write_text(d / "samples" / name, text)
    write_text(d / "base" / "faq.md", FAQ_MD)
    write_json(d / "data" / "clientes.json", CLIENTES_JSON)
    write_text(d / "mcp" / "clientes_server.py", CLIENTES_SERVER)
    write_json(d / ".workspace" / "mcps" / "clientes.json", MCP_CONFIG)
    return nodes, edges


# ---------------------------------------------------------------------------------------------- exemplo 4
def build_04():
    """Memoria de projeto de ponta a ponta: uma licao e um resumo sao gravados, persistidos e
    injetados no agente; a resposta vira um artefato de saida."""
    seed_body = (
        'memory.recordLesson(projectId, "memoria", "auditoria_anterior",\n'
        '    "A resposta deve citar a regra aprendida antes de concluir.", "semear");\n'
        'memory.setWorking(projectId, "pipeline", "resumo", taskforge::Json("chamado de exemplo"));\n'
        'std::cout << "[Semear] licao e resumo gravados\\n";'
    )
    nodes = [
        static("semear", "Semear_Licao", 0, 0, seed_body),
        agent("agente", "Agente_Com_Memoria", 450, 0, "skills/memoria.md", tools=["read", "write"],
              prompt=("Leia a memoria injetada e grave workspace/resposta.txt com uma frase citando a regra aprendida. "
                      "Responda apenas OK.")),
        output("entrega", "Entrega_Resposta", 900, 0, "workspace/resposta.txt", "exports/resposta.md",
               "markdown", "Resposta com memória"),
    ]
    nodes[1]["agentSettings"]["inheritMemory"] = False
    nodes[1]["agentSettings"]["memory"] = {
        "enabled": True,
        "persistent": True,
        "projectId": "exemplo-memoria",
        "readBlackboard": True,
        "writeBlackboard": True,
        "semanticLessons": True,
        "recordLessons": True,
        "allowedKeys": ["resumo"],
        "domains": ["memoria"],
        "contextBudgetTokens": 400,
    }
    edges = [
        edge("semear", 0, "agente"),
        edge("agente", 0, "entrega"),
    ]
    g = graph("exemplo-memoria", "Memoria_de_Projeto",
              "Memoria de projeto: licao persistente, Blackboard e injecao no agente", nodes, edges)
    d = OUT / "04-memoria"
    write_json(d / "arestrix.project.json",
               manifest("exemplo-memoria", "Exemplo 4 - Memória de Projeto", g["metadata"]["description"]))
    write_json(d / "pipelines" / "main.json", g)
    write_text(d / "skills" / "memoria.md", """
---
name: agente-com-memoria
description: Aplica as licoes e o resumo carregados da memoria do projeto
---

# Papel: AGENTE COM MEMORIA

1. Leia as licoes semanticas e o resumo do Blackboard injetados no seu contexto.
2. Se precisar de detalhe extra, use `memory_search`.
3. Se descobrir uma regra nova e duradoura, registre-a com `memory_record_lesson`.
4. Grave `workspace/resposta.txt` com uma frase curta citando a regra aprendida.
5. Responda apenas `OK`.
""")


def build_05():
    """Pesquisa web via MCP (Keenable) + redacao de matematica com memoria de projeto."""
    seed_body = (
        'memory.recordLesson(projectId, "matematica", "revisao_anterior",\n'
        '    "Toda afirmacao matematica deve citar a fonte ou justificar o passo.", "semear");\n'
        'memory.setWorking(projectId, "pipeline", "tema", taskforge::Json("teorema de Pitagoras"));\n'
        'std::cout << "[Semear] licao e tema gravados na memoria\\n";'
    )
    recall_body = (
        'memory.setWorking(projectId, "pipeline", "ultimo_texto", taskforge::Json("workspace/texto-matematica.md"));\n'
        'memory.recordLesson(projectId, "matematica", "pos_redacao",\n'
        '    "Antes de publicar, confira se cada fonte citada foi realmente consultada.", "guardar_licao");\n'
        'std::cout << "[Guardar] licao de revisao registrada\\n";'
    )
    nodes = [
        static("semear", "Semear_Memoria", 0, 0, seed_body),
        mcp("pesquisar", "Pesquisar_Fontes", 450, 0, "keenable", "search_web_pages",
            {"query": "{{input}}"}, {"kind": "file", "path": "samples/tema.md"},
            "workspace/fontes.json"),
        agent("redigir", "Redigir_Texto_Matematico", 900, 0, "skills/redator-matematica.md",
              tools=["read", "write"],
              prompt=("Leia workspace/fontes.json (resultados da pesquisa web) e escreva "
                      "workspace/texto-matematica.md seguindo a skill. Responda apenas OK.")),
        static("guardar", "Guardar_Licao", 1350, 0, recall_body),
        output("entrega", "Entrega_Texto", 1800, 0, "workspace/texto-matematica.md",
               "exports/texto-matematica.md", "markdown", "Texto de matematica com fontes"),
    ]
    nodes[2]["agentSettings"]["inheritMemory"] = False
    nodes[2]["agentSettings"]["memory"] = {
        "enabled": True,
        "persistent": True,
        "projectId": "exemplo-matematica-memoria",
        "readBlackboard": True,
        "writeBlackboard": True,
        "semanticLessons": True,
        "recordLessons": True,
        "allowedKeys": ["tema"],
        "domains": ["matematica"],
        "contextBudgetTokens": 600,
    }
    edges = [
        edge("semear", 0, "pesquisar"),
        edge("pesquisar", 0, "redigir"),
        edge("redigir", 0, "guardar"),
        edge("guardar", 0, "entrega"),
    ]
    g = graph("exemplo-matematica-memoria", "Matematica_Com_Memoria",
              "Pesquisa web via MCP Keenable + redacao de matematica com licoes persistentes",
              nodes, edges)
    d = OUT / "05-matematica-com-memoria"
    write_json(d / "arestrix.project.json",
               manifest("exemplo-matematica-memoria", "Exemplo 5 - Matematica com Memoria",
                        g["metadata"]["description"]))
    write_json(d / "pipelines" / "main.json", g)
    write_json(d / ".workspace" / "mcps" / "keenable.json", {
        "id": "keenable",
        "name": "Keenable Web Search",
        "transport": "http",
        "url": "https://api.keenable.ai/mcp",
        "headers": {"X-API-Key": "${KEENABLE_API}"},
        "isEnabled": True,
    })
    write_text(d / "samples" / "tema.md", """
# Tema de pesquisa

Demonstracoes do teorema de Pitagoras e aplicacoes em problemas de geometria.
""")
    write_text(d / "skills" / "redator-matematica.md", """
---
name: redator-matematica
description: Escreve um texto didatico de matematica com fontes verificaveis e memoria do projeto
---

# Papel: REDATOR DE MATEMATICA

1. Leia `workspace/fontes.json` com a ferramenta `read`. Ele contem os resultados da pesquisa web.
2. Use a memoria do projeto (licoes injetadas) para seguir as regras de precisao e citacao.
3. Escreva `workspace/texto-matematica.md` em Markdown, com:
   - uma explicacao curta do tema;
   - uma demonstracao ou justificativa passo a passo;
   - um exemplo resolvido;
   - uma secao de referencias com as URLs realmente presentes em `fontes.json`.
4. Nao invente fontes, numeros ou citacoes. Se a pesquisa nao trouxer evidencia, declare a limitacao.
5. Responda apenas `OK`.
""")


# ---------------------------------------------------------------------------------------------- exemplo 3
def prefixed(nodes, edges, prefix, dx=0, dy=0):
    """Copia vertices/arestas com ids prefixados (portas e arestas incluidas) e posicoes deslocadas."""
    nodes = copy.deepcopy(nodes)
    edges = copy.deepcopy(edges)
    old_ids = [n["id"] for n in nodes]

    def fix(text):
        for oid in sorted(old_ids, key=len, reverse=True):
            if text == oid or text.startswith(oid + "_"):
                return prefix + text
        return text

    for n in nodes:
        n["id"] = prefix + n["id"]
        n["position"]["x"] += dx
        n["position"]["y"] += dy
        for p in n["inputs"] + n["outputs"]:
            p["id"] = prefix + p["id"]
        rules = (n.get("switchSettings") or {}).get("rules")
        if rules:
            n["switchSettings"]["rules"] = {prefix + k: v for k, v in rules.items()}
    for e in edges:
        e["id"] = prefix + e["id"]
        for k in ("sourceNodeId", "targetNodeId", "sourcePortId", "targetPortId"):
            e[k] = fix(e[k])
    return nodes, edges


def build_03(ex1_graph, ex2_nodes, ex2_edges):
    """Projeto de projetos: a triagem (02); na ramificacao "duvida" a resposta ganha uma explicacao em SVG animado (01)."""
    a_nodes, a_edges = prefixed(ex2_nodes, ex2_edges, "atd_")
    # a ramificacao "duvida" entrega a resposta escrita e DEPOIS segue para o agente de SVG
    for n in a_nodes:
        if n["id"] == "atd_entrega_faq":
            n["label"] = "Entrega_Resposta"
            n["outputSettings"]["displayName"] = "Resposta à dúvida"
            n["outputSettings"]["description"] = "Resposta escrita antes da explicação animada"

    s_nodes, s_edges = prefixed(ex1_graph["nodes"], ex1_graph["edges"], "svg_", dx=2700, dy=430)
    for n in s_nodes:
        if n["id"] == "svg_gerador":
            n["agentSettings"]["model"]["promptTemplate"] = (
                "Crie um SVG animado curto em workspace/scene.svg que explique visualmente, em 3 passos, a resposta escrita em "
                "workspace/resposta.md (leia-a primeiro). Esta e a execucao numero {{iteration}}; se existir um parecer do auditor "
                "em workspace/audit_decision.txt, corrija o que ele apontou.")
        if n["id"] == "svg_entrega":
            n["customLambdaBody"] = ('std::cout << "[Entrega] resposta em workspace/resposta.md e explicacao animada em '
                                     'workspace/scene.svg (caminho: duvida)\\n";')
            n["outputSettings"]["displayName"] = "Explicação animada da resposta"
            n["outputSettings"]["description"] = "SVG animado que ilustra a resposta ao chamado"
    link = edge("atd_entrega_faq", 0, "svg_gerador")

    nodes = a_nodes + s_nodes
    edges = a_edges + s_edges + [link]
    g = graph("exemplo-atendimento-ilustrado", "Atendimento_Ilustrado",
              "Triagem de suporte (exemplo 2) com resposta ilustrada por um agente de SVG (exemplo 1)", nodes, edges,
              extra_tools=["validate_svg"])
    d = OUT / "03-atendimento-ilustrado"
    m = manifest("exemplo-atendimento-ilustrado", "Exemplo 3 - Atendimento Ilustrado", g["metadata"]["description"],
                 extra_tools=["validate_svg"])
    write_json(d / "arestrix.project.json", m)
    write_json(d / "pipelines" / "main.json", g)

    # tudo o que os dois fluxos usam, copiado dos exemplos de origem (sem duplicar a manutencao)
    for sub in ("skills", "samples", "base", "data", "mcp", ".workspace"):
        for origin in ("01-svg-agente", "02-triagem-de-suporte"):
            src = OUT / origin / sub
            if src.exists():
                shutil.copytree(src, d / sub, dirs_exist_ok=True)


def main():
    # Os README sao escritos a mao: sobrevivem a regeneracao (o resto de cada pasta e recriado do zero).
    kept = {}
    if OUT.exists():
        for child in OUT.iterdir():
            if child.is_dir() and child.name[:2].isdigit():
                kept[child.name] = {f.name: f.read_text(encoding="utf-8") for f in child.glob("README*.md")}
                shutil.rmtree(child)
    ex1 = build_01()
    ex2_nodes, ex2_edges = build_02()
    build_03(ex1, ex2_nodes, ex2_edges)
    build_04()
    build_05()
    for name, files in kept.items():
        for fname, text in files.items():
            (OUT / name / fname).write_text(text, encoding="utf-8")
    print("Exemplos gerados em", OUT)


if __name__ == "__main__":
    main()
