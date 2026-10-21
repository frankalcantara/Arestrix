import { Graph } from '../../core/Graph.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

export class ProjectMemoryModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: 'blackboard' | 'lessons' | 'maintenance' = 'blackboard';

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public show(): void {
    this.activeTab = 'blackboard';
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
    this.refreshMemoryData();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const projectId = this.graph.getProjectId();
    const defaults = this.graph.getGlobalDefaults();
    const isGloballyPersistent = !!defaults.memory?.persistent;
    const isGloballyEnabled = !!defaults.memory?.enabled;

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('project_memory.title'),
      subtitle: t('project_memory.subtitle', { projectId: this.escapeHtml(projectId), graphName: this.escapeHtml(this.graph.getMetadata().graphName) }),
      icon: '💾',
      closeBtnId: 'btn-close-project-mem'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog project-memory-modal-dialog">
        ${titleBarHtml}

        <!-- Abas Fluent / Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'blackboard' ? 'active' : ''}" data-tab="blackboard">
            <span>📋</span> ${t('project_memory.tab_blackboard', '1. Blackboard')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'lessons' ? 'active' : ''}" data-tab="lessons">
            <span>📚</span> ${t('project_memory.tab_lessons', '2. Lições Globais')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'maintenance' ? 'active' : ''}" data-tab="maintenance">
            <span>⚡</span> ${t('project_memory.tab_maintenance', '3. Manutenção')}
          </button>
        </div>

        <div class="modal-body-scrollable">
          <!-- ABA 1: Blackboard Viewer -->
          <div class="tab-pane" id="pane-tab-blackboard" style="${this.activeTab === 'blackboard' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">📋</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.bb_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.bb_desc')}</p>
                </div>
              </div>
              <div id="blackboard-viewer" class="code-editor-header" style="background:#0f172a; color:#f8fafc; font-family:monospace; padding:12px; border-radius:6px; max-height:280px; overflow-y:auto; font-size:11.5px; white-space:pre-wrap;">
  ${t('project_memory.bb_loading')}
              </div>
            </div>
          </div>

          <!-- ABA 2: Semantic Lessons Viewer -->
          <div class="tab-pane" id="pane-tab-lessons" style="${this.activeTab === 'lessons' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">📚</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.lessons_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.lessons_desc')}</p>
                </div>
              </div>
              <div id="lessons-list" style="display: flex; flex-direction: column; gap: 8px; max-height: 240px; overflow-y: auto;">
                <div style="color: #64748b; font-size: 11px;">${t('project_memory.lessons_loading')}</div>
              </div>

              <!-- Adicionar Lição de Teste -->
              <div style="margin-top: 14px; border-top: 1px solid #edebe9; padding-top: 12px;">
                <details>
                  <summary style="cursor: pointer; font-size: 11.5px; font-weight: 700; color: #0f6cbd;">${t('project_memory.manual_lesson_summary')}</summary>
                  <div class="form-row-grid" style="margin-top: 8px;">
                    <div class="form-group">
                      <label class="form-label" for="manual-lesson-domain">${t('project_memory.manual_domain_label')}</label>
                      <input type="text" id="manual-lesson-domain" class="form-input" placeholder="ex: svg_typography, validation" />
                    </div>
                    <div class="form-group">
                      <label class="form-label" for="manual-lesson-trigger">${t('project_memory.manual_trigger_label')}</label>
                      <input type="text" id="manual-lesson-trigger" class="form-input" placeholder="ex: text-shadow detectado no SVG" />
                    </div>
                  </div>
                  <div class="form-group" style="margin-top: 8px;">
                    <label class="form-label" for="manual-lesson-rule">${t('project_memory.manual_rule_label')}</label>
                    <input type="text" id="manual-lesson-rule" class="form-input" placeholder="ex: Proibido usar text-shadow ou feDropShadow em fontes SVG; usar peso 800 e pílulas sólidas" />
                  </div>
                  <button class="btn btn-primary btn-primary-office" id="btn-add-manual-lesson" style="margin-top: 10px; padding: 6px 14px; font-size: 11.5px;">${t('project_memory.btn_save_lesson')}</button>
                </details>
              </div>
            </div>
          </div>

          <!-- ABA 3: Overview & Manutenção -->
          <div class="tab-pane" id="pane-tab-maintenance" style="${this.activeTab === 'maintenance' ? '' : 'display: none;'}">
            <div class="form-row-grid">
              <div class="settings-section-card">
                <div class="section-card-header">
                  <span class="section-card-icon">🧠</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('project_memory.persistence_regime')}</h3>
                    <p class="section-card-desc">${t('project_memory.persistence_desc')}</p>
                  </div>
                </div>
                <div style="margin-top: 8px;">
                  <span class="inheritance-status-badge ${isGloballyPersistent ? 'badge-memory-persistent' : 'badge-memory-ephemeral'}">
                    ${isGloballyPersistent ? t('project_memory.badge_persistent') : t('project_memory.badge_ephemeral')}
                  </span>
                  <div class="form-hint" style="margin-top: 8px;">
                    ${t('project_memory.pipeline_default_hint', { status: isGloballyEnabled ? t('project_memory.status_active') : t('project_memory.status_optin') })}
                  </div>
                </div>
              </div>

              <div class="settings-section-card">
                <div class="section-card-header">
                  <span class="section-card-icon">⚡</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('project_memory.maintenance_title')}</h3>
                    <p class="section-card-desc">${t('project_memory.maintenance_desc')}</p>
                  </div>
                </div>
                <div style="display: flex; gap: 8px; flex-wrap: wrap; margin-top: 10px;">
                  <button class="btn btn-secondary-pill btn-secondary-office" id="btn-refresh-memory">${t('project_memory.btn_refresh')}</button>
                  <button class="btn btn-secondary-pill btn-secondary-office" id="btn-clear-session-mem" title="${t('project_memory.btn_clear_session_tip')}">${t('project_memory.btn_clear_session')}</button>
                  <button class="btn btn-danger-pill" id="btn-clear-all-mem" title="${t('project_memory.btn_clear_all_tip')}" style="background: #dc2626; color: #fff; border: none; border-radius: 4px; padding: 6px 12px; font-size: 11.5px; cursor: pointer;">${t('project_memory.btn_clear_all')}</button>
                </div>
              </div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">📊</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.usage_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.usage_desc')}</p>
                </div>
              </div>
              <pre id="memory-usage-viewer" style="margin: 8px 0 0; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11.5px; white-space: pre-wrap;">${t('project_memory.usage_loading')}</pre>
            </div>
          </div>
        </div>

        <div class="modal-footer office-modal-footer">
          <button class="btn btn-secondary-pill btn-secondary-office" id="btn-close-project-mem-footer">${t('project_memory.btn_close')}</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 540,
        minHeight: 400,
        onClose: () => this.hide()
      });
    }

    this.attachDialogEvents();
  }

  private attachDialogEvents(): void {
    this.overlay.querySelector('#btn-close-project-mem')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-close-project-mem-footer')?.addEventListener('click', () => this.hide());

    // Manipulador de abas
    const tabButtons = this.overlay.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'blackboard' | 'lessons' | 'maintenance';
        if (!tab) return;
        this.activeTab = tab;
        tabButtons.forEach(b => {
          const bTab = b.dataset?.tab || b.getAttribute('data-tab');
          if (b.classList && typeof b.classList.toggle === 'function') {
            b.classList.toggle('active', bTab === tab);
          }
        });
        const panes = this.overlay.querySelectorAll<HTMLElement>('.tab-pane');
        panes.forEach(p => {
          p.style.display = p.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });
    this.overlay.querySelector('#btn-close-project-mem')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-close-project-mem-footer')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-refresh-memory')?.addEventListener('click', () => this.refreshMemoryData());

    this.overlay.querySelector('#btn-clear-session-mem')?.addEventListener('click', async () => {
      if (confirm(t('project_memory.confirm_clear_session'))) {
        await this.clearMemory(true);
        this.refreshMemoryData();
      }
    });

    this.overlay.querySelector('#btn-clear-all-mem')?.addEventListener('click', async () => {
      if (confirm(t('project_memory.confirm_clear_all', { projectId: this.graph.getProjectId() }))) {
        await this.clearMemory(false);
        this.refreshMemoryData();
      }
    });

    this.overlay.querySelector('#btn-add-manual-lesson')?.addEventListener('click', async () => {
      const txtDomain = this.overlay.querySelector<HTMLInputElement>('#manual-lesson-domain');
      const txtTrigger = this.overlay.querySelector<HTMLInputElement>('#manual-lesson-trigger');
      const txtRule = this.overlay.querySelector<HTMLInputElement>('#manual-lesson-rule');

      const domain = txtDomain?.value.trim() || 'geral';
      const trigger = txtTrigger?.value.trim() || 'Auditoria Manual';
      const rule = txtRule?.value.trim();

      if (!rule) {
        alert('Por favor, informe a regra profilática formulada.');
        return;
      }

      await this.recordLesson(domain, trigger, rule);
      if (txtDomain) txtDomain.value = '';
      if (txtTrigger) txtTrigger.value = '';
      if (txtRule) txtRule.value = '';
      this.refreshMemoryData();
    });
  }

  public async refreshMemoryData(): Promise<void> {
    const projectId = this.graph.getProjectId();
    const bbViewer = this.overlay.querySelector('#blackboard-viewer');
    const lessonsList = this.overlay.querySelector('#lessons-list');
    const usageViewer = this.overlay.querySelector('#memory-usage-viewer');

    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("get_memory_status", JSON.stringify({ projectId }));
        const data = JSON.parse(raw);

        // Exibe Blackboard
        if (bbViewer) {
          const bbObj = data.blackboard || {};
          const keysCount = Object.keys(bbObj).length;
          if (keysCount === 0) {
            bbViewer.textContent = t('project_memory.bb_empty');
          } else {
            bbViewer.textContent = JSON.stringify(bbObj, null, 2);
          }
        }

        // Exibe Lições Semânticas
        if (lessonsList) {
          const lessons = data.semantic?.lessons || [];
          if (lessons.length === 0) {
            lessonsList.innerHTML = `<div style="color: #64748b; font-size: 11px; padding: 10px; background: #f8fafc; border-radius: 8px;">${t('project_memory.lessons_empty')}</div>`;
          } else {
            lessonsList.innerHTML = lessons.map((l: any, idx: number) => `
              <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px 14px; display: flex; flex-direction: column; gap: 4px;">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                  <span style="font-size: 11px; font-weight: 800; color: #4338ca;">[#${idx + 1}] Domínio: ${this.escapeHtml(l.domain)}</span>
                  <span style="font-size: 10px; color: #94a3b8;">${this.escapeHtml(l.createdAt || '')}</span>
                </div>
                <div style="font-size: 12px; font-weight: 700; color: #0f172a;">${this.escapeHtml(l.lessonRule)}</div>
                ${l.triggerError ? `<div style="font-size: 10px; color: #b91c1c;">Gatilho: ${this.escapeHtml(l.triggerError)}</div>` : ''}
              </div>
            `).join('');
          }
        }

        // Exibe consumo de API e contexto de memória
        if (usageViewer) {
          const u = data.usage || {};
          const runs = Number(u.runs || 0);
          const calls = Number(u.calls || 0);
          const total = Number(u.totalTokens || 0);
          const prompt = Number(u.promptTokens || 0);
          const completion = Number(u.completionTokens || 0);
          const ctx = Number(u.memoryContextChars || 0);
          const latency = Number(u.avgLatencyMs || 0);
          const retries = Number(u.retries || 0);
          const success = Number(u.successRuns || 0);
          const failure = Number(u.failureRuns || 0);
          const hits = data.hits || {};
          usageViewer.textContent = runs === 0
            ? t('project_memory.usage_empty')
            : [
                `${t('project_memory.usage_runs')}: ${runs} (${t('project_memory.usage_success')} ${success} / ${t('project_memory.usage_failure')} ${failure})`,
                `${t('project_memory.usage_calls')}: ${calls} · ${t('project_memory.usage_retries')}: ${retries}`,
                `${t('project_memory.usage_tokens')}: ${total} (prompt ${prompt} / ${t('project_memory.usage_output')} ${completion})`,
                `${t('project_memory.usage_context')}: ${ctx}`,
                `${t('project_memory.usage_hits')}: ${Number(hits.injected || 0)} · ${t('project_memory.usage_used')}: ${Number(hits.used || 0)}`,
                `${t('project_memory.usage_latency')}: ${latency.toFixed(0)} ms`,
              ].join('\n');
        }
      } else {
        if (bbViewer) bbViewer.textContent = 'Backend WebUI em memória não detectado (Ambiente Local Vite).';
        if (lessonsList) lessonsList.innerHTML = '<div style="color:#64748b; font-size:11px;">Sem conexão nativa C++.</div>';
      }
    } catch (e: any) {
      if (bbViewer) bbViewer.textContent = `Erro ao consultar memória: ${e.message}`;
    }
  }

  private async clearMemory(sessionOnly: boolean): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        await webui.call("clear_memory", JSON.stringify({ projectId, sessionOnly }));
      }
    } catch (e) {
      console.warn("clear_memory error:", e);
    }
  }

  private async recordLesson(domain: string, triggerError: string, lessonRule: string): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        await webui.call("record_semantic_lesson", JSON.stringify({
          projectId,
          domain,
          triggerError,
          lessonRule,
          sourceItem: 'manual'
        }));
      }
    } catch (e) {
      console.warn("record_semantic_lesson error:", e);
    }
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });

    window.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && !this.overlay.classList.contains('hidden')) {
        this.hide();
      }
    });
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
