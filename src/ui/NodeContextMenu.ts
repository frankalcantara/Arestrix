import type { Graph } from '../core/Graph.ts';
import type { EventBus } from '../core/EventBus.ts';
import { NODE_ACTIONS } from '../view/NodeActions.ts';
import {
  AddNodeCommand,
  RemoveNodeCommand,
  RemoveEdgeCommand,
  CompoundCommand
} from '../core/history/GraphCommands.ts';
import {
  copyNode,
  hasClipboardNode,
  buildPastedNode,
  cloneNodeData,
  resetPasteCounter
} from '../core/NodeClipboard.ts';
import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { t } from '../i18n/index.ts';

interface IMenuItem {
  label: string;
  shortcut?: string;
  danger?: boolean;
  disabled?: boolean;
  color?: string;
  run: () => void;
}

/**
 * Arestrix - Menu de contexto dos vértices (botão direito).
 *
 * Reúne as ações rápidas do nó (Sandbox, Modelo, Ferramentas, Memória, Skills) e as
 * operações de edição: duplicar, copiar, recortar, colar, entrar no subfluxo,
 * desconectar e apagar. Toda mutação passa pelo CommandManager (desfazível).
 */
export class NodeContextMenu {
  private el: HTMLElement | null = null;
  private selectedId: string | null = null;

  constructor(private graph: Graph, private bus: EventBus) {
    this.bus.on('node:context-menu', ({ nodeId, clientX, clientY }: { nodeId: string; clientX: number; clientY: number }) =>
      this.open(nodeId, clientX, clientY)
    );
    this.bus.on('node:selected', (payload: string | { id: string }) => {
      this.selectedId = typeof payload === 'string' ? payload : payload?.id ?? null;
    });
    this.bus.on('node:deselected', () => { this.selectedId = null; this.close(); });
    this.bus.on('node:removed', ({ id }: { id: string }) => { if (this.selectedId === id) this.selectedId = null; });
    this.bus.on('graph:cleared', () => this.close());
    if (typeof window !== 'undefined') {
      window.addEventListener('pointerdown', (e) => {
        if (this.el && !this.el.contains(e.target as Node)) this.close();
      }, true);
      window.addEventListener('keydown', (e) => this.onKeyDown(e));
      window.addEventListener('blur', () => this.close());
      window.addEventListener('resize', () => this.close());
    }
  }

  private onKeyDown(e: KeyboardEvent): void {
    if (e.key === 'Escape') { this.close(); return; }
    const target = e.target as HTMLElement | null;
    if (target && (['INPUT', 'TEXTAREA', 'SELECT'].includes(target.tagName) || target.isContentEditable)) return;
    if (!(e.ctrlKey || e.metaKey) || e.shiftKey || e.altKey) return;

    const key = e.key.toLowerCase();
    if (key === 'v') {
      if (hasClipboardNode()) { e.preventDefault(); this.paste(); }
      return;
    }
    if (!this.selectedId || !this.graph.getNode(this.selectedId)) return;
    if (key === 'c') { e.preventDefault(); this.copy(this.selectedId); }
    else if (key === 'x') { e.preventDefault(); this.cut(this.selectedId); }
    else if (key === 'd') { e.preventDefault(); this.duplicate(this.selectedId); }
  }

  // --- Operações (também usadas pelos atalhos de teclado) ---

  duplicate(nodeId: string): void {
    const src = this.graph.getNode(nodeId);
    if (!src) return;
    const copy = cloneNodeData(this.graph, src, t('context_menu.copy_suffix', '(cópia)'));
    this.graph.commandManager.execute(new AddNodeCommand(this.graph, copy));
  }

  copy(nodeId: string): void {
    const src = this.graph.getNode(nodeId);
    if (!src) return;
    copyNode(src);
    resetPasteCounter();
  }

  cut(nodeId: string): void {
    this.copy(nodeId);
    this.remove(nodeId);
  }

  paste(): void {
    const node = buildPastedNode(this.graph, t('context_menu.copy_suffix', '(cópia)'));
    if (!node) return;
    this.graph.commandManager.execute(new AddNodeCommand(this.graph, node));
  }

  saveAsTemplate(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;
    const res = AgentTemplateRegistry.getInstance().saveNodeAsTemplate(node);
    let message: string;
    if (res.status === 'created') {
      message = t('context_menu.template_saved', 'Template "{{name}}" salvo em Plugins Customizados.', { name: res.template.name });
    } else if (res.status === 'duplicate') {
      message = t('context_menu.template_duplicate', 'Já existe no catálogo um template equivalente: "{{name}}". Nada foi incluído.', { name: res.existing.name });
    } else {
      message = res.reason;
    }
    if (typeof window !== 'undefined' && typeof window.alert === 'function') window.alert(message);
  }

