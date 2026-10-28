import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { ProjectManager, classifyOpenedFile, uint8ArrayToBase64 } from '../src/core/ProjectManager.ts';
import { baseName, dirName, joinPath, pathCrumbs, stripExtension } from '../src/core/paths.ts';
import { BackstageView, recencyGroup } from '../src/ui/ribbon/BackstageView.ts';
import { IGraphSchema, IOpenableListing } from '../src/core/types.ts';
import { setLocale, t } from '../src/i18n/index.ts';

type Call = [string, any];

function graphSchema(nodeId: string): IGraphSchema {
  const g = new Graph(new EventBus());
  g.addNode({ id: nodeId, type: 'agent', label: `Vertice ${nodeId}`, position: { x: 0, y: 0 }, inputs: [], outputs: [] } as any);
  return g.getRootSchema();
}

function base64Of(text: string): string {
  return uint8ArrayToBase64(new TextEncoder().encode(text));
}

describe('Tela Abrir - utilitarios de caminho', () => {
  it('extrai nome, pasta e extensao com barras normais e invertidas', () => {
    expect(baseName('C:/a/b.arestrix')).toBe('b.arestrix');
    expect(baseName('C:\\a\\pipelines\\')).toBe('pipelines');
    expect(dirName('C:/a/b')).toBe('C:/a');
    expect(dirName('C:/a')).toBe('C:/');
    expect(dirName('C:\\a\\b.arestrix')).toBe('C:\\a');
    expect(dirName('/home')).toBe('/');
    expect(dirName('solto')).toBe('');
    expect(joinPath('C:/', 'x')).toBe('C:/x');
    expect(joinPath('C:/a', 'x')).toBe('C:/a/x');
    expect(stripExtension('pacote.arestrix')).toBe('pacote');
    expect(stripExtension('.oculto')).toBe('.oculto');
  });

  it('gera as migalhas clicaveis do caminho', () => {
    expect(pathCrumbs('C:/Projetos/Arestrix')).toEqual([
      { label: 'C:', path: 'C:/' },
      { label: 'Projetos', path: 'C:/Projetos' },
      { label: 'Arestrix', path: 'C:/Projetos/Arestrix' }
    ]);
    expect(pathCrumbs('C:\\')).toEqual([{ label: 'C:', path: 'C:/' }]);
    expect(pathCrumbs('/home/user')).toEqual([
      { label: '/', path: '/' },
      { label: 'home', path: '/home' },
      { label: 'user', path: '/home/user' }
    ]);
  });

  it('agrupa os recentes por data como no Abrir do Office', () => {
    const now = new Date(2026, 9, 16, 12, 0);
    expect(recencyGroup(new Date(2026, 9, 16, 8).toISOString(), now)).toBe('today');
    expect(recencyGroup(new Date(2026, 9, 15, 23).toISOString(), now)).toBe('yesterday');
    expect(recencyGroup(new Date(2026, 9, 12).toISOString(), now)).toBe('last_week');
    expect(recencyGroup(new Date(2026, 9, 1).toISOString(), now)).toBe('last_month');
    expect(recencyGroup(new Date(2026, 7, 1).toISOString(), now)).toBe('older');
    expect(recencyGroup(undefined, now)).toBe('older');
    expect(recencyGroup('data-invalida', now)).toBe('older');
  });
});

