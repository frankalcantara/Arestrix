/**
 * Modelo intermediário (JSON) da importação de planilhas — contrato central da Fase 1 (Raio-X).
 * Guarda estrutura, padrões de fórmula e estatísticas; nunca todas as células.
 */

export type RegionId = string; // "<aba>!<intervalo>"
export type RegionRef = RegionId;

export type RegionRole = 'input_table' | 'calculated_table' | 'parameters' | 'report' | 'unknown';
export type ColumnType = 'number' | 'integer' | 'date' | 'currency' | 'text' | 'boolean' | 'mixed';
export type SheetState = 'visible' | 'hidden' | 'veryHidden';

export type RiskCode =
  | 'EXTERNAL_LINK'
  | 'MACROS'
  | 'CACHED_ERROR'
  | 'CIRCULAR_REFERENCE'
  | 'HARDCODED_IN_FORMULA'
  | 'VOLATILE_FUNCTION'
  | 'ARRAY_FORMULA'
  | 'HIDDEN_SHEET'
  | 'PROTECTED_SHEET'
  | 'MERGED_IN_DATA'
  | 'UNKNOWN_FUNCTION';

export type RiskSeverity = 'high' | 'medium' | 'low';

export interface ColumnModel {
  name: string;
  inferredType: ColumnType;
  nullable: boolean;
  distinctRatio: number; // candidata a chave quando ~1
  hasFormulas: boolean;
}

export interface FormulaPattern {
  r1c1: string;
  count: number;
  example: string;
}

export interface RegionModel {
  id: RegionId;
  range: string;
  role: RegionRole;
  title?: string;
  header?: { row: number; columns: ColumnModel[] };
  /** "rows": itens nas linhas e períodos/cenários nas colunas (modelos financeiros). Ausente = tabela comum. */
  orientation?: 'rows';
  rowLabels?: string[]; // só com orientation "rows": rótulos da primeira coluna (no máximo 40)
  rowCount: number;
  formulaPatterns: FormulaPattern[];
}

export interface SheetModel {
  name: string;
  state: SheetState;
  protected: boolean;
  dimensions: string;
  mergedRanges: string[];
  regions: RegionModel[];
}

export interface Risk {
  severity: RiskSeverity;
  code: RiskCode;
  where: string;
  message: string;
}

export interface WorkbookModel {
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
