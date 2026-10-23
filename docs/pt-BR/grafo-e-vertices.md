# Grafo e vértices

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-19

[English](../en/graph-and-nodes.md) · [Índice](../README.md)

Um pipeline é um grafo direcionado. Os **vértices** (nós) são tarefas; as **arestas** são a ordem em que elas executam.

## Tipos de vértice

Cada tipo corresponde a uma primitiva do Taskflow quando o programa é gerado.

| Tipo | Cor do cabeçalho | Significado no Taskflow | Para quê |
| :--- | :--- | :--- | :--- |
| **Estático** | Azul | `taskflow.emplace(...)` | Trabalho determinístico. |
| **Condição** | Laranja | Tarefa que retorna um `int` | Decisões, verificação de convergência, laços de feedback. Suas portas de saída são ramos numerados. |
| **Switch** | Ouro | Tarefa que retorna um `int` (o índice do caso) | Escolher um entre vários caminhos: uma porta de saída por caso, mais um `default`. |
| **MCP** | Ciano | Tarefa que chama o cliente MCP nativo | Chamar UMA ferramenta de um servidor MCP (um CRM, um banco, uma busca) sem IA no meio. |
| **Subfluxo** | Roxo | Tarefa que recebe `tf::Subflow&` | Decompor o trabalho em um grafo filho em tempo de execução. |
| **Módulo** | Verde | Grafo composto reutilizável | Empacotar um subpipeline para reuso. |
| **Crítico** | Azul-petróleo | `tf::CriticalSection` | Acesso mutuamente exclusivo a um recurso compartilhado. |
| **CUDA** | Azul | Tarefa de grafo CUDA (Taskflow 4.1) | Kernels de GPU. |
| **Agente** | Índigo | Tarefa que executa um agente de IA | Um agente autônomo em sandbox. Veja [Agentes e execução](agentes-e-execucao.md). |
| **Fork** | Azul-petróleo | Despachante concorrente | Iniciar vários ramos em paralelo. |
| **Join** | Roxo | Barreira de sincronização | Esperar os ramos paralelos terminarem. |

Dê duplo clique em um vértice **subfluxo** ou **módulo** para abrir o grafo filho. `Esc` volta um nível; a
barra de navegação acima do canvas mostra onde você está.

### Vértice MCP

Um vértice **MCP** chama **uma ferramenta** de um servidor MCP configurado no projeto (`.workspace/mcps/`), sem modelo de IA no
meio: você escolhe o **servidor** e a **ferramenta** em listas (o Studio pergunta ao servidor quais ferramentas ele oferece),
escreve os **argumentos** em JSON e diz de onde vem a **entrada** (o vértice anterior, um arquivo do projeto ou nada).

- Os argumentos aceitam `{{input}}` (o texto de entrada) e `{{input.campo}}` (um campo, quando a entrada for JSON). Uma string que é
  só uma referência preserva o tipo do valor: `{"id": "{{input.id}}"}` envia um número se o campo for número.
- O resultado fica em `.arestrix/out/<id>.txt`, que é justamente onde **os switches e os agentes leem "o vértice anterior"**; assim
  um vértice MCP alimenta uma decisão ou um agente diretamente. Você também pode gravá-lo em um arquivo do projeto.
- Se o servidor não existir ou não responder, um campo referenciado faltar, ou a ferramenta devolver erro, o pipeline **para com uma
  mensagem clara**.
