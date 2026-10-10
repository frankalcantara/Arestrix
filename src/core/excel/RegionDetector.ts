import { Bounds, boundsContain, boundsToRange, cellKey, numToCol } from './addressing.ts';
import { FormulaAnalysis } from './FormulaAnalyzer.ts';
import { RawCell, RawSheet } from './RawWorkbook.ts';
import { ColumnModel, ColumnType, FormulaPattern, RegionModel, RegionRole } from './WorkbookModel.ts';

export interface DetectedRegion {
  model: RegionModel;
  bounds: Bounds; // corpo da região (cabeçalho + dados), sem o título
}

const AGGREGATES = new Set([
  'SUM', 'AVERAGE', 'COUNT', 'COUNTA', 'MIN', 'MAX', 'SUMIF', 'SUMIFS', 'COUNTIF', 'COUNTIFS',
  'AVERAGEIF', 'AVERAGEIFS', 'SUMPRODUCT', 'SUBTOTAL', 'MEDIAN'
]);

type ColKind = Exclude<ColumnType, 'mixed'>;

function isConstString(c: RawCell): boolean {
  return c.formula === undefined && c.error === undefined && typeof c.value === 'string';
}

function stripFormatNoise(fmt: string): string {
  return fmt.replace(/"[^"]*"/g, '').replace(/\[[^\]]*\]/g, '').replace(/\\./g, '');
}

function isDateFormat(fmt: string): boolean {
  return /[ymdhs]/i.test(stripFormatNoise(fmt));
}

