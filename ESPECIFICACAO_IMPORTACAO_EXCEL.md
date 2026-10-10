# Especificação — Planilhas Excel no Arestrix

> Rascunho v0.1 — 2026-10-10. Substitui, como documento de referência, as perguntas abertas em
> `DUVIDAS_IMPORTACAO_EXCEL.md` (que continua como histórico, não versionado).
> Nada aqui está implementado. Windows/MSVC é a plataforma principal; tudo deve funcionar também no Linux/WSL.

## 1. Objetivo

Transformar planilhas Excel em projetos Arestrix: dados em tabelas, fórmulas em funções testadas,
telas e relatórios gerados, processos no grafo. A meta de longo prazo é **substituir** o Excel; o caminho é
**conviver primeiro** (importar, rodar em paralelo, comparar) e só desligar o arquivo quando os testes baterem.

Uma planilha é um programa sem disciplina. Ela mistura quatro coisas, e cada uma tem destino próprio:

| No Excel | No Arestrix |
| :--- | :--- |
| Blocos de dados digitados | Tabelas (SQLite do projeto) com esquema e tipos |
| Fórmulas | Funções puras com entradas e saídas explícitas |
| Layout, validações, gráficos, dinâmicas | Telas e relatórios web |
| Macros, links entre arquivos, rotinas manuais | Processos (nós e arestas do grafo) |

## 2. Decisões tomadas

| # | Decisão | Motivo |
| :--- | :--- | :--- |
| D1 | Primeira entrega é a **Fase 1: Raio-X** (somente leitura). | Gera valor sem traduzir nada e valida o modelo intermediário. |
| D2 | **Tudo local por padrão.** Nenhum conteúdo de célula sai da máquina. | Planilhas de empresa têm dados sensíveis (LGPD). |
| D3 | A Fase 1 **não usa LLM**. Classificação por heurística determinística. | Atende D2 e deixa o resultado reproduzível e testável. |
| D4 | Parsing em **TypeScript com ExcelJS** (MIT), arquivo recebido por `<input type="file">`. | Funciona no modo nativo e no Vite, igual no Windows e no Linux; dispensa seletor nativo. |
| D5 | O contrato central é um **modelo intermediário em JSON** (`WorkbookModel`). | Permite trocar o parser (por C++, por exemplo) sem refazer o resto. |
| D6 | A unidade de análise é a **região** detectada, não a aba. | Uma aba costuma ter vários blocos de natureza diferente. |
| D7 | Cálculo é sempre **determinístico**; LLM só entende e documenta (fases posteriores), nunca calcula. | Confiabilidade e testabilidade. |
| D8 | Oráculo de testes: os **valores em cache** que o `.xlsx` já guarda para cada fórmula. | Prova que a tradução está correta, sem fé. |
| D9 | Formatos: `.xlsx` na Fase 1. `.xlsm` é aceito, mas o VBA é só detectado e sinalizado. `.xls`/`.ods`/`.csv` ficam fora por ora. | Escopo mínimo útil. |

## 3. Pipeline

```
.xlsx ──► [1 Extração] ──► [2 Análise] ──► [3 Modelagem] ──► [4 Geração] ──► [5 Verificação]
          determinística   heurística      projeto Arestrix   banco/telas/nós   cache do Excel
          (Fase 1)         (Fase 1)        (Fases 2-3)        (Fases 2-4)       (Fase 3)
```

## 4. Fase 1 — Raio-X

### 4.1 O que o usuário vê

No modal de importação Excel (hoje um mock, `src/ui/modals/ImportExcelModal.ts`):

1. Seleciona um `.xlsx` (seletor de arquivo do navegador/WebUI).
2. Recebe um relatório com três visões:
   - **Abas e regiões:** cada aba com suas regiões (intervalo, papel, linhas × colunas, cabeçalhos).
   - **Dependências:** grafo entre regiões (qual região alimenta qual), com contagem de fórmulas por aresta.
   - **Riscos:** lista priorizada (seção 4.4).
