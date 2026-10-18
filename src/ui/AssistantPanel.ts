import DOMPurify from 'dompurify';
import { Graph } from '../core/Graph.ts';
import { EventBus } from '../core/EventBus.ts';
import { ProjectManager } from '../core/ProjectManager.ts';
import { ICommand } from '../core/history/ICommand.ts';
import {
  AddNodeCommand,
  RemoveNodeCommand,
  UpdateNodeCommand,
  MoveNodeCommand,
  AddEdgeCommand,
  RemoveEdgeCommand,
  CompoundCommand
} from '../core/history/GraphCommands.ts';
import { INodeData } from '../core/types.ts';
import { renderMarkdown } from './modals/SkillEditorModal.ts';
import { t, getLocale } from '../i18n/index.ts';

// Estado do assistente segundo o backend (assistant_status).
interface AssistantStatus {
  configured: boolean;
  provider: string;
  model: string;
  supportsTools: boolean;
  providers: Array<{ id: string; supportsTools: boolean }>;
}

interface SessionSummary {
  id: string;
  title: string;
  updatedAt: string;
  turns: number;
}

// Itens da conversa como o C++ os grava na transcricao (mais a confirmacao pendente, so na interface).
type ChatItem =
  | { kind: 'user'; text: string }
  | { kind: 'assistant'; text: string }
  | { kind: 'activity'; activity: { tool: string; params?: Record<string, any>; status: string; detail?: string } }
  | { kind: 'error'; code: string; detail?: string }
  | { kind: 'cancelled' }
  | { kind: 'confirm'; tool: string; params: Record<string, any>; answer?: boolean };

const CONFIRM_TOOLS = ['create_project', 'open_project', 'clear_graph', 'save_project', 'run_pipeline'];

/**
 * Aba "Assistente" do painel lateral. Só interface: o laço da conversa, as ferramentas, a validação,
 * a edição do grafo e a gravação das sessões rodam no C++ (assistant_* no backend). Aqui a conversa é
 * desenhada, as operações recebidas são aplicadas no canvas como comandos (uma resposta = um desfazer)
 * e as confirmações são repassadas ao backend.
 */
export class AssistantPanel {
  private root: HTMLElement;
  private graph: Graph;
  private bus: EventBus;
  private projectManager: ProjectManager;

  private status: AssistantStatus | null = null;
  private sessions: SessionSummary[] = [];
  private sessionId = '';
  private items: ChatItem[] = [];
  private busy = false;
  private stopping = false;
  private thinking = false;
  private draft = '';

  // Comandos aplicados na resposta em andamento: viram um único passo de desfazer no fim.
  private pending: ICommand[] = [];
  private opQueue: Promise<void> = Promise.resolve();

  constructor(root: HTMLElement, graph: Graph, bus: EventBus) {
    this.root = root;
    this.graph = graph;
    this.bus = bus;
    this.projectManager = ProjectManager.getInstance(bus);

    if (typeof window !== 'undefined') {
      (window as any).onArestrixAssistant = (ev: any) => this.onEvent(ev);
    }
    this.bus.on('locale:changed', () => this.render());
    this.bus.on('taskpane:tab-changed', (tab: string) => {
      if (tab === 'assistant') void this.refresh();
    });
    this.bus.on('project:loaded', () => {
      if (!this.busy) {
        this.sessionId = '';
        this.items = [];
      }
      void this.refreshSessions();
    });
    this.render();
  }

  private webui(): any | null {
    const w = typeof window !== 'undefined' ? (window as any).webui : undefined;
    return w && typeof w.call === 'function' ? w : null;
  }

  private async call(name: string, arg?: string): Promise<any> {
    const webui = this.webui();
    if (!webui) return null;
    const raw = arg === undefined ? await webui.call(name) : await webui.call(name, arg);
    if (typeof raw !== 'string') return raw;
    try {
      return JSON.parse(raw);
    } catch {
      return raw;
    }
  }

  public async refresh(): Promise<void> {
    try {
      this.status = await this.call('assistant_status');
    } catch {
      this.status = null;
    }
    await this.refreshSessions();
  }

  private async refreshSessions(): Promise<void> {
    try {
      const list = await this.call('assistant_sessions');
      this.sessions = Array.isArray(list) ? list : [];
    } catch {
      this.sessions = [];
    }
    this.render();
  }

  // --- Eventos do backend -------------------------------------------------------------------

