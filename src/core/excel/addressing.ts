/** Utilitários de endereçamento A1 (linhas e colunas base 1). */

export const MAX_ROW = 1048576;
export const MAX_COL = 16384;

export interface Bounds {
  top: number;
  left: number;
  bottom: number;
  right: number;
}

export function colToNum(letters: string): number {
  let n = 0;
  for (const ch of letters.toUpperCase()) n = n * 26 + (ch.charCodeAt(0) - 64);
  return n;
}

export function numToCol(n: number): string {
  let s = '';
  while (n > 0) {
    const m = (n - 1) % 26;
    s = String.fromCharCode(65 + m) + s;
    n = Math.floor((n - 1) / 26);
  }
  return s;
}

export function cellKey(row: number, col: number): number {
  return row * (MAX_COL + 1) + col;
}

export function cellAddress(row: number, col: number): string {
  return `${numToCol(col)}${row}`;
}

export function boundsToRange(b: Bounds): string {
  const a = cellAddress(b.top, b.left);
  return b.top === b.bottom && b.left === b.right ? a : `${a}:${cellAddress(b.bottom, b.right)}`;
}

export function boundsIntersect(a: Bounds, b: Bounds): boolean {
  return a.top <= b.bottom && b.top <= a.bottom && a.left <= b.right && b.left <= a.right;
}

export function boundsContain(b: Bounds, row: number, col: number): boolean {
  return row >= b.top && row <= b.bottom && col >= b.left && col <= b.right;
}

/** "A1", "A1:B2", "$A$1:$B$2" → Bounds normalizado; null se não for um intervalo A1. */
export function parseRange(text: string): Bounds | null {
  const m = /^\$?([A-Za-z]{1,3})\$?(\d+)(?::\$?([A-Za-z]{1,3})\$?(\d+))?$/.exec(text.trim());
  if (!m) return null;
  const c1 = colToNum(m[1]);
  const r1 = Number(m[2]);
  const c2 = m[3] ? colToNum(m[3]) : c1;
  const r2 = m[4] ? Number(m[4]) : r1;
  return {
    top: Math.min(r1, r2),
    bottom: Math.max(r1, r2),
    left: Math.min(c1, c2),
    right: Math.max(c1, c2)
  };
}

/** "Aba!$A$1:$B$5" ou "'Minha Aba'!A1" → { sheet, bounds }. */
export function parseSheetRange(text: string): { sheet: string; bounds: Bounds } | null {
  const bang = text.lastIndexOf('!');
  if (bang < 0) return null;
  let sheet = text.slice(0, bang);
  if (sheet.startsWith("'") && sheet.endsWith("'")) sheet = sheet.slice(1, -1).replace(/''/g, "'");
  const bounds = parseRange(text.slice(bang + 1));
  return bounds ? { sheet, bounds } : null;
}