describe('Tela Abrir - abrir projetos, pacotes e grafos do disco', () => {
  let bus: EventBus;
  let pm: ProjectManager;
  let graph: Graph;
  let calls: Call[];
  let originalWindow: any;

  function mockBridge(handler: (method: string, arg: any) => any): void {
    calls = [];
    (globalThis as any).window = {
      webui: {
        call: async (method: string, arg?: any) => {
          calls.push([method, arg]);
          return handler(method, arg);
        }
      }
    };
  }

  function called(method: string): any[] {
    return calls.filter(c => c[0] === method).map(c => c[1]);
  }

  beforeEach(() => {
    originalWindow = (globalThis as any).window;
    bus = new EventBus();
    pm = ProjectManager.getInstance(bus);
    pm.resetForTesting(bus);
    graph = new Graph(bus);
    calls = [];
  });

  afterEach(() => {
    (globalThis as any).window = originalWindow;
  });

  it('classifica o conteudo: pacote gzip, manifesto, grafo e arquivo desconhecido', async () => {
    const bundle = pm.exportArestrixBundle(graph);
    expect((await classifyOpenedFile(await pm.compressArestrixBundle(bundle))).type).toBe('package');
    const encode = (o: any) => new TextEncoder().encode(JSON.stringify(o));
    expect((await classifyOpenedFile(encode({ projectId: 'p', pipelines: [] }))).type).toBe('manifest');
    expect((await classifyOpenedFile(encode(graphSchema('a')))).type).toBe('graph');
    expect((await classifyOpenedFile(encode({ qualquer: 1 }))).type).toBe('unknown');
    expect((await classifyOpenedFile(new TextEncoder().encode('nao e json'))).type).toBe('unknown');
  });

  it('abre uma pasta de projeto e um arestrix.project.json pelo load_project', async () => {
    mockBridge((method) => {
      if (method === 'load_project') return true;
      if (method === 'get_current_project') {
        return JSON.stringify({ ...pm.createDefaultManifest(), projectId: 'meu', name: 'Meu', rootPath: 'C:/Proj/Meu' });
      }
      return '';
    });

    expect(await pm.openFromDisk('C:/Proj/Meu', graph)).toEqual({ ok: true, kind: 'project', path: 'C:/Proj/Meu' });
    expect(await pm.openFromDisk('C:/Proj/Outro/arestrix.project.json', graph)).toMatchObject({ ok: true, kind: 'project' });
    expect(called('load_project')).toEqual(['C:/Proj/Meu', 'C:/Proj/Outro/arestrix.project.json']);
    expect(called('load_arestrix_file')).toEqual([]);
  });

  it('informa falha quando a pasta nao e um projeto', async () => {
    mockBridge((method) => (method === 'load_project' ? false : ''));
    expect(await pm.openFromDisk('C:/Qualquer/Pasta', graph)).toEqual({ ok: false, reason: 'invalid' });
  });

  it('extrai um pacote .arestrix numa pasta nova ao lado dele, sem sobrescrever a existente', async () => {
    const source = new Graph(new EventBus());
    source.addNode({ id: 'n1', type: 'agent', label: 'Agente', position: { x: 0, y: 0 }, inputs: [], outputs: [] } as any);
    const bundle = pm.exportArestrixBundle(source);
    bundle.manifest = { ...bundle.manifest, projectId: 'pkg-id', name: 'Pacote Teste', rootPath: 'C:/Antigo' };
    const b64 = uint8ArrayToBase64(await pm.compressArestrixBundle(bundle));

    mockBridge((method) => {
      switch (method) {
        case 'load_arestrix_file': return JSON.stringify({ success: true, base64: b64 });
        case 'list_openable_entries':
          return JSON.stringify({
            path: 'C:/Projetos', parent: 'C:/', exists: true, truncated: false,
            entries: [
              { name: 'pacote', path: 'C:/Projetos/pacote', kind: 'project' },
              { name: 'Pacote.arestrix', path: 'C:/Projetos/Pacote.arestrix', kind: 'package' }
            ]
          });
        case 'create_project': return true;
        case 'save_pipeline': return true;
      }
      return '';
    });

    const res = await pm.openFromDisk('C:/Projetos/Pacote.arestrix', graph);
    expect(res).toEqual({ ok: true, kind: 'package', path: 'C:/Projetos/Pacote (2)' });
    expect(called('list_openable_entries')).toEqual(['C:/Projetos']);

    const created = JSON.parse(called('create_project')[0]);
    expect(created.rootPath).toBe('C:/Projetos/Pacote (2)');
    expect(created.projectId).toBe('pkg-id');
    expect(created.name).toBe('Pacote Teste');

    const saved = JSON.parse(called('save_pipeline')[0]);
    expect(saved.nodes.map((n: any) => n.id)).toEqual(['n1']);
    expect(graph.getAllNodes().map(n => n.id)).toEqual(['n1']);
    expect(pm.getCurrentProject().rootPath).toBe('C:/Projetos/Pacote (2)');
  });

  it('um grafo dentro de <projeto>/pipelines abre o projeto inteiro', async () => {
    mockBridge((method, arg) => {
      if (method === 'load_arestrix_file') return JSON.stringify({ success: true, base64: base64Of(JSON.stringify(graphSchema('g1'))) });
      if (method === 'load_project') return arg === 'C:/Proj/Meu';
      if (method === 'get_current_project') {
        return JSON.stringify({ ...pm.createDefaultManifest(), projectId: 'meu', name: 'Meu', rootPath: 'C:/Proj/Meu' });
      }
      return '';
    });

    const res = await pm.openFromDisk('C:/Proj/Meu/pipelines/main.json', graph);
    expect(res).toEqual({ ok: true, kind: 'project', path: 'C:/Proj/Meu' });
    expect(called('load_project')).toEqual(['C:/Proj/Meu']);
  });

  it('um grafo avulso so substitui o canvas com confirmacao', async () => {
    mockBridge((method) => (method === 'load_arestrix_file'
      ? JSON.stringify({ success: true, base64: base64Of(JSON.stringify(graphSchema('solto'))) })
      : ''));

    const asked: string[] = [];
    const refused = await pm.openFromDisk('C:/Downloads/grafo.json', graph, {
      confirmLooseGraph: (name) => { asked.push(name); return false; }
    });
    expect(refused).toEqual({ ok: false, reason: 'cancelled' });
    expect(graph.getAllNodes()).toHaveLength(0);
    expect(asked).toEqual(['grafo.json']);
    expect(called('load_project')).toEqual([]);

    const accepted = await pm.openFromDisk('C:/Downloads/grafo.json', graph, { confirmLooseGraph: () => true });
    expect(accepted).toEqual({ ok: true, kind: 'graph', path: 'grafo.json' });
    expect(graph.getAllNodes().map(n => n.id)).toEqual(['solto']);
  });

  it('rejeita arquivos .json que nao sao do Arestrix', async () => {
    mockBridge((method) => (method === 'load_arestrix_file'
      ? JSON.stringify({ success: true, base64: base64Of('{"nome": "planilha"}') })
      : ''));
    expect(await pm.openFromDisk('C:/x/dados.json', graph)).toEqual({ ok: false, reason: 'invalid' });
  });

  it('no navegador sem backend abre pacotes pelos bytes e recusa manifestos', async () => {
    const source = new Graph(new EventBus());
    source.addNode({ id: 'b1', type: 'agent', label: 'B', position: { x: 0, y: 0 }, inputs: [], outputs: [] } as any);
    const gz = await pm.compressArestrixBundle(pm.exportArestrixBundle(source));

    expect(pm.hasNativeBridge()).toBe(false);
    expect((await pm.listOpenableEntries('C:/')).available).toBe(false);
    expect(await pm.pickFile({ title: 'x', filters: [] })).toBe('');

    expect(await pm.openFromBytes(gz, 'p.arestrix', graph)).toMatchObject({ ok: true, kind: 'project' });
    expect(graph.getAllNodes().map(n => n.id)).toEqual(['b1']);

    const manifest = new TextEncoder().encode(JSON.stringify({ projectId: 'p', pipelines: [] }));
    expect(await pm.openFromBytes(manifest, 'arestrix.project.json', graph)).toEqual({ ok: false, reason: 'unsupported' });
  });
});