  public onEvent(ev: any): void {
    if (!ev || (ev.sessionId && this.sessionId && ev.sessionId !== this.sessionId)) return;
    switch (ev.type) {
      case 'thinking':
        this.thinking = true;
        break;
      case 'text':
        this.items.push({ kind: 'assistant', text: String(ev.text || '') });
        break;
      case 'activity':
        this.items.push({ kind: 'activity', activity: ev.activity || { tool: '?', status: 'error' } });
        break;
      case 'op':
        this.opQueue = this.opQueue.then(() => this.applyOp(ev.op)).catch(err => console.warn('[Assistente] operação falhou:', err));
        return;
      case 'confirm':
        this.items.push({ kind: 'confirm', tool: String(ev.tool || ''), params: ev.params || {} });
        break;
      case 'error':
        this.items.push({ kind: 'error', code: String(ev.code || 'unknown'), detail: ev.detail });
        break;
      case 'done':
        this.opQueue = this.opQueue.then(() => this.finishTurn(Boolean(ev.cancelled)));
        return;
    }
    this.render();
  }

  private async finishTurn(cancelled: boolean): Promise<void> {
    this.commitUndo();
    if (cancelled) this.items.push({ kind: 'cancelled' });
    this.busy = false;
    this.stopping = false;
    this.thinking = false;
    await this.refreshSessions();
  }

  private commitUndo(): void {
    if (this.pending.length > 0) {
      this.graph.commandManager.record(new CompoundCommand(t('assistant.undo_label'), this.pending));
    }
    this.pending = [];
  }

  // Aplica no canvas uma operação já validada pelo C++. As edições valem no nível raiz do grafo.
  private async applyOp(op: any): Promise<void> {
    if (!op || typeof op.op !== 'string') return;
    if (op.op === 'load_project') {
      this.commitUndo();
      await this.projectManager.loadProject(String(op.rootPath || ''), this.graph);
      return;
    }
    while (!this.graph.isAtRoot() && this.graph.navigateUp()) { /* sobe até a raiz */ }

    let cmd: ICommand | null = null;
    switch (op.op) {
      case 'add_node':
        cmd = new AddNodeCommand(this.graph, op.node as INodeData);
        break;
      case 'remove_node':
        if (this.graph.getNode(op.id)) cmd = new RemoveNodeCommand(this.graph, op.id);
        break;
      case 'update_node': {
        const node = this.graph.getNode(op.id) as any;
        if (!node) break;
        const patch = op.patch || {};
        const old: Record<string, any> = {};
        for (const key of Object.keys(patch)) old[key] = node[key];
        cmd = new UpdateNodeCommand(this.graph, op.id, old, patch);
        break;
      }
      case 'move_node': {
        const node = this.graph.getNode(op.id);
        if (node) cmd = new MoveNodeCommand(this.graph, op.id, { x: node.position.x, y: node.position.y }, { x: op.x, y: op.y });
        break;
      }
      case 'add_edge':
        cmd = new AddEdgeCommand(this.graph, op.edge);
        break;
      case 'remove_edge':
        if (this.graph.getEdge(op.id)) cmd = new RemoveEdgeCommand(this.graph, op.id);
        break;
    }
    if (cmd) {
      cmd.execute();
      this.pending.push(cmd);
    }
  }

  // --- Ações do usuário ---------------------------------------------------------------------

  private async send(text: string): Promise<void> {
    const message = text.trim();
    if (!message || !this.canSend()) return;
    this.busy = true;
    this.thinking = true;
    this.draft = '';
    this.pending = [];
    this.items.push({ kind: 'user', text: message });
    this.render();

    const res = await this.call('assistant_send', JSON.stringify({
      sessionId: this.sessionId,
      text: message,
      locale: getLocale(),
      graph: this.graph.getRootSchema()
    })).catch(() => null);

    if (!res || !res.ok) {
      this.busy = false;
      this.thinking = false;
      this.items.push({ kind: 'error', code: res?.error === 'busy' ? 'busy' : 'unknown', detail: res?.error });
      this.render();
      return;
    }
    this.sessionId = String(res.sessionId || '');
  }

  private async answerConfirm(index: number, accepted: boolean): Promise<void> {
    const item = this.items[index];
    if (!item || item.kind !== 'confirm' || item.answer !== undefined) return;
    item.answer = accepted;
    this.render();
    await this.call('assistant_confirm', accepted ? 'true' : 'false');
  }

  private async stop(): Promise<void> {
    if (!this.busy) return;
    this.stopping = true;
    this.render();
    await this.call('assistant_cancel');
  }

  private newSession(): void {
    if (this.busy) return;
    this.sessionId = '';
    this.items = [];
    this.render();
  }

