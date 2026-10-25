import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { ImportPiModal } from '../src/ui/modals/ImportPiModal.ts';
import { ImportExcelModal } from '../src/ui/modals/ImportExcelModal.ts';
import { ImportClaudeModal } from '../src/ui/modals/ImportClaudeModal.ts';
import { ImportOpenAiModal } from '../src/ui/modals/ImportOpenAiModal.ts';
import { ImportHermesModal } from '../src/ui/modals/ImportHermesModal.ts';
import { OfficeRibbon } from '../src/ui/ribbon/OfficeRibbon.ts';

class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  type: string = '';
  title: string = '';
  checked: boolean = false;
  private _textContent: string = '';

  get textContent(): string {
    if (this._textContent) return this._textContent;
    if (this.children.length > 0) {
      return this.children.map(c => c.textContent).filter(Boolean).join(' ').trim();
    }
    return '';
  }

  set textContent(val: string) {
    this._textContent = val;
  }
  style: Record<string, string> = {};
  children: MockElement[] = [];
  parentNode: MockElement | null = null;
  attributes: Record<string, string> = {};
  private _html: string = '';
  private _listeners: Record<string, Function[]> = {};

  get classList() {
    return {
      add: (...classes: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        classes.forEach(c => set.add(c));
        this.className = Array.from(set).join(' ');
      },
      remove: (...classes: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        classes.forEach(c => set.delete(c));
        this.className = Array.from(set).join(' ');
      },
      contains: (cls: string) => {
        return this.className.split(/\s+/).includes(cls);
      }
    };
  }

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

  get innerHTML(): string {
    return this._html;
  }

  set innerHTML(html: string) {
    this._html = html;
    this.children = [];
    parseHTMLToMock(html, this);
  }

  appendChild(child: MockElement) {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  addEventListener(event: string, fn: Function) {
    this._listeners[event] = this._listeners[event] || [];
    this._listeners[event].push(fn);
  }

  click() {
    (this._listeners['click'] || []).forEach(fn => fn({ preventDefault: () => {}, stopPropagation: () => {} }));
  }

  contains(child: any): boolean {
    if (child === this) return true;
    return this.children.some(c => c.contains(child));
  }

  querySelector(selector: string): MockElement | null {
    for (const child of this.children) {
      if (child.matches(selector)) return child;
      const found = child.querySelector(selector);
      if (found) return found;
    }
    return null;
  }

  querySelectorAll(selector: string): MockElement[] {
    const list: MockElement[] = [];
    for (const child of this.children) {
      if (child.matches(selector)) list.push(child);
      list.push(...child.querySelectorAll(selector));
    }
    return list;
  }

  matches(sel: string): boolean {
    if (sel.startsWith('#')) return this.id === sel.slice(1);
    if (sel.startsWith('.')) return this.className.split(/\s+/).includes(sel.slice(1));
    return this.tagName.toLowerCase() === sel.toLowerCase();
  }
}

function parseHTMLToMock(html: string, root: MockElement) {
  const tokenRegex = /<(\/)?([a-zA-Z0-9\-]+)([^>]*?)>|([^<]+)/g;
  const stack: MockElement[] = [root];
  let match: RegExpExecArray | null;

  while ((match = tokenRegex.exec(html)) !== null) {
    const [, isClosing, tagName, attrs, text] = match;
    if (text) {
      const trimmed = text.trim();
      if (trimmed && stack.length > 0) {
        const top = stack[stack.length - 1];
        top.textContent = top.textContent ? `${top.textContent} ${trimmed}` : trimmed;
      }
      continue;
    }

    if (isClosing) {
      if (stack.length > 1) {
        stack.pop();
      }
    } else {
      const el = new MockElement(tagName);
      if (attrs) {
        const idMatch = attrs.match(/id=["']([^"']+)["']/);
        if (idMatch) el.id = idMatch[1];

        const classMatch = attrs.match(/class=["']([^"']+)["']/);
        if (classMatch) el.className = classMatch[1];

        const titleMatch = attrs.match(/title=["']([^"']+)["']/);
        if (titleMatch) el.title = titleMatch[1];

        const typeMatch = attrs.match(/type=["']([^"']+)["']/);
        if (typeMatch) el.type = typeMatch[1];

        if (attrs.includes('checked')) el.checked = true;
      }
      stack[stack.length - 1].appendChild(el);

      const isSelfClosing = attrs?.endsWith('/') || ['img', 'br', 'hr', 'input'].includes(tagName.toLowerCase());
      if (!isSelfClosing) {
        stack.push(el);
      }
    }
  }
}

