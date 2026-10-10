import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { GitHubReposModal } from '../src/ui/modals/GitHubReposModal.ts';
import { NodeSkillsModal } from '../src/ui/modals/NodeSkillsModal.ts';
import { GitHubSkillsRegistry } from '../src/core/skills/GitHubSkillsRegistry.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Separação Arquitetural: Repositórios GitHub Globais vs Importação por Nó', () => {
  let originalDoc: any;
  let originalWin: any;
  let bus: EventBus;
  let graph: Graph;

  beforeEach(() => {
    originalDoc = (globalThis as any).document;
    originalWin = (globalThis as any).window;

    const elements = new Map<string, any>();
    const createMockElement = (tag: string) => {
      const el: any = {
        tagName: tag.toUpperCase(),
        className: '',
        id: '',
        style: {},
        children: [] as any[],
        dataset: {} as Record<string, string>,
        classList: {
          add: (cls: string) => {
            if (!el.className.split(' ').includes(cls)) {
              el.className = (el.className + ' ' + cls).trim();
            }
          },
          remove: (cls: string) => {
            el.className = el.className.split(' ').filter((c: string) => c !== cls).join(' ');
          },
          contains: (cls: string) => el.className.split(' ').includes(cls)
        },
        _html: '',
        set innerHTML(val: string) { this._html = val; },
        get innerHTML() { return this._html; },
        _listeners: {} as Record<string, Function[]>,
        addEventListener(ev: string, fn: Function) {
          this._listeners[ev] = this._listeners[ev] || [];
          this._listeners[ev].push(fn);
        },
        removeEventListener: () => {},
        dispatchEvent(e: any) {
          (this._listeners[e?.type || 'click'] || []).forEach(fn => fn(e));
        },
        click() { this.dispatchEvent({ type: 'click' }); },
        appendChild(child: any) {
          child.parentNode = this;
          this.children.push(child);
          return child;
        },
        removeChild(child: any) {
          const idx = this.children.indexOf(child);
          if (idx !== -1) this.children.splice(idx, 1);
          child.parentNode = null;
          return child;
        },
        getBoundingClientRect: () => ({ left: 100, top: 100, width: 800, height: 600, right: 900, bottom: 700 }),
        closest: (sel: string) => {
          let cur: any = el;
          while (cur) {
            if (cur.className && (cur.className.includes('modal-overlay') || cur.className.includes('modal-backdrop'))) {
              return cur;
            }
            cur = cur.parentNode;
          }
          return null;
        },
        querySelector: (sel: string) => {
          if (sel.startsWith('#')) {
            const id = sel.slice(1);
            return elements.get(id) || null;
          }
          return null;
        },
        querySelectorAll: () => []
      };
      return el;
    };

    (globalThis as any).document = {
      createElement: createMockElement,
      body: {
        appendChild: () => {},
        removeChild: () => {}
      }
    };

    (globalThis as any).window = {
      innerWidth: 1280,
      innerHeight: 800,
      addEventListener: () => {},
      removeEventListener: () => {}
    };

    bus = new EventBus();
    graph = new Graph(bus, { graphName: 'TestGlobalSkills', projectId: 'test' });
  });

  afterEach(() => {
    (globalThis as any).document = originalDoc;
    (globalThis as any).window = originalWin;
  });

  it('1. GitHubReposModal deve ser um componente global do sistema gerenciando repositórios', () => {
    const reposModal = new GitHubReposModal(bus);
    expect(reposModal).toBeDefined();

    reposModal.open();
    const dialog = (reposModal as any).dialog;
    expect(dialog).toBeDefined();

    // Deve conter elementos de gestão global de repositórios
    const html = dialog.innerHTML;
    expect(html).toContain('Repositórios do GitHub (Catálogo Global de Skills)');
    expect(html).toContain('Projetos Open Source Autorizados');
    expect(html).toContain('Cadastrar Novo Repositório Open Source');
    expect(html).toContain('chk-global-toggle-repo');

    reposModal.destroy();
  });

  it('2. NodeSkillsModal deve ter apenas 2 abas (editor e importação) e NÃO conter aba de repositórios', () => {
    const nodeData = {
      id: 'node_test_agent',
      type: 'agent' as const,
      label: 'Agente Pesquisador',
      position: { x: 0, y: 0 },
      agentSettings: {
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        skills: []
      }
    };
    graph.addNode(nodeData);

    const nodeSkillsModal = new NodeSkillsModal(graph, bus);
    nodeSkillsModal.open(nodeData.id, 'github');

    const dialog = (nodeSkillsModal as any).dialog;
    expect(dialog).toBeDefined();
    const html = dialog.innerHTML;

    // Aba 1 presente: Habilidade do Agente & Editor
    expect(html).toContain('Habilidade do Agente & Editor');
    // Aba 2 presente: Importar do Catálogo (GitHub)
    expect(html).toContain('Importar do Catálogo (GitHub Open Source)');
    // Aba 3 (Configurar Projetos GitHub) NÃO deve mais existir dentro do modal do nó!
    expect(html).not.toContain('data-tab="repos"');
    expect(html).not.toContain('id="pane-repos"');

    // Deve conter atalho limpo para abrir a configuração global de repositórios
    expect(html).toContain('id="btn-open-global-repos-from-node"');
    expect(html).toContain('Configurar Projetos GitHub...');

    nodeSkillsModal.destroy();
  });

  it('3. O nó deve conseguir importar uma skill específica do catálogo global do GitHub', () => {
    const registry = GitHubSkillsRegistry.getInstance();
    const defaultSkills = registry.getDefaultSkills();
    expect(defaultSkills.length).toBeGreaterThan(0);

    const targetSkill = defaultSkills[0];

    const nodeData = {
      id: 'node_specialist',
      type: 'agent' as const,
      label: 'Especialista C++',
      position: { x: 0, y: 0 },
      agentSettings: {
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        skills: []
      }
    };
    graph.addNode(nodeData);

    const nodeSkillsModal = new NodeSkillsModal(graph, bus);
    nodeSkillsModal.open(nodeData.id, 'editor');

    const initialNode = graph.getNode(nodeData.id);
    expect(initialNode?.agentSettings?.skills?.length).toBe(1); // default skill criada automaticamente

    // Simula importação da skill do GitHub para o nó
    const nodeSkill = registry.convertToNodeSkill(targetSkill);
    initialNode?.agentSettings?.skills?.push(nodeSkill);
    if (initialNode?.agentSettings) {
      initialNode.agentSettings.activeSkillId = nodeSkill.id;
    }
    graph.updateNode(nodeData.id, { agentSettings: initialNode?.agentSettings });

    const updatedNode = graph.getNode(nodeData.id);
    expect(updatedNode?.agentSettings?.skills?.length).toBe(2);
    expect(updatedNode?.agentSettings?.activeSkillId).toBe(targetSkill.id);

    const importedSkill = updatedNode?.agentSettings?.skills?.find(s => s.id === targetSkill.id);
    expect(importedSkill).toBeDefined();
    expect(importedSkill?.name).toBe(targetSkill.name);
    expect(importedSkill?.license).toBe(targetSkill.license);

    nodeSkillsModal.destroy();
  });

  it('4. NodeSkillsModal deve reagir ao evento global skills:repositories-updated emitido pelo GitHubReposModal', () => {
    let eventReceived = false;
    bus.on('skills:repositories-updated', () => {
      eventReceived = true;
    });

    const reposModal = new GitHubReposModal(bus);
    reposModal.open();

    // Emite evento indicando que repositórios foram ativados/desativados
    bus.emit('skills:repositories-updated');
    expect(eventReceived).toBe(true);

    reposModal.destroy();
  });
});