  private async openSession(id: string): Promise<void> {
    if (this.busy || !id) return;
    const res = await this.call('assistant_session_load', id);
    if (!res || !res.ok) return;
    this.sessionId = res.id;
    this.items = Array.isArray(res.transcript) ? res.transcript : [];
    this.render();
  }

  private async deleteSession(): Promise<void> {
    if (this.busy || !this.sessionId) return;
    if (typeof confirm !== 'undefined' && !confirm(t('assistant.delete_confirm'))) return;
    await this.call('assistant_session_delete', this.sessionId);
    this.newSession();
    await this.refreshSessions();
  }

  // --- Desenho ------------------------------------------------------------------------------

  private esc(value: unknown): string {
    return String(value ?? '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  private markdown(text: string): string {
    const html = renderMarkdown(text);
    // renderMarkdown já escapa o HTML de entrada; o DOMPurify é a segunda barreira quando há DOM.
    return (DOMPurify as any).isSupported ? (DOMPurify as any).sanitize(html) : html;
  }

  // Parâmetros de texto escapados para entrar na tradução.
  private params(raw: Record<string, any> | undefined): Record<string, string> {
    const out: Record<string, string> = {};
    for (const [k, v] of Object.entries(raw || {})) {
      if (typeof v === 'string' || typeof v === 'number' || typeof v === 'boolean') out[k] = this.esc(v);
    }
    return out;
  }

  private renderBanner(): string {
    if (!this.webui()) return `<div class="assistant-banner">${t('assistant.desktop_only')}</div>`;
    if (!this.status) return '';
    if (!this.status.configured) {
      return `<div class="assistant-banner warn">${t('assistant.not_configured')}
        <button type="button" class="assistant-link-btn" id="assistant-open-defaults">${t('assistant.open_defaults')}</button></div>`;
    }
    if (!this.status.supportsTools) {
      return `<div class="assistant-banner unsupported">⛔ ${t('assistant.tools_unsupported_banner', { provider: this.esc(this.status.provider) })}
        <button type="button" class="assistant-link-btn" id="assistant-open-defaults">${t('assistant.open_defaults')}</button></div>`;
    }
    return '';
  }

  private canSend(): boolean {
    return Boolean(this.webui() && this.status?.configured && this.status.supportsTools && !this.busy);
  }

  private renderItem(item: ChatItem, index: number): string {
    switch (item.kind) {
      case 'user':
        return `<div class="assistant-msg user">${this.esc(item.text).replace(/\n/g, '<br>')}</div>`;
      case 'assistant':
        return `<div class="assistant-msg bot">${this.markdown(item.text)}</div>`;
      case 'activity': {
        const a = item.activity;
        const icon = a.status === 'ok' ? '✓' : a.status === 'declined' ? '⊘' : '✗';
        const label = a.status === 'declined'
          ? t('assistant.activity_declined', { action: t(`assistant.activity.${a.tool}`, this.params(a.params)) })
          : t(`assistant.activity.${a.tool}`, this.params(a.params));
        const detail = a.status === 'error' && a.detail ? `<div class="assistant-activity-detail">${this.esc(a.detail)}</div>` : '';
        return `<div class="assistant-activity ${this.esc(a.status)}"><span class="assistant-activity-icon">${icon}</span><span>${label}</span>${detail}</div>`;
      }
      case 'error':
        return `<div class="assistant-error">⚠ ${t(`assistant.errors.${item.code}`, t('assistant.errors.unknown'))}${
          item.detail ? `<div class="assistant-activity-detail">${this.esc(item.detail)}</div>` : ''}</div>`;
      case 'cancelled':
        return `<div class="assistant-activity declined"><span class="assistant-activity-icon">⏹</span><span>${t('assistant.cancelled')}</span></div>`;
      case 'confirm': {
        const p = this.params(item.params);
        const question = CONFIRM_TOOLS.includes(item.tool) ? t(`assistant.confirm.${item.tool}`, p) : this.esc(item.tool);
        const actions = item.answer === undefined
          ? `<div class="assistant-confirm-actions">
               <button type="button" class="assistant-btn primary" data-confirm="${index}" data-accept="true">${t('assistant.confirm_accept')}</button>
               <button type="button" class="assistant-btn" data-confirm="${index}" data-accept="false">${t('assistant.confirm_decline')}</button>
             </div>`
          : `<div class="assistant-confirm-answer">${item.answer ? t('assistant.confirm_answered_yes') : t('assistant.confirm_answered_no')}</div>`;
        return `<div class="assistant-confirm"><div>❓ ${question}</div>${actions}</div>`;
      }
    }
  }

  public render(): void {
    if (!this.root) return;
    const status = this.status;
    const model = status?.configured ? t('assistant.model_label', { model: this.esc(`${status.provider}/${status.model}`) }) : '';
    const suggestions = ['suggest_learn', 'suggest_build', 'suggest_project'];
    const body = this.items.length === 0
      ? `<div class="assistant-empty">
           <div class="assistant-empty-title">${t('assistant.empty_title')}</div>
           <div class="assistant-empty-hint">${t('assistant.empty_hint')}</div>
           <div class="assistant-suggestions">
             ${suggestions.map(key => `<button type="button" class="assistant-chip" data-suggest="${key}" ${this.canSend() ? '' : 'disabled'}>${t(`assistant.${key}`)}</button>`).join('')}
           </div>
         </div>`
      : this.items.map((item, i) => this.renderItem(item, i)).join('');

    this.root.innerHTML = `
      <div class="assistant-panel">
        <div class="assistant-toolbar">
          <select id="assistant-session-select" class="assistant-session-select" title="${t('assistant.sessions_label')}" ${this.busy ? 'disabled' : ''}>
            <option value="">${t('assistant.new_session')}</option>
            ${this.sessions.map(s => `<option value="${this.esc(s.id)}" ${s.id === this.sessionId ? 'selected' : ''}>${this.esc(s.title || s.id)}</option>`).join('')}
          </select>
          <button type="button" class="assistant-icon-btn" id="assistant-new" title="${t('assistant.new_session')}" ${this.busy ? 'disabled' : ''}>+</button>
          <button type="button" class="assistant-icon-btn" id="assistant-delete" title="${t('assistant.delete_session')}" ${this.busy || !this.sessionId ? 'disabled' : ''}>✕</button>
        </div>
        ${this.renderBanner()}
        <div class="assistant-messages" id="assistant-messages">
          ${body}
          ${this.busy && this.thinking ? `<div class="assistant-thinking">${this.stopping ? t('assistant.stopping') : t('assistant.thinking')}</div>` : ''}
        </div>
        <div class="assistant-composer">
          <textarea id="assistant-input" rows="3" placeholder="${t('assistant.input_placeholder')}" ${this.canSend() || this.busy ? '' : 'disabled'}>${this.esc(this.draft)}</textarea>
          <div class="assistant-composer-bar">
            <span class="assistant-model">${model}</span>
            ${this.busy
              ? `<button type="button" class="assistant-btn" id="assistant-stop" ${this.stopping ? 'disabled' : ''}>${t('assistant.stop')}</button>`
              : `<button type="button" class="assistant-btn primary" id="assistant-send" ${this.canSend() ? '' : 'disabled'}>${t('assistant.send')}</button>`}
          </div>
        </div>
      </div>
    `;
    this.bind();
    const list = this.root.querySelector('#assistant-messages') as HTMLElement | null;
    if (list) list.scrollTop = list.scrollHeight;
  }

  private bind(): void {
    const q = <T extends Element>(sel: string) => this.root.querySelector(sel) as T | null;
    const input = q<HTMLTextAreaElement>('#assistant-input');
    input?.addEventListener('input', () => { this.draft = input.value; });
    input?.addEventListener('keydown', (e: KeyboardEvent) => {
      if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        if (this.canSend()) void this.send(input.value);
      }
    });
    q('#assistant-send')?.addEventListener('click', () => void this.send(input?.value || ''));
    q('#assistant-stop')?.addEventListener('click', () => void this.stop());
    q('#assistant-new')?.addEventListener('click', () => this.newSession());
    q('#assistant-delete')?.addEventListener('click', () => void this.deleteSession());
    q('#assistant-open-defaults')?.addEventListener('click', () => this.bus.emit('modal:open-global-defaults'));
    const select = q<HTMLSelectElement>('#assistant-session-select');
    select?.addEventListener('change', () => {
      if (select.value) void this.openSession(select.value);
      else this.newSession();
    });
    this.root.querySelectorAll<HTMLElement>('[data-suggest]').forEach(btn => {
      btn.addEventListener('click', () => void this.send(t(`assistant.${btn.dataset.suggest}`)));
    });
    this.root.querySelectorAll<HTMLElement>('[data-confirm]').forEach(btn => {
      btn.addEventListener('click', () => void this.answerConfirm(Number(btn.dataset.confirm), btn.dataset.accept === 'true'));
    });
  }
}
