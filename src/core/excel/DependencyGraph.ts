import { RegionId } from './WorkbookModel.ts';

/** Agrega dependências entre regiões: "to" lê de "from"; cada fórmula conta uma vez por par. */
export class RegionDependencies {
  private counts = new Map<string, { from: RegionId; to: RegionId; formulaCount: number }>();

  addFormula(to: RegionId, fromIds: Iterable<RegionId>): void {
    for (const from of new Set(fromIds)) {
      if (from === to) continue;
      const key = `${from}\u0000${to}`;
      const e = this.counts.get(key);
      if (e) e.formulaCount++;
      else this.counts.set(key, { from, to, formulaCount: 1 });
    }
  }

  toList(): { from: RegionId; to: RegionId; formulaCount: number }[] {
    return [...this.counts.values()].sort((a, b) => a.from.localeCompare(b.from) || a.to.localeCompare(b.to));
  }
}

/** Tarjan iterativo (sem recursão, seguro para grafos grandes). Devolve todas as componentes fortemente conexas. */
export function stronglyConnectedComponents(adj: number[][]): number[][] {
  const n = adj.length;
  const index = new Int32Array(n).fill(-1);
  const low = new Int32Array(n);
  const onStack = new Uint8Array(n);
  const edgePos = new Int32Array(n);
  const stack: number[] = [];
  const result: number[][] = [];
  let counter = 0;

  for (let s = 0; s < n; s++) {
    if (index[s] !== -1) continue;
    const calls = [s];
    index[s] = low[s] = counter++;
    stack.push(s);
    onStack[s] = 1;
    while (calls.length) {
      const v = calls[calls.length - 1];
      if (edgePos[v] < adj[v].length) {
        const w = adj[v][edgePos[v]++];
        if (index[w] === -1) {
          index[w] = low[w] = counter++;
          stack.push(w);
          onStack[w] = 1;
          calls.push(w);
        } else if (onStack[w]) {
          low[v] = Math.min(low[v], index[w]);
        }
      } else {
        if (low[v] === index[v]) {
          const comp: number[] = [];
          let w: number;
          do {
            w = stack.pop()!;
            onStack[w] = 0;
            comp.push(w);
          } while (w !== v);
          result.push(comp);
        }
        calls.pop();
        if (calls.length) {
          const p = calls[calls.length - 1];
          low[p] = Math.min(low[p], low[v]);
        }
      }
    }
  }
  return result;
}

/** Componentes que são ciclos de verdade: mais de um nó, ou um nó que aponta para si mesmo. */
export function findCycles(adj: number[][]): number[][] {
  return stronglyConnectedComponents(adj)
    .filter(c => c.length > 1 || adj[c[0]].includes(c[0]))
    .map(c => c.sort((a, b) => a - b))
    .sort((a, b) => a[0] - b[0]);
}