- Os servidores marcados com ⚡ são **ferramentas nativas do Arestrix** (arquivos, HTTP, dados, memória, tempo, Git, SQLite): não exigem
  instalação e rodam dentro do programa. Veja [Ferramentas nativas](workspaces-memoria-skills-mcp.md#ferramentas-nativas-sem-instalação-sem-processo).
- Roda no **cliente MCP nativo em C++**, com uma **sessão persistente** por servidor (veja [Servidores MCP](workspaces-memoria-skills-mcp.md#servidores-mcp)).

### Switch

Um **switch** manda o fluxo por um entre vários caminhos. Cada **caso** é uma porta de saída; a última porta é sempre o
**default**, usado quando nada mais serve. Você decide *como* o caminho é escolhido no Inspetor (aba **Geral**), sem
precisar de código:

| Modo | O que faz | Precisa de |
| :--- | :--- | :--- |
| **Um modelo de IA decide** *(padrão)* | O modelo lê o texto e escolhe um caso **pelo nome**. Você pode acrescentar uma orientação ("escolha *alto* se houver risco para o paciente"). | Um modelo: o do próprio vértice (botão **Escolher o modelo deste vértice**) ou o padrão do pipeline. |
| **Procurar palavras no texto** | Vai para o primeiro caso cujas palavras aparecerem no texto. Acentos e maiúsculas não importam. | Nada: sem IA e sem internet. |
| **Código C++** *(avançado)* | Você escreve a regra na aba **Lambda** e devolve o número do caso (`0`, `1`, `2`...). | Conhecimento de C++. |

**Que texto é analisado:** a resposta final do vértice anterior (um agente) ou um arquivo do projeto
(por exemplo `relatorio/auditoria.txt`).

Como se comporta:
- Se o modelo responder algo que não seja o nome de um caso, o fluxo segue pelo **default** (e o console avisa).
- Se não houver o que analisar (o vértice anterior não escreveu texto, o arquivo não existe) ou o modelo não puder ser
  acessado, o pipeline **para com um erro claro**. Ele nunca escolhe um caminho em silêncio.
- No modo código, qualquer número fora do intervalo também vai para o **default**.
- Adicione, renomeie e remova casos no mesmo painel. Remover um caso remove também as conexões dele, e um único
  desfazer restaura os dois. O default é fixo.
- Um caso sem conexão gera um aviso de validação; o default sem conexão não (o fluxo simplesmente termina ali).
- Como a condição, o switch pode fechar um laço (a aresta de retorno vira aresta de feedback).
- Para converter um vértice de/para switch, remova antes as conexões de saída dele, porque as portas mudam.

## Arestas de chamada (delegação)

Uma **aresta de chamada** é tracejada em violeta e declara uma delegação, não uma dependência de
agendamento. Ela significa:

> O agente de origem **pode invocar** o agente de destino de forma síncrona, no máximo N vezes, e
> recebe a resposta.

No v1:

- a origem é um **agente**; o destino é um **agente** ou uma **interação**;
- o destino precisa ser **somente chamável**: não tem arestas de fluxo (entrada ou saída). Se ele
  também precisar rodar como etapa normal, duplique o vértice;
- o destino **não** é uma task top-level do Taskflow; ele roda dentro da task do chamador.

Para criar uma chamada, arraste da porta **`call`** do agente (violeta) até a porta `in` do destino.
Selecione o agente de origem e use a seção **Delegação** do Inspetor para definir `maxCalls` e a
descrição que o modelo vê ao escolher o alvo.

Em execução, o chamador recebe a ferramenta `call_agent` com um **enum de alvos declarados**. O
skill orienta; o grafo autoriza; o runtime aplica:

| Limite | Padrão | Variável de ambiente |
| :--- | :--- | :--- |
| `maxCalls` por aresta, por execução do chamador | 1 | — |
| Profundidade de aninhamento | 3 | `ARESTRIX_MAX_CALL_DEPTH` |
| Total de chamadas por processo | 50 | `ARESTRIX_MAX_TOTAL_CALLS` |

O resultado é uma mensagem de ferramenta JSON com `callId`, `target`, `status`, `output` e `depth`;
um destino de interação também traz `answers`. O texto final do alvo também é gravado em
`.arestrix/out/<target>.txt`.

Veja [`examples/06-chamada-agente`](../examples/06-chamada-agente) para um coordenador chamando um
especialista.

## Vértices de interação

Um vértice **interaction** pergunta algo a uma pessoa. Ele aceita um schema de formulário seguro
(texto, textarea, número, booleano, escolha, múltipla escolha, data), um canal (`studio`, `runner`,
`cli`, `any`), timeout, quem responde e flag de resposta sensível. Pode rodar no fluxo ou ser
chamado por um agente por uma aresta de chamada.

Execuções headless usam `--answers` (veja [`arestrix-cli`](arestrix-cli.md#interação-humana-headless));
as respostas são validadas e gravadas em `.arestrix/out/<nodeId>.txt`/`.json`. No Studio, o mesmo
formulário seguro abre como modal e devolve as respostas validadas ao fluxo ou ao agente chamador.
Sem canal, o vértice falha fechado.

## Portas e arestas

Cada vértice tem **portas de entrada** (azuis) e **portas de saída** (laranja). Uma porta carrega
**fluxo** (ordem de execução) ou **dados**. Arraste de uma porta de saída até uma de entrada para conectá-las.

### Os dois tipos de aresta que você verá

| Aresta | Aparência | Significado |
| :--- | :--- | :--- |
| **Aresta de fluxo** | Sólida, cinza-ardósia | Dependência normal: o destino executa depois que a origem termina. |
| **Aresta de feedback** | Tracejada, âmbar | Aresta de retorno que fecha um laço. O aplicativo a marca automaticamente. |
| **Aresta de chamada** | Tracejada, violeta | Delegação: a origem pode invocar o destino de forma síncrona e receber a resposta. Não agenda o destino. |

Uma aresta vira **aresta de feedback** quando sai de um vértice de **condição** e aponta de volta para esse
vértice ou para um que leva de volta a ele. No código gerado ela vira um laço do Taskflow, e não uma
dependência comum. Em execução, cada agente é limitado a 10 execuções (`ARESTRIX_MAX_AGENT_RUNS`); um laço
que nunca converge para com erro em vez de gastar dinheiro indefinidamente.

Exemplo: no preset *arquitetura completa de agentes*, o vértice de decisão do LLM envia o ramo 0 para o
armazenamento e o consenso, e o ramo 1 de volta para a fila. Esse retorno é a aresta tracejada.

## Validação

Antes de executar, o grafo é verificado:

| Achado | Severidade |
| :--- | :--- |
| Vértice sem conexões (quando o grafo tem mais de um vértice) | Aviso |
| Vértice de condição com um ramo desconectado | Aviso |
| Ciclo que **não** contém vértice de condição | **Erro**: travaria ou repetiria para sempre |

Ciclos só são permitidos quando um vértice de condição pode interrompê-los.

## Edição

| Ação | Como |
| :--- | :--- |
| Adicionar vértice | **Página Inicial → + Tarefa**, o seletor de tipo, ou a biblioteca de vértices (**Catálogo**). |
| Mover | Arrastar. Encaixa numa grade de 10 px; segure `Shift` para mover livremente. |
| Conectar | Arrastar de uma porta de saída a uma de entrada. |
| Pan / zoom | Arrastar o canvas vazio / roda do mouse. |
| Desfazer / refazer | `Ctrl+Z` / `Ctrl+Y` (ou `Ctrl+Shift+Z`). |
| Apagar | `Del` ou `Backspace` no vértice ou aresta selecionados. |

### Menu de contexto (botão direito em um vértice)

| Item | Atalho | Observações |
| :--- | :--- | :--- |
| Sandbox, Modelo, Ferramentas, Memória, Skills | | As mesmas cinco ações rápidas da barra acima do vértice. |
| Duplicar | `Ctrl+D` | Novo id, novos ids de porta, rótulo ganha o sufixo "(cópia)", deslocado 40 px. Arestas não são copiadas. |
| Copiar / Recortar / Colar | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Área de transferência interna; cada colagem desloca mais um passo. |
| Salvar como template | | Inclui o vértice na biblioteca (abaixo). |
| Entrar no subfluxo | | Só para vértices subfluxo e módulo. |
| Remover todas as conexões | | Um único desfazer as restaura. |
| Apagar | `Del` | |

## Biblioteca de vértices

**Página Inicial → Catálogo** abre a *Biblioteca de Vértices*: vértices prontos, com portas, corpo C++,
headers necessários e configurações de agente já preenchidos.

- Filtre por categoria; **Inserir** adiciona o vértice no centro da vista e o seleciona.
- **Ver C++20** abre o corpo gerado numa janela somente leitura, com botão Copiar, para você auditá-lo
  antes de inserir.
- **Instanciar Arquitetura Completa de Agentes** substitui o grafo atual por um pipeline multiagente completo
  (fila → subfluxo de visão → decisão do LLM → SQLite → consenso, com laço de feedback).
- **Importar / Exportar** trocam templates em JSON, para uma equipe compartilhá-los.
- **Salvar como template** (menu de contexto) guarda um vértice em *Plugins Customizados*. A biblioteca recusa
  quando já existe template equivalente: mesmo tipo, mesmas portas, mesmos nomes de parâmetros e mesmo
  corpo C++. Vértices que contêm um grafo filho ainda não podem ser salvos.

Templates embutidos: agente de decisão LLM, worker de fila thread-safe, persistência SQLite, pipeline de
visão paralelo (subfluxo), kernel CUDA e módulo de consenso.

## Relacionados

- [Arquitetura](arquitetura.md)
- [Tour pela interface](interface.md)
