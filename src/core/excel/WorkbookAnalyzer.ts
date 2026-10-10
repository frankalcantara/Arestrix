import { Bounds, boundsContain, boundsIntersect, boundsToRange, cellAddress, cellKey, parseSheetRange } from './addressing.ts';
import { RegionDependencies, findCycles } from './DependencyGraph.ts';
import { FormulaAnalysis, analyzeFormula } from './FormulaAnalyzer.ts';
import { RawSheet, RawWorkbook } from './RawWorkbook.ts';
import { DetectedRegion, detectRegions } from './RegionDetector.ts';
import { CycleInfo, ErrorCellInfo, FormulaCellInfo, analyzeRisks } from './RiskAnalyzer.ts';
import { RegionId, SheetModel, WorkbookModel } from './WorkbookModel.ts';

export interface AnalyzeOptions {
  importedAt?: string; // injetável nos testes; padrão: agora
  onProgress?: (stage: string, fraction: number) => void;
}

export interface SourceInfo {
  fileName: string;
  sizeBytes: number;
  sha256: string;
}

const MAX_CYCLES_REPORTED = 50;

interface NameTarget {
  sheet: string | null;
  bounds: Bounds;
  positional?: boolean; // não entra no grafo de ciclos
}

function resolveDefinedNames(raw: RawWorkbook): Map<string, NameTarget[]> {
  const out = new Map<string, NameTarget[]>();
  for (const n of raw.definedNames) {
    const targets: NameTarget[] = [];
    for (const part of n.ref.split(',')) {
      const r = parseSheetRange(part.trim());
      if (r) targets.push(r);
    }
    out.set(n.name.toLowerCase(), targets);
  }
  return out;
}

function sheetDimensions(sheet: RawSheet): string {
  if (sheet.cells.length === 0) return '';
  const b: Bounds = { top: Infinity, left: Infinity, bottom: 0, right: 0 };
  for (const c of sheet.cells) {
    b.top = Math.min(b.top, c.row);
    b.left = Math.min(b.left, c.col);
    b.bottom = Math.max(b.bottom, c.row);
    b.right = Math.max(b.right, c.col);
  }
  return boundsToRange(b);
}