describe('Fase 15: Interface Estilo Microsoft Office & Conectores de Importação', () => {
  let bus: EventBus;
  let graph: Graph;
  let projectManager: ProjectManager;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'Workspace_Office_Test',
      projectId: 'office_test_proj'
    });
  });

  it('deve sintetizar agente do Pi.ai com raciocínio empático e memória configurada (Fase 16 prioridade 1)', () => {
    const initialNodes = graph.getAllNodes().length;
    const piModal = new ImportPiModal(graph);

    piModal.injectPiAgent({
      name: 'Agente_Pi_Mentor_Estrategico',
      style: 'empathetic_dialogue',
      conversation: 'User: Como estruturar agentes concorrentes?\nPi: Use Taskflow e Blackboard particionado.'
    });

    const currentNodes = graph.getAllNodes();
    expect(currentNodes.length).toBe(initialNodes + 1);

    const piNode = currentNodes.find(n => n.label.includes('Pi') || n.id.startsWith('pi_agent'));
    expect(piNode).toBeDefined();
    expect(piNode?.type).toBe('agent');
    expect(piNode?.agentSettings).toBeDefined();
    expect(piNode?.agentSettings?.model?.model).toBe('pi-3-empathic');
    expect(piNode?.agentSettings?.memory?.enabled).toBe(true);
    expect(piNode?.agentSettings?.memory?.persistent).toBe(true);
    expect(piNode?.agentSettings?.memory?.domains).toContain('strategy');
  });

  it('deve sintetizar agentes do Claude, OpenAI e Hermes com configurações adequadas', () => {
    const claudeModal = new ImportClaudeModal(graph);
    claudeModal.injectClaudeAgent({ name: 'Agente_Claude_35_Sonnet', model: 'claude-3-5-sonnet' });

    const openaiModal = new ImportOpenAiModal(graph);
    openaiModal.injectOpenAiAgent({ name: 'Agente_GPT4o_Task', model: 'gpt-4o' });

    const hermesModal = new ImportHermesModal(graph);
    hermesModal.injectHermesAgent({ name: 'Agente_Hermes_Local_VM', model: 'hermes-3-llama-3.1-70b' });

    const nodes = graph.getAllNodes();
    const claudeNode = nodes.find(n => n.id.startsWith('claude_agent'));
    const openAiNode = nodes.find(n => n.id.startsWith('openai_agent'));
    const hermesNode = nodes.find(n => n.id.startsWith('hermes_agent'));

    expect(claudeNode).toBeDefined();
    expect(claudeNode?.agentSettings?.model?.provider).toBe('anthropic');
    expect(claudeNode?.agentSettings?.model?.model).toBe('claude-3-5-sonnet');

    expect(openAiNode).toBeDefined();
    expect(openAiNode?.agentSettings?.model?.provider).toBe('openai');
    expect(openAiNode?.agentSettings?.model?.model).toBe('gpt-4o');

    expect(hermesNode).toBeDefined();
    expect(hermesNode?.agentSettings?.sandbox?.isolationLevel).toBe('microvm');
    expect(hermesNode?.agentSettings?.model?.model).toBe('hermes-3-llama-3.1-70b');
  });

  it('deve desconstruir planilha complexa do Excel em banco relacional SQLite e rede de 4 agentes concorrentes', () => {
    const excelModal = new ImportExcelModal(graph);
    excelModal.synthesizeSpreadsheetSystem();

    const nodes = graph.getAllNodes();
    expect(nodes.length).toBe(4);

    const ingestNode = nodes.find(n => n.id === 'node_excel_ingest');
    const calcNode = nodes.find(n => n.id === 'node_excel_calc');
    const evalNode = nodes.find(n => n.id === 'node_excel_eval');
    const reportNode = nodes.find(n => n.id === 'node_excel_report');

    expect(ingestNode).toBeDefined();
    expect(calcNode).toBeDefined();
    expect(evalNode).toBeDefined();
    expect(reportNode).toBeDefined();

    // Valida arestas e loops de feedback
    const edges = graph.getAllEdges();
    expect(edges.length).toBe(4);

    // Feedback loop de inconsistência de auditoria
    const feedbackEdge = edges.find(e => e.sourceNodeId === 'node_excel_eval' && e.targetNodeId === 'node_excel_ingest');
    expect(feedbackEdge).toBeDefined();

    // Banco de dados deve ter sido atualizado no manifesto do projeto
    const proj = projectManager.getCurrentProject();
    expect(proj.database?.type).toBe('sqlite');
    expect(proj.database?.filePath).toBe('storage/database.sqlite');
  });

  it('deve validar estrutura das classes CSS da Ribbon compatíveis com o padrão Microsoft Office Fluent', () => {
    // Classes de controle do padrão Office Ribbon
    const ribbonClasses = [
      'office-ribbon-container',
      'office-titlebar',
      'office-tabs-bar',
      'office-ribbon-body',
      'office-backstage-overlay',
      'office-backstage-sidebar',
      'office-backstage-main',
      'ribbon-btn-large',
      'ribbon-btn-small'
    ];

    expect(ribbonClasses).toContain('office-ribbon-container');
    expect(ribbonClasses).toContain('office-backstage-overlay');
    expect(ribbonClasses).toContain('ribbon-btn-large');
    expect(ribbonClasses.length).toBe(9);
  });

  describe('Salvamento Automático (AutoSave) na Barra de Títulos', () => {
    let ribbon: OfficeRibbon;
    let parent: any;
    let originalDoc: any;
    let originalWin: any;

    beforeAll(() => {
      originalDoc = (globalThis as any).document;
      originalWin = (globalThis as any).window;

      const mockBody = new MockElement('BODY');
      (globalThis as any).document = {
        createElement: (tag: string) => new MockElement(tag),
        body: mockBody,
        querySelector: (selector: string) => mockBody.querySelector(selector),
        querySelectorAll: (selector: string) => mockBody.querySelectorAll(selector)
      };

      (globalThis as any).window = {
        addEventListener: () => {},
        removeEventListener: () => {}
      };

      const storageMap = new Map<string, string>();
      (globalThis as any).localStorage = {
        getItem: (k: string) => storageMap.get(k) ?? null,
        setItem: (k: string, v: string) => storageMap.set(k, String(v)),
        removeItem: (k: string) => storageMap.delete(k),
        clear: () => storageMap.clear()
      };
    });

    afterAll(() => {
      (globalThis as any).document = originalDoc;
      (globalThis as any).window = originalWin;
      delete (globalThis as any).localStorage;
    });

    beforeEach(() => {
      if (typeof localStorage !== 'undefined') {
        localStorage.clear();
      }
      ProjectManager.getInstance().resetForTesting();
      if ((globalThis as any).document?.body) {
        (globalThis as any).document.body.children = [];
      }
      parent = (globalThis as any).document.createElement('div');
      const fakeViewport = {
        getCameraCenter: () => ({ x: 0, y: 0 }),
        zoomToFit: () => {}
      } as any;
      const fakeCodeModal = { show: () => {} } as any;
      const fakeTemplateModal = { show: () => {} } as any;
      const fakeGlobalDefaultsModal = { show: () => {} } as any;

      ribbon = new OfficeRibbon(
        parent,
        graph,
        fakeViewport,
        fakeCodeModal,
        fakeTemplateModal,
        fakeGlobalDefaultsModal
      );
    });

    it('deve exibir o switch de salvamento automático ativado por padrão ao lado do ícone de disquete', () => {
      const container = ribbon.getContainer();
      const autosaveGroup = container.querySelector('.office-autosave-group');
      const toggle = container.querySelector('#qat-toggle-autosave') as HTMLInputElement;
      const label = container.querySelector('#qat-autosave-label');
      const badge = container.querySelector('#qat-autosave-badge');
      const saveBtn = container.querySelector('#qat-btn-save');

      expect(autosaveGroup).toBeDefined();
      expect(toggle).toBeDefined();
      expect(toggle.checked).toBe(true);
      expect(ribbon.getAutoSaveEnabled()).toBe(true);
      expect(label?.textContent).toBe('Salvamento Automático');
      expect(badge?.textContent).toBe('✓');
      expect(saveBtn).toBeDefined();

      // Verifica se o switch precede ou é adjacente ao botão de salvar
      const qatButtons = container.querySelector('.office-qat-buttons');
      expect(qatButtons?.contains(autosaveGroup!)).toBe(true);
      expect(qatButtons?.contains(saveBtn!)).toBe(true);
    });

    it('deve salvar todo o projeto, grafo avulso e configurações de ambiente ao chamar saveAll', async () => {
      // Adiciona nó e atualiza defaults globais
      graph.createNode('node_autosave_1', 'Agente Auditor', 'agent', 10, 20);
      graph.updateGlobalDefaults({
        sandbox: { timeoutMs: 45000, maxMemoryMb: 1024 }
      });

      const ok = await ribbon.saveAll(true);
      expect(ok).toBe(true);

      // Verifica se o grafo avulso foi salvo no localStorage
      const standalone = localStorage.getItem('arestrix_standalone_graph');
      expect(standalone).toBeDefined();
      const parsedStandalone = JSON.parse(standalone!);
      expect(parsedStandalone.nodes.length).toBe(1);
      expect(parsedStandalone.nodes[0].label).toBe('Agente Auditor');

      // Verifica se as configurações de ambiente / defaults globais foram salvas
      const savedDefaults = localStorage.getItem('arestrix_global_defaults');
      expect(savedDefaults).toBeDefined();
      const parsedDefaults = JSON.parse(savedDefaults!);
      expect(parsedDefaults.sandbox.timeoutMs).toBe(45000);

      // Verifica se o projeto ativo tem os defaults sincronizados
      const currentProj = projectManager.getCurrentProject();
      expect(currentProj.globalDefaults?.sandbox?.timeoutMs).toBe(45000);
    });

    it('deve acionar o salvamento imediato ao clicar no botão do disquete (qat-btn-save)', async () => {
      graph.createNode('node_manual_save', 'Agente Manual', 'agent', 50, 50);

      const saveBtn = ribbon.getContainer().querySelector('#qat-btn-save') as any;
      expect(saveBtn).toBeDefined();

      // Dispara o clique e aguarda a conclusão assíncrona
      saveBtn.click();
      await new Promise(r => setTimeout(r, 50));

      const standalone = localStorage.getItem('arestrix_standalone_graph');
      expect(standalone).toBeDefined();
      expect(standalone).toContain('Agente Manual');
    });

    it('deve permitir desativar e reativar o Salvamento Automático respeitando a escolha do usuário', () => {
      const toggle = ribbon.getContainer().querySelector('#qat-toggle-autosave') as HTMLInputElement;

      // Desativa
      ribbon.setAutoSaveEnabled(false);
      expect(ribbon.getAutoSaveEnabled()).toBe(false);
      expect(toggle.checked).toBe(false);
      expect(localStorage.getItem('arestrix_autosave_enabled')).toBe('false');

      // Reativa
      ribbon.setAutoSaveEnabled(true);
      expect(ribbon.getAutoSaveEnabled()).toBe(true);
      expect(toggle.checked).toBe(true);
      expect(localStorage.getItem('arestrix_autosave_enabled')).toBe('true');
    });

    it('deve renderizar a tabela com projetos recentes ao abrir o Backstage View pela primeira vez', async () => {
      const mockWebui = {
        call: async (method: string) => {
          if (method === 'list_recent_projects') {
            return JSON.stringify([
              {
                projectId: 'proj_first_open',
                name: 'CriaVideoMatematica',
                rootPath: 'C:/Projetos/CriaVideoMatematica'
              }
            ]);
          }
          if (method === 'get_current_project') {
            return JSON.stringify({
              projectId: 'proj_first_open',
              name: 'CriaVideoMatematica',
              rootPath: 'C:/Projetos/CriaVideoMatematica'
            });
          }
          return '';
        }
      };

      (globalThis as any).webui = mockWebui;
      if (typeof window !== 'undefined') {
        (window as any).webui = mockWebui;
      }

      try {
        await ribbon.openBackstage('info');
        const backstage = ribbon.getBackstageView();
        expect(backstage.isOpen()).toBe(true);
        const overlay = backstage.getOverlay();
        expect(overlay).toBeDefined();
        expect(overlay?.innerHTML).toContain('CriaVideoMatematica');
        expect(overlay?.innerHTML).toContain('backstage-mru-table');
        expect(overlay?.innerHTML).not.toContain('Nenhum projeto recente registrado no histórico');
      } finally {
        delete (globalThis as any).webui;
        if (typeof window !== 'undefined') {
          delete (window as any).webui;
        }
      }
    });

    it('deve abrir o modal de Repositórios GitHub diretamente sobre a Backstage View sem fechá-la', async () => {
      const { globalEventBus } = await import('../src/core/EventBus.ts');
      let githubModalEventFired = false;
      globalEventBus.on('modal:open-github-repos', () => {
        githubModalEventFired = true;
      });

      await ribbon.openBackstage('info');
      const backstage = ribbon.getBackstageView();
      expect(backstage.isOpen()).toBe(true);

      const overlay = backstage.getOverlay();
      const btnGithub = overlay?.querySelector('#btn-backstage-github-repos') as any;
      expect(btnGithub).toBeDefined();

      btnGithub?.click();

      expect(githubModalEventFired).toBe(true);
      expect(backstage.isOpen()).toBe(true);
    });

    it('deve renderizar o dropdown select de idioma com opções pt-BR e en-US nas Opções do Backstage', async () => {
      await ribbon.openBackstage('options');
      const backstage = ribbon.getBackstageView();
      expect(backstage.isOpen()).toBe(true);

      const overlay = backstage.getOverlay();
      const selectEl = overlay?.querySelector('#backstage-language-select') as any;
      expect(selectEl).toBeDefined();
      expect(selectEl?.tagName).toBe('SELECT');

      const html = overlay?.innerHTML || '';
      expect(html).toContain('BR Português (Brasil)');
      expect(html).toContain('US English');
    });

    it('deve transformar o botão Build & Run em botão vermelho Stop ao rodar, e voltar a Build & Run ao finalizar', async () => {
      const { globalEventBus } = await import('../src/core/EventBus.ts');
      const container = ribbon.getContainer();

      const btnRun = container.querySelector('#ribbon-btn-run') as any;
      const btnQat = container.querySelector('#qat-btn-run') as any;

      expect(ribbon.isRunning()).toBe(false);
      expect(btnRun.classList.contains('is-running-stop')).toBe(false);
      expect(btnRun.querySelector('.ribbon-btn-icon')?.textContent).toBe('▶');

      // 1. Inicia execução
      globalEventBus.emit('pipeline:running', 'Executando pipeline...');
      expect(ribbon.isRunning()).toBe(true);
      expect(btnRun.classList.contains('is-running-stop')).toBe(true);
      expect(btnRun.querySelector('.ribbon-btn-icon')?.textContent).toBe('⏹');
      expect(btnRun.querySelector('.ribbon-btn-label')?.textContent).toContain('Parar');
      expect(btnQat.classList.contains('is-running-stop')).toBe(true);
      expect(btnQat.textContent).toBe('⏹');

      // 2. Finaliza ou interrompe execução
      globalEventBus.emit('pipeline:stopped');
      expect(ribbon.isRunning()).toBe(false);
      expect(btnRun.classList.contains('is-running-stop')).toBe(false);
      expect(btnRun.querySelector('.ribbon-btn-icon')?.textContent).toBe('▶');
      expect(btnRun.querySelector('.ribbon-btn-label')?.textContent).toContain('Build & Run');
      expect(btnQat.classList.contains('is-running-stop')).toBe(false);
      expect(btnQat.textContent).toBe('▶');
    });

    describe('Abas Contextuais de Componentes / Vértices (Estilo Microsoft Office)', () => {
      it('não deve exibir a aba contextual quando nenhum nó estiver selecionado', () => {
        const container = ribbon.getContainer();
        const contextualTabBtn = container.querySelector('#tab-btn-contextual');
        const contextualPane = container.querySelector('.office-ribbon-pane[data-pane="contextual"]');

        expect(contextualTabBtn).toBeNull();
        expect(contextualPane).toBeNull();
        expect(ribbon.getSelectedNode()).toBeNull();
      });

      it('deve exibir a aba contextual com opções especializadas ao selecionar um nó Agente', async () => {
        const { globalEventBus } = await import('../src/core/EventBus.ts');
        const agentNode = graph.createNode('node_agent_test', 'Agente_Auditor_Senior', 'agent', 100, 100);

        globalEventBus.emit('node:selected', agentNode);

        const container = ribbon.getContainer();
        const contextualTabBtn = container.querySelector('#tab-btn-contextual');
        expect(contextualTabBtn).toBeDefined();
        expect(contextualTabBtn?.className).toContain('contextual-tab');
        expect(contextualTabBtn?.textContent).toContain('AGENTE');
        expect(contextualTabBtn?.textContent).toContain('Agente_Auditor_Senior');

        // Painel contextual deve estar presente com as ferramentas do agente
        expect(container.querySelector('#btn-ctx-agent-model')).toBeDefined();
        expect(container.querySelector('#btn-ctx-agent-sandbox')).toBeDefined();
        expect(container.querySelector('#btn-ctx-agent-tools')).toBeDefined();
        expect(container.querySelector('#btn-ctx-agent-skills')).toBeDefined();
        expect(container.querySelector('#btn-ctx-agent-memory')).toBeDefined();
        expect(container.querySelector('#btn-ctx-node-rename')).toBeDefined();
        expect(container.querySelector('#btn-ctx-node-duplicate')).toBeDefined();
        expect(container.querySelector('#btn-ctx-node-delete')).toBeDefined();
      });

      it('deve alternar as opções contextuais conforme o tipo do nó selecionado (condition, cuda, mcp)', async () => {
        const { globalEventBus } = await import('../src/core/EventBus.ts');

        // 1. Nó Condicional
        const condNode = graph.createNode('node_cond_test', 'Decisor_Risco', 'condition', 200, 100);
        globalEventBus.emit('node:selected', condNode);

        let container = ribbon.getContainer();
        let contextualTabBtn = container.querySelector('#tab-btn-contextual');
        expect(contextualTabBtn?.textContent).toContain('DECISOR');
        expect(container.querySelector('#btn-ctx-cond-expr')).toBeDefined();
        expect(container.querySelector('#btn-ctx-cond-prob')).toBeDefined();

        // 2. Nó CUDA GPU
        const cudaNode = graph.createNode('node_cuda_test', 'Kernel_Matmul', 'cuda', 300, 100);
        globalEventBus.emit('node:selected', cudaNode);

        container = ribbon.getContainer();
        contextualTabBtn = container.querySelector('#tab-btn-contextual');
        expect(contextualTabBtn?.textContent).toContain('CUDA GPU');
        expect(container.querySelector('#btn-ctx-cuda-code')).toBeDefined();
        expect(container.querySelector('#btn-ctx-cuda-stream')).toBeDefined();

        // 3. Nó MCP
        const mcpNode = graph.createNode('node_mcp_test', 'MCP_FetchTool', 'mcp', 400, 100);
        globalEventBus.emit('node:selected', mcpNode);

        container = ribbon.getContainer();
        contextualTabBtn = container.querySelector('#tab-btn-contextual');
        expect(contextualTabBtn?.textContent).toContain('MCP');
        expect(container.querySelector('#btn-ctx-mcp-servers')).toBeDefined();
        expect(container.querySelector('#btn-ctx-mcp-test')).toBeDefined();
      });

      it('deve remover a aba contextual e retornar à aba inicial quando o nó perde o foco (deseleção)', async () => {
        const { globalEventBus } = await import('../src/core/EventBus.ts');
        const agentNode = graph.createNode('node_agent_focus', 'Agente_Foco', 'agent', 50, 50);

        // Seleciona o nó: aba contextual surge
        globalEventBus.emit('node:selected', agentNode);
        expect(ribbon.getContainer().querySelector('#tab-btn-contextual')).toBeDefined();
        expect(ribbon.getSelectedNode()?.id).toBe('node_agent_focus');

        // Deseleciona o nó: aba contextual some e ribbon volta para home
        globalEventBus.emit('node:deselected');
        expect(ribbon.getContainer().querySelector('#tab-btn-contextual')).toBeNull();
        expect(ribbon.getSelectedNode()).toBeNull();
      });

      it('deve remover a aba contextual se o nó selecionado for removido do grafo', async () => {
        const { globalEventBus } = await import('../src/core/EventBus.ts');
        const agentNode = graph.createNode('node_agent_to_remove', 'Agente_Temporario', 'agent', 80, 80);

        globalEventBus.emit('node:selected', agentNode);
        expect(ribbon.getContainer().querySelector('#tab-btn-contextual')).toBeDefined();

        globalEventBus.emit('node:removed', { id: 'node_agent_to_remove' });
        expect(ribbon.getContainer().querySelector('#tab-btn-contextual')).toBeNull();
        expect(ribbon.getSelectedNode()).toBeNull();
      });
    });
  });
});