3. Pode **exportar o `WorkbookModel` em JSON**.
4. Opcional (confirmar, ver seção 8): botão **"Gerar grafo de regiões"** que cria um projeto Arestrix
   com um nó por região e as arestas de dependência.

Nenhuma escrita no Excel original. Nenhum dado sai da máquina.

### 4.2 `WorkbookModel` (esboço)

```ts
interface WorkbookModel {
  schemaVersion: 1;
  source: { fileName: string; sizeBytes: number; sha256: string; importedAt: string };
  hasMacros: boolean;
  sheets: SheetModel[];
  definedNames: { name: string; ref: string }[];
  externalLinks: { target: string; usedBy: RegionRef[] }[];
  dependencies: { from: RegionId; to: RegionId; formulaCount: number }[]; // "to" lê de "from"
  risks: Risk[];
  stats: { sheets: number; regions: number; cells: number; formulaCells: number };
}

interface SheetModel {
  name: string;
  state: 'visible' | 'hidden' | 'veryHidden';
  protected: boolean;
  dimensions: string;            // ex.: "A1:N500"
  mergedRanges: string[];
  regions: RegionModel[];
}

interface RegionModel {
  id: RegionId;                  // estável: "<aba>!<intervalo inicial>"
  range: string;                 // ex.: "A3:F120"
  role: 'input_table' | 'calculated_table' | 'parameters' | 'report' | 'unknown';
  title?: string;                // texto logo acima, se houver
  header?: { row: number; columns: ColumnModel[] };
  rowCount: number;
  formulaPatterns: { r1c1: string; count: number; example: string }[]; // fórmulas deduplicadas
}

interface ColumnModel {
  name: string;
  inferredType: 'number' | 'integer' | 'date' | 'currency' | 'text' | 'boolean' | 'mixed';
  nullable: boolean;
  distinctRatio: number;         // candidata a chave quando ~1
  hasFormulas: boolean;
}

interface Risk {
  severity: 'high' | 'medium' | 'low';
  code: RiskCode;
  where: string;                 // aba!intervalo
  message: string;
}
```

Fórmulas de preenchimento (a mesma fórmula copiada para centenas de linhas) são normalizadas para **R1C1**
e agrupadas em `formulaPatterns`; isso mantém o modelo pequeno e é a base da tradução na Fase 3.

### 4.3 Detecção de regiões e papéis (heurística)

1. **Regiões:** componentes conexos de células não vazias; uma linha ou coluna totalmente em branco separa regiões.
2. **Cabeçalho:** primeira linha da região majoritariamente de texto, seguida de linhas com tipos homogêneos por coluna.
3. **Papel:**
   - `input_table`: cabeçalho + 2 ou mais linhas, maioria de constantes.
   - `calculated_table`: cabeçalho + maioria de células com fórmula.
   - `parameters`: bloco pequeno de pares rótulo/valor (2 colunas, poucas linhas).
   - `report`: poucas linhas, muitas fórmulas de agregação (SOMA, MÉDIA, SOMASE…) apontando para outras regiões.
   - `unknown`: o resto.
4. **Tipos de coluna:** inferidos dos valores e do formato numérico da célula (data, moeda, inteiro).
5. **Dependências:** referências nas fórmulas (inclusive entre abas e por nome definido) mapeadas para a região de destino.

### 4.4 Riscos reportados

