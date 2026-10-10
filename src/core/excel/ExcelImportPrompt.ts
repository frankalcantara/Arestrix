import { RegionRole, WorkbookModel } from './WorkbookModel.ts';

/**
 * Prompt do agente de importação de planilhas.
 *
 * A importação nunca será 100% determinística: o detector heurístico faz o melhor possível e o modelo
 * padrão do sistema revisa. Este texto é o prompt desse agente. O usuário pode editá-lo no modal de
 * importação; o padrão abaixo evolui junto com o detector (suba EXCEL_PROMPT_VERSION a cada mudança).
 */
export const EXCEL_PROMPT_VERSION = 2;
export const SUMMARY_PLACEHOLDER = '{{WORKBOOK_SUMMARY}}';

export const VALID_ROLES: readonly RegionRole[] = ['input_table', 'calculated_table', 'parameters', 'report', 'unknown'];

export const DEFAULT_EXCEL_IMPORT_PROMPT = `Você é o Agente de Importação de Planilhas do Arestrix Studio.

O Arestrix transforma planilhas Excel em projetos: blocos de dados viram tabelas, fórmulas viram funções
testadas, layouts viram telas e relatórios, e processos viram nós e arestas de um grafo. Um detector
determinístico já leu o arquivo e produziu o "Raio-X" abaixo. Ele erra em casos de borda. Sua tarefa é
REVISAR esse Raio-X, não refazê-lo.

[O QUE VOCÊ RECEBE]
Somente a ESTRUTURA da planilha: abas, regiões (intervalo, papel, título, cabeçalhos e tipos de coluna,
rótulos de linha), padrões de fórmula em notação R1C1 (a mesma fórmula copiada para N linhas aparece uma
vez, com contagem), dependências entre regiões e riscos já detectados. Você NÃO recebe os valores das
células e não deve pedi-los. Nunca calcule resultados e nunca invente números ou dados.

[PAPÉIS DE REGIÃO]
- input_table: cabeçalho + várias linhas, maioria de constantes (dados digitados).
- calculated_table: maioria das células é fórmula.
- parameters: pares rótulo/valor (taxas, datas-base, premissas). Candidatos naturais a entradas do projeto.
- report: poucas linhas, agregações (SOMA, MÉDIA, SOMASE…) que leem de outras regiões.
- unknown: o detector não soube classificar.
"orientation": "rows" indica itens nas linhas e períodos/cenários nas colunas (comum em modelos
financeiros); o papel continua sendo input_table ou calculated_table.

[COMO O DETECTOR DECIDE — verifique estes pontos, pois são onde ele costuma errar]
1. Uma linha ou coluna totalmente em branco separa regiões. Um bloco lógico pode ter sido partido em
   vários fragmentos (rótulos numa região, valores noutra, título solto). Aponte fragmentos que
   deveriam ser uma região só.
2. Rótulos e valores separados por UMA coluna em branco são fundidos como parâmetros. Duas tabelas
   lado a lado com uma coluna em branco entre elas NÃO são fundidas.
3. Referências usadas só pela posição, como OFFSET(M10,-1,0) ou ROW(A1), não contam como dependência
   nem como referência circular (idioma de saldo acumulado).
4. "unknown" com poucas células costuma ser título, legenda ou formulário (muitas células mescladas).
   Isso é aceitável; só aponte se for dado ou cálculo de verdade.
5. Número fixo no meio de uma fórmula (HARDCODED_IN_FORMULA) tem falsos positivos: ROUND(x,2) não é
   premissa escondida; =B2*1,18 é. Diga quais casos parecem premissas escondidas e que nome dar a elas.
6. CACHED_ERROR e CIRCULAR_REFERENCE são sempre graves. EXTERNAL_LINK e MACROS impedem a migração
   automática: explique o que precisará de decisão humana.

[SUA TAREFA]
a) Para cada região, confirme ou corrija o papel (só corrija com motivo claro na estrutura).
b) Para regiões de dados, sugira nome de tabela, chave primária provável (coluna com distinctRatio
   próximo de 1) e tipos relevantes. Para parâmetros, sugira nomes de entradas.
c) Aponte dependências que parecem faltar ou sobrar, e fluxos de cálculo que formam um pipeline
   (entrada → cálculo → relatório).
d) Priorize os riscos para quem vai migrar a planilha e proponha o que fazer em cada um.
e) Liste perguntas que só uma pessoa pode responder (regra de negócio ambígua, intenção de uma aba oculta).

[REGRAS]
- Use apenas ids de região que existem no Raio-X. Papéis válidos: input_table, calculated_table,
  parameters, report, unknown.
- Não invente colunas, abas ou valores. Se algo não está na estrutura, diga que não dá para saber.
- Seja conciso: cada item em uma ou duas frases.
- Responda ESTRITAMENTE com JSON válido, sem markdown ao redor, neste formato:

{
  "confidenceScore": 0-100,
  "summary": "o que esta planilha faz, em duas ou três frases",
  "regionCorrections": [
    { "regionId": "Aba!A1:B5", "role": "parameters", "reason": "motivo curto" }
  ],
  "tableSuggestions": [
    { "regionId": "Aba!A1:D20", "tableName": "nome_sugerido", "primaryKey": "coluna ou null", "notes": "opcional" }
  ],
  "findings": ["apontamento sobre estrutura, dependências ou riscos"],
  "questionsForUser": ["pergunta que só uma pessoa responde"]
}

[RAIO-X DA PLANILHA]
${SUMMARY_PLACEHOLDER}
`;

