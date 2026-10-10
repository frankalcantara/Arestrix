import { Bounds, boundsIntersect, cellAddress, parseRange } from './addressing.ts';
import { FormulaAnalysis } from './FormulaAnalyzer.ts';
import { RawWorkbook } from './RawWorkbook.ts';
import { DetectedRegion } from './RegionDetector.ts';
import { Risk, RiskCode, RiskSeverity } from './WorkbookModel.ts';

export interface FormulaCellInfo {
  sheet: string;
  row: number;
  col: number;
  regionId: string | null;
  analysis: FormulaAnalysis;
  isArray: boolean;
}

export interface ErrorCellInfo {
  sheet: string;
  row: number;
  col: number;
  regionId: string | null;
  error: string;
}

export interface CycleInfo {
  sheet: string;
  size: number;
  example: string; // endereço de uma das células
}

export interface RiskInput {
  raw: RawWorkbook;
  regionsBySheet: Map<string, DetectedRegion[]>;
  formulaCells: FormulaCellInfo[];
  errorCells: ErrorCellInfo[];
  cycles: CycleInfo[];
}

const VOLATILE = new Set(['NOW', 'TODAY', 'RAND', 'RANDBETWEEN', 'OFFSET', 'INDIRECT', 'INFO', 'CELL']);

/** Funções que a tradução (Fase 3) saberá cobrir primeiro; o resto é sinalizado. */
const KNOWN_FUNCTIONS = new Set([
  'SUM', 'AVERAGE', 'MIN', 'MAX', 'COUNT', 'COUNTA', 'IF', 'IFERROR', 'AND', 'OR', 'NOT', 'ROUND',
  'ROUNDUP', 'ROUNDDOWN', 'ABS', 'SUMIF', 'SUMIFS', 'COUNTIF', 'COUNTIFS', 'AVERAGEIF', 'VLOOKUP',
  'XLOOKUP', 'INDEX', 'MATCH', 'TODAY', 'NOW', 'DATE', 'YEAR', 'MONTH', 'DAY', 'LEFT', 'RIGHT', 'MID',
  'LEN', 'CONCATENATE', 'CONCAT', 'TEXT', 'PRODUCT', 'SUMPRODUCT', 'RAND', 'RANDBETWEEN', 'OFFSET',
  'INDIRECT'
]);

const SEVERITY: Record<RiskCode, RiskSeverity> = {
  EXTERNAL_LINK: 'high',
  MACROS: 'high',
  CACHED_ERROR: 'high',
  CIRCULAR_REFERENCE: 'high',
  HARDCODED_IN_FORMULA: 'medium',
  VOLATILE_FUNCTION: 'medium',
  ARRAY_FORMULA: 'medium',
  HIDDEN_SHEET: 'medium',
  PROTECTED_SHEET: 'medium',
  MERGED_IN_DATA: 'low',
  UNKNOWN_FUNCTION: 'low'
};

const RANK: Record<RiskSeverity, number> = { high: 0, medium: 1, low: 2 };

interface Group {
  code: RiskCode;
  where: string;
  count: number;
  examples: string[];
  extra: Set<string>;
}

class Groups {
  private map = new Map<string, Group>();

  add(code: RiskCode, where: string, example: string, extra?: string): void {
    const key = `${code}\u0000${where}`;
    let g = this.map.get(key);
    if (!g) this.map.set(key, (g = { code, where, count: 0, examples: [], extra: new Set() }));
    g.count++;
    if (g.examples.length < 3) g.examples.push(example);
    if (extra) g.extra.add(extra);
  }

  values(): Group[] {
    return [...this.map.values()];
  }
}

