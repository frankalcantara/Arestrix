import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { WorkspaceSkillAuditorAgent } from '../src/core/workspace/WorkspaceSkillAuditorAgent.ts';
import { APPROVED_WORKSPACE_SKILLS, APPROVED_WORKSPACE_MCPS } from '../src/core/workspace/ApprovedWorkspaceCatalog.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';
import { INodeData, INodeSkill } from '../src/core/types.ts';
import { NodeSkillsModal } from '../src/ui/modals/NodeSkillsModal.ts';
import { BackstageView } from '../src/ui/ribbon/BackstageView.ts';
import { CodeModal } from '../src/ui/CodeModal.ts';
import { GlobalDefaultsModal } from '../src/ui/GlobalDefaultsModal.ts';

class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  textContent: string = '';
  style: Record<string, string> = {};
  children: MockElement[] = [];
  parentNode: MockElement | null = null;
  dataset: Record<string, string> = {};
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
    (this._listeners['click'] || []).forEach(fn => fn({ currentTarget: this, target: this, preventDefault: () => {}, stopPropagation: () => {} }));
  }

  getAttribute(attr: string): string | null {
    if (attr.startsWith('data-')) {
      const key = attr.slice(5);
      return this.dataset[key] ?? null;
    }
    if (attr === 'id') return this.id || null;
    if (attr === 'class') return this.className || null;
    return null;
  }

  closest(sel: string): MockElement | null {
    let current: MockElement | null = this;
    while (current) {
      if (current.matches(sel)) return current;
      current = current.parentNode;
    }
    return null;
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
    if (sel.startsWith('[data-section="')) {
      const val = sel.match(/\[data-section="([^"]+)"\]/)?.[1];
      return this.dataset['section'] === val;
    }
    if (sel.includes('[data-section]')) {
      return Boolean(this.dataset['section']);
    }
    if (sel.includes('[data-tab]')) {
      return Boolean(this.dataset['tab']);
    }
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

        const dataSectionMatch = attrs.match(/data-section=["']([^"']+)["']/);
        if (dataSectionMatch) el.dataset['section'] = dataSectionMatch[1];

        const dataTabMatch = attrs.match(/data-tab=["']([^"']+)["']/);
        if (dataTabMatch) el.dataset['tab'] = dataTabMatch[1];

        const dataSkillMatch = attrs.match(/data-skill-id=["']([^"']+)["']/);
        if (dataSkillMatch) el.dataset['skillId'] = dataSkillMatch[1];
      }
      stack[stack.length - 1].appendChild(el);
      if (!['input', 'img', 'br', 'hr'].includes(tagName.toLowerCase())) {
        stack.push(el);
      }
    }
  }
}

