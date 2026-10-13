# Grafo e vértices

[English](../en/03-graph-and-nodes.md) · [Índice](../../README.pt-BR.md#documentação)

Um pipeline é um grafo direcionado. Os **vértices** (nós) são tarefas; as **arestas** são a ordem em que elas executam.

## Tipos de vértice

Cada tipo corresponde a uma primitiva do Taskflow quando o programa é gerado.

| Tipo | Cor do cabeçalho | Significado no Taskflow | Para quê |
| :--- | :--- | :--- | :--- |
| **Estático** | Azul | `taskflow.emplace(...)` | Trabalho determinístico. |
| **Condição** | Laranja | Tarefa que retorna um `int` | Decisões, verificação de convergência, laços de feedback. Suas portas de saída são ramos numerados. |
| **Subfluxo** | Roxo | Tarefa que recebe `tf::Subflow&` | Decompor o trabalho em um grafo filho em tempo de execução. |
| **Módulo** | Verde | Grafo composto reutilizável | Empacotar um subpipeline para reuso. |
| **Crítico** | Azul-petróleo | `tf::CriticalSection` | Acesso mutuamente exclusivo a um recurso compartilhado. |
| **CUDA** | Azul | Tarefa de grafo CUDA (Taskflow 4.1) | Kernels de GPU. |
| **Agente** | Índigo | Tarefa que executa um agente de IA | Um agente autônomo em sandbox. Veja [Agentes e execução](04-agentes-e-execucao.md). |
| **Fork** | Azul-petróleo | Despachante concorrente | Iniciar vários ramos em paralelo. |
| **Join** | Roxo | Barreira de sincronização | Esperar os ramos paralelos terminarem. |

Dê duplo clique em um vértice **subfluxo** ou **módulo** para abrir o grafo filho. `Esc` volta um nível; a
barra de navegação acima do canvas mostra onde você está.

## Portas e arestas

Cada vértice tem **portas de entrada** (azuis) e **portas de saída** (laranja). Uma porta carrega
**fluxo** (ordem de execução) ou **dados**. Arraste de uma porta de saída até uma de entrada para conectá-las.

### Os dois tipos de aresta que você verá

| Aresta | Aparência | Significado |
| :--- | :--- | :--- |
| **Aresta de fluxo** | Sólida, cinza-ardósia | Dependência normal: o destino executa depois que a origem termina. |
| **Aresta de feedback** | Tracejada, âmbar | Aresta de retorno que fecha um laço. O aplicativo a marca automaticamente. |

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

- [Arquitetura](01-arquitetura.md)
- [Tour pela interface](06-interface.md)