function describe(g: Group): string {
  const ex = g.examples.join(', ');
  switch (g.code) {
    case 'EXTERNAL_LINK': return `${g.count} fórmula(s) leem de outro arquivo (ex.: ${ex})`;
    case 'CACHED_ERROR': return `${g.count} célula(s) com erro (${[...g.extra].sort().join(', ')}) (ex.: ${ex})`;
    case 'HARDCODED_IN_FORMULA': return `${g.count} fórmula(s) com número fixo no meio do cálculo (ex.: ${ex})`;
    case 'VOLATILE_FUNCTION': return `${g.count} fórmula(s) usam ${[...g.extra].sort().join(', ')}, que mudam a cada cálculo (ex.: ${ex})`;
    case 'ARRAY_FORMULA': return `${g.count} fórmula(s) matricial(is) (ex.: ${ex})`;
    case 'UNKNOWN_FUNCTION': return `Funções ainda sem tradução prevista: ${[...g.extra].sort().join(', ')} (ex.: ${ex})`;
    case 'MERGED_IN_DATA': return `${g.count} intervalo(s) mesclado(s) dentro dos dados (ex.: ${ex})`;
    default: return ex;
  }
}

export function analyzeRisks(input: RiskInput): Risk[] {
  const groups = new Groups();
  const risks: Risk[] = [];

  if (input.raw.hasMacros) {
    risks.push({
      severity: SEVERITY.MACROS,
      code: 'MACROS',
      where: 'workbook',
      message: 'O arquivo contém macros VBA. Elas não são traduzidas; a lógica delas precisa ser levantada à parte.'
    });
  }

  for (const s of input.raw.sheets) {
    if (s.state !== 'visible') {
      risks.push({ severity: SEVERITY.HIDDEN_SHEET, code: 'HIDDEN_SHEET', where: s.name, message: `Aba ${s.state === 'veryHidden' ? 'muito oculta' : 'oculta'}: pode conter lógica ou dados que ninguém vê.` });
    }
    if (s.protected) {
      risks.push({ severity: SEVERITY.PROTECTED_SHEET, code: 'PROTECTED_SHEET', where: s.name, message: 'Aba protegida: as regras de edição não são importadas.' });
    }
  }

  for (const f of input.formulaCells) {
    const where = f.regionId ?? `${f.sheet}!${cellAddress(f.row, f.col)}`;
    const addr = `${f.sheet}!${cellAddress(f.row, f.col)}`;
    const a = f.analysis;
    if (a.usesExternal) groups.add('EXTERNAL_LINK', where, addr);
    if (a.literals.length > 0) groups.add('HARDCODED_IN_FORMULA', where, `${addr} (${a.literals.join(', ')})`);
    const volatile = a.functions.filter(fn => VOLATILE.has(fn));
    for (const fn of volatile) groups.add('VOLATILE_FUNCTION', where, addr, fn);
    if (f.isArray) groups.add('ARRAY_FORMULA', where, addr);
    for (const fn of a.functions) {
      if (!KNOWN_FUNCTIONS.has(fn)) groups.add('UNKNOWN_FUNCTION', where, addr, fn);
    }
  }

  for (const e of input.errorCells) {
    const where = e.regionId ?? `${e.sheet}!${cellAddress(e.row, e.col)}`;
    groups.add('CACHED_ERROR', where, `${e.sheet}!${cellAddress(e.row, e.col)}`, e.error);
  }

  for (const s of input.raw.sheets) {
    const regions = input.regionsBySheet.get(s.name) ?? [];
    for (const m of s.mergedRanges) {
      const mb: Bounds | null = parseRange(m);
      if (!mb) continue;
      for (const r of regions) {
        const table = r.model.role === 'input_table' || r.model.role === 'calculated_table';
        if (!table) continue;
        const dataTop = r.model.header ? r.bounds.top + 1 : r.bounds.top;
        if (boundsIntersect(mb, { ...r.bounds, top: dataTop })) groups.add('MERGED_IN_DATA', r.model.id, `${s.name}!${m}`);
      }
    }
  }

  for (const g of groups.values()) {
    risks.push({ severity: SEVERITY[g.code], code: g.code, where: g.where, message: describe(g) });
  }

  for (const c of input.cycles) {
    risks.push({
      severity: SEVERITY.CIRCULAR_REFERENCE,
      code: 'CIRCULAR_REFERENCE',
      where: `${c.sheet}!${c.example}`,
      message: `Referência circular entre ${c.size} célula(s), incluindo ${c.sheet}!${c.example}.`
    });
  }

  return risks.sort(
    (a, b) => RANK[a.severity] - RANK[b.severity] || a.code.localeCompare(b.code) || a.where.localeCompare(b.where)
  );
}
