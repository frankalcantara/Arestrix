import { t } from '../i18n/index.ts';

/**
 * Seletor de servidores MCP e de suas ferramentas, usado nos Padroes Globais e no modal "Ferramentas" de cada agente.
 *
 * Mostra os servidores do projeto (os nativos do Arestrix primeiro, marcados com ⚡) com uma caixa para cada um e, ao pedir,
 * a lista de ferramentas do servidor para ligar ou desligar uma a uma. A lista de ferramentas so e carregada quando o
 * usuario a abre (um servidor externo precisa ser iniciado para dizer quais ferramentas oferece).
 *
 * Formato do resultado (o mesmo que o compilador e o runtime leem):
 *   servers: ids dos servidores escolhidos
 *   tools:   [] = todas as ferramentas dos servidores escolhidos; ou entradas "servidor/ferramenta" e "servidor/*".
 */
export interface McpPickerValue {
  servers: string[];
  tools: string[];
}

interface ServerInfo { id: string; name: string; isEnabled: boolean; isNative?: boolean }
interface ToolInfo { name: string; description: string; risk?: string }

export class McpToolPicker {
  private servers: ServerInfo[] = [];
  private selected = new Set<string>();
  private toolsOf = new Map<string, ToolInfo[]>();
  private checkedTools = new Map<string, Set<string>>();
  private expanded = new Set<string>();
  private loading = new Set<string>();
  private initialAllowed: string[] = [];
  private disabled = false;
  private message = '';

  constructor(private readonly root: HTMLElement) {}

  /** Regra identica a do runtime (McpToolbox::isToolAllowed). */
  static isToolAllowed(allowed: string[], serverId: string, toolName: string): boolean {
    if (allowed.length === 0) return true;
    return allowed.some(e => e === toolName || e === `${serverId}/${toolName}` || e === `${serverId}/*`);
  }

  async load(value: McpPickerValue, disabled: boolean): Promise<void> {
    this.selected = new Set(value.servers || []);
    this.initialAllowed = [...(value.tools || [])];
    this.disabled = disabled;
    this.toolsOf.clear();
    this.checkedTools.clear();
    this.expanded.clear();
    this.message = '';
    const b = this.backend();
    if (!b) {
      this.message = t('mcp_picker.needs_native');
      this.render();
      return;
    }
    try {
      const res = JSON.parse((await b.call('list_project_mcp_servers')) || '{}');
      this.servers = (res.servers || []) as ServerInfo[];
      if (res.error) this.message = String(res.error);
    } catch {
      this.servers = [];
      this.message = t('mcp_picker.load_failed');
    }
    // servidores citados na configuracao mas ausentes do projeto continuam visiveis (para o usuario poder tira-los)
    for (const id of this.selected) {
      if (!this.servers.some(s => s.id === id)) this.servers.push({ id, name: id, isEnabled: false });
    }
    this.render();
  }

  setDisabled(disabled: boolean): void {
    this.disabled = disabled;
    this.render();
  }

  getValue(): McpPickerValue {
    const servers = this.servers.filter(s => this.selected.has(s.id)).map(s => s.id);
    const out: string[] = [];
    let restricted = false;
    for (const id of servers) {
      const tools = this.toolsOf.get(id);
      const checked = this.checkedTools.get(id);
      if (tools && checked) {
        if (checked.size === tools.length) {
          out.push(`${id}/*`);
        } else {
          restricted = true;
          for (const tool of tools) if (checked.has(tool.name)) out.push(`${id}/${tool.name}`);
        }
      } else {
        // servidor cujas ferramentas nao foram abertas: preserva o que ja estava configurado para ele
        const carried = this.initialAllowed.filter(e => e.startsWith(`${id}/`) || !e.includes('/'));
        if (this.initialAllowed.length > 0 && carried.length > 0) {
          restricted = restricted || !carried.every(e => e === `${id}/*`);
          out.push(...carried);
        } else {
          out.push(`${id}/*`);
        }
      }
    }
    return { servers, tools: restricted ? Array.from(new Set(out)) : [] };
  }

  // ---------------------------------------------------------------------------------------------------------------------

  private backend(): any | null {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    return w && w.webui && typeof w.webui.call === 'function' ? w.webui : null;
  }

