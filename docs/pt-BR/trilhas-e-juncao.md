# Trilhas e junção

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.2`)

Um grafo pode ter mais de um caminho de execução. Quando dois caminhos não compartilham nenhum
vértice, o Studio os trata como **trilhas independentes**: elas começam juntas, rodam em paralelo e o
pipeline só termina quando a última concluir. Esta página cobre como isso aparece no Studio, o aviso
de conflito de escrita e as duas formas de propor uma junção.

## O que é uma trilha

Uma trilha começa em um vértice **sem fluxo de entrada** e leva consigo tudo o que alcança por arestas
de fluxo. A projeção usada pelo Studio é a mesma do gerador C++:

- arestas de **chamada** (`call`) são delegação, não precedência — não criam trilha nem ordenam nada;
- arestas de **feedback** fecham laço e não contam como entrada;
- **alvo puro de chamada** não vira tarefa de fluxo e fica fora das trilhas.

No código gerado, todas as entradas são ligadas ao `Pipeline_Bootstrap`; não existe “vértice inicial”
que você precise desenhar.

## O que o Studio mostra

| Sinal | Onde | Quando aparece |
| :--- | :--- | :--- |
| Selos **INÍCIO** / **FIM** | Fora do cartão, na altura do header | Só quando o grafo tem mais de uma entrada **ou** mais de uma saída |
| Contador `6 vértices, 4 conexões · 2 trilhas` | Barra de status | Sempre |
| `[RUN] N trilhas independentes: …` | Console, antes de compilar | Só com mais de uma trilha |
| `concurrency=` no resumo `[TRACE]` | Console, ao final | Sempre que o trace traz horários |

Com uma única entrada e uma única saída o desenho já é inequívoco e nenhum selo aparece — inclusive
em um fork com join.

O botão **Trilhas** da barra de status colore cada trilha com uma faixa no rodapé dos cartões e tinge
o selo **INÍCIO** correspondente, ligando a cor à entrada. É só leitura: não altera a topologia nem a
execução.

## Conflito de escrita

Dois vértices **sem caminho de fluxo entre si** podem executar ao mesmo tempo. Se ambos declaram
gravar no mesmo arquivo — `destinationPath` de um vértice `output` ou `output.file` de um vértice
`mcp` — a última gravação vence sem nenhum aviso em runtime. O Studio transforma isso em um aviso
`resource_conflict`, mostrado em **Validar DAG** e no Console antes de cada execução.

- **Nunca bloqueia**: `isValid` continua contando apenas erros.
- **Só o que está declarado**: corpo C++ livre é opaco de propósito.
- **Ordenado não avisa**: se existe caminho de fluxo entre os dois, um executa depois do outro.
- O mesmo teste existe no validador do Assistente (`validate_graph`), com a mesma normalização de
  caminho.

## Unir trilhas

### Pelo Assistente

O menu de contexto de um vértice com selo **FIM** oferece **Unir trilhas com o Assistente…**. O
Studio enumera as trilhas e envia ao modelo: rótulos, tipos, habilidade ativa, servidor MCP,
artefatos declarados e o **primeiro comentário escrito por você** no corpo C++ (o texto do template,
que repete o rótulo, é descartado). O pedido instrui o modelo a:

1. decidir semanticamente se as trilhas devem convergir e onde;
2. criar **um** vértice `join` no melhor ponto, ligando a saída do último vértice de cada trilha;
3. não remover nem reconectar conexões existentes;
4. não alterar o grafo se as trilhas forem independentes de propósito.

Todas as operações do turno viram **um único passo de Ctrl+Z**. Dentro de um subfluxo, o pedido leva o
`parent_path` e o Assistente edita o subgrafo certo; o Studio sobe para a raiz antes de aplicar, porque
as operações chegam com caminho absoluto.

Sem modelo configurado (ou sem suporte a ferramentas), a ação não altera nada e explica o motivo.

### Pela análise local

**Sugerir junção (análise local)** é a alternativa sem IA. Ela pontua sinais declarados:

| Sinal | Peso |
| :--- | :--- |
| Mesma pasta de entrega | +2 |
| Mesmo tipo de artefato (declarado ou pela extensão) | +2 |
| Mesma habilidade ativa | +2 |
| Mesmo servidor MCP | +2 |
| Termo em comum nos nomes (sem acentos, sem palavras vazias) | +1 |
| Pastas **e** tipos todos distintos | −2 |

Com **2 pontos ou mais**, a junção é criada depois do último vértice de cada trilha — a única posição
que não reescreve conexões — e a notificação diz exatamente quais sinais pesaram. Abaixo disso, nada é
alterado e a notificação explica por quê.

## Limites conhecidos

- A junção criada automaticamente é sempre **depois** das trilhas: consolidar *antes* das entregas
  exigiria reconectar arestas e é uma decisão manual (o Assistente pode fazer isso, mas só quando você
  pede).
- A análise local é heurística: dois artefatos do mesmo tipo em pastas diferentes podem ser pipelines
  independentes. Ela é uma sugestão, não um veredito.
- Conflitos dentro de `childGraph` de subfluxo não entram na validação do grafo raiz.

## Exemplo executável

[`examples/13-trilhas-independentes`](../../examples/13-trilhas-independentes) tem duas trilhas de três
vértices cada, sem modelo, e um roteiro para observar selos, contador, cores e as duas formas de junção.

## Relacionados

- [Grafo e vértices](grafo-e-vertices.md)
- [Interface](interface.md)
- [Exemplos](../../examples/README.pt-BR.md)
