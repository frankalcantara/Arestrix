# Exemplo 2 — Triagem de suporte (agentes, vértices de decisão e MCP)

[English](README.md) · [Todos os exemplos](../README.pt-BR.md)

Um chamado de suporte é lido, classificado e respondido. Nada aqui é código que você precise escrever: os caminhos são
escolhidos por um modelo de IA (e por palavras), e um cadastro de clientes é acessado por um servidor MCP.

```
Receber ─► Consultar cliente (vértice MCP) ─► Leitor (agente) ─► Triagem (switch, IA decide) ─┬─ urgente ─► Escalonador (agente + MCP) ─► Entrega
                                                                 ├─ normal ──► Redator ─► Revisor ─► Qualidade (switch, palavras) ─┬─ aprovado ─► Entrega
                                                                 │                ▲                                                ├─ ajustar ─┘ (laço)
                                                                 │                └────────────────────────────────────────────────┘
                                                                 ├─ duvida ──► FAQ (agente) ─► Entrega
                                                                 └─ revisao_humana ─► Revisão humana (também alcançada pela Qualidade)
```

| Vértice | O que mostra |
| :--- | :--- |
| Consultar_Cliente | Um **vértice MCP**: uma chamada direta e determinística a `lookup_customer` (sem IA), usando o e-mail que o *Receber_Chamado* extraiu. O resultado vai para `workspace/cliente_dados.json`. |
| Agente_Leitor | Um agente que lê o chamado e os dados do cliente e escreve o resumo. A resposta final dele alimenta a triagem. |
| Triagem_de_Urgencia | Um **switch no modo "modelo de IA decide"**: o modelo lê a resposta do leitor e escolhe `urgente`, `normal`, `duvida` ou o default. |
| Agente_Escalonador | Um agente que recebe um servidor MCP como **ferramentas** e decide chamar `registrar_escalonamento` (grava `output/escalonamentos.jsonl`). As duas formas de usar MCP, lado a lado. |
| Redator → Revisor → Controle_de_Qualidade | Um **laço de feedback**: o revisor grava `STATUS: AJUSTAR` ou `APROVADO`; um **switch no modo "palavras"** (sem IA) devolve ao redator até a aprovação. |
| Agente_FAQ | Um agente que responde a partir de `base/faq.md`. |
| Revisao_Humana | O default do switch: quando nada serve, uma pessoa assume. |

**Precisa de:** um modelo + a chave de API dele, e **Python** (ele executa o servidor MCP de demonstração em
`mcp/clientes_server.py`; defina `ARESTRIX_PYTHON` se o seu comando não for `python`). Escolha o chamado com
`ARESTRIX_EXAMPLE_TICKET=chamado_urgente.txt` (ou `_normal`, `_duvida`).

**MCP:** o servidor é configurado em `.workspace/mcps/clientes.json` e cada agente lista os servidores que pode usar. O servidor
de demonstração é fictício (`data/clientes.json`, somente endereços `@example.com`). Para usar o seu sistema real, aponte
`command`/`args` para o seu servidor MCP; se ele precisar de credenciais, passe-as como `"env": {"TOKEN": "${MEU_TOKEN}"}`: o valor
é lido da **sua** variável de ambiente e nunca fica no arquivo.

**Traga seus próprios agentes:** reescreva as skills em `skills/`, edite a orientação do switch no Inspetor (o campo
*orientação* de **Triagem_de_Urgencia**), mude as palavras do **Controle_de_Qualidade**, acrescente casos e ramos.
