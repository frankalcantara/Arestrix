/**
 * Estrutura bruta, independente do ExcelJS, produzida pelo leitor e consumida pelo analisador.
 * Só contém células não vazias. Linhas e colunas são base 1.
 */

export interface RawCell {
  row: number;
  col: number;
  value: string | number | boolean | Date | null; // para fórmula, o valor em cache
  formula?: string; // sem o "=" inicial
  arrayFormula?: boolean;
  error?: string; // ex.: "#REF!" (valor constante ou resultado em cache)
  numFmt?: string;
}

export interface RawSheet {
  name: string;
  state: 'visible' | 'hidden' | 'veryHidden';
  protected: boolean;
  mergedRanges: string[];
  cells: RawCell[];
}

export interface RawWorkbook {
  hasMacros: boolean;
  sheets: RawSheet[];
  definedNames: { name: string; ref: string }[];
}
