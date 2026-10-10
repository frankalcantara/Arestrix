import { describe, it, expect } from 'vitest';
import ExcelJS from 'exceljs';
import { analyzeRaw } from '../src/core/excel/WorkbookAnalyzer.ts';
import { analyzeWorkbookFile } from '../src/core/excel/analyzeWorkbookFile.ts';
import { WorkbookReadError } from '../src/core/excel/ExcelWorkbookReader.ts';
import { RawCell, RawWorkbook } from '../src/core/excel/RawWorkbook.ts';
import { WorkbookModel } from '../src/core/excel/WorkbookModel.ts';

const SRC = { fileName: 't.xlsx', sizeBytes: 0, sha256: '' };
const AT = '2026-01-01T00:00:00.000Z';

/** Constrói um .xlsx real em memória. */
async function buildXlsx(fill: (wb: ExcelJS.Workbook) => void): Promise<Uint8Array> {
  const wb = new ExcelJS.Workbook();
  fill(wb);
  return new Uint8Array(await wb.xlsx.writeBuffer());
}

function salesWorkbook(wb: ExcelJS.Workbook): void {
  const v = wb.addWorksheet('Vendas');
  v.getCell('A1').value = 'Vendas 2025';
  ['Produto', 'Qtd', 'Preco', 'Total'].forEach((h, i) => (v.getCell(3, i + 1).value = h));
  const rows: [string, number, number][] = [['Caneta', 10, 2.5], ['Lapis', 20, 1.5], ['Caderno', 5, 12], ['Borracha', 8, 1], ['Regua', 3, 4]];
  rows.forEach(([p, q, pr], i) => {
    const r = 4 + i;
    v.getCell(r, 1).value = p;
    v.getCell(r, 2).value = q;
    v.getCell(r, 3).value = pr;
    v.getCell(r, 4).value = { formula: `B${r}*C${r}`, result: q * pr };
  });
  const p = wb.addWorksheet('Parametros');
  p.getCell('A1').value = 'Taxa';
  p.getCell('B1').value = 0.18;
  p.getCell('A2').value = 'Moeda';
  p.getCell('B2').value = 'BRL';
  const r = wb.addWorksheet('Resumo');
  r.getCell('A1').value = 'Total geral';
  r.getCell('B1').value = { formula: 'SUM(Vendas!D4:D8)', result: 77 };
  r.getCell('A2').value = 'Com taxa';
  r.getCell('B2').value = { formula: 'Vendas!D4*1.18', result: 29.5 };
}