/** Núcleo da análise: estrutura bruta → WorkbookModel. Determinístico (salvo importedAt). */
export function analyzeRaw(raw: RawWorkbook, source: SourceInfo, options: AnalyzeOptions = {}): WorkbookModel {
  const progress = options.onProgress ?? (() => {});
  const nameTargets = resolveDefinedNames(raw);
  const definedLower = new Set(nameTargets.keys());
  const sheetByLower = new Map(raw.sheets.map(s => [s.name.toLowerCase(), s]));

  // 1. Fórmulas e regiões por aba
  const formulasBySheet = new Map<string, Map<number, FormulaAnalysis>>();
  const regionsBySheet = new Map<string, DetectedRegion[]>();
  raw.sheets.forEach((s, i) => {
    progress('regiões', i / raw.sheets.length);
    const idx = new Map<number, FormulaAnalysis>();
    for (const c of s.cells) {
      if (c.formula !== undefined) idx.set(cellKey(c.row, c.col), analyzeFormula(c.formula, c.row, c.col, definedLower));
    }
    formulasBySheet.set(s.name, idx);
    regionsBySheet.set(s.name, detectRegions(s, idx));
  });

  const regionAt = (sheet: string, row: number, col: number): DetectedRegion | undefined =>
    regionsBySheet.get(sheet)?.find(r => boundsContain(r.bounds, row, col));

  // 2. Nós do grafo de células (só células com fórmula)
  interface Node { sheet: string; row: number; col: number }
  const nodes: Node[] = [];
  const nodeIdBySheet = new Map<string, Map<number, number>>();
  const nodesOfSheet = new Map<string, number[]>();
  for (const s of raw.sheets) {
    const ids = new Map<number, number>();
    const list: number[] = [];
    for (const c of s.cells) {
      if (c.formula === undefined) continue;
      ids.set(cellKey(c.row, c.col), nodes.length);
      list.push(nodes.length);
      nodes.push({ sheet: s.name, row: c.row, col: c.col });
    }
    nodeIdBySheet.set(s.name, ids);
    nodesOfSheet.set(s.name, list);
  }

  const formulaNodesIn = (sheet: string, b: Bounds): number[] => {
    const ids = nodeIdBySheet.get(sheet);
    if (!ids || ids.size === 0) return [];
    const found: number[] = [];
    const area = (b.bottom - b.top + 1) * (b.right - b.left + 1);
    if (area <= ids.size) {
      for (let r = b.top; r <= b.bottom; r++) {
        for (let c = b.left; c <= b.right; c++) {
          const id = ids.get(cellKey(r, c));
          if (id !== undefined) found.push(id);
        }
      }
    } else {
      for (const id of nodesOfSheet.get(sheet)!) {
        const n = nodes[id];
        if (boundsContain(b, n.row, n.col)) found.push(id);
      }
    }
    return found;
  };

  // 3. Dependências, links externos e dados para riscos
  const deps = new RegionDependencies();
  const adj: number[][] = nodes.map(() => []);
  const formulaCells: FormulaCellInfo[] = [];
  const errorCells: ErrorCellInfo[] = [];
  const externalUsage = new Map<string, Set<string>>();
  let totalCells = 0;
  let totalFormulas = 0;

  raw.sheets.forEach((s, i) => {
    progress('dependências', i / raw.sheets.length);
    totalCells += s.cells.length;
    const idx = formulasBySheet.get(s.name)!;
    const ids = nodeIdBySheet.get(s.name)!;

    for (const c of s.cells) {
      const region = regionAt(s.name, c.row, c.col);
      const regionId = region?.model.id ?? null;
      if (c.error !== undefined) errorCells.push({ sheet: s.name, row: c.row, col: c.col, regionId, error: c.error });
      if (c.formula === undefined) continue;

      totalFormulas++;
      const analysis = idx.get(cellKey(c.row, c.col))!;
      formulaCells.push({ sheet: s.name, row: c.row, col: c.col, regionId, analysis, isArray: c.arrayFormula === true });

      const targets: NameTarget[] = analysis.refs.filter(r => !r.external).map(r => ({ sheet: r.sheet, bounds: r.bounds, positional: r.positional }));
      for (const n of analysis.names) targets.push(...(nameTargets.get(n) ?? []));

      for (const r of analysis.refs) {
        if (!r.external) continue;
        const label = `[${r.externalIndex}]`;
        let users = externalUsage.get(label);
        if (!users) externalUsage.set(label, (users = new Set()));
        users.add(regionId ?? `${s.name}!${cellAddress(c.row, c.col)}`);
      }

      const from = new Set<RegionId>();
      const me = ids.get(cellKey(c.row, c.col))!;
      for (const t of targets) {
        const target = sheetByLower.get((t.sheet ?? s.name).toLowerCase());
        if (!target) continue;
        for (const r of regionsBySheet.get(target.name) ?? []) {
          if (boundsIntersect(r.bounds, t.bounds)) from.add(r.model.id);
        }
        if (!t.positional) for (const id of formulaNodesIn(target.name, t.bounds)) adj[me].push(id);
      }
      if (regionId) deps.addFormula(regionId, from);
    }
  });

  // 4. Referências circulares (nível de célula)
  progress('ciclos', 0);
  const cycles: CycleInfo[] = findCycles(adj)
    .slice(0, MAX_CYCLES_REPORTED)
    .map(comp => {
      const n = nodes[comp[0]];
      return { sheet: n.sheet, size: comp.length, example: cellAddress(n.row, n.col) };
    });

  // 5. Riscos e montagem
  progress('riscos', 0);
  const risks = analyzeRisks({ raw, regionsBySheet, formulaCells, errorCells, cycles });

  const sheets: SheetModel[] = raw.sheets.map(s => ({
    name: s.name,
    state: s.state,
    protected: s.protected,
    dimensions: sheetDimensions(s),
    mergedRanges: [...s.mergedRanges],
    regions: (regionsBySheet.get(s.name) ?? []).map(r => r.model)
  }));

  progress('concluído', 1);
  return {
    schemaVersion: 1,
    source: { ...source, importedAt: options.importedAt ?? new Date().toISOString() },
    hasMacros: raw.hasMacros,
    sheets,
    definedNames: [...raw.definedNames].sort((a, b) => a.name.localeCompare(b.name)),
    externalLinks: [...externalUsage.entries()]
      .map(([target, users]) => ({ target, usedBy: [...users].sort() }))
      .sort((a, b) => a.target.localeCompare(b.target)),
    dependencies: deps.toList(),
    risks,
    stats: {
      sheets: sheets.length,
      regions: sheets.reduce((n, s) => n + s.regions.length, 0),
      cells: totalCells,
      formulaCells: totalFormulas
    }
  };
}

