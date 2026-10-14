import { describe, it, expect } from 'vitest';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { EventBus } from '../src/core/EventBus.ts';

const EXAMPLES = resolve(__dirname, '..', 'examples');
const dirs = readdirSync(EXAMPLES, { withFileTypes: true }).filter(d => d.isDirectory()).map(d => d.name);
const read = (...p: string[]) => JSON.parse(readFileSync(join(EXAMPLES, ...p), 'utf8'));

describe('projetos de exemplo (examples/)', () => {
  it('existem os três projetos, do simples ao complexo', () => {
    expect(dirs).toEqual(['01-svg-agente', '02-triagem-de-suporte', '03-atendimento-ilustrado']);
  });

  for (const name of dirs) {
    describe(name, () => {
      const manifest = read(name, 'arestrix.project.json');
      const schema = read(name, 'pipelines', 'main.json');

      it('o manifesto é portátil, sem segredos e aponta para um pipeline existente', () => {
        expect(manifest.rootPath).toBe('.');
        expect(manifest.environment).toEqual({});
        expect(manifest.pipelines[0].file).toBe('pipelines/main.json');
        expect(existsSync(join(EXAMPLES, name, manifest.pipelines[0].file))).toBe(true);
        // o modelo vem só como provedor/nome: nenhuma chave
        expect(Object.keys(manifest.globalDefaults.model).sort()).toEqual(['model', 'promptTemplate', 'provider', 'skillPath']);
      });

      it('o grafo carrega e passa na validação (sem erros)', () => {
        const graph = new Graph(new EventBus());
        graph.fromJSON(schema);
        const result = GraphValidator.validate(graph);
        const errors = result.errors.filter((e: any) => e.severity === 'error');
        expect(errors, JSON.stringify(errors)).toEqual([]);
        expect(graph.getAllNodes().length).toBeGreaterThan(4);
      });

      it('toda skill referenciada existe, e todo servidor MCP usado está configurado no projeto', () => {
        for (const node of schema.nodes) {
          const skill = node.agentSettings?.model?.skillPath;
          if (skill) expect(existsSync(join(EXAMPLES, name, skill)), `${node.id}: ${skill}`).toBe(true);
          if (node.type === 'mcp') {
            expect(node.mcpSettings.server, `${node.id}: servidor`).not.toBe('');
            expect(node.mcpSettings.tool, `${node.id}: ferramenta`).not.toBe('');
            expect(existsSync(join(EXAMPLES, name, '.workspace', 'mcps', `${node.mcpSettings.server}.json`)), `${node.id}: servidor ${node.mcpSettings.server}`).toBe(true);
          }
          for (const server of node.agentSettings?.codeMode?.mcpServers ?? []) {
            const cfg = join(EXAMPLES, name, '.workspace', 'mcps', `${server}.json`);
            expect(existsSync(cfg), `${node.id}: servidor MCP ${server}`).toBe(true);
            expect(read(name, '.workspace', 'mcps', `${server}.json`).id).toBe(server);
          }
        }
      });

      it('toda aresta liga portas que existem', () => {
        const ports = new Map<string, string>();
        for (const n of schema.nodes) for (const p of [...n.inputs, ...n.outputs]) ports.set(p.id, n.id);
        for (const e of schema.edges) {
          expect(ports.get(e.sourcePortId), `${e.id}: origem`).toBe(e.sourceNodeId);
          expect(ports.get(e.targetPortId), `${e.id}: destino`).toBe(e.targetNodeId);
        }
      });
    });
  }

  it('os vértices switch dos exemplos são configuráveis sem código', () => {
    const triagem = read('02-triagem-de-suporte', 'pipelines', 'main.json').nodes;
    const ai = triagem.find((n: any) => n.id === 'triagem');
    const rules = triagem.find((n: any) => n.id === 'qualidade');
    expect(ai.type).toBe('switch');
    expect(ai.switchSettings.mode).toBe('model');
    expect(ai.outputs.map((p: any) => p.name)).toEqual(['urgente', 'normal', 'duvida', 'revisao_humana']);
    expect(rules.switchSettings.mode).toBe('rules');
    // "ajustar" vem antes de "aprovado": "STATUS: AJUSTAR ... nao aprovado" nunca pode ser lido como aprovação
    expect(rules.outputs.map((p: any) => p.name).slice(0, 2)).toEqual(['ajustar', 'aprovado']);
  });

  it('o exemplo 3 é a composição dos exemplos 1 e 2', () => {
    const ids = (n: string) => read(n, 'pipelines', 'main.json').nodes.map((x: any) => x.id);
    const three = ids('03-atendimento-ilustrado');
    for (const id of ids('01-svg-agente')) expect(three).toContain(`svg_${id}`);
    for (const id of ids('02-triagem-de-suporte').filter((i: string) => i !== 'entrega_faq')) expect(three).toContain(`atd_${id}`);
  });
});