function isCurrencyFormat(fmt: string): boolean {
  return /R\$|[€£¥]|(?<!\[)\$|\[\$[^\]-]+/.test(fmt);
}

function cellKind(c: RawCell): ColKind | null {
  if (c.error !== undefined || c.value === null) return null;
  const v = c.value;
  if (v instanceof Date) return 'date';
  if (typeof v === 'boolean') return 'boolean';
  if (typeof v === 'number') {
    if (c.numFmt && isDateFormat(c.numFmt)) return 'date';
    if (c.numFmt && isCurrencyFormat(c.numFmt)) return 'currency';
    return Number.isInteger(v) ? 'integer' : 'number';
  }
  return 'text';
}

function resolveType(kinds: Set<ColKind>): ColumnType {
  if (kinds.size === 0) return 'text';
  if (kinds.size === 1) return [...kinds][0];
  const numeric = [...kinds].every(k => k === 'integer' || k === 'number' || k === 'currency');
  if (numeric) return kinds.has('currency') ? 'currency' : 'number';
  return 'mixed';
}

interface Component {
  cells: RawCell[];
  bounds: Bounds;
  title?: string;
}

/** Componentes conexos (vizinhança de 8): uma linha ou coluna em branco separa regiões. */
function connectedComponents(cells: RawCell[]): Component[] {
  const byKey = new Map<number, RawCell>();
  for (const c of cells) byKey.set(cellKey(c.row, c.col), c);
  const sorted = [...cells].sort((a, b) => a.row - b.row || a.col - b.col);
  const seen = new Set<number>();
  const out: Component[] = [];

  for (const start of sorted) {
    const startKey = cellKey(start.row, start.col);
    if (seen.has(startKey)) continue;
    seen.add(startKey);
    const stack = [start];
    const members: RawCell[] = [];
    const bounds: Bounds = { top: start.row, bottom: start.row, left: start.col, right: start.col };
    while (stack.length) {
      const c = stack.pop()!;
      members.push(c);
      bounds.top = Math.min(bounds.top, c.row);
      bounds.bottom = Math.max(bounds.bottom, c.row);
      bounds.left = Math.min(bounds.left, c.col);
      bounds.right = Math.max(bounds.right, c.col);
      for (let dr = -1; dr <= 1; dr++) {
        for (let dc = -1; dc <= 1; dc++) {
          if (dr === 0 && dc === 0) continue;
          const k = cellKey(c.row + dr, c.col + dc);
          if (seen.has(k)) continue;
          const n = byKey.get(k);
          if (n) {
            seen.add(k);
            stack.push(n);
          }
        }
      }
    }
    out.push({ cells: members.sort((a, b) => a.row - b.row || a.col - b.col), bounds });
  }
  return out.sort((a, b) => a.bounds.top - b.bounds.top || a.bounds.left - b.bounds.left);
}

/** Um texto solto 1–2 linhas em branco acima de um bloco, sobre as suas colunas, é o título do bloco. */
function absorbSeparateTitles(comps: Component[]): Component[] {
  const absorbed = new Set<Component>();
  for (const t of comps) {
    if (t.cells.length !== 1 || !isConstString(t.cells[0])) continue;
    const { row, col } = t.cells[0];
    let best: Component | undefined;
    for (const o of comps) {
      if (o === t || o.cells.length < 2 || absorbed.has(o) || o.title !== undefined) continue;
      const gap = o.bounds.top - row;
      if (gap < 2 || gap > 3 || col < o.bounds.left || col > o.bounds.right) continue;
      if (!best || gap < best.bounds.top - row) best = o;
    }
    if (best) {
      best.title = String(t.cells[0].value);
      absorbed.add(t);
    }
  }
  return comps.filter(c => !absorbed.has(c));
}

/**
 * Rótulos numa coluna e valores noutra, separados por uma única coluna em branco (F46:F47 + H46:H47),
 * são um bloco só. Só junta duas colunas isoladas com as mesmas linhas, a da esquerda de texto.
 */
function mergeLabelValuePairs(comps: Component[]): Component[] {
  const out = [...comps];
  for (let i = 0; i < out.length; i++) {
    const a = out[i];
    if (a.bounds.left !== a.bounds.right || a.cells.length < 2 || a.cells.filter(isConstString).length / a.cells.length < 0.8) continue;
    const j = out.findIndex(b =>
      b !== a && b.bounds.left === b.bounds.right && b.bounds.left === a.bounds.right + 2 &&
      b.bounds.top === a.bounds.top && b.bounds.bottom === a.bounds.bottom
    );
    if (j < 0) continue;
    const b = out[j];
    const gapTaken = out.some(o => o !== a && o !== b && o.bounds.left <= a.bounds.right + 1 && o.bounds.right >= a.bounds.right + 1 &&
      o.bounds.top <= a.bounds.bottom && o.bounds.bottom >= a.bounds.top);
    if (gapTaken) continue;
    out[i] = {
      cells: [...a.cells, ...b.cells].sort((x, y) => x.row - y.row || x.col - y.col),
      bounds: { top: a.bounds.top, bottom: a.bounds.bottom, left: a.bounds.left, right: b.bounds.right },
      title: a.title ?? b.title
    };
    out.splice(j, 1);
    if (j < i) i--;
  }
  return out.sort((x, y) => x.bounds.top - y.bounds.top || x.bounds.left - y.bounds.left);
}

/** Linhas iniciais com uma única célula de texto, seguidas de uma linha com 2+ células, são título. */
function peelInlineTitle(comp: Component): { body: RawCell[]; bounds: Bounds; title?: string } {
  const width = comp.bounds.right - comp.bounds.left + 1;
  let body = comp.cells;
  let title = comp.title;
  for (;;) {
    const top = Math.min(...body.map(c => c.row));
    const first = body.filter(c => c.row === top);
    if (width < 2 || first.length !== 1 || !isConstString(first[0])) break;
    const rest = body.filter(c => c.row > top);
    if (rest.length === 0) break;
    const nextTop = Math.min(...rest.map(c => c.row));
    if (rest.filter(c => c.row === nextTop).length < 2) break;
    title = title ?? String(first[0].value);
    body = rest;
  }
  const bounds: Bounds = {
    top: Math.min(...body.map(c => c.row)),
    bottom: Math.max(...body.map(c => c.row)),
    left: Math.min(...body.map(c => c.col)),
    right: Math.max(...body.map(c => c.col))
  };
  return { body, bounds, title };
}

function buildColumns(body: RawCell[], bounds: Bounds, headerRow: number): ColumnModel[] {
  const dataRows = bounds.bottom - headerRow;
  const columns: ColumnModel[] = [];
  const headerCells = new Map<number, RawCell>();
  const perCol = new Map<number, RawCell[]>();
  for (const c of body) {
    if (c.row === headerRow) headerCells.set(c.col, c);
    else {
      let list = perCol.get(c.col);
      if (!list) perCol.set(c.col, (list = []));
      list.push(c);
    }
  }
  for (let col = bounds.left; col <= bounds.right; col++) {
    const cells = perCol.get(col) ?? [];
    const kinds = new Set<ColKind>();
    const distinct = new Set<string>();
    let nonNull = 0;
    let hasFormulas = false;
    for (const c of cells) {
      if (c.formula !== undefined) hasFormulas = true;
      const k = cellKind(c);
      if (k === null) continue;
      kinds.add(k);
      nonNull++;
      distinct.add(c.value instanceof Date ? c.value.toISOString() : String(c.value));
    }
    const h = headerCells.get(col);
    columns.push({
      name: h && h.value !== null ? String(h.value) : `Coluna ${numToCol(col)}`,
      inferredType: resolveType(kinds),
      nullable: cells.length < dataRows,
      distinctRatio: nonNull === 0 ? 0 : Math.round((distinct.size / nonNull) * 1000) / 1000,
      hasFormulas
    });
  }
  return columns;
}

function classify(
  body: RawCell[],
  bounds: Bounds,
  hasHeader: boolean,
  formulas: Map<number, FormulaAnalysis>,
  headerRow: number,
  rowOriented: boolean
): RegionRole {
  const height = bounds.bottom - bounds.top + 1;
  const width = bounds.right - bounds.left + 1;
  const formulaCells = body.filter(c => c.formula !== undefined);

  // report: agregações que leem de fora da própria região
  if (formulaCells.length > 0 && height <= 15) {
    const reporting = formulaCells.filter(c => {
      const a = formulas.get(cellKey(c.row, c.col));
      if (!a || !a.functions.some(f => AGGREGATES.has(f))) return false;
      return a.refs.some(r => r.sheet !== null || !boundsContain(bounds, r.bounds.top, r.bounds.left));
    });
    if (reporting.length / formulaCells.length >= 0.5) return 'report';
  }

  if (rowOriented) {
    const labeled = body.filter(c => c.col === bounds.left).length;
    const values = body.filter(c => c.col > bounds.left);
    if (labeled < 2) return 'unknown';
    return values.filter(c => c.formula !== undefined).length / values.length >= 0.5 ? 'calculated_table' : 'input_table';
  }

  if (hasHeader) {
    const data = body.filter(c => c.row > headerRow);
    const dataRows = bounds.bottom - headerRow;
    if (dataRows < 2) return 'unknown';
    const formulaFrac = data.filter(c => c.formula !== undefined).length / data.length;
    return formulaFrac >= 0.5 ? 'calculated_table' : 'input_table';
  }

  // parameters: pares rótulo/valor em duas colunas
  const occupiedCols = new Set(body.map(c => c.col)).size;
  if (width >= 2 && occupiedCols === 2 && height <= 20) {
    const labels = body.filter(c => c.col === bounds.left);
    if (labels.filter(isConstString).length / labels.length >= 0.8) return 'parameters';
  }
  return 'unknown';
}

const MAX_ROW_LABELS = 40;

/**
 * Itens nas linhas, períodos nas colunas: primeira coluna de rótulos de texto e o resto numérico/calculado.
 * Para não confundir com uma tabela comum (Produto | Qtd | Preço), exige também uma destas pistas:
 * largura >= 8, canto superior esquerdo vazio, ou linha do topo sem texto (datas/anos).
 */
function isRowOriented(body: RawCell[], bounds: Bounds): boolean {
  const width = bounds.right - bounds.left + 1;
  if (width < 3) return false;
  const labels = body.filter(c => c.col === bounds.left);
  const rest = body.filter(c => c.col > bounds.left);
  if (labels.length < 2 || new Set(rest.map(c => c.col)).size < 2) return false;
  if (labels.filter(isConstString).length / labels.length < 0.7) return false;
  if (rest.filter(c => !isConstString(c)).length / rest.length < 0.8) return false;

  const topRest = rest.filter(c => c.row === bounds.top);
  const cornerEmpty = !labels.some(c => c.row === bounds.top);
  const topWithoutText = topRest.length > 0 && topRest.filter(isConstString).length / topRest.length < 0.2;
  return width >= 8 || cornerEmpty || topWithoutText;
}

function detectHeader(body: RawCell[], bounds: Bounds): boolean {
  if (bounds.bottom - bounds.top < 1) return false;
  const first = body.filter(c => c.row === bounds.top);
  if (first.length < 2) return false;
  return first.filter(isConstString).length / first.length >= 0.8;
}

function formulaPatterns(body: RawCell[], formulas: Map<number, FormulaAnalysis>): FormulaPattern[] {
  const groups = new Map<string, FormulaPattern>();
  for (const c of body) {
    if (c.formula === undefined) continue;
    const a = formulas.get(cellKey(c.row, c.col));
    if (!a) continue;
    const g = groups.get(a.r1c1);
    if (g) g.count++;
    else groups.set(a.r1c1, { r1c1: a.r1c1, count: 1, example: c.formula });
  }
  return [...groups.values()].sort((a, b) => b.count - a.count || a.r1c1.localeCompare(b.r1c1));
}

export function detectRegions(sheet: RawSheet, formulas: Map<number, FormulaAnalysis>): DetectedRegion[] {
  const comps = mergeLabelValuePairs(absorbSeparateTitles(connectedComponents(sheet.cells)));
  const regions: DetectedRegion[] = [];

  for (const comp of comps) {
    const { body, bounds, title } = peelInlineTitle(comp);
    const rowOriented = isRowOriented(body, bounds);
    const hasHeader = !rowOriented && detectHeader(body, bounds);
    const role = classify(body, bounds, hasHeader, formulas, bounds.top, rowOriented);
    const range = boundsToRange(bounds);
    const model: RegionModel = {
      id: `${sheet.name}!${range}`,
      range,
      role,
      rowCount: rowOriented ? body.filter(c => c.col === bounds.left).length : bounds.bottom - bounds.top + 1 - (hasHeader ? 1 : 0),
      formulaPatterns: formulaPatterns(body, formulas)
    };
    if (title !== undefined) model.title = title;
    if (rowOriented) {
      model.orientation = 'rows';
      model.rowLabels = body.filter(c => c.col === bounds.left && c.value !== null).slice(0, MAX_ROW_LABELS).map(c => String(c.value));
    }
    if (hasHeader) model.header = { row: bounds.top, columns: buildColumns(body, bounds, bounds.top) };
    regions.push({ model, bounds });
  }
  return regions;
}
