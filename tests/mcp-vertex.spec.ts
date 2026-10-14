import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { GraphvizGenerator } from '../src/core/GraphvizGenerator.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { UpdateNodeCommand } from '../src/core/history/GraphCommands.ts';
import { defaultMcpSettings } from '../src/core/types.ts';

describe('Vértice MCP', () => {
  it('nasce sem código, com uma porta de saída e configurações vazias para completar', () => {
    const g = new Graph(new EventBus());
    const n = g.createNode('m', 'Consulta', 'mcp', 0, 0);
    expect(n.customLambdaBody).toBe('');                      // o gerador chama o cliente MCP nativo
    expect(n.outputs).toHaveLength(1);
    expect(n.mcpSettings).toEqual(defaultMcpSettings());
    expect(n.mcpSettings!.source.kind).toBe('previous');
    expect(g.createNode('x', 'X', 'static', 0, 0).mcpSettings).toBeUndefined();
  });

  it('as configurações sobrevivem ao JSON e a edição por comando é desfazível', () => {
    const g = new Graph(new EventBus());
    const n = g.createNode('m', 'Consulta', 'mcp', 0, 0);
    const old = JSON.parse(JSON.stringify(n.mcpSettings));
    const next = { server: 'clientes', tool: 'lookup_customer', arguments: { email: '{{input.email}}' },
                   source: { kind: 'file' as const, path: 'workspace/cliente.json' }, output: { file: 'workspace/saida.json' } };
    g.commandManager.execute(new UpdateNodeCommand(g, 'm', { mcpSettings: old }, { mcpSettings: next }, 'mcp'));
    expect(g.getNode('m')!.mcpSettings!.tool).toBe('lookup_customer');

    const copy = new Graph(new EventBus());
    copy.fromJSON(JSON.parse(JSON.stringify(g.toJSON())));
    expect(copy.getNode('m')!.mcpSettings).toEqual(next);

    g.undo();
    expect(g.getNode('m')!.mcpSettings!.server).toBe('');
  });

  it('entra no grafo como qualquer vértice: valida, liga e aparece no Graphviz', () => {
    const g = new Graph(new EventBus());
    const a = g.createNode('a', 'A', 'static', 0, 0);
    const m = g.createNode('m', 'Consulta', 'mcp', 200, 0);
    g.createEdge(a.id, a.outputs[0].id, m.id, m.inputs[0].id);
    expect(GraphValidator.validate(g).isValid).toBe(true);
    expect(GraphvizGenerator.generateDot(g)).toContain('#0e7490');
  });
});

describe('Inspetor: vértice MCP', () => {
  const mount = async () => {
    const origDoc = (globalThis as any).document;
    const origWin = (globalThis as any).window;
    const createMockElement = (tag: string) => {
      const el: any = {
        tagName: tag.toUpperCase(), className: '', style: {}, children: [] as any[], _html: '',
        set innerHTML(val: string) { this._html = val; },
        get innerHTML() { return this._html; },
        querySelector: () => ({ tagName: 'DIV', value: '', textContent: '', style: {}, addEventListener: () => {},
                                removeEventListener: () => {}, classList: { add: () => {}, remove: () => {}, contains: () => false } }),
        querySelectorAll: () => [],
        addEventListener: () => {},
        classList: { add: () => {}, remove: () => {}, contains: () => false },
        appendChild: (c: any) => { el.children.push(c); return c; },
        insertBefore: (c: any) => { el.children.push(c); return c; }
      };
      return el;
    };
    (globalThis as any).document = { createElement: createMockElement, body: { style: {} } };
    (globalThis as any).window = { innerWidth: 1920, innerHeight: 1080, addEventListener: () => {}, removeEventListener: () => {} };
    const { Inspector } = await import('../src/ui/Inspector.ts');
    const bus = new EventBus();
    const graph = new Graph(bus);
    const inspector = new Inspector(createMockElement('div') as any, graph, bus);
    return { inspector, graph, bus, restore: () => { (globalThis as any).document = origDoc; (globalThis as any).window = origWin; } };
  };

  it('mostra servidor, ferramenta, argumentos, origem da entrada e arquivo de saída', async () => {
    const { inspector, graph, bus, restore } = await mount();
    try {
      const node = graph.buildNode('m1', 'Consulta', 'mcp', 0, 0);
      node.mcpSettings = { server: 'clientes', tool: 'lookup_customer', arguments: { email: '{{input.email}}' },
                           source: { kind: 'file', path: 'workspace/cliente.json' }, output: { file: 'workspace/r.json' } };
      graph.addNode(node);
      bus.emit('node:selected', node);
      const html = (inspector as any).container.innerHTML as string;

      expect(html).toContain('Ferramenta MCP');
      expect(html).toContain('id="mcp-server"');
      expect(html).toContain('value="clientes" selected');
      expect(html).toContain('value="lookup_customer" selected');
      expect(html).toContain('id="mcp-args"');
      expect(html).toContain('{{input.email}}');
      expect(html).toContain('id="mcp-source-path"');                 // origem = arquivo mostra o caminho
      expect(html).toContain('value="workspace/r.json"');
      expect(html).not.toContain('param-input');                        // sem a seção de parâmetros de template
    } finally { restore(); }
  });
});
