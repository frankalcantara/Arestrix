import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { INodeData, INodeMemoryConfig, DEFAULT_NODE_MEMORY_CONFIG, type VertexMemoryMode } from '../../core/types.ts';
import { memoryModeOf, memoryThemeOf, memoryThemesOf } from '../../core/memory/MemoryConfig.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { t } from '../../i18n/index.ts';

/**
 * Memória do vértice: modo (Desligada / Consultar / Consultar e aprender) e tema.
 * Persistência, busca semântica, manutenção e retenção são configuração do projeto.
 */
export class NodeMemoryModal extends BaseModal {
  private graph: Graph;
  private activeNodeId: string | null = null;

  constructor(graph: Graph) {
    super({
      id: 'node-memory-modal',
      title: t('node_modals.memory_title'),
      icon: '💾',
      minWidth: 680,
      minHeight: 480,
      dialogClass: 'node-settings-modal-dialog',
      closeOnBackdropClick: true
    });
    this.graph = graph;
  }

  public override open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;
    this.activeNodeId = nodeId;
    this.config.subtitle = t('node_modals.memory_subtitle', {
      label: this.escapeHtml(node.label),
      id: node.id,
      projectId: this.escapeHtml(this.graph.getProjectId())
    });
    super.open();
  }

  public override hide(): void {
    super.hide();
    this.activeNodeId = null;
  }

  protected override renderBody(): string {
    const node = this.activeNodeId ? this.graph.getNode(this.activeNodeId) : null;
    if (!node) {
      return `<div class="properties-empty">${t('node_modals.no_active_vertex', 'Nenhum vértice ativo selecionado.')}</div>`;
    }

    const settings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true
    };
    const isInherited = settings.inheritMemory !== false;
    const globalMemory = this.graph.getGlobalDefaults().memory || DEFAULT_NODE_MEMORY_CONFIG;
    const nodeMemory = settings.memory || {};
    const effective: INodeMemoryConfig = { ...DEFAULT_NODE_MEMORY_CONFIG, ...globalMemory, ...nodeMemory };
    const mode = memoryModeOf(effective);
    const fallbackTheme = (node.label || node.id || '').trim();
    const theme = memoryThemeOf(effective, fallbackTheme);
    const readThemes = (effective.readThemes || []).filter((item) => item.trim());
    const taskType = (effective.taskType || '').trim();
    const budget = effective.contextBudgetTokens ?? 800;
    const maxLessons = effective.maxLessonsPerRun ?? 20;
    const maxChars = effective.maxLessonChars ?? 4000;
    const disabled = isInherited;

    const statusText = mode === 'off'
      ? t('node_modals.status_memory_disabled', 'Memória desligada')
      : mode === 'learn'
        ? t('node_modals.status_memory_learn', 'Consultar e aprender')
        : t('node_modals.status_memory_consult', 'Consultar');
    const statusDesc = mode === 'off'
      ? t('node_modals.desc_memory_disabled', 'O agente não lê nem grava memória.')
      : mode === 'learn'
        ? t('node_modals.desc_memory_learn', 'Lê o brief e a busca; propõe lições, fatos e playbooks.')
        : t('node_modals.desc_memory_consult', 'Lê o brief e a busca; não cria conhecimento novo.');

    return `
      <div class="inheritance-toggle-card">
        <label class="inheritance-checkbox-label">
          <input type="checkbox" id="chk-inherit-memory" ${isInherited ? 'checked' : ''} />
          <div class="inheritance-label-text">
            <span class="inheritance-title">${t('node_modals.inherit_memory_title')}</span>
            <span class="inheritance-desc">${t('node_modals.inherit_memory_desc')}</span>
          </div>
        </label>
        <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
          ${isInherited ? t('node_modals.badge_inherited') : t('node_modals.badge_overridden')}
        </div>
      </div>

      <div class="memory-status-banner badge-memory-${mode === 'off' ? 'disabled' : mode === 'learn' ? 'persistent' : 'ephemeral'}">
        <div class="memory-status-icon">${mode === 'off' ? '🔒' : mode === 'learn' ? '🟣' : '⚪'}</div>
        <div class="memory-status-info">
          <span class="memory-status-title">${statusText}</span>
          <span class="memory-status-desc">${statusDesc}</span>
        </div>
      </div>

      <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}">
        <div class="form-hint" style="margin-bottom: 10px;">
          ${t('node_modals.memory_effective', 'Configuração efetiva')}:
          ${mode} · ${this.escapeHtml(theme)} · ${this.escapeHtml(taskType || node.id)} · ${budget} tokens
        </div>
        <div class="settings-section-card">
          <div class="form-group">
            <label class="form-label" for="node-memory-mode">${t('node_modals.memory_mode_label', 'Modo do vértice')}</label>
            <select id="node-memory-mode" class="form-select" ${disabled ? 'disabled' : ''}>
              <option value="off" ${mode === 'off' ? 'selected' : ''}>${t('node_modals.memory_mode_off', 'Desligada')}</option>
              <option value="consult" ${mode === 'consult' ? 'selected' : ''}>${t('node_modals.memory_mode_consult', 'Consultar')}</option>
              <option value="learn" ${mode === 'learn' ? 'selected' : ''}>${t('node_modals.memory_mode_learn', 'Consultar e aprender')}</option>
            </select>
            <div class="form-hint">${t('node_modals.memory_mode_hint', 'Aprender implica salvar no projeto. A persistência é do projeto, não do vértice.')}</div>
          </div>

          <div class="form-group" style="margin-top: 12px;">
            <label class="form-label" for="node-memory-theme">${t('node_modals.memory_theme_label', 'Tema')}</label>
            <input type="text" id="node-memory-theme" class="form-input" value="${this.escapeHtml(theme)}"
                   placeholder="${t('node_modals.memory_theme_placeholder', 'ex: svg_typography')}" ${disabled ? 'disabled' : ''} />
            <div class="form-hint">${t('node_modals.memory_theme_hint', 'O tema filtra a leitura e autoriza a escrita de lições.')}</div>
          </div>
        </div>

        <details class="settings-section-card" style="margin-top: 12px;">
          <summary style="cursor: pointer; font-weight: 600;">${t('node_modals.memory_advanced', 'Avançado')}</summary>
          <div class="form-group" style="margin-top: 12px;">
            <label class="form-label" for="node-memory-read-themes">${t('node_modals.memory_read_themes_label', 'Temas extras de leitura')}</label>
            <input type="text" id="node-memory-read-themes" class="form-input" value="${this.escapeHtml(readThemes.join(', '))}"
                   placeholder="ex: validation, auditoria" ${disabled ? 'disabled' : ''} />
          </div>
          <div class="form-group" style="margin-top: 10px;">
            <label class="form-label" for="node-memory-task-type">${t('node_modals.memory_task_type_label', 'task_type (continuidade)')}</label>
            <input type="text" id="node-memory-task-type" class="form-input" value="${this.escapeHtml(taskType)}"
                   placeholder="${this.escapeHtml(node.id)}" ${disabled ? 'disabled' : ''} />
            <div class="form-hint">${t('node_modals.memory_task_type_hint', 'Default: id do vértice. Recriar o vértice reseta a continuidade.')}</div>
          </div>
          <div class="form-group" style="margin-top: 10px;">
            <label class="form-label" for="node-memory-budget">${t('node_modals.memory_budget_label', 'Orçamento do brief (tokens)')}</label>
            <input type="number" id="node-memory-budget" class="form-input" min="0" max="32000" step="100"
                   value="${budget}" ${disabled ? 'disabled' : ''} />
          </div>
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 10px;">
            <div class="form-group">
              <label class="form-label" for="node-memory-max-lessons">${t('node_modals.max_lessons_label')}</label>
              <input type="number" id="node-memory-max-lessons" class="form-input" min="1" max="500" step="1"
                     value="${maxLessons}" ${disabled ? 'disabled' : ''} />
            </div>
            <div class="form-group">
              <label class="form-label" for="node-memory-max-lesson-chars">${t('node_modals.max_lesson_chars_label')}</label>
              <input type="number" id="node-memory-max-lesson-chars" class="form-input" min="100" max="20000" step="100"
                     value="${maxChars}" ${disabled ? 'disabled' : ''} />
            </div>
          </div>
        </details>

        <div class="settings-section-card" style="margin-top: 12px;">
          <button type="button" class="btn btn-secondary-pill" id="btn-preview-memory-brief">
            ${t('node_modals.memory_preview_button', 'Ver o que este vértice vai receber')}
          </button>
          <pre id="node-memory-preview" style="margin-top: 10px; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11px; white-space: pre-wrap; max-height: 260px; overflow: auto; display: none;"></pre>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    return this.renderStandardFooter({
      secondaryId: 'btn-cancel-memory-modal',
      primaryId: 'btn-save-memory-modal',
      secondaryLabel: t('global_defaults.btn_cancel'),
      primaryLabel: t('node_modals.btn_save_agent_settings')
    });
  }

  protected override bindEvents(): void {
    super.bindEvents();
    const node = this.activeNodeId ? this.graph.getNode(this.activeNodeId) : null;
    if (!node) return;

    const chkInherit = this.dialog.querySelector<HTMLInputElement>('#chk-inherit-memory');
    const btnCancel = this.dialog.querySelector('#btn-cancel-memory-modal');
    const btnSave = this.dialog.querySelector('#btn-save-memory-modal');
    const btnPreview = this.dialog.querySelector('#btn-preview-memory-brief');

    btnCancel?.addEventListener('click', () => this.hide());
    btnPreview?.addEventListener('click', () => { void this.previewBrief(node); });
    chkInherit?.addEventListener('change', () => {
      this.saveFormState(node, chkInherit.checked);
      this.render();
    });
    btnSave?.addEventListener('click', () => {
      this.saveFormState(node, chkInherit ? chkInherit.checked : false);
      this.hide();
    });
  }

  private async previewBrief(node: INodeData): Promise<void> {
    const pre = this.dialog.querySelector<HTMLPreElement>('#node-memory-preview');
    if (!pre) return;
    const mode = (this.dialog.querySelector<HTMLSelectElement>('#node-memory-mode')?.value || 'off') as VertexMemoryMode;
    if (mode === 'off') {
      pre.style.display = 'block';
      pre.textContent = t('node_modals.memory_preview_off', 'Memória desligada: nada será injetado neste vértice.');
      return;
    }
    const themeInput = this.dialog.querySelector<HTMLInputElement>('#node-memory-theme')?.value.trim() || '';
    const theme = themeInput || (node.label || node.id || '').trim();
    const readThemes = (this.dialog.querySelector<HTMLInputElement>('#node-memory-read-themes')?.value || '')
      .split(',').map((item) => item.trim()).filter(Boolean);
    const budget = Math.max(0, Math.min(32000, Math.floor(Number(this.dialog.querySelector<HTMLInputElement>('#node-memory-budget')?.value) || 800)));
    const domains = memoryThemesOf({ theme, readThemes });
    const webui = typeof window !== 'undefined' ? (window as any).webui : null;
    if (!webui || typeof webui.call !== 'function') {
      pre.style.display = 'block';
      pre.textContent = t('node_modals.memory_preview_unavailable', 'A pré-visualização do brief está disponível no aplicativo Arestrix.');
      return;
    }
    try {
      const raw = await webui.call('preview_memory_brief', JSON.stringify({
        projectId: this.graph.getProjectId(),
        itemId: 'pipeline',
        domains,
        budget,
        taskType: this.dialog.querySelector<HTMLInputElement>('#node-memory-task-type')?.value.trim() || node.id,
        includeLessons: true
      }));
      const data = raw ? JSON.parse(String(raw)) : {};
      const meta = `[${Number(data.chars || 0)} chars · ~${Number(data.estimatedTokens || 0)} tokens${data.truncated ? ' · truncado' : ''}]`;
      pre.style.display = 'block';
      pre.textContent = `${String(data.brief || t('node_modals.memory_preview_empty', '(brief vazio)'))}\n\n${meta}`;
    } catch (error) {
      pre.style.display = 'block';
      pre.textContent = `${t('node_modals.memory_preview_error', 'Falha ao gerar o brief')}: ${String((error as any)?.message || error)}`;
    }
  }

  private saveFormState(node: INodeData, inheritMemory: boolean): void {
    const mode = (this.dialog.querySelector<HTMLSelectElement>('#node-memory-mode')?.value || 'off') as VertexMemoryMode;
    const themeInput = this.dialog.querySelector<HTMLInputElement>('#node-memory-theme')?.value.trim() || '';
    const theme = themeInput || (node.label || node.id || '').trim();
    const readThemes = (this.dialog.querySelector<HTMLInputElement>('#node-memory-read-themes')?.value || '')
      .split(',').map((item) => item.trim()).filter(Boolean);
    const taskType = this.dialog.querySelector<HTMLInputElement>('#node-memory-task-type')?.value.trim() || '';
    const budget = Math.max(0, Math.min(32000, Math.floor(Number(this.dialog.querySelector<HTMLInputElement>('#node-memory-budget')?.value) || 800)));
    const maxLessons = Math.max(1, Math.min(500, Math.floor(Number(this.dialog.querySelector<HTMLInputElement>('#node-memory-max-lessons')?.value) || 20)));
    const maxChars = Math.max(100, Math.min(20000, Math.floor(Number(this.dialog.querySelector<HTMLInputElement>('#node-memory-max-lesson-chars')?.value) || 4000)));
    const themes = memoryThemesOf({ theme, readThemes });

    if (taskType && !/^[a-z0-9_.-]{1,64}$/.test(taskType)) {
      alert(t('node_modals.memory_task_type_invalid', 'task_type inválido: use até 64 caracteres entre a-z, 0-9, _, . e -.'));
      return;
    }
    if (mode !== 'off' && budget === 0) {
      const proceed = confirm(t('node_modals.memory_budget_zero_confirm', 'Orçamento 0 desativa o brief de memória neste vértice. Continuar?'));
      if (!proceed) return;
    }

    const memoryConfig: INodeMemoryConfig = {
      enabled: mode !== 'off',
      // Compatibilidade: o runtime deriva o modo de `mode`; persistência é do projeto.
      persistent: false,
      mode,
      theme,
      readThemes,
      taskType,
      domains: themes,
      readBlackboard: false,
      writeBlackboard: false,
      semanticLessons: mode !== 'off',
      recordLessons: mode === 'learn',
      allowedKeys: [],
      writeKeys: [],
      contextBudgetTokens: budget,
      maxLessonsPerRun: maxLessons,
      maxLessonChars: maxChars
    };

    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true
    };
    const updatedSettings = { ...currentSettings, inheritMemory, memory: memoryConfig };
    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      t('node_modals.cmd_change_memory', { label: node.label })
    );
    this.graph.commandManager.execute(cmd);
  }
}