describe('Workspace Governance, Execution & Pre-Approved Catalogs', () => {
  let bus: EventBus;
  let workspaceManager: WorkspaceManager;
  let auditorAgent: WorkspaceSkillAuditorAgent;
  let origDocument: any;
  let origWindow: any;

  beforeEach(() => {
    origDocument = (globalThis as any).document;
    origWindow = (globalThis as any).window;

    const mockBody = new MockElement('BODY');
    (globalThis as any).document = {
      body: mockBody,
      createElement: (tag: string) => new MockElement(tag)
    };
    (globalThis as any).window = {
      addEventListener: () => {},
      removeEventListener: () => {}
    };

    bus = new EventBus();
    workspaceManager = WorkspaceManager.getInstance(bus);
    workspaceManager.resetForTesting(bus);
    auditorAgent = WorkspaceSkillAuditorAgent.getInstance();
  });

  afterEach(() => {
    (globalThis as any).document = origDocument;
    (globalThis as any).window = origWindow;
  });

  describe('1. Catálogo Homologado e Provisionamento de Diretórios', () => {
    it('deve inicializar com as 7 habilidades e 6 MCPs pré-aprovados do workspace', () => {
      const skills = workspaceManager.getApprovedSkills();
      const mcps = workspaceManager.getApprovedMcps();

      expect(skills.length).toBe(7);
      expect(skills.map(s => s.id)).toContain('redacao-tecnica-profunda');
      expect(skills.map(s => s.id)).toContain('ilustracao-vetorial-svg');
      expect(skills.map(s => s.id)).toContain('auditoria-codigo-cpp20');

      expect(mcps.length).toBe(6);
      expect(mcps.map(m => m.id)).toContain('workspace-filesystem');
      expect(mcps.map(m => m.id)).toContain('workspace-taskflow-compiler');
      expect(mcps.map(m => m.id)).toContain('clientes');
      expect(mcps.map(m => m.id)).toContain('keenable');
    });

    it('deve converter uma habilidade homologada em INodeSkill para anexar a vértices', () => {
      const approved = APPROVED_WORKSPACE_SKILLS[0];
      const nodeSkill = workspaceManager.convertToNodeSkill(approved);

      expect(nodeSkill.id).toBe(approved.id);
      expect(nodeSkill.name).toBe(approved.name);
      expect(nodeSkill.tools).toEqual(approved.tools);
      expect(nodeSkill.systemPrompt).toBe(approved.systemPrompt);
      expect(nodeSkill.markdownContent).toBe(approved.markdownContent);
    });

    it('deve provisionar a estrutura de diretórios .workspace no caminho do usuário', async () => {
      workspaceManager.setWorkspaceRoot('C:/User/Projetos/MeuApp');
      const dirs = workspaceManager.getWorkspaceDirectories();

      expect(dirs.root).toBe('C:/User/Projetos/MeuApp/.workspace');
      expect(dirs.skills).toBe('C:/User/Projetos/MeuApp/.workspace/skills');
      expect(dirs.mcps).toBe('C:/User/Projetos/MeuApp/.workspace/mcps');
      expect(dirs.agents).toBe('C:/User/Projetos/MeuApp/.workspace/agents');

      const provisioned = await workspaceManager.ensureWorkspaceDirectories();
      expect(provisioned).toBe(true);
    });

    it('preserva as propriedades editadas ao regravar workspace.config.json', async () => {
      let writtenConfig = '';
      (globalThis as any).window.webui = {
        call: async (name: string, payload: string) => {
          const data = JSON.parse(payload);
          if (name === 'read_workspace_asset') {
            return JSON.stringify({
              content: JSON.stringify({
                properties: {
                  rootPath: 'C:/User/Projetos/MeuApp',
                  defaults: { sandbox: { maxMemoryMB: 2048 } },
                  security: { autoVerifySkills: false, strictSecurityMode: false }
                }
              })
            });
          }
          if (name === 'write_workspace_asset' && String(data.filePath).endsWith('workspace.config.json')) {
            writtenConfig = data.content;
          }
          return true;
        }
      };

      workspaceManager.setWorkspaceRoot('C:/User/Projetos/MeuApp');
      expect(await workspaceManager.ensureWorkspaceDirectories()).toBe(true);

      const config = JSON.parse(writtenConfig);
      expect(config.properties.defaults.sandbox.maxMemoryMB).toBe(2048);
      expect(config.autoVerifySkills).toBe(false);
      expect(config.strictSecurityMode).toBe(false);
      expect(config.installedAssetsVersion).toBe('3.0.0');
    });
  });

  describe('2. WorkspaceSkillAuditorAgent & Governança Estrita', () => {
    it('deve auditar habilidades pré-aprovadas com status aprovado e risco 0', () => {
      const approved = APPROVED_WORKSPACE_SKILLS.find(s => s.id === 'ilustracao-vetorial-svg')!;
      const verdict = auditorAgent.auditSkill(approved, true);

      expect(verdict.status).toBe('approved');
      expect(verdict.riskScore).toBe(0);
      expect(verdict.passedSvgZeroShadowRule).toBe(true);
      expect(verdict.hasTypographyShadowViolation).toBe(false);
      expect(verdict.authorizedTools).toContain('validate_svg');
    });

    it('deve detectar violação da regra de Zero Sombras em SVG e emitir alerta de governança', () => {
      const dirtySkill: Partial<INodeSkill> = {
        id: 'dirty-svg-skill',
        name: 'Skill com Sombras Proibidas',
        description: 'Usa drop shadow em fontes',
        tools: ['validate_svg'],
        markdownContent: 'Use filter: drop-shadow(2px 2px 4px rgba(0,0,0,0.5)) em textos para destacar',
        systemPrompt: 'Aplique feDropShadow em <text> para dar brilho'
      };

      const verdict = auditorAgent.auditSkill(dirtySkill, false);
      expect(verdict.passedSvgZeroShadowRule).toBe(false);
      expect(verdict.hasTypographyShadowViolation).toBe(true);
      expect(verdict.status).toBe('warning');
      expect(verdict.riskScore).toBeGreaterThanOrEqual(25);
      expect(verdict.warnings.some(w => w.toLowerCase().includes('sombra'))).toBe(true);
    });

    it('deve pontuar risco elevado quando ferramenta de terminal perigosa (bash) é importada', () => {
      const riskySkill: Partial<INodeSkill> = {
        id: 'risky-bash-skill',
        name: 'Executor Bash',
        description: 'Roda scripts shell',
        tools: ['bash', 'write'],
        markdownContent: '# Executor\n\nRoda bash livremente',
        systemPrompt: 'Execute scripts bash sem checagem'
      };

      const verdict = auditorAgent.auditSkill(riskySkill, false);
      expect(verdict.riskScore).toBeGreaterThanOrEqual(30);
      expect(verdict.warnings.some(w => w.includes('bash'))).toBe(true);
    });
  });

  describe('3. Execução Integrada no Workspace atrelada a Eventos', () => {
    it('deve executar o agente de governança do workspace ao emitir workspace:run-agent', async () => {
      let executionEvent: any = null;
      bus.on('workspace:agent-executed', (res) => {
        executionEvent = res;
      });

      const res = await workspaceManager.runAgent('agent-workspace-skill-auditor');
      expect(res.status).toBe('success');
      expect(res.targetType).toBe('agent');
      expect(res.details?.auditedCount).toBe(7);

      bus.emit('workspace:run-agent', { agentId: 'agent-workspace-skill-auditor' });
      await new Promise(r => setTimeout(r, 20));
      expect(executionEvent).not.toBeNull();
      expect(executionEvent.targetId).toBe('agent-workspace-skill-auditor');
    });

    it('deve auditar nós de um grafo ao executar workspace:run-graph', async () => {
      const graph = new Graph();
      graph.addNode({
        id: 'node-1',
        label: 'Redator Especialista',
        type: 'agent',
        x: 0,
        y: 0,
        agentSettings: {
          inheritSandbox: false,
          inheritModel: false,
          inheritCodeMode: false,
          skills: [
            workspaceManager.convertToNodeSkill(APPROVED_WORKSPACE_SKILLS[0])
          ]
        }
      });

      let graphResult: any = null;
      bus.on('workspace:graph-executed', (res) => {
        graphResult = res;
      });

      const result = await workspaceManager.runGraph(graph, 'pipeline-test');
      expect(result.status).toBe('success');
      expect(result.targetType).toBe('graph');
      expect(result.details?.nodeCount).toBe(1);

      bus.emit('workspace:run-graph', { graph, graphId: 'pipeline-test' });
      await new Promise(r => setTimeout(r, 20));
      expect(graphResult).not.toBeNull();
    });

    it('deve verificar habilidades sob demanda ao emitir skill:import-requested', async () => {
      let verifiedPayload: any = null;
      bus.on('skill:verified', (payload) => {
        verifiedPayload = payload;
      });

      const approved = APPROVED_WORKSPACE_SKILLS[1];
      bus.emit('skill:import-requested', { skill: approved, isCurated: true, nodeId: 'node-99' });

      expect(verifiedPayload).not.toBeNull();
      expect(verifiedPayload.verdict.status).toBe('approved');
      expect(verifiedPayload.nodeId).toBe('node-99');
    });
  });

  describe('4. NodeSkillsModal - Aba de Habilidades Pré-Aprovadas do Workspace', () => {
    it('deve abrir exibindo a aba 🛡️ Habilidades Pré-Aprovadas do Workspace por padrão', () => {
      const graph = new Graph();
      graph.addNode({
        id: 'agent-n1',
        label: 'Agente Matemático',
        type: 'agent',
        x: 0,
        y: 0
      });

      const modal = new NodeSkillsModal(graph, bus);
      modal.open('agent-n1');

      const body = (modal as any).renderBody();
      expect(body).toContain('Habilidades Pré-Aprovadas do Workspace');
      expect(body).toContain('pane-workspace');
      expect(body).toContain('btn-import-workspace-skill');
      expect(body).toContain('Rigor Matemático');
      expect(body).toContain('Redação Técnica e Conceitual');
    });

    it('deve conter as 3 abas oficiais na barra de abas', () => {
      const graph = new Graph();
      graph.addNode({
        id: 'agent-n2',
        label: 'Agente C++',
        type: 'agent',
        x: 0,
        y: 0
      });

      const modal = new NodeSkillsModal(graph, bus);
      modal.open('agent-n2');

      const body = (modal as any).renderBody();
      expect(body).toContain('data-tab="workspace"');
      expect(body).toContain('data-tab="editor"');
      expect(body).toContain('data-tab="github"');
    });

    it('deve exibir o callout de auditoria do workspace aprovado quando o editor estiver aberto', () => {
      const graph = new Graph();
      graph.addNode({
        id: 'agent-n3',
        label: 'Agente Especialista',
        type: 'agent',
        x: 0,
        y: 0
      });

      const modal = new NodeSkillsModal(graph, bus);
      modal.open('agent-n3', 'editor');

      const body = (modal as any).renderBody();
      expect(body).toContain('Auditoria do Workspace:');
      expect(body).toContain('Pontuação de Risco:');
    });
  });

  describe('5. BackstageView - Seção Opções do Studio', () => {
    it('deve possuir o item de navegação data-section="options" na barra lateral', () => {
      const graph = new Graph();
      const codeModal = new CodeModal();
      const defaultsModal = new GlobalDefaultsModal(bus);
      const backstage = new BackstageView(graph, codeModal, defaultsModal);

      backstage.open();
      const overlay = backstage.getOverlay()!;
      expect(overlay.innerHTML).toContain('data-section="options"');
      expect(overlay.innerHTML).toContain('Opções do Studio');
      backstage.close();
    });

    it('deve renderizar os diretórios do workspace e botões de execução ao selecionar a seção options', () => {
      const graph = new Graph();
      const codeModal = new CodeModal();
      const defaultsModal = new GlobalDefaultsModal(bus);
      const backstage = new BackstageView(graph, codeModal, defaultsModal);

      (backstage as any).activeSection = 'options';
      (backstage as any).render();

      const overlay = backstage.getOverlay()!;
      expect(overlay.innerHTML).toContain('Opções do Studio & Governança do Workspace');
      expect(overlay.innerHTML).toContain('Pastas Físicas do Workspace');
      expect(overlay.innerHTML).toContain('btn-sync-workspace-disk');
      expect(overlay.innerHTML).toContain('btn-run-workspace-auditor');
      expect(overlay.innerHTML).toContain('btn-run-workspace-linter');
      expect(overlay.innerHTML).toContain('btn-run-workspace-project');
      expect(overlay.innerHTML).toContain('Habilidades Pré-Aprovadas (7)');
      expect(overlay.innerHTML).toContain('Servidores MCP Pré-Aprovados (6)');
    });
  });
});