  private esc(s: string): string {
    return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  private ensureChecked(serverId: string): Set<string> {
    let set = this.checkedTools.get(serverId);
    if (!set) {
      const tools = this.toolsOf.get(serverId) || [];
      set = new Set(tools.filter(tool => McpToolPicker.isToolAllowed(this.initialAllowed, serverId, tool.name)).map(tool => tool.name));
      this.checkedTools.set(serverId, set);
    }
    return set;
  }

  private async openTools(serverId: string): Promise<void> {
    if (this.toolsOf.has(serverId)) return;
    const b = this.backend();
    if (!b) return;
    this.loading.add(serverId);
    this.render();
    try {
      const res = JSON.parse((await b.call('list_project_mcp_tools', JSON.stringify({ server: serverId }))) || '{}');
      const tools = (res.tools || []) as ToolInfo[];
      this.toolsOf.set(serverId, tools);
      this.ensureChecked(serverId);
      if (res.error) this.message = String(res.error);
    } catch {
      this.message = t('mcp_picker.load_failed');
    }
    this.loading.delete(serverId);
    this.render();
  }

  private render(): void {
    if (this.servers.length === 0) {
      this.root.innerHTML = `<div class="form-hint">${this.esc(this.message || t('mcp_picker.no_servers'))}</div>`;
      return;
    }
    const rows = this.servers.map(s => this.renderServer(s)).join('');
    const note = this.message ? `<div class="form-hint" style="margin-top: 6px;">${this.esc(this.message)}</div>` : '';
    this.root.innerHTML = `<div class="mcp-picker-list" style="display: flex; flex-direction: column; gap: 8px;">${rows}</div>${note}`;
    this.bind();
  }

  private renderServer(s: ServerInfo): string {
    const isSel = this.selected.has(s.id);
    const tools = this.toolsOf.get(s.id);
    const isOpen = this.expanded.has(s.id);
    const checked = this.checkedTools.get(s.id);
    const title = s.isNative ? `⚡ ${this.esc(s.name)}` : this.esc(s.name || s.id);
    const tag = s.isNative
      ? `<span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${this.esc(t('mcp_picker.native_tag'))}</span>`
      : (s.isEnabled ? '' : `<span style="font-size: 10px; margin-left: 6px; color: #a80000;">${this.esc(t('mcp_picker.disabled'))}</span>`);
    const count = tools && checked ? `<span style="font-size: 11px; color: var(--text-secondary); margin-left: 8px;">${this.esc(t('mcp_picker.count', { count: checked.size, total: tools.length }))}</span>` : '';

    let body = '';
    if (isSel && isOpen) {
      if (this.loading.has(s.id)) {
        body = `<div class="form-hint" style="margin: 6px 0 0 26px;">${this.esc(t('mcp_picker.loading'))}</div>`;
      } else if (tools) {
        body = `<div class="tools-checkbox-grid" style="margin: 8px 0 0 26px;">${tools.map(tool => {
          const on = !!checked?.has(tool.name);
          const risk = tool.risk === 'moderate' || tool.risk === 'high'
            ? ` <span style="font-size: 10px; color: #a80000;">${this.esc(t('mcp_picker.risk_' + tool.risk))}</span>` : '';
          return `<label class="tool-checkbox-pill ${on ? 'checked' : ''}">
            <input type="checkbox" class="mcp-pick-tool" data-server="${this.esc(s.id)}" data-tool="${this.esc(tool.name)}" ${on ? 'checked' : ''} ${this.disabled ? 'disabled' : ''} />
            <span class="tool-pill-name">${this.esc(tool.name)}${risk}</span>
            <span class="tool-pill-desc">${this.esc(tool.description || '')}</span>
          </label>`;
        }).join('')}</div>`;
      }
    }

    const toggle = isSel
      ? `<button type="button" class="btn btn-secondary-pill mcp-pick-expand" data-server="${this.esc(s.id)}" style="font-size: 11px; height: 24px; padding: 0 10px; margin-left: auto;" ${this.disabled ? 'disabled' : ''}>${this.esc(isOpen ? t('mcp_picker.hide_tools') : t('mcp_picker.pick_tools'))}</button>`
      : '';
    const warn = isSel && isOpen && !s.isNative && !tools && !this.loading.has(s.id)
      ? `<div class="form-hint" style="margin: 6px 0 0 26px;">${this.esc(t('mcp_picker.external_hint'))}</div>` : '';

    return `<div class="mcp-picker-row" style="border: 1px solid var(--border-color); border-radius: 6px; padding: 8px 10px;">
      <div style="display: flex; align-items: center; gap: 8px;">
        <label style="display: flex; align-items: center; gap: 8px; cursor: pointer;">
          <input type="checkbox" class="mcp-pick-server" data-server="${this.esc(s.id)}" ${isSel ? 'checked' : ''} ${this.disabled ? 'disabled' : ''} />
          <span style="font-weight: 600;">${title}</span>${tag}
        </label>${count}${toggle}
      </div>
      ${s.isNative && s.id ? `<div style="font-size: 11px; color: var(--text-secondary); margin: 2px 0 0 26px;">${this.esc(s.id)}</div>` : ''}
      ${warn}${body}
    </div>`;
  }

  private bind(): void {
    this.root.querySelectorAll<HTMLInputElement>('.mcp-pick-server').forEach(chk => {
      chk.addEventListener('change', () => {
        const id = chk.dataset.server || '';
        if (chk.checked) this.selected.add(id);
        else { this.selected.delete(id); this.expanded.delete(id); }
        this.render();
      });
    });
    this.root.querySelectorAll<HTMLButtonElement>('.mcp-pick-expand').forEach(btn => {
      btn.addEventListener('click', () => {
        const id = btn.dataset.server || '';
        if (this.expanded.has(id)) {
          this.expanded.delete(id);
          this.render();
        } else {
          this.expanded.add(id);
          this.render();
          void this.openTools(id);
        }
      });
    });
    this.root.querySelectorAll<HTMLInputElement>('.mcp-pick-tool').forEach(chk => {
      chk.addEventListener('change', () => {
        const set = this.ensureChecked(chk.dataset.server || '');
        const name = chk.dataset.tool || '';
        if (chk.checked) set.add(name);
        else set.delete(name);
        chk.closest('.tool-checkbox-pill')?.classList.toggle('checked', chk.checked);
        this.render();
      });
    });
  }
}
