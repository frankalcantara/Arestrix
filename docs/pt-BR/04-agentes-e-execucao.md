# Agentes e execução

[English](../en/04-agents-and-execution.md) · [Índice](../../README.pt-BR.md#documentação)

## Compilar e Executar (F5)

1. O grafo é validado e enviado ao backend C++.
2. O backend gera um `main.cpp` Taskflow, compila com MSVC (Windows) ou g++/clang++ (Linux) contra o
   `arestrix_core` e **executa dentro da pasta do projeto**. Um grafo inalterado reaproveita o binário compilado.
3. Logs e telemetria dos agentes chegam em fluxo ao console e aos selos nos vértices.

`F5` ou `Ctrl+Enter` executa, `Shift+F5` interrompe. O mesmo motor está disponível sem a interface:

```bash
arestrix-cli gen graph.json                    # imprime o C++ gerado
arestrix-cli run graph.json --root <projeto>   # gera, compila e executa
```

## O que um vértice de agente faz

Um vértice de agente executa um **laço real de uso de ferramentas** contra o provedor configurado:

- A **skill** (`SKILL.md`) é o prompt de sistema; o template de prompt do vértice é a tarefa.
- O modelo pode chamar ferramentas: `read`, `write`, `edit`, `bash`, `validate_svg`, mais as ferramentas dos servidores MCP que o agente
  declarar (inclusive as [ferramentas nativas do Arestrix](05-workspaces-memoria-skills-mcp.md#ferramentas-nativas-sem-instalação-sem-processo)) (veja [Servidores MCP](05-workspaces-memoria-skills-mcp.md#servidores-mcp)). As nativas ficam
  confinadas à pasta do projeto.
- O laço continua até o modelo parar de chamar ferramentas. Skills podem acrescentar ferramentas e servidores MCP.

**Idioma do agente.** Em tempo de execução o agente é configurado no idioma da sua própria constituição: a skill e o template
de prompt (os textos que o definem). O runtime detecta o idioma e emite tudo o que o agente enxerga — o bloco de contexto do
sistema, o prompt padrão, as descrições e esquemas das ferramentas nativas e internas, e as mensagens de erro devolvidas a ele
— nesse idioma (português ou inglês; português é o padrão quando os textos não dão sinal claro). Descrições de ferramentas MCP
externas são dados de terceiros e passam como vieram. Logs de console e telemetria ficam no idioma da interface.

Os provedores estão em [Primeiros passos](02-primeiros-passos.md#chaves-de-api-e-provedores).

### As cinco ações rápidas

Todo vértice tem uma barra de cinco botões (também no menu de botão direito):

| Ação | Escopo | Configura |
| :--- | :--- | :--- |
| Sandbox | Workspace | Nível de isolamento, política de rede, memória, timeout, montagens. |
| Modelo | Projeto | Provedor, modelo, arquivo de skill, template de prompt. |
| Ferramentas | Projeto | Ferramentas permitidas e servidores MCP. |
| Memória | Projeto | Blackboard, lições semânticas, persistência. |
| Skills | Workspace / Projeto | Skills ligadas ao vértice. |

As configurações podem ser herdadas dos **padrões globais** (barra de ferramentas) ou substituídas por vértice.

## Sandboxes

| Nível | Isolamento | Observações |
| :--- | :--- | :--- |
| `host_direct` | Job Object / grupo de processos, timeout, limite de memória | **Não é uma fronteira de segurança.** Remove `*_API_KEY`, `*_TOKEN`, `*_SECRET` do ambiente do processo filho. |
| `container` | Docker | `--cap-drop=ALL`, raiz somente leitura, sem rede por padrão, montagens só do workspace. |
| `microvm` | WSL2 | Compartilha o sistema de arquivos do host; recusa políticas `offline` que não consegue impor. |

Política de rede por vértice: `offline`, `egress_llm_only` ou `full`.

## Propriedades de segurança

- Um nível de sandbox indisponível **falha de forma fechada**. Não recorre silenciosamente ao host, a menos que
  `allowFallbackToHost` esteja ativado.
- As ferramentas de arquivo e os endpoints de arquivo expostos à interface rejeitam caminhos fora do projeto:
  `..`, caminhos absolutos, links simbólicos e streams NTFS.
- Nenhum shell é usado para iniciar processos (`std::system` não é usado).
- Um laço de feedback não gasta dinheiro sem limite: cada vértice de agente é limitado a 10 execuções
  (`ARESTRIX_MAX_AGENT_RUNS`).

## Telemetria

O runtime transmite eventos que o canvas mostra como selos em cada vértice:

| Evento | Significado |
| :--- | :--- |
| `thinking` | Raciocínio em andamento, com contagem acumulada de tokens/bytes. |
| `tool_start` / `tool_end` | Uma chamada de ferramenta começou ou terminou, com argumentos e status. |
| `429 retry` | Limite de taxa detectado (`RESOURCE_EXHAUSTED`, "Rate limit reached"); uma contagem regressiva mostra a nova tentativa agendada. |
| `completed` | O vértice terminou. |

## Lotes e retomada

Para pipelines que processam muitos itens (artigos, lotes de dados), o **motor de retomada** mantém um
checkpoint por item e por etapa em `pipeline-state.json`, gravado de forma atômica.

- **Validação por artefato:** uma etapa é marcada como concluída sem chamar a API de novo se sua saída
  existe, tem um tamanho mínimo e casa com um padrão esperado (por exemplo, front matter YAML). Isso
  é registrado como `recoveredFromArtifact: true`.
- **Gargalos serializados:** etapas pesadas podem rodar um item por vez para respeitar os limites do provedor,
  enquanto as etapas seguintes rodam concorrentemente.
- A aba **Executar** tem o orquestrador de lotes: itens, concorrência, etapas serializadas e uma matriz de checkpoints ao vivo.

## Entrega de Artefatos e Vértice de Saída (Output)

Todo pipeline voltado a produzir entregáveis (gráficos vetoriais, relatórios, planilhas, modelos ou datasets) pode declarar um ou mais vértices do tipo **`output` (Saída / Artefato)**.

- **Configuração de Origem e Destino:** O vértice define `sourcePath` (ex.: `workspace/scene.svg` ou `workspace/relatorio.md`) gerado pelos agentes anteriores e opcionalmente um `destinationPath` (ex.: `exports/scene.svg`). No término da execução, o arquivo é copiado de forma atômica para a pasta final de entrega.
- **Suporte Multi-Formato Integrado:**
  - **SVG:** Visualizador vetorial interativo com controles de zoom (+, -, 100%, ajuste à tela), inspeção de código-fonte e garantia de nitidez (zero sombras/filtros em fontes).
  - **Planilhas (CSV, TSV, XLSX):** Grid de dados tabular interativo com busca instantânea por células e visualização dos dados brutos.
  - **Texto / Markdown:** Leitor formatado de documentos técnicos e relatórios com realce de sintaxe e cópia em um clique.
  - **PDF e JSON:** Visualização estruturada com indentação limpa e botão para abertura direta no leitor nativo do sistema operacional.
- **Aba Artefatos no Painel Inferior:** Ao lado do *Terminal* e da *Telemetria*, a aba *Artefatos* exibe a contagem de entregáveis em tempo real e permite alternar entre visualização gráfica e código, copiar o conteúdo ou abrir a pasta `exports/` no Windows Explorer com um clique.
- **Operação via Assistente Local (Chat):** O assistente de chat nativo do Studio é integrado ao ciclo de vida de artefatos pelas ferramentas `list_artifacts` e `get_artifact`, sendo capaz de inspecionar a saída produzida e reportar conclusões ao usuário.

## Relacionados

- [Workspaces, memória, skills e MCP](05-workspaces-memoria-skills-mcp.md)
- [Arquitetura](01-arquitetura.md)
