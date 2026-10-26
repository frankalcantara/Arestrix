# Marca

> **Aplica-se a:** Arestrix Beta 2 (`2.0.0-beta.1`) · Última revisão: 2026-10-19

[English](../en/brand.md) · [Índice](../README.md)

Como o Arestrix se parece, resumido do **Manual de Marca v1.0 (outubro de 2026)**. O manual completo, com
todos os arquivos de logotipo, ícone, redes sociais e favicon, fica fora do repositório (`Manual_de_Marca/`
está no `.gitignore`). Os poucos arquivos de logotipo usados pelos READMEs estão copiados em `docs/assets/`.

## O A é um grafo

*Arestrix* nasce de *aresta*, a ligação entre dois vértices de um grafo. A primeira letra do nome é
desenhada como o próprio objeto que a ferramenta orquestra: tarefas ligadas por dependências.

| Parte | Significado |
| :--- | :--- |
| Três nós | Tarefas: o topo dispara o fluxo e as bases o recebem. |
| As duas pernas | As dependências (arestas) que conectam as tarefas. |
| A barra em Brasa | A **aresta de decisão**, onde o fluxo se ramifica. É a única cor do símbolo. |

<p align="center"><img src="../assets/arestrix-icon.svg" width="120" alt="Ícone do Arestrix"></p>

### Construção

- A altura do A é a altura das maiúsculas da Space Grotesk Bold; o símbolo se apoia na linha de base do texto.
- As pernas têm espessura de 14% da altura, com terminais arredondados. A barra tem 12% da altura e fica
  a 61% do topo, entre (25, 61) e (75, 61). Os nós têm raio de 11% da altura.
- **Abaixo de 32 px os nós são removidos** (a versão simplificada, com pernas mais grossas).
- A versão em caixa alta tem as pernas 20% mais espessas, para equilibrar as maiúsculas.

## Versões do logotipo

| Versão | Uso | Arquivo |
| :--- | :--- | :--- |
| **Positivo** | Sobre Papel ou branco. Padrão para produto, site, documentação e repositório. | [`arestrix-logo.svg`](../assets/arestrix-logo.svg) |
| **Negativo** | Sobre Grafite ou fundos escuros. | [`arestrix-logo-reverse.svg`](../assets/arestrix-logo-reverse.svg) |
| **Preto** | Impressão em uma cor e carimbos. | só no manual |
| **Branco** | Sobre Brasa ou fotografia escura. | só no manual |
| **Ícone** | O A sozinho, Papel sobre Grafite, cantos de 23% do lado. | [`arestrix-icon.svg`](../assets/arestrix-icon.svg) |

O README alterna entre positivo e negativo conforme o tema de quem lê.

### Área de proteção e tamanho mínimo

- Área de proteção **x** = metade da altura do A. Nenhum texto, borda ou imagem deve invadir essa margem em volta do logotipo.
- Largura mínima do logotipo: **96 px** em tela e **25 mm** em impressão. O símbolo isolado: **16 px**.

## Cores

| Nome | Hex | RGB | CMYK | Uso |
| :--- | :--- | :--- | :--- | :--- |
| **Grafite** | `#14151A` | 20 21 26 | 23 19 0 90 | Superfícies escuras, fundo do ícone. |
| **Papel** | `#F4F2EE` | 244 242 238 | 0 1 2 4 | Superfícies claras, traços e nós. |
| **Brasa** | `#E8590C` | 232 89 12 | 0 62 95 9 | Somente a aresta de decisão e destaques pontuais. |
| **Ardósia** | `#4B4F5C` | 75 79 92 | 19 14 0 64 | Texto secundário e neutros. |

A Brasa tem contraste de apenas **3,2:1** sobre Papel: use-a em elementos gráficos e em textos acima de 24 px.

## Tipografia

As duas famílias usam a licença SIL Open Font License. Em documentos Office sem elas instaladas, use Arial.

| Papel | Fonte | Peso |
| :--- | :--- | :--- |
| Títulos e logotipo | **Space Grotesk** | 700, espaçamento entre letras de -3,5% em títulos grandes |
| Texto e interface | **Space Grotesk** | 500 |
| Código e assinatura técnica | **IBM Plex Mono** | 500 |

## Usos incorretos

Não distorça as proporções · não gire o logotipo · não troque a cor da aresta · não separe o A do nome ·
não use fundos de baixo contraste · não converta em contorno.

## Neste repositório

Cores e fontes são as variáveis CSS `--brand-grafite`, `--brand-papel`, `--brand-brasa`,
`--brand-ardosia`, `--font-display` e `--font-tech` em `src/styles/soft-neomorphic-theme.css`. A aba do
navegador usa o favicon de `index.html`. Na interface, o texto nunca leva sombra nem brilho.

## Relacionados

- [Tour pela interface](interface.md)
