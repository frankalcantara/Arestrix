import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { McpToolPicker } from '../src/ui/McpToolPicker.ts';

// O seletor e logica pura sobre um elemento raiz; basta um elemento falso e um backend simulado.
function fakeRoot(): any {
  return { innerHTML: '', querySelectorAll: () => [] };
}

const SERVERS = [
  { id: 'arestrix.dados', name: 'Dados', isEnabled: true, isNative: true },
  { id: 'clientes', name: 'Clientes', isEnabled: true }
];
const TOOLS: Record<string, Array<{ name: string; description: string }>> = {
  'arestrix.dados': [{ name: 'json_query', description: '' }, { name: 'regex_extract', description: '' }, { name: 'base64_encode', description: '' }],
  clientes: [{ name: 'lookup_customer', description: '' }]
};

describe('McpToolPicker', () => {
  let originalWindow: any;

  beforeEach(() => {
    originalWindow = (globalThis as any).window;
    (globalThis as any).window = {
      webui: {
        call: async (name: string, arg?: string) => {
          if (name === 'list_project_mcp_servers') return JSON.stringify({ servers: SERVERS });
          if (name === 'list_project_mcp_tools') return JSON.stringify({ tools: TOOLS[JSON.parse(arg || '{}').server] || [] });
          return '{}';
        }
      }
    };
  });

  afterEach(() => {
    (globalThis as any).window = originalWindow;
  });

  it('segue a mesma regra do runtime para permitir ferramentas', () => {
    expect(McpToolPicker.isToolAllowed([], 'a', 'x')).toBe(true);
    expect(McpToolPicker.isToolAllowed(['x'], 'a', 'x')).toBe(true);
    expect(McpToolPicker.isToolAllowed(['x'], 'a', 'y')).toBe(false);
    expect(McpToolPicker.isToolAllowed(['a/x'], 'a', 'x')).toBe(true);
    expect(McpToolPicker.isToolAllowed(['a/x'], 'b', 'x')).toBe(false);
    expect(McpToolPicker.isToolAllowed(['a/*'], 'a', 'qualquer')).toBe(true);
    expect(McpToolPicker.isToolAllowed(['a/*'], 'b', 'qualquer')).toBe(false);
  });

  it('lista os servidores, nativos marcados, e devolve a configuracao recebida sem mudancas', async () => {
    const root = fakeRoot();
    const picker = new McpToolPicker(root);
    await picker.load({ servers: ['arestrix.dados'], tools: [] }, false);
    expect(root.innerHTML).toContain('⚡ Dados');
    expect(root.innerHTML).toContain('Clientes');
    expect(picker.getValue()).toEqual({ servers: ['arestrix.dados'], tools: [] });
  });

  it('sem servidor escolhido nao devolve nada', async () => {
    const picker = new McpToolPicker(fakeRoot());
    await picker.load({ servers: [], tools: [] }, false);
    expect(picker.getValue()).toEqual({ servers: [], tools: [] });
  });

  it('preserva a restricao ja configurada de um servidor cujas ferramentas nao foram abertas', async () => {
    const picker = new McpToolPicker(fakeRoot());
    await picker.load({ servers: ['arestrix.dados', 'clientes'], tools: ['arestrix.dados/regex_extract', 'clientes/*'] }, false);
    const v = picker.getValue();
    expect(v.servers).toEqual(['arestrix.dados', 'clientes']);
    expect(v.tools.sort()).toEqual(['arestrix.dados/regex_extract', 'clientes/*']);
  });

  it('um servidor recem-marcado entra inteiro quando outro esta restrito', async () => {
    const picker = new McpToolPicker(fakeRoot());
    await picker.load({ servers: ['arestrix.dados'], tools: ['arestrix.dados/json_query'] }, false);
    (picker as any).selected.add('clientes');
    const v = picker.getValue();
    expect(v.servers.sort()).toEqual(['arestrix.dados', 'clientes']);
    expect(v.tools.sort()).toEqual(['arestrix.dados/json_query', 'clientes/*']);
  });

  it('ao abrir as ferramentas, desmarcar uma gera a lista restrita; remarcar tudo volta a "todas"', async () => {
    const picker = new McpToolPicker(fakeRoot());
    await picker.load({ servers: ['arestrix.dados'], tools: [] }, false);
    await (picker as any).openTools('arestrix.dados');
    const checked: Set<string> = (picker as any).checkedTools.get('arestrix.dados');
    expect(checked.size).toBe(3);                                  // lista vazia = todas marcadas
    expect(picker.getValue().tools).toEqual([]);
    checked.delete('base64_encode');
    expect(picker.getValue().tools.sort()).toEqual(['arestrix.dados/json_query', 'arestrix.dados/regex_extract']);
    checked.add('base64_encode');
    expect(picker.getValue().tools).toEqual([]);
  });

  it('abre as ferramentas respeitando a restricao antiga em formato simples (so o nome)', async () => {
    const picker = new McpToolPicker(fakeRoot());
    await picker.load({ servers: ['arestrix.dados'], tools: ['regex_extract'] }, false);
    await (picker as any).openTools('arestrix.dados');
    expect(Array.from((picker as any).checkedTools.get('arestrix.dados'))).toEqual(['regex_extract']);
  });

  it('sem o aplicativo (navegador), avisa em vez de falhar', async () => {
    (globalThis as any).window = {};
    const root = fakeRoot();
    const picker = new McpToolPicker(root);
    await picker.load({ servers: [], tools: [] }, false);
    expect(root.innerHTML).toContain('form-hint');
  });
});