| Código | O que sinaliza | Severidade |
| :--- | :--- | :--- |
| `EXTERNAL_LINK` | Fórmula aponta para outro arquivo | alta |
| `MACROS` | `.xlsm` / VBA presente (não traduzido) | alta |
| `CACHED_ERROR` | Célula com `#REF!`, `#N/A`, `#DIV/0!`… | alta |
| `CIRCULAR_REFERENCE` | Ciclo no grafo de dependências | alta |
| `HARDCODED_IN_FORMULA` | Literal numérico dentro de fórmula (ex.: `=A2*1.18`) | média |
| `VOLATILE_FUNCTION` | `NOW`, `TODAY`, `RAND`, `OFFSET`, `INDIRECT` | média |
| `ARRAY_FORMULA` | Fórmula matricial / dinâmica | média |
| `HIDDEN_SHEET` / `PROTECTED_SHEET` | Aba oculta ou protegida | média |
| `MERGED_IN_DATA` | Células mescladas dentro de região de dados | baixa |
| `UNKNOWN_FUNCTION` | Função fora do conjunto que saberemos traduzir | baixa (por ora) |

Arquivos protegidos por senha: a Fase 1 informa que não consegue abrir e não tenta contornar.

### 4.5 Módulos propostos

```
src/core/excel/
  WorkbookModel.ts        tipos acima
  ExcelWorkbookReader.ts  ExcelJS → estrutura bruta (células, fórmulas, formatos, abas, nomes)
  FormulaAnalyzer.ts      extrai referências e funções; normaliza para R1C1
  RegionDetector.ts       regiões, cabeçalhos, papéis, tipos de coluna
  DependencyGraph.ts      dependências entre regiões; detecção de ciclos
  RiskAnalyzer.ts         riscos da seção 4.4
  WorkbookAnalyzer.ts     orquestra tudo e devolve o WorkbookModel
src/ui/modals/ImportExcelModal.ts   reescrito (seletor + 3 visões + exportar JSON)
tests/excel-xray.spec.ts            testes da Fase 1
```

### 4.6 Critérios de aceite da Fase 1

- Abre um `.xlsx` e produz um `WorkbookModel` válido, sem rede e sem LLM.
- Dado um arquivo de teste conhecido, detecta as regiões e os papéis esperados.
- Reporta cada risco da seção 4.4 em fixtures que o contenham.
- Mesmo arquivo → mesmo JSON (determinístico, salvo `importedAt`).
- Funciona no modo nativo (Windows/MSVC e Linux) e no `npm run dev`.
- O teste do mock em `tests/office-ribbon-interface.spec.ts` (≈ linha 78) é reescrito junto.

### 4.7 Fixtures de teste

- **Sintéticas:** geradas por código nos testes (ExcelJS cria o `.xlsx`), cobrindo cada risco e cada papel. Versionadas.
- **Reais:** 1 ou 2 planilhas que o Frank vai fornecer, **anonimizadas**, em `tests/fixtures/real/`.
  Esse diretório entra no `.gitignore` até a anonimização ser confirmada.

## 5. Fases seguintes (visão, não compromisso)

| Fase | Entrega | Observação |
| :--- | :--- | :--- |
| 2 | Regiões de entrada → tabelas SQLite do projeto + telas de edição geradas | Esquema vem do `WorkbookModel` |
| 3 | Fórmulas → funções, testadas contra os valores em cache do Excel | Começa por SOMA, SE, PROCV/XLOOKUP, SOMASE, datas. Alvo da tradução (SQL, C++ ou TS) a decidir |
| 4 | Gráficos e tabelas dinâmicas → relatórios | |
| 5 | Projeto como nó de um grafo maior | Ver contrato abaixo |

LLM entra apenas a partir da Fase 2, de forma **opcional**, e por padrão vendo só estrutura (cabeçalhos e tipos), nunca valores.

## 6. Contrato do projeto (reservado para a Fase 5)

Para que um projeto gerado de planilha seja depois um nó de um grafo maior, ele deve declarar:

```json
{ "interface": { "parameters": [], "inputs": [], "outputs": [] } }
```

Na Fase 1 nada disso é gravado; apenas as regiões com papel `parameters` já são candidatas naturais a entradas do contrato.

## 7. Plataforma

- Sem dependência de seletor nativo: o arquivo vem do `<input type="file">` (conteúdo, não caminho).
- Caminhos que forem persistidos usam `/` e são relativos ao workspace (Windows e Linux).
- Nenhum código novo depende de MSVC ou GCC; a Fase 1 é só TypeScript.