describe('Tela Abrir - renderizacao no Backstage', () => {
  let bus: EventBus;
  let pm: ProjectManager;
  let graph: Graph;

  const recents = [
    { projectId: 'p1', name: 'Proj Novo', rootPath: 'C:/Novo', manifestPath: '', lastOpened: new Date().toISOString() },
    { projectId: 'p2', name: 'Proj Velho', rootPath: 'C:/Velho', manifestPath: '', lastOpened: '2020-01-01T00:00:00Z' }
  ];

  const listing: IOpenableListing = {
    path: 'C:/Projetos',
    parent: 'C:/',
    exists: true,
    truncated: false,
    available: true,
    entries: [
      { name: 'docs', path: 'C:/Projetos/docs', kind: 'folder', modified: Date.now() },
      { name: 'Arestrix', path: 'C:/Projetos/Arestrix', kind: 'project', modified: Date.now() },
      { name: 'backup.arestrix', path: 'C:/Projetos/backup.arestrix', kind: 'package', modified: Date.now() }
    ]
  };

  beforeEach(async () => {
    await setLocale('pt-BR');
    bus = new EventBus();
    pm = ProjectManager.getInstance(bus);
    pm.resetForTesting(bus);
    graph = new Graph(bus);
  });

  afterEach(async () => {
    await setLocale('pt-BR');
  });

  function renderOpen(setup?: (view: any) => void): string {
    const view: any = new BackstageView(graph, {} as any, {} as any);
    view.activeSection = 'open';
    setup?.(view);
    return view.renderSectionContent(pm.getCurrentProject(), recents);
  }

  it('mostra os locais, a busca e os recentes agrupados por data', () => {
    const html = renderOpen();
    expect(t('backstage.sidebar.open')).toBe('Abrir');
    expect(html).toContain('data-open-place="recent"');
    expect(html).toContain('data-open-place="computer"');
    expect(html).toContain('id="btn-open-browse"');
    expect(html).toContain('Este Computador');
    expect(html).toContain('Procurar um projeto ou grafo');
    expect(html).toContain('accept=".arestrix,.json"');
    expect(html).toContain('backstage-open-table-header');
    expect(html).toContain('backstage-open-table-row');
    for (const column of [
      t('backstage.open.col_name'),
      t('backstage.open.col_modified'),
      t('backstage.open.col_type'),
      t('backstage.open.col_size')
    ]) {
      expect(html).toContain(column);
    }

    const today = html.indexOf('>Hoje<');
    const older = html.indexOf('>Mais antigos<');
    expect(today).toBeGreaterThan(-1);
    expect(older).toBeGreaterThan(today);
    expect(html.indexOf('Proj Novo')).toBeLessThan(older);
    expect(html.indexOf('Proj Velho')).toBeGreaterThan(older);
  });

  it('usa somente textos do idioma ativo', async () => {
    await setLocale('en-US');
    const html = renderOpen();
    expect(t('backstage.sidebar.open')).toBe('Open');
    for (const text of ['This PC', 'Browse', 'Search for a project or graph', 'Today', 'Older', 'Projects', 'Graphs']) {
      expect(html).toContain(text);
    }
    for (const text of ['Este Computador', 'Procurar', 'Hoje', 'Mais antigos', 'Projetos', 'Grafos']) {
      expect(html).not.toContain(text);
    }
  });

  it('em Este Computador lista pastas para navegar e projetos ou pacotes para abrir', () => {
    const html = renderOpen(view => {
      view.openPlace = 'computer';
      view.openListing = listing;
      view.openDrives = ['C:\\', 'D:\\'];
    });

    expect(html).toContain('data-open-action="navigate" data-path="C:/Projetos/docs"');
    expect(html).toContain('data-open-action="disk" data-path="C:/Projetos/Arestrix"');
    expect(html).toContain('data-open-action="disk" data-path="C:/Projetos/backup.arestrix"');
    expect(html).toContain('Projeto Arestrix');
    expect(html).toContain('Pacote Arestrix (.arestrix)');
    // So a pasta de projeto tem o botao de entrar sem abrir
    expect(html.match(/class="backstage-open-enter"/g)).toHaveLength(1);
    // Migalhas e unidade ativa
    expect(html).toContain('data-open-navigate="C:/Projetos"');
    expect(html).toMatch(/backstage-open-drive active" data-open-navigate="C:\/"/);
    expect(html).not.toMatch(/backstage-open-drive active" data-open-navigate="D:\/"/);
    expect(html).toContain('Filtrar esta pasta');
  });

  it('avisa quando a navegacao no disco nao esta disponivel e quando a pasta nao existe', () => {
    const unavailable = renderOpen(view => {
      view.openPlace = 'computer';
      view.openListing = { ...listing, available: false, entries: [] };
    });
    expect(unavailable).toContain(t('backstage.open.desktop_only'));

    const missing = renderOpen(view => {
      view.openPlace = 'computer';
      view.openListing = { ...listing, exists: false, entries: [], path: 'C:/Sumiu' };
    });
    expect(missing).toContain('Pasta não encontrada: C:/Sumiu');
    expect(missing).toContain('id="btn-open-up"');
  });
});