const MAX_REGIONS_IN_SUMMARY = 200;
const MAX_PATTERNS_PER_REGION = 5;
const MAX_LABELS_PER_REGION = 15;

/**
 * Resumo só de estrutura para o modelo: sem nome do arquivo, hash nem valores de célula.
 * Cabeçalhos, títulos e rótulos de linha são texto da planilha e vão junto; o usuário é avisado na tela.
 */
export function summarizeForPrompt(model: WorkbookModel): string {
  const regions = model.sheets.flatMap(s => s.regions.map(r => ({ sheet: s.name, r })));
  const shown = regions.slice(0, MAX_REGIONS_IN_SUMMARY);
  const summary = {
    stats: model.stats,
    hasMacros: model.hasMacros,
    sheets: model.sheets.map(s => ({ name: s.name, state: s.state, protected: s.protected, dimensions: s.dimensions, regionCount: s.regions.length })),
    regions: shown.map(({ r }) => ({
      id: r.id,
      role: r.role,
      ...(r.orientation ? { orientation: r.orientation } : {}),
      ...(r.title ? { title: r.title } : {}),
      rowCount: r.rowCount,
      ...(r.header ? { columns: r.header.columns.map(c => ({ name: c.name, type: c.inferredType, nullable: c.nullable, distinctRatio: c.distinctRatio, hasFormulas: c.hasFormulas })) } : {}),
      ...(r.rowLabels ? { rowLabels: r.rowLabels.slice(0, MAX_LABELS_PER_REGION) } : {}),
      formulaPatterns: r.formulaPatterns.slice(0, MAX_PATTERNS_PER_REGION).map(p => ({ r1c1: p.r1c1, count: p.count })),
      ...(r.formulaPatterns.length > MAX_PATTERNS_PER_REGION ? { morePatterns: r.formulaPatterns.length - MAX_PATTERNS_PER_REGION } : {})
    })),
    ...(regions.length > shown.length ? { omittedRegions: regions.length - shown.length } : {}),
    dependencies: model.dependencies,
    definedNames: model.definedNames,
    externalLinkCount: model.externalLinks.length,
    risks: model.risks
  };
  return JSON.stringify(summary, null, 1);
}

export function buildExcelImportPrompt(model: WorkbookModel, template: string = DEFAULT_EXCEL_IMPORT_PROMPT): string {
  const summary = summarizeForPrompt(model);
  return template.includes(SUMMARY_PLACEHOLDER)
    ? template.split(SUMMARY_PLACEHOLDER).join(summary)
    : `${template.trimEnd()}\n\n[RAIO-X DA PLANILHA]\n${summary}\n`;
}

// ---------- Persistência do prompt editado ----------

const STORAGE_KEY = 'arestrix_excel_import_prompt';

interface StoredPrompt {
  text: string;
  basedOnVersion: number;
}

/** O prompt editado fica no navegador (localStorage); sem edição vale o padrão do código. */
export class ExcelPromptStore {
  private static read(): StoredPrompt | null {
    try {
      if (typeof localStorage === 'undefined') return null;
      const raw = localStorage.getItem(STORAGE_KEY);
      const p = raw ? JSON.parse(raw) : null;
      return p && typeof p.text === 'string' && p.text.trim() ? { text: p.text, basedOnVersion: Number(p.basedOnVersion) || 0 } : null;
    } catch {
      return null;
    }
  }

  static get(): string {
    return this.read()?.text ?? DEFAULT_EXCEL_IMPORT_PROMPT;
  }

  static isCustomized(): boolean {
    return this.read() !== null;
  }

  /** O padrão evoluiu depois que o usuário editou: vale avisar para ele reavaliar ou restaurar. */
  static isDefaultNewerThanCustom(): boolean {
    const s = this.read();
    return s !== null && s.basedOnVersion < EXCEL_PROMPT_VERSION;
  }

  static set(text: string): void {
    if (!text.trim() || text === DEFAULT_EXCEL_IMPORT_PROMPT) return this.reset();
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ text, basedOnVersion: EXCEL_PROMPT_VERSION } satisfies StoredPrompt));
    } catch {
      /* sem armazenamento disponível: a edição vale só até fechar */
    }
  }

  static reset(): void {
    try {
      localStorage.removeItem(STORAGE_KEY);
    } catch {
      /* nada a fazer */
    }
  }
}
