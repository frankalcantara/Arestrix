import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import {
  DEFAULT_EXCEL_IMPORT_PROMPT, SUMMARY_PLACEHOLDER, ExcelPromptStore, buildExcelImportPrompt, summarizeForPrompt
} from '../src/core/excel/ExcelImportPrompt.ts';
import { analyzeRaw } from '../src/core/excel/WorkbookAnalyzer.ts';
import { DefaultIntelligenceService } from '../src/core/ai/DefaultIntelligenceService.ts';
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

function fakeLocalStorage() {
  const data = new Map<string, string>();
  (globalThis as any).localStorage = {
    getItem: (k: string) => data.get(k) ?? null,
    setItem: (k: string, v: string) => void data.set(k, v),
    removeItem: (k: string) => void data.delete(k)
  };
}

describe('Prompt do agente de importação de planilhas', () => {
  beforeEach(fakeLocalStorage);
  afterEach(() => {
    delete (globalThis as any).localStorage;
    DefaultIntelligenceService.getInstance().setCustomApiCaller(undefined);
  });

  it('o resumo enviado ao modelo só tem estrutura: sem valores de célula, nome de arquivo nem hash', () => {
    const summary = summarizeForPrompt(model());
    expect(summary).toContain('"Cliente"');
    for (const secret of ['SEGREDO-ACME', '98765', 'cliente-confidencial', 'abc123hash']) expect(summary).not.toContain(secret);
  });

  it('o prompt padrão tem o marcador do Raio-X e ele é substituído', () => {
    expect(DEFAULT_EXCEL_IMPORT_PROMPT).toContain(SUMMARY_PLACEHOLDER);
    const built = buildExcelImportPrompt(model());
    expect(built).not.toContain(SUMMARY_PLACEHOLDER);
    expect(built).toContain('"regions"');
  });

  it('um prompt editado sem o marcador recebe o Raio-X no final', () => {
    const built = buildExcelImportPrompt(model(), 'Só confira os papéis.');
    expect(built.startsWith('Só confira os papéis.')).toBe(true);
    expect(built).toContain('"regions"');
  });

  it('salva, lê e restaura o prompt editado', () => {
    expect(ExcelPromptStore.get()).toBe(DEFAULT_EXCEL_IMPORT_PROMPT);
    ExcelPromptStore.set('meu prompt {{WORKBOOK_SUMMARY}}');
    expect(ExcelPromptStore.isCustomized()).toBe(true);
    expect(ExcelPromptStore.get()).toBe('meu prompt {{WORKBOOK_SUMMARY}}');
    ExcelPromptStore.reset();
    expect(ExcelPromptStore.isCustomized()).toBe(false);
    ExcelPromptStore.set(DEFAULT_EXCEL_IMPORT_PROMPT); // salvar igual ao padrão não cria personalização
    expect(ExcelPromptStore.isCustomized()).toBe(false);
  });

  it('a revisão usa o prompt editado e descarta ids de região e papéis inventados pelo modelo', async () => {
    const m = model();
    const regionId = m.sheets[0].regions[0].id;
    let seenPrompt = '';
    const svc = DefaultIntelligenceService.getInstance();
    svc.setCustomApiCaller(async (_key, prompt) => {
      seenPrompt = prompt;
      return JSON.stringify({
        confidenceScore: 150,
        summary: 'Cadastro com total.',
        regionCorrections: [
          { regionId, role: 'parameters', reason: 'ok' },
          { regionId: 'Inventada!A1:B2', role: 'input_table', reason: 'x' },
          { regionId, role: 'papel_falso', reason: 'x' }
        ],
        tableSuggestions: [{ regionId, tableName: 'clientes', primaryKey: 'Cliente' }, { regionId: 'Nada!A1', tableName: 'x' }],
        findings: ['a', 42, 'b'],
        questionsForUser: ['Qual a moeda?']
      });
    });

    const r = await svc.reviewExcelImport(m, 'REVISE:\n{{WORKBOOK_SUMMARY}}');
    expect(seenPrompt.startsWith('REVISE:')).toBe(true);
    expect(r.isSuccess).toBe(true);
    expect(r.confidenceScore).toBe(100);
    expect(r.regionCorrections).toEqual([{ regionId, role: 'parameters', reason: 'ok' }]);
    expect(r.tableSuggestions).toEqual([{ regionId, tableName: 'clientes', primaryKey: 'Cliente', notes: undefined }]);
    expect(r.findings).toEqual(['a', 'b']);
  });

  it('sem chave e sem caller, a revisão avisa qual variável falta em vez de falhar', async () => {
    const r = await DefaultIntelligenceService.getInstance().reviewExcelImport(model());
    expect(r.isSuccess).toBe(false);
    expect(r.missingEnvKey).toBe('DEEPSEEK_API_KEY');
  });

  it('resposta que não é JSON vira erro legível', async () => {
    const svc = DefaultIntelligenceService.getInstance();
    svc.setCustomApiCaller(async () => 'isso não é json');
    const r = await svc.reviewExcelImport(model());
    expect(r.isSuccess).toBe(false);
    expect(r.error).toContain('Falha na revisão');
  });
});