## 8. Decididas depois da v0.1 (2026-10-10)

| # | Decisão |
| :--- | :--- |
| D10 | O botão **"Gerar grafo de regiões" entra na Fase 1**. |
| D11 | **Sem limite fixo de tamanho de arquivo**: o objetivo é suportar o maior possível. Consequências: o modelo guarda padrões de fórmula e estatísticas, nunca todas as células; a análise roda fora da thread da UI (Web Worker) e reporta progresso; aviso (não bloqueio) acima de um tamanho configurável. O leitor de streaming do ExcelJS fica como evolução se o leitor padrão não bastar. |
| D12 | O alvo da tradução de fórmulas (Fase 3) é **C++** (corpos de lambda dos nós Taskflow). |

## 8.1 Ajustes após testar com planilhas reais (2026-10-10)

Testado com 3 `.xlsx` reais (Valuation, Custos e Formação de Preços, money-manager): abrem em < 1 s, sem erro.

| # | Ajuste |
| :--- | :--- |
| A1 | `RegionModel` ganhou `orientation: 'rows'` e `rowLabels`: modelos financeiros têm itens nas linhas e períodos nas colunas; o papel continua `input_table`/`calculated_table`. |
| A2 | Referências usadas só pela posição (`OFFSET(M10,-1,0)`, `ROW(A1)`) não entram na detecção de referência circular (eram 42 falsos positivos no money-manager). |
| A3 | Rótulos e valores separados por uma coluna em branco viram um bloco só (`parameters`). |
| A4 | **`.xls` antigo fica fora por ora** (decisão do Frank): o leitor devolve `LEGACY_XLS` com mensagem pedindo para salvar como `.xlsx`. O SheetJS 0.18.5 do npm tem CVEs conhecidos e o CDN do 0.20.3 estava bloqueado no sandbox. Há 2 `.xls` em `teste-excel/` para quando isso for retomado. |

Limitação conhecida: muitos blocos pequenos continuam `unknown` (rótulos soltos, linhas de título, formulários com células mescladas). Não afeta dependências, riscos nem padrões de fórmula.

## 8.2 Agente de importação e prompt (decisão do Frank, 2026-10-10)

A importação nunca será 100% determinística. O detector faz o melhor possível e o **modelo padrão do sistema** revisa, por meio de um agente que roda um prompt.

| # | Decisão |
| :--- | :--- |
| D13 | O prompt do agente de importação existe desde já (`src/core/excel/ExcelImportPrompt.ts`, `EXCEL_PROMPT_VERSION`) e **evolui junto com o detector**: toda heurística nova ou falso positivo descoberto entra no prompt. |
| D14 | O prompt é **editável no modal de importação** (salvo no `localStorage`; "Restaurar padrão" volta ao do código; aviso quando o padrão evolui depois da edição). |
| D15 | Revisão **sob demanda** ("Revisar com IA"), nunca automática. O modelo recebe só a **estrutura** (abas, títulos, cabeçalhos, rótulos de linha, fórmulas em R1C1, riscos); **não** recebe valores de célula, nome do arquivo nem hash. Isso ajusta D3/D7: o LLM entra já na Fase 1, de forma opcional. |
| D16 | A resposta do modelo é dado não confiável: só ficam ids de região existentes e papéis válidos, e as sugestões não alteram o Raio-X sozinhas. |

Pendente: aplicar as sugestões do modelo ao Raio-X (com confirmação) e rodar a revisão como nó de um grafo, não só pelo modal.

## 9. Questões em aberto (adiadas de propósito)

1. Licença/escolha do parser de fórmulas da Fase 3: escrever o próprio ou usar biblioteca (HyperFormula tem restrições de licença).
2. Identidade estável das regiões na reimportação (a região "A3:F120" muda quando alguém insere uma linha).
