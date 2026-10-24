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
  it('existem os doze projetos, do simples ao complexo', () => {
    expect(dirs).toEqual(['01-svg-agente', '02-triagem-de-suporte', '03-atendimento-ilustrado', '04-memoria', '05-matematica-com-memoria', '06-chamada-agente', '07-interacao', '08-formulario-gerado', '09-chamadas-paralelas', '10-subflow-chamavel', '11-call-vertex', '12-chamadas-assincronas']);
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
        expect(graph.getAllNodes().length).toBeGreaterThanOrEqual(3);
      });

      it('toda skill referenciada existe, e todo servidor MCP usado está configurado no projeto', () => {
        for (const node of schema.nodes) {
          const skill = node.agentSettings?.model?.skillPath;
          if (skill) expect(existsSync(join(EXAMPLES, name, skill)), `${node.id}: ${skill}`).toBe(true);
          if (node.type === 'mcp') {
            expect(node.mcpSettings.server, `${node.id}: servidor`).not.toBe('');
            expect(node.mcpSettings.tool, `${node.id}: ferramenta`).not.toBe('');
            if (!node.mcpSettings.server.startsWith('arestrix.')) {   // servidores nativos nao tem configuracao
              expect(existsSync(join(EXAMPLES, name, '.workspace', 'mcps', `${node.mcpSettings.server}.json`)), `${node.id}: servidor ${node.mcpSettings.server}`).toBe(true);
            }
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

  it('todo exemplo entrega seus produtos por vértices de saída (output)', () => {
    for (const name of dirs) {
      const nodes = read(name, 'pipelines', 'main.json').nodes;
      const outputs = nodes.filter((n: any) => n.type === 'output');
      expect(outputs.length, `${name}: sem vértice de saída`).toBeGreaterThan(0);
      for (const node of outputs) {
        const settings = node.outputSettings ?? {};
        expect(settings.sourcePath, `${name}/${node.id}: origem`).toBeTruthy();
        expect(settings.destinationPath, `${name}/${node.id}: destino`).toBeTruthy();
        expect(settings.destinationPath.startsWith('exports/'), `${name}/${node.id}: destino em exports/`).toBe(true);
        expect(settings.artifactType, `${name}/${node.id}: formato`).toBeTruthy();
        expect(node.inputs.some((p: any) => p.name === 'data_in'), `${name}/${node.id}: porta de dados`).toBe(true);
      }
    }
  });

  it('o exemplo 2 entrega a resposta do chamado por cada ramo', () => {
    const outputs = read('02-triagem-de-suporte', 'pipelines', 'main.json').nodes.filter((n: any) => n.type === 'output');
    expect(outputs.map((n: any) => n.outputSettings.destinationPath).sort()).toEqual([
      'exports/resposta-duvida.md',
      'exports/resposta-normal.md',
      'exports/resposta-urgente.md',
    ]);
  });

  it('o exemplo 4 exercita a memória de projeto de ponta a ponta', () => {
    const nodes = read('04-memoria', 'pipelines', 'main.json').nodes;
    const seed = nodes.find((n: any) => n.id === 'semear');
    const agent = nodes.find((n: any) => n.id === 'agente');
    const delivery = nodes.find((n: any) => n.id === 'entrega');
    expect(seed.customLambdaBody).toContain('recordLesson');
    expect(agent.agentSettings.memory).toMatchObject({
      enabled: true,
      persistent: true,
      readBlackboard: true,
      writeBlackboard: true,
      semanticLessons: true,
      recordLessons: true,
      allowedKeys: ['resumo'],
      domains: ['memoria'],
    });
    expect(delivery.type).toBe('output');
    expect(delivery.outputSettings.destinationPath).toBe('exports/resposta.md');
  });

  it('o exemplo 5 usa o vértice MCP Keenable e a memória na redação', () => {
    const nodes = read('05-matematica-com-memoria', 'pipelines', 'main.json').nodes;
    const research = nodes.find((n: any) => n.id === 'pesquisar');
    const writer = nodes.find((n: any) => n.id === 'redigir');
    expect(research.type).toBe('mcp');
    expect(research.mcpSettings).toMatchObject({ server: 'keenable', tool: 'search_web_pages' });
    expect(writer.agentSettings.memory).toMatchObject({
      enabled: true,
      persistent: true,
      domains: ['matematica'],
      contextBudgetTokens: 600,
    });
    const keenable = read('05-matematica-com-memoria', '.workspace', 'mcps', 'keenable.json');
    expect(keenable.transport).toBe('http');
    expect(keenable.url).toBe('https://api.keenable.ai/mcp');
    expect(keenable.headers['X-API-Key']).toBe('${KEENABLE_API}');
  });

  it('o exemplo 3 é a composição dos exemplos 1 e 2', () => {
    const ids = (n: string) => read(n, 'pipelines', 'main.json').nodes.map((x: any) => x.id);
    const three = ids('03-atendimento-ilustrado');
    for (const id of ids('01-svg-agente')) expect(three).toContain(`svg_${id}`);
    for (const id of ids('02-triagem-de-suporte')) expect(three).toContain(`atd_${id}`);
  });
});