  remove(nodeId: string): void {
    if (!this.graph.getNode(nodeId)) return;
    this.graph.commandManager.execute(new RemoveNodeCommand(this.graph, nodeId));
    this.bus.emit('node:deselected');
  }

  disconnect(nodeId: string): void {
    const edges = this.graph.getEdgesForNode(nodeId);
    if (edges.length === 0) return;
    const cmds = edges.map(e => new RemoveEdgeCommand(this.graph, e.id));
    this.graph.commandManager.execute(
      new CompoundCommand(t('context_menu.disconnect_history', 'Desconectar Vértice'), cmds)
    );
  }

  // --- UI ---

  private buildItems(nodeId: string): (IMenuItem | 'sep')[] {
    const node = this.graph.getNode(nodeId);
    if (!node) return [];

    const items: (IMenuItem | 'sep')[] = NODE_ACTIONS.map(a => ({
      label: a.label,
      color: a.color,
      run: () => this.bus.emit(a.event, nodeId)
    }));

    items.push('sep');
    items.push({ label: t('context_menu.duplicate', 'Duplicar'), shortcut: 'Ctrl+D', run: () => this.duplicate(nodeId) });
    items.push({ label: t('context_menu.copy', 'Copiar'), shortcut: 'Ctrl+C', run: () => this.copy(nodeId) });
    items.push({ label: t('context_menu.cut', 'Recortar'), shortcut: 'Ctrl+X', run: () => this.cut(nodeId) });
    items.push({
      label: t('context_menu.paste', 'Colar'),
      shortcut: 'Ctrl+V',
      disabled: !hasClipboardNode(),
      run: () => this.paste()
    });

    items.push('sep');
    items.push({ label: t('context_menu.save_template', 'Salvar como template'), run: () => this.saveAsTemplate(nodeId) });
    if (node.type === 'subflow' || node.type === 'module') {
      items.push({
        label: t('context_menu.enter_subflow', 'Entrar no subfluxo'),
        run: () => { this.graph.diveIntoSubflow(nodeId); }
      });
    }
    items.push({
      label: t('context_menu.disconnect', 'Remover todas as conexões'),
      disabled: this.graph.getEdgesForNode(nodeId).length === 0,
      run: () => this.disconnect(nodeId)
    });

    items.push('sep');
    items.push({ label: t('context_menu.delete', 'Apagar'), shortcut: 'Del', danger: true, run: () => this.remove(nodeId) });
    return items;
  }

  open(nodeId: string, clientX: number, clientY: number): void {
    this.close();
    if (typeof document === 'undefined') return;
    const items = this.buildItems(nodeId);
    if (items.length === 0) return;

    const menu = document.createElement('div');
    menu.className = 'node-context-menu';
    menu.setAttribute('role', 'menu');
    menu.addEventListener('contextmenu', (e) => e.preventDefault());

    for (const item of items) {
      if (item === 'sep') {
        const sep = document.createElement('div');
        sep.className = 'node-context-menu-sep';
        sep.setAttribute('role', 'separator');
        menu.appendChild(sep);
        continue;
      }
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'node-context-menu-item' + (item.danger ? ' danger' : '');
      btn.setAttribute('role', 'menuitem');
      btn.disabled = !!item.disabled;

      const dot = document.createElement('span');
      dot.className = 'node-context-menu-dot';
      if (item.color) dot.style.background = item.color;
      btn.appendChild(dot);

      const label = document.createElement('span');
      label.className = 'node-context-menu-label';
      label.textContent = item.label;
      btn.appendChild(label);

      if (item.shortcut) {
        const sc = document.createElement('span');
        sc.className = 'node-context-menu-shortcut';
        sc.textContent = item.shortcut;
        btn.appendChild(sc);
      }

      btn.addEventListener('click', () => {
        this.close();
        item.run();
      });
      menu.appendChild(btn);
    }

    menu.style.left = '0px';
    menu.style.top = '0px';
    menu.style.visibility = 'hidden';
    document.body.appendChild(menu);
    this.el = menu;

    // Mantém o menu dentro da janela
    const rect = menu.getBoundingClientRect();
    const x = Math.max(4, Math.min(clientX, window.innerWidth - rect.width - 4));
    const y = Math.max(4, Math.min(clientY, window.innerHeight - rect.height - 4));
    menu.style.left = `${x}px`;
    menu.style.top = `${y}px`;
    menu.style.visibility = 'visible';
  }

  close(): void {
    if (this.el) {
      this.el.remove();
      this.el = null;
    }
  }
}