describe('Raio-X de planilhas — fluxo completo com .xlsx real', () => {
  it('detecta regiões, papéis, cabeçalhos e tipos de coluna', async () => {
    const m = await analyzeWorkbookFile(await buildXlsx(salesWorkbook), 'vendas.xlsx', { importedAt: AT });
    expect(m.sheets.map(s => s.name)).toEqual(['Vendas', 'Parametros', 'Resumo']);

    const vendas = m.sheets[0].regions;
    expect(vendas).toHaveLength(1);
    expect(vendas[0]).toMatchObject({ id: 'Vendas!A3:D8', role: 'input_table', title: 'Vendas 2025', rowCount: 5 });
    // só 5 das 20 células de dados têm fórmula: maioria de constantes
    expect(vendas[0].header!.columns.map(c => [c.name, c.inferredType])).toEqual([
      ['Produto', 'text'], ['Qtd', 'integer'], ['Preco', 'number'], ['Total', 'integer']
    ]);
    expect(vendas[0].header!.columns[3].hasFormulas).toBe(true);
    expect(vendas[0].header!.columns[0].distinctRatio).toBe(1);
    expect(vendas[0].formulaPatterns).toEqual([{ r1c1: 'RC[-2]*RC[-1]', count: 5, example: 'B4*C4' }]);

    expect(m.sheets[1].regions[0]).toMatchObject({ role: 'parameters', range: 'A1:B2' });
    expect(m.sheets[2].regions[0].role).toBe('report');
  });

  it('liga as regiões por dependência e conta fórmulas', async () => {
    const m = await analyzeWorkbookFile(await buildXlsx(salesWorkbook), 'vendas.xlsx', { importedAt: AT });
    expect(m.dependencies).toEqual([{ from: 'Vendas!A3:D8', to: 'Resumo!A1:B2', formulaCount: 2 }]);
    expect(m.stats).toMatchObject({ sheets: 3, regions: 3, formulaCells: 7 });
  });

  it('é determinístico: mesmo arquivo, mesmo JSON (salvo importedAt)', async () => {
    const bytes = await buildXlsx(salesWorkbook);
    const a = await analyzeWorkbookFile(bytes, 'v.xlsx', { importedAt: AT });
    const b = await analyzeWorkbookFile(bytes, 'v.xlsx', { importedAt: AT });
    expect(JSON.stringify(a)).toBe(JSON.stringify(b));
    expect(a.source.sha256).toMatch(/^[0-9a-f]{64}$/);
  });

  it('reporta número fixo em fórmula, abas ocultas e erros em cache', async () => {
    const bytes = await buildXlsx(wb => {
      salesWorkbook(wb);
      const h = wb.addWorksheet('Rascunho');
      h.state = 'hidden';
      h.getCell('A1').value = { formula: 'NOW()', result: new Date(0) };
      h.getCell('A2').value = { formula: '1/0', result: { error: '#DIV/0!' } };
    });
    const m = await analyzeWorkbookFile(bytes, 'x.xlsx', { importedAt: AT });
    const codes = m.risks.map(r => r.code);
    expect(codes).toEqual(expect.arrayContaining(['HARDCODED_IN_FORMULA', 'HIDDEN_SHEET', 'VOLATILE_FUNCTION', 'CACHED_ERROR']));
    expect(m.risks.find(r => r.code === 'CACHED_ERROR')!.message).toContain('#DIV/0!');
    expect(m.risks.find(r => r.code === 'HARDCODED_IN_FORMULA')!.where).toBe('Resumo!A1:B2');
    // alta severidade vem primeiro
    expect(m.risks[0].severity).toBe('high');
  });

  it('reporta aba protegida e intervalos mesclados dentro de dados', async () => {
    const bytes = await buildXlsx(wb => {
      salesWorkbook(wb);
      const v = wb.getWorksheet('Vendas')!;
      v.mergeCells('A6:A7');
      void v.protect('x', {});
    });
    const m = await analyzeWorkbookFile(bytes, 'x.xlsx', { importedAt: AT });
    expect(m.risks.some(r => r.code === 'MERGED_IN_DATA' && r.where === 'Vendas!A3:D8')).toBe(true);
    expect(m.risks.some(r => r.code === 'PROTECTED_SHEET' && r.where === 'Vendas')).toBe(true);
  });

  it('recusa arquivos protegidos por senha e não-xlsx', async () => {
    const utf16 = (t: string): number[] => [...t].flatMap(c => [c.charCodeAt(0), 0]);
    const ole = new Uint8Array([0xd0, 0xcf, 0x11, 0xe0, ...utf16('EncryptedPackage')]);
    await expect(analyzeWorkbookFile(ole, 'x.xlsx')).rejects.toMatchObject({ code: 'ENCRYPTED' });
    const xls = new Uint8Array([0xd0, 0xcf, 0x11, 0xe0, ...utf16('Workbook')]);
    await expect(analyzeWorkbookFile(xls, 'x.xls')).rejects.toMatchObject({ code: 'LEGACY_XLS' });
    await expect(analyzeWorkbookFile(new TextEncoder().encode('a,b'), 'x.csv')).rejects.toBeInstanceOf(WorkbookReadError);
  });

  it('reporta progresso', async () => {
    const stages: string[] = [];
    await analyzeWorkbookFile(await buildXlsx(salesWorkbook), 'v.xlsx', { onProgress: s => stages.push(s) });
    expect(stages[0]).toBe('lendo arquivo');
    expect(stages.at(-1)).toBe('concluído');
  });
});

// ---------- estrutura bruta, sem ExcelJS ----------

function raw(cells: Partial<RawCell>[], extra: Partial<RawWorkbook> = {}): RawWorkbook {
  return {
    hasMacros: false,
    definedNames: [],
    sheets: [{ name: 'S', state: 'visible', protected: false, mergedRanges: [], cells: cells as RawCell[] }],
    ...extra
  };
}

