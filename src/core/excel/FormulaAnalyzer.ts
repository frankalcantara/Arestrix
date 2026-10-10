import { Bounds, MAX_ROW, colToNum } from './addressing.ts';

export interface FormulaRef {
  sheet: string | null; // null = mesma aba da fórmula
  external: boolean; // aponta para outro arquivo ([n]Aba!A1)
  externalIndex?: number; // o n de [n], quando external
  positional?: boolean; // só marca posição (OFFSET(A1,..), ROW(A1)): não lê o valor da célula
  bounds: Bounds;
}

export interface FormulaAnalysis {
  refs: FormulaRef[];
  functions: string[]; // maiúsculas, sem prefixo _xlfn., sem repetição
  names: string[]; // nomes definidos usados (minúsculas)
  literals: number[]; // literais numéricos diferentes de 0 e 1
  r1c1: string; // fórmula normalizada para R1C1
  usesExternal: boolean;
}

const COORD = String.raw`(?<c1>\$?[A-Z]{1,3})(?<r1>\$?\d+)(?::(?<c2>\$?[A-Z]{1,3})(?<r2>\$?\d+))?|(?<wc1>\$?[A-Z]{1,3}):(?<wc2>\$?[A-Z]{1,3})`;

const TOKEN = new RegExp(
  [
    String.raw`(?<str>"(?:[^"]|"")*")`,
    String.raw`(?<![\p{L}\p{N}_.])(?:(?:\[(?<ext>\d+)\])?(?:'(?<qs>(?:[^']|'')+)'|(?<us>[\p{L}_][\p{L}\p{N}_.]*))!)?(?:${COORD})(?![\p{L}\p{N}_(])`,
    String.raw`(?<fn>[\p{L}_][\p{L}\p{N}_.]*)(?=\()`,
    String.raw`(?<![\p{L}\p{N}_.])(?<nm>[\p{L}_\\][\p{L}\p{N}_.]*)(?![\p{L}\p{N}_(!.])`,
    String.raw`(?<![\p{L}\p{N}_.$])(?<num>\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)(?![\p{L}\p{N}_(])`
  ].join('|'),
  'gu'
);

/** Referência logo após "OFFSET(", "ROW("…: usada só pela posição. */
const POSITIONAL_CALL = /(?:^|[^\p{L}\p{N}_.])(?:OFFSET|ROW|COLUMN|ROWS|COLUMNS)\($/iu;

function stripDollar(s: string): string {
  return s.replace('$', '');
}

/** Um eixo em R1C1: absoluto → R5, relativo → R[-1], mesma linha/coluna → R. */
function axis(letter: 'R' | 'C', token: string, n: number, current: number): string {
  if (token.startsWith('$')) return `${letter}${n}`;
  return n === current ? letter : `${letter}[${n - current}]`;
}

export function analyzeFormula(
  formula: string,
  row: number,
  col: number,
  definedNames?: ReadonlySet<string>
): FormulaAnalysis {
  const text = formula.startsWith('=') ? formula.slice(1) : formula;
  const refs: FormulaRef[] = [];
  const functions = new Set<string>();
  const names = new Set<string>();
  const literals: number[] = [];
  let usesExternal = false;
  let out = '';
  let last = 0;

  TOKEN.lastIndex = 0;
  for (let m = TOKEN.exec(text); m !== null; m = TOKEN.exec(text)) {
    const g = m.groups!;
    let replacement = m[0];

    if (g.c1 !== undefined || g.wc1 !== undefined) {
      const ext = g.ext !== undefined;
      const sheet = g.qs !== undefined ? g.qs.replace(/''/g, "'") : (g.us ?? null);
      const prefix = (ext ? `[${g.ext}]` : '') + (g.qs !== undefined ? `'${g.qs}'` : (g.us ?? '')) + (sheet !== null ? '!' : '');
      let bounds: Bounds;
      let coords: string;
      if (g.c1 !== undefined) {
        const c1 = colToNum(stripDollar(g.c1));
        const r1 = Number(stripDollar(g.r1));
        const c2 = g.c2 !== undefined ? colToNum(stripDollar(g.c2)) : c1;
        const r2 = g.r2 !== undefined ? Number(stripDollar(g.r2)) : r1;
        bounds = { top: Math.min(r1, r2), bottom: Math.max(r1, r2), left: Math.min(c1, c2), right: Math.max(c1, c2) };
        const a = axis('R', g.r1, r1, row) + axis('C', g.c1, c1, col);
        coords = g.c2 !== undefined ? `${a}:${axis('R', g.r2, r2, row)}${axis('C', g.c2, c2, col)}` : a;
      } else {
        const c1 = colToNum(stripDollar(g.wc1));
        const c2 = colToNum(stripDollar(g.wc2));
        bounds = { top: 1, bottom: MAX_ROW, left: Math.min(c1, c2), right: Math.max(c1, c2) };
        coords = `${axis('C', g.wc1, c1, col)}:${axis('C', g.wc2, c2, col)}`;
      }
      const ref: FormulaRef = ext ? { sheet, external: true, externalIndex: Number(g.ext), bounds } : { sheet, external: false, bounds };
      if (POSITIONAL_CALL.test(text.slice(Math.max(0, m.index - 8), m.index))) ref.positional = true;
      refs.push(ref);
      if (ext) usesExternal = true;
      replacement = prefix + coords;
    } else if (g.fn !== undefined) {
      functions.add(g.fn.replace(/^_xl(?:fn|ws)\./i, '').toUpperCase());
    } else if (g.nm !== undefined) {
      const key = g.nm.toLowerCase();
      if (definedNames?.has(key)) names.add(key);
    } else if (g.num !== undefined) {
      const v = Number(g.num);
      if (v !== 0 && v !== 1) literals.push(v);
    }

    out += text.slice(last, m.index) + replacement;
    last = m.index + m[0].length;
  }
  out += text.slice(last);

  return { refs, functions: [...functions], names: [...names], literals, r1c1: out, usesExternal };
}
