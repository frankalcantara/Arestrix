import { describe, it, expect } from 'vitest';
import { analyzeFormula } from '../src/core/excel/FormulaAnalyzer.ts';

describe('FormulaAnalyzer', () => {
  it('normaliza referências relativas e absolutas para R1C1', () => {
    const a = analyzeFormula('B4*$C$1', 4, 4);
    expect(a.r1c1).toBe('RC[-2]*R1C3');
    expect(a.refs.map(r => r.bounds)).toEqual([
      { top: 4, bottom: 4, left: 2, right: 2 },
      { top: 1, bottom: 1, left: 3, right: 3 }
    ]);
  });

  it('a mesma fórmula copiada para linhas diferentes gera o mesmo padrão', () => {
    expect(analyzeFormula('B4*C4', 4, 4).r1c1).toBe(analyzeFormula('B9*C9', 9, 4).r1c1);
  });

  it('trata intervalos e referências entre abas', () => {
    const a = analyzeFormula("SUM(Vendas!D4:D8)+'Minha Aba'!A1", 1, 2);
    expect(a.functions).toEqual(['SUM']);
    expect(a.refs[0]).toMatchObject({ sheet: 'Vendas', external: false, bounds: { top: 4, bottom: 8, left: 4, right: 4 } });
    expect(a.refs[1].sheet).toBe('Minha Aba');
    expect(a.r1c1).toBe("SUM(Vendas!R[3]C[2]:R[7]C[2])+'Minha Aba'!R[0]C[-1]".replace('R[0]', 'R'));
  });

  it('ignora texto entre aspas e não confunde funções com células', () => {
    const a = analyzeFormula('IF(A1="B2",LOG10(A1),0)', 1, 2);
    expect(a.refs).toHaveLength(2);
    expect(a.functions.sort()).toEqual(['IF', 'LOG10']);
  });

  it('detecta literais numéricos (exceto 0 e 1), nomes definidos e colunas inteiras', () => {
    const a = analyzeFormula('SUMIF(A:A,"x",B:B)*Taxa*1.18+1', 1, 3, new Set(['taxa']));
    expect(a.literals).toEqual([1.18]);
    expect(a.names).toEqual(['taxa']);
    expect(a.refs).toHaveLength(2);
    expect(a.refs[0].bounds).toMatchObject({ left: 1, right: 1, top: 1 });
  });

  it('marca referências externas e remove prefixo _xlfn.', () => {
    const a = analyzeFormula('_xlfn.XLOOKUP(A1,[1]Preços!A:A,[1]Preços!B:B)', 1, 2);
    expect(a.usesExternal).toBe(true);
    expect(a.functions).toEqual(['XLOOKUP']);
    expect(a.refs.filter(r => r.external)).toHaveLength(2);
  });
});

describe('FormulaAnalyzer — referências posicionais', () => {
  it('marca a base de OFFSET/ROW como posicional, mas não os outros argumentos', () => {
    const a = analyzeFormula('SUM(OFFSET(M10,-1,0),J10)+ROW(A1)', 10, 13);
    expect(a.refs.map(r => !!r.positional)).toEqual([true, false, true]);
  });
});