describe('Raio-X — análise sobre estrutura bruta', () => {
  it('detecta referência circular entre células e auto-referência', () => {
    const m = analyzeRaw(raw([
      { row: 1, col: 1, value: 0, formula: 'B1+1' },
      { row: 1, col: 2, value: 0, formula: 'A1+1' },
      { row: 3, col: 1, value: 0, formula: 'A3+1' }
    ]), SRC, { importedAt: AT });
    const cycles = m.risks.filter(r => r.code === 'CIRCULAR_REFERENCE');
    expect(cycles.map(c => c.where)).toEqual(['S!A1', 'S!A3']);
    expect(cycles[0].message).toContain('2 célula');
  });

  it('não acusa ciclo em cadeia comum', () => {
    const m = analyzeRaw(raw([
      { row: 1, col: 1, value: 1 },
      { row: 2, col: 1, value: 1, formula: 'A1+A1' },
      { row: 3, col: 1, value: 1, formula: 'A2+A1' }
    ]), SRC);
    expect(m.risks.some(r => r.code === 'CIRCULAR_REFERENCE')).toBe(false);
  });

  it('sinaliza links externos, arrays, macros e funções desconhecidas', () => {
    const m = analyzeRaw(raw([
      { row: 1, col: 1, value: 1, formula: "[1]Precos!A1*A2" },
      { row: 2, col: 1, value: 2 },
      { row: 3, col: 1, value: 1, formula: 'FOO(A2)', arrayFormula: true }
    ], { hasMacros: true }), SRC);
    const codes = new Set(m.risks.map(r => r.code));
    for (const c of ['EXTERNAL_LINK', 'ARRAY_FORMULA', 'MACROS', 'UNKNOWN_FUNCTION'] as const) expect(codes.has(c)).toBe(true);
    expect(m.externalLinks).toEqual([{ target: '[1]', usedBy: ['S!A1:A3'] }]);
  });

  it('junta rótulos e valores separados por uma coluna em branco num bloco de parâmetros', () => {
    const m = analyzeRaw(raw([
      { row: 46, col: 6, value: 'Taxa de Desconto' }, { row: 46, col: 8, value: 0.31 },
      { row: 47, col: 6, value: 'Cresc Perpetuidade' }, { row: 47, col: 8, value: 0.02 }
    ]), SRC);
    expect(m.sheets[0].regions.map(r => [r.range, r.role])).toEqual([['F46:H47', 'parameters']]);
  });

  it('reconhece itens nas linhas e períodos nas colunas, sem confundir com tabela comum', () => {
    const cells: Partial<RawCell>[] = [];
    for (let c = 3; c <= 10; c++) cells.push({ row: 1, col: c, value: 2016 + c });
    ['Receita', 'Custo', 'Lucro'].forEach((label, i) => {
      cells.push({ row: 2 + i, col: 2, value: label });
      for (let c = 3; c <= 10; c++) cells.push({ row: 2 + i, col: c, value: c * 10, formula: `C${2 + i}*1.1` });
    });
    const m = analyzeRaw(raw(cells), SRC);
    expect(m.sheets[0].regions).toHaveLength(1);
    expect(m.sheets[0].regions[0]).toMatchObject({ role: 'calculated_table', orientation: 'rows', rowLabels: ['Receita', 'Custo', 'Lucro'] });
  });

  it('separa regiões por linha em branco e resolve nomes definidos nas dependências', () => {
    const cells: Partial<RawCell>[] = [
      { row: 1, col: 1, value: 'Taxa' }, { row: 1, col: 2, value: 0.2 },
      { row: 5, col: 1, value: 'Item' }, { row: 5, col: 2, value: 'Valor' },
      { row: 6, col: 1, value: 'a' }, { row: 6, col: 2, value: 10, formula: 'Taxa*100' },
      { row: 7, col: 1, value: 'b' }, { row: 7, col: 2, value: 20, formula: 'Taxa*200' }
    ];
    const m = analyzeRaw(raw(cells, { definedNames: [{ name: 'Taxa', ref: 'S!$B$1' }] }), SRC);
    expect(m.sheets[0].regions.map(r => [r.range, r.role])).toEqual([['A1:B1', 'parameters'], ['A5:B7', 'calculated_table']]);
    expect(m.dependencies).toEqual([{ from: 'S!A1:B1', to: 'S!A5:B7', formulaCount: 2 }]);
  });
});
