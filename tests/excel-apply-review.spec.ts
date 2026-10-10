import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import {
  DEFAULT_EXCEL_IMPORT_PROMPT, SUMMARY_PLACEHOLDER, ExcelPromptStore, buildExcelImportPrompt, summarizeForPrompt
} from '../src/core/excel/ExcelImportPrompt.ts';
import { analyzeRaw } from '../src/core/excel/WorkbookAnalyzer.ts';
import { DefaultIntelligenceService } from '../src/core/ai/DefaultIntelligenceService.ts';
import { applyReview } from '../src/core/excel/applyReview.ts';
import { RawCell } from '../src/core/excel/RawWorkbook.ts';

function model() {
  const cells: Partial<RawCell>[] = [
    { row: 1, col: 1, value: 'Cliente' }, { row: 1, col: 2, value: 'Valor' },
    { row: 2, col: 1, value: 'SEGREDO-ACME' }, { row: 2, col: 2, value: 98765.43 },
    { row: 3, col: 1, value: 'Beta' }, { row: 3, col: 2, value: 12 },
    { row: 6, col: 1, value: 'Total' }, { row: 6, col: 2, value: 98777.43, formula: 'SUM(B2:B3)' }
  ];
  return analyzeRaw(
    { hasMacros: false, definedNames: [], sheets: [{ name: 'S', state: 'visible', protected: false, mergedRanges: [], cells: cells as RawCell[] }] },
    { fileName: 'cliente-confidencial.xlsx', sizeBytes: 1, sha256: 'abc123hash' }
  );
}

describe('applyReview', () => {
  it('aplica papéis e tabelas sem intervenção e guarda as dúvidas', () => {
    const m = model();
    const r = m.sheets[0].regions[0];
    const before = r.role;
    const out = applyReview(m, {
      isSuccess: true, confidenceScore: 80, summary: 'ok',
      regionCorrections: [{ regionId: r.id, role: before === 'report' ? 'parameters' : 'report', reason: 'x' }, { regionId: 'inexistente', role: 'report', reason: 'y' }],
      tableSuggestions: [{ regionId: r.id, tableName: 'clientes', primaryKey: 'Cliente' }],
      findings: ['a'], questionsForUser: ['Qual a moeda?']
    });
    expect(out).toEqual({ roles: 1, tables: 1 });
    expect(r.detectedRole).toBe(before);
    expect(r.table?.name).toBe('clientes');
    expect(m.review?.questions).toEqual([{ text: 'Qual a moeda?' }]);
  });
});
