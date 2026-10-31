import { Graph } from '../../core/Graph.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';
import { memoryModeOf, memoryThemeOf, memoryThemesOf } from '../../core/memory/MemoryConfig.ts';
import { DEFAULT_NODE_MEMORY_CONFIG } from '../../core/types.ts';

export class ProjectMemoryModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: 'knowledge' | 'lessons' | 'maintenance' = 'knowledge';

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public show(): void {
    this.activeTab = 'knowledge';
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
    const globalMemory = defaults.memory || DEFAULT_NODE_MEMORY_CONFIG;
    const anyLearn = this.graph.getNodes().some((node) => {
      if (node.type !== 'agent') return false;
      const memory = node.agentSettings?.inheritMemory === false
        ? { ...globalMemory, ...(node.agentSettings?.memory || {}) }
        : globalMemory;
      return memoryModeOf(memory) === 'learn';
    });
    const anyActive = this.graph.getNodes().some((node) => {
      if (node.type !== 'agent') return false;
      const memory = node.agentSettings?.inheritMemory === false
        ? { ...globalMemory, ...(node.agentSettings?.memory || {}) }
        : globalMemory;
      return memoryModeOf(memory) !== 'off';
    });
    const effectiveRows = this.graph.getNodes()
      .filter((node) => node.type === 'agent')
      .map((node) => {
        const inherited = node.agentSettings?.inheritMemory !== false;
        const memory = inherited
          ? globalMemory
          : { ...globalMemory, ...(node.agentSettings?.memory || {}) };
        return {
          id: node.id,
          label: node.label,
          mode: memoryModeOf(memory),
          theme: memoryThemeOf(memory, node.label || node.id),
          taskType: memory.taskType || node.id,
          inherited
        };
      });
    const effectiveHtml = effectiveRows.length === 0
      ? `<div class="form-hint">${t('project_memory.no_agents', 'Nenhum vértice de agente no projeto.')}</div>`
      : `<table style="width: 100%; border-collapse: collapse; font-size: 11.5px;">
           <thead><tr>
             <th style="text-align: left; padding: 6px;">${t('project_memory.col_vertex', 'Vértice')}</th>
             <th style="text-align: left; padding: 6px;">${t('project_memory.col_mode', 'Modo')}</th>
             <th style="text-align: left; padding: 6px;">${t('project_memory.col_theme', 'Tema')}</th>
             <th style="text-align: left; padding: 6px;">task_type</th>
             <th style="text-align: left; padding: 6px;">${t('project_memory.col_origin', 'Origem')}</th>
           </tr></thead>
           <tbody>
             ${effectiveRows.map((row) => `<tr>
               <td style="padding: 6px; border-top: 1px solid #edebe9;">${this.escapeHtml(row.label || row.id)}</td>
               <td style="padding: 6px; border-top: 1px solid #edebe9;">${row.mode}</td>
               <td style="padding: 6px; border-top: 1px solid #edebe9;">${this.escapeHtml(row.theme)}</td>
               <td style="padding: 6px; border-top: 1px solid #edebe9;">${this.escapeHtml(row.taskType)}</td>
               <td style="padding: 6px; border-top: 1px solid #edebe9;">${row.inherited ? t('project_memory.origin_inherited', 'Herdado') : t('project_memory.origin_override', 'Próprio')}</td>
             </tr>`).join('')}
           </tbody>
         </table>`;
    const retentionDays = defaults.memory?.retentionDays ?? 180;

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
          <button type="button" class="tab-btn ${this.activeTab === 'knowledge' ? 'active' : ''}" data-tab="knowledge">
            <span>📋</span> ${t('project_memory.tab_knowledge', '1. Conhecimento')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'lessons' ? 'active' : ''}" data-tab="lessons">
            <span>📚</span> ${t('project_memory.tab_lessons', '2. Lições Globais')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'maintenance' ? 'active' : ''}" data-tab="maintenance">
            <span>⚡</span> ${t('project_memory.tab_maintenance', '3. Manutenção')}
          </button>
        </div>

        <div class="modal-body-scrollable">
          <!-- ABA 1: Conhecimento do projeto -->
          <div class="tab-pane" id="pane-tab-knowledge" style="${this.activeTab === 'knowledge' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">📋</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.knowledge_title', 'Conhecimento do projeto')}</h3>
                  <p class="section-card-desc">${t('project_memory.knowledge_desc', 'Lições, fatos e estado de trabalho que a memória pode injetar no prompt.')}</p>
                </div>
              </div>
              <div id="knowledge-viewer" class="code-editor-header" style="background:#0f172a; color:#f8fafc; font-family:monospace; padding:12px; border-radius:6px; max-height:280px; overflow-y:auto; font-size:11.5px; white-space:pre-wrap;">
  ${t('project_memory.knowledge_loading', 'Carregando conhecimento…')}
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
                  <span class="inheritance-status-badge ${anyLearn ? 'badge-memory-persistent' : 'badge-memory-ephemeral'}">
                    ${anyLearn ? t('project_memory.badge_saving', 'Salvar no projeto: ligado') : t('project_memory.badge_idle', 'Salvar no projeto: ocioso')}
                  </span>
                  <div class="form-hint" style="margin-top: 8px;">
                    ${t('project_memory.save_auto_hint', 'O aprendizado é salvo automaticamente quando algum vértice aprende.')}
                    ${anyActive ? '' : ` ${t('project_memory.status_optin', 'Nenhum vértice usa memória.')}`}
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
                  <button class="btn btn-secondary-pill btn-secondary-office" id="btn-run-memory-ab">${t('project_memory.btn_run_ab', 'Comparar com e sem memória (contexto)')}</button>
                  <button class="btn btn-secondary-pill btn-secondary-office" id="btn-clear-session-mem" title="${t('project_memory.btn_clear_session_tip')}">${t('project_memory.btn_clear_session')}</button>
                  <button class="btn btn-danger-pill" id="btn-clear-all-mem" title="${t('project_memory.btn_clear_all_tip')}" style="background: #dc2626; color: #fff; border: none; border-radius: 4px; padding: 6px 12px; font-size: 11.5px; cursor: pointer;">${t('project_memory.btn_clear_all')}</button>
                </div>

                <div style="display: flex; gap: 8px; align-items: end; flex-wrap: wrap; margin-top: 14px; border-top: 1px solid #edebe9; padding-top: 12px;">
                  <div class="form-group" style="margin: 0; min-width: 150px;">
                    <label class="form-label" for="memory-retention-days">${t('project_memory.retention_label')}</label>
                    <input type="number" id="memory-retention-days" class="form-input" min="0" max="3650" step="30" value="${retentionDays}" />
                  </div>
                  <button class="btn btn-secondary-pill btn-secondary-office" id="btn-prune-memory">${t('project_memory.btn_prune')}</button>
                </div>
                <div class="form-hint" style="margin-top: 6px;">${t('project_memory.retention_hint')}</div>
                <pre id="memory-ab-report" style="display: none; margin-top: 10px; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11px; white-space: pre-wrap; max-height: 240px; overflow: auto;"></pre>
              </div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">🧾</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.effective_title', 'Configuração efetiva por vértice')}</h3>
                  <p class="section-card-desc">${t('project_memory.effective_desc', 'Modo, tema e task_type que cada agente realmente usa, com a origem da herança.')}</p>
                </div>
              </div>
              ${effectiveHtml}
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">🧠</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.embedding_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.embedding_desc')}</p>
                </div>
              </div>
              <pre id="memory-embedding-status" style="margin: 8px 0 0; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11px; white-space: pre-wrap;">${t('project_memory.embedding_loading')}</pre>
              <div style="display: flex; gap: 8px; flex-wrap: wrap; margin-top: 8px;">
                <button class="btn btn-secondary-pill btn-secondary-office" id="btn-test-embeddings">${t('project_memory.btn_test_embeddings')}</button>
                <button class="btn btn-secondary-pill btn-secondary-office" id="btn-reindex-embeddings">${t('project_memory.btn_reindex_embeddings')}</button>
              </div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">🧹</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.consolidation_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.consolidation_desc')}</p>
                </div>
              </div>
              <pre id="memory-consolidation-status" style="margin: 8px 0 0; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11px; white-space: pre-wrap;">${t('project_memory.consolidation_loading')}</pre>
              <div id="memory-conflicts-list" style="display: flex; flex-direction: column; gap: 6px; margin-top: 8px; max-height: 180px; overflow-y: auto;"></div>
              <div style="margin-top: 8px;">
                <button class="btn btn-secondary-pill btn-secondary-office" id="btn-consolidate-memory">${t('project_memory.btn_consolidate')}</button>
              </div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">🌐</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.workspace_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.workspace_desc')}</p>
                </div>
              </div>
              <pre id="memory-workspace-status" style="margin: 8px 0 0; padding: 10px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 11px; white-space: pre-wrap;">${t('project_memory.workspace_loading')}</pre>
              <div style="margin-top: 8px;">
                <button class="btn btn-secondary-pill btn-secondary-office" id="btn-init-workspace">${t('project_memory.btn_init_workspace')}</button>
              </div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">📘</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.playbooks_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.playbooks_desc')}</p>
                </div>
              </div>
              <div id="memory-playbooks-list" style="display: flex; flex-direction: column; gap: 8px; margin-top: 10px; max-height: 260px; overflow-y: auto;"></div>
            </div>

            <div class="settings-section-card" style="margin-top: 12px;">
              <div class="section-card-header">
                <span class="section-card-icon">🧩</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('project_memory.proposals_title')}</h3>
                  <p class="section-card-desc">${t('project_memory.proposals_desc')}</p>
                </div>
              </div>
              <div id="memory-proposals-list" style="display: flex; flex-direction: column; gap: 8px; margin-top: 10px; max-height: 260px; overflow-y: auto;">
                <div style="color: #64748b; font-size: 11px;">${t('project_memory.proposals_loading')}</div>
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
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'knowledge' | 'lessons' | 'maintenance';
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
    this.overlay.querySelector('#btn-run-memory-ab')?.addEventListener('click', () => { void this.runMemoryAb(); });
    this.overlay.querySelector('#btn-test-embeddings')?.addEventListener('click', () => { void this.testEmbeddings(); });
    this.overlay.querySelector('#btn-reindex-embeddings')?.addEventListener('click', () => { void this.reindexEmbeddings(); });
    this.overlay.querySelector('#btn-consolidate-memory')?.addEventListener('click', () => { void this.consolidateMemory(); });
    this.overlay.querySelector('#btn-init-workspace')?.addEventListener('click', () => { void this.initWorkspace(); });

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

    this.overlay.querySelector('#btn-prune-memory')?.addEventListener('click', async () => {
      const input = this.overlay.querySelector<HTMLInputElement>('#memory-retention-days');
      const raw = Number(input?.value);
      const days = Number.isFinite(raw) ? Math.max(0, Math.min(3650, Math.floor(raw))) : 180;
      const result = await this.pruneMemory(days);
      if (result) {
        alert(t('project_memory.prune_done', {
          days,
          episodes: result.episodes,
          hits: result.hits,
          usage: result.usage
        }));
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
    const knowledgeViewer = this.overlay.querySelector('#knowledge-viewer');
    const lessonsList = this.overlay.querySelector('#lessons-list');
    const usageViewer = this.overlay.querySelector('#memory-usage-viewer');
    const proposalsList = this.overlay.querySelector('#memory-proposals-list');
    const embeddingStatus = this.overlay.querySelector('#memory-embedding-status');
    const consolidationStatus = this.overlay.querySelector('#memory-consolidation-status');
    const conflictsList = this.overlay.querySelector('#memory-conflicts-list');
    const playbooksList = this.overlay.querySelector('#memory-playbooks-list');
    const workspaceStatus = this.overlay.querySelector('#memory-workspace-status');
    const initWorkspaceButton = this.overlay.querySelector<HTMLButtonElement>('#btn-init-workspace');

    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("get_memory_status", JSON.stringify({
          projectId,
          embedding: this.embeddingConfig()
        }));
        const data = JSON.parse(raw);

        // Exibe o conhecimento do projeto.
        if (knowledgeViewer) {
          const knowledge = data.working || {};
          const keysCount = Object.keys(knowledge).length;
          if (keysCount === 0) {
            knowledgeViewer.textContent = t('project_memory.knowledge_empty', 'Nenhum estado de trabalho gravado.');
          } else {
            knowledgeViewer.textContent = JSON.stringify(knowledge, null, 2);
          }
        }

        // Exibe as lições do projeto.
        if (lessonsList) {
          const lessons = data.lessons || [];
          const canPromote = !!data.workspace?.configured;
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
                ${canPromote && l.id ? `<button class="btn btn-secondary-pill btn-secondary-office btn-promote-lesson" data-lesson-id="${this.escapeHtml(String(l.id))}" style="align-self: flex-start; padding: 3px 9px; font-size: 10px;">${t('project_memory.btn_promote_workspace')}</button>` : ''}
              </div>
            `).join('');
            lessonsList.querySelectorAll<HTMLButtonElement>('.btn-promote-lesson').forEach(button => {
              button.addEventListener('click', () => {
                void this.promoteLesson(button.dataset.lessonId || '');
              });
            });
          }
        }

        // Exibe propostas L2 pendentes
        if (proposalsList) {
          this.renderProposals(proposalsList, Array.isArray(data.proposals) ? data.proposals : []);
        }

        if (embeddingStatus) {
          const emb = data.embedding || {};
          const state = String(emb.state || (emb.active ? 'ready' : 'stale'));
          const stateLabel = state === 'ready'
            ? t('project_memory.embedding_ready', 'pronto')
            : state === 'pending'
              ? t('project_memory.embedding_pending', 'reindexando')
              : state === 'partial'
                ? t('project_memory.embedding_partial', 'parcial')
                : state === 'failed'
                  ? t('project_memory.embedding_failed', 'falhou')
                  : t('project_memory.embedding_stale', 'stale (reindexe)');
          const lines = [
            `${t('project_memory.embedding_state')}: ${stateLabel}`,
            `${t('project_memory.embedding_model')}: ${emb.model || '-'}`,
            `${t('project_memory.embedding_endpoint')}: ${emb.localBaseUrl || '-'}`,
            `${t('project_memory.embedding_indexed')}: ${Number(emb.indexed || 0)} / ${Number(emb.candidates || 0)}`,
            `${t('project_memory.embedding_fingerprint', 'Fingerprint')}: ${emb.fingerprintShort || '-'}`
          ];
          if (emb.error) lines.push(`${t('project_memory.embedding_error', 'Erro')}: ${emb.error}`);
          embeddingStatus.textContent = lines.join('\n');
        }

        if (consolidationStatus) {
          const c = data.consolidation || {};
          const maintenance = c.maintenance || {};
          const configuredMode = this.graph.getGlobalDefaults().memory?.consolidationMode || 'off';
          let maintenanceLines: string[];
          if (maintenance.enabled) {
            maintenanceLines = [
              `${t('project_memory.consolidation_background')}: ${maintenance.mode || 'off'}${maintenance.busy ? ` (${t('project_memory.consolidation_background_running')})` : ''}`,
              `${t('project_memory.consolidation_background_runs')}: ${Number(maintenance.runs || 0)}`,
              `${t('project_memory.consolidation_background_last')}: ${maintenance.lastRunAt || '-'}`
            ];
          } else if (configuredMode !== 'off') {
            maintenanceLines = [
              `${t('project_memory.consolidation_background')}: ${t('project_memory.consolidation_background_configured', { mode: configuredMode })}`
            ];
          } else {
            maintenanceLines = [
              `${t('project_memory.consolidation_background')}: ${t('project_memory.consolidation_background_off')}`
            ];
          }
          if (maintenance.enabled && maintenance.nextInEpisodes !== undefined) {
            maintenanceLines.push(`${t('project_memory.consolidation_background_next_episodes')}: ${Number(maintenance.nextInEpisodes)}`);
          }
          if (maintenance.enabled && maintenance.nextInSeconds !== undefined) {
            maintenanceLines.push(`${t('project_memory.consolidation_background_next_seconds')}: ${Number(maintenance.nextInSeconds)}`);
          }
          if (maintenance.lastError) {
            maintenanceLines.push(`${t('project_memory.consolidation_background_error')}: ${maintenance.lastError}`);
          }
          consolidationStatus.textContent = [
            `${t('project_memory.consolidation_active')}: ${Number(c.active || 0)}`,
            `${t('project_memory.consolidation_candidate')}: ${Number(c.candidate || 0)}`,
            `${t('project_memory.consolidation_superseded')}: ${Number(c.superseded || 0)}`,
            `${t('project_memory.consolidation_dormant')}: ${Number(c.dormant || 0)}`,
            `${t('project_memory.consolidation_conflicts')}: ${Number(c.conflicts || 0)}`,
            `${t('project_memory.consolidation_aliases')}: ${Number(c.aliases || 0)}`,
            ...maintenanceLines
          ].join('\n');
        }
        if (conflictsList) {
          const conflictEdges = Array.isArray(data.consolidation?.conflictEdges)
            ? data.consolidation.conflictEdges : [];
          if (conflictEdges.length === 0) {
            conflictsList.innerHTML = `<div style="color: #64748b; font-size: 11px;">${t('project_memory.consolidation_no_conflicts')}</div>`;
          } else {
            conflictsList.innerHTML = conflictEdges.map((edge: any) => `
              <div style="background: #fff7ed; border: 1px solid #fed7aa; border-radius: 6px; padding: 6px 8px; font-size: 10.5px; color: #9a3412;">
                <strong>${this.escapeHtml(String(edge.srcName || '?'))}</strong>
                --CONTRADICTS-->
                <strong>${this.escapeHtml(String(edge.dstName || '?'))}</strong>
                <span style="color: #64748b;"> (${this.escapeHtml(String(edge.edgeStatus || edge.status || ''))})</span>
              </div>`).join('');
          }
        }

        if (workspaceStatus) {
          const ws = data.workspace || {};
          workspaceStatus.textContent = ws.configured
            ? [
                `${t('project_memory.workspace_id')}: ${ws.workspaceId || '-'}`,
                `${t('project_memory.workspace_database')}: ${ws.database || '-'}`,
                `${t('project_memory.workspace_lessons')}: ${Number(ws.lessons || 0)}`,
                `${t('project_memory.workspace_playbooks')}: ${Number(ws.playbooks || 0)}`,
                `${t('project_memory.workspace_edges')}: ${Number(ws.activeEdges || 0)}`
              ].join('\n')
            : t('project_memory.workspace_not_configured');
          if (initWorkspaceButton) {
            initWorkspaceButton.style.display = ws.configured ? 'none' : '';
          }
        }

        if (playbooksList) {
          const playbooks = Array.isArray(data.playbooks) ? data.playbooks : [];
          if (playbooks.length === 0) {
            playbooksList.innerHTML = `<div style="color: #64748b; font-size: 11px;">${t('project_memory.playbooks_empty')}</div>`;
          } else {
            playbooksList.innerHTML = playbooks.map((playbook: any) => {
              const steps = Array.isArray(playbook.steps)
                ? playbook.steps.map((step: any) => String(step.tool || step.action || '')).filter(Boolean).join(' -> ')
                : '';
              return `
                <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 8px 10px;">
                  <div style="display: flex; justify-content: space-between; gap: 8px; align-items: center;">
                    <strong style="font-size: 11px; color: #1d4ed8;">${this.escapeHtml(String(playbook.taskType || ''))}</strong>
                    <span style="font-size: 10px; color: #64748b;">${this.escapeHtml(String(playbook.status || ''))} · ${Number(playbook.successCount || 0)}/${Number(playbook.failureCount || 0)}</span>
                  </div>
                  <div style="font-size: 11px; color: #0f172a; margin-top: 3px;">${this.escapeHtml(String(playbook.title || ''))}</div>
                  <div style="font-size: 10.5px; color: #475569; margin-top: 4px; white-space: pre-wrap;">${this.escapeHtml(steps)}</div>
                </div>`;
            }).join('');
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
                `${t('project_memory.usage_cached', 'Prompt em cache')}: ${Number(u.cachedTokens || 0)} (${t('project_memory.usage_cache_write', 'escrita')} ${Number(u.cacheWriteTokens || 0)})`,
                `${t('project_memory.usage_context')}: ${ctx}`,
                `${t('project_memory.usage_hits')}: ${Number(hits.injected || 0)} · ${t('project_memory.usage_used_injected')}: ${Number(hits.usedInjected || 0)} · ${t('project_memory.usage_consulted')}: ${Number(hits.consulted || 0)}`,
                `${t('project_memory.usage_latency')}: ${latency.toFixed(0)} ms`,
              ].join('\n');
        }
      } else {
        if (knowledgeViewer) knowledgeViewer.textContent = 'Backend WebUI em memória não detectado (Ambiente Local Vite).';
        if (lessonsList) lessonsList.innerHTML = '<div style="color:#64748b; font-size:11px;">Sem conexão nativa C++.</div>';
      }
    } catch (e: any) {
      if (knowledgeViewer) knowledgeViewer.textContent = `Erro ao consultar memória: ${e.message}`;
    }
  }

  private embeddingConfig(): any {
    const defaults: any = this.graph.getGlobalDefaults()?.memory || {};
    return {
      embeddingEnabled: !!defaults.embeddingEnabled,
      embeddingProvider: defaults.embeddingProvider || 'local',
      embeddingModel: defaults.embeddingModel || 'embeddinggemma-2',
      embeddingBaseUrl: defaults.embeddingBaseUrl || '',
      embeddingTimeoutSeconds: Number(defaults.embeddingTimeoutSeconds || 60),
      embeddingBatchSize: Number(defaults.embeddingBatchSize || 8),
      embeddingMaxChars: Number(defaults.embeddingMaxChars || 8000),
      embeddingDimensions: Number(defaults.embeddingDimensions || 768)
    };
  }

  private async initWorkspace(): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("init_workspace", JSON.stringify({ projectId }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t('project_memory.workspace_created')
          : t('project_memory.workspace_error', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.workspace_error', { error: String(e?.message || e) }));
    }
  }

  private async promoteLesson(lessonId: string): Promise<void> {
    if (!lessonId) return;
    const note = prompt(t('project_memory.workspace_evidence_prompt'), t('project_memory.workspace_evidence_default'));
    if (note === null) return;
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("promote_to_workspace", JSON.stringify({
          projectId,
          kind: 'lesson',
          id: lessonId,
          evidence: note
        }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t('project_memory.workspace_promoted')
          : t('project_memory.workspace_error', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.workspace_error', { error: String(e?.message || e) }));
    }
  }

  private async consolidateMemory(): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("consolidate_memory", JSON.stringify({
          projectId,
          consolidation: {
            deduplicate: true,
            detectContradictions: true,
            expireFacts: true,
            applyDecay: true,
            generalize: true,
            dormantAfterDays: 180,
            generalizeMinSources: 3
          }
        }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t('project_memory.consolidation_done', {
              duplicates: String(result.duplicates || 0),
              conflicts: String(result.contradictions || 0),
              dormant: String(result.dormant || 0),
              generalizations: String(result.generalizations || 0)
            })
          : t('project_memory.consolidation_error', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.consolidation_error', { error: String(e?.message || e) }));
    }
  }

  private async testEmbeddings(): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("test_memory_embeddings",
          JSON.stringify({ projectId, embedding: this.embeddingConfig() }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t('project_memory.embedding_test_ok', {
              model: String(result.model || ''),
              dim: String(result.dim || 0),
              ms: String(result.latencyMs || 0)
            })
          : t('project_memory.embedding_test_fail', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.embedding_test_fail', { error: String(e?.message || e) }));
    }
  }

  private async reindexEmbeddings(): Promise<void> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("reindex_memory_embeddings",
          JSON.stringify({ projectId, embedding: this.embeddingConfig(), force: true }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t('project_memory.embedding_reindex_done', {
              indexed: String(result.indexed || 0),
              skipped: String(result.skipped || 0),
              failed: String(result.failed || 0)
            })
          : t('project_memory.embedding_test_fail', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.embedding_test_fail', { error: String(e?.message || e) }));
    }
  }

  private renderProposals(container: Element, proposals: any[]): void {
    if (proposals.length === 0) {
      container.innerHTML = `<div style="color: #64748b; font-size: 11px; padding: 10px; background: #f8fafc; border-radius: 8px;">${t('project_memory.proposals_empty')}</div>`;
      return;
    }
    container.innerHTML = proposals.map((proposal: any) => {
      const id = this.escapeHtml(String(proposal.id || ''));
      const kind = this.escapeHtml(String(proposal.kind || ''));
      const confidence = Number(proposal.confidence || 0).toFixed(2);
      const payload = this.escapeHtml(String(proposal.payloadJson || ''));
      return `
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px 12px;">
          <div style="display: flex; justify-content: space-between; gap: 8px; align-items: center;">
            <span style="font-size: 11px; font-weight: 800; color: #4338ca;">${kind}</span>
            <span style="font-size: 10px; color: #64748b;">${confidence}</span>
          </div>
          <pre style="margin: 6px 0; padding: 8px; border-radius: 6px; background: #f8fafc; color: #0f172a; font-size: 10.5px; white-space: pre-wrap; max-height: 90px; overflow-y: auto;">${payload}</pre>
          <div style="display: flex; gap: 6px;">
            <button class="btn btn-primary btn-primary-office btn-accept-proposal" data-proposal-id="${id}" style="padding: 4px 10px; font-size: 10.5px;">${t('project_memory.btn_accept_proposal')}</button>
            <button class="btn btn-secondary-pill btn-secondary-office btn-reject-proposal" data-proposal-id="${id}" style="padding: 4px 10px; font-size: 10.5px;">${t('project_memory.btn_reject_proposal')}</button>
          </div>
        </div>`;
    }).join('');

    container.querySelectorAll<HTMLButtonElement>('.btn-accept-proposal').forEach(button => {
      button.addEventListener('click', async () => {
        await this.reviewProposal(button.dataset.proposalId || '', true);
      });
    });
    container.querySelectorAll<HTMLButtonElement>('.btn-reject-proposal').forEach(button => {
      button.addEventListener('click', async () => {
        await this.reviewProposal(button.dataset.proposalId || '', false);
      });
    });
  }

  private async reviewProposal(proposalId: string, accept: boolean): Promise<void> {
    if (!proposalId) return;
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call(accept ? "accept_memory_proposal" : "reject_memory_proposal",
          JSON.stringify({ projectId, proposalId, reason: accept ? '' : 'rejeitada no Studio' }));
        const result = JSON.parse(raw);
        alert(result.ok
          ? t(accept ? 'project_memory.proposal_accepted' : 'project_memory.proposal_rejected')
          : t('project_memory.proposal_error', { error: String(result.error || '') }));
        this.refreshMemoryData();
      }
    } catch (e: any) {
      alert(t('project_memory.proposal_error', { error: String(e?.message || e) }));
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

  private async pruneMemory(days: number): Promise<{ episodes: number; hits: number; usage: number } | null> {
    const projectId = this.graph.getProjectId();
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const raw = await webui.call("prune_memory", JSON.stringify({ projectId, days }));
        const parsed = JSON.parse(raw);
        return {
          episodes: Number(parsed.episodes || 0),
          hits: Number(parsed.hits || 0),
          usage: Number(parsed.usage || 0)
        };
      }
    } catch (e) {
      console.warn("prune_memory error:", e);
    }
    return null;
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

  private async runMemoryAb(): Promise<void> {
    const pre = this.overlay.querySelector<HTMLPreElement>('#memory-ab-report');
    if (!pre) return;
    const defaults = this.graph.getGlobalDefaults();
    const globalMemory = defaults.memory || DEFAULT_NODE_MEMORY_CONFIG;
    const tasks: Array<{ id: string; domains: string[]; taskType: string; budget: number }> = [];
    for (const node of this.graph.getNodes()) {
      if (node.type !== 'agent') continue;
      const inherited = node.agentSettings?.inheritMemory !== false;
      const memory = inherited
        ? globalMemory
        : { ...globalMemory, ...(node.agentSettings?.memory || {}) };
      if (memoryModeOf(memory) === 'off') continue;
      tasks.push({
        id: node.id,
        domains: memoryThemesOf(memory, node.label || node.id),
        taskType: memory.taskType || node.id,
        budget: memory.contextBudgetTokens ?? 800
      });
    }
    if (tasks.length === 0) {
      pre.style.display = 'block';
      pre.textContent = t('project_memory.ab_no_tasks', 'Nenhum vértice ativo usa memória. Nada para comparar.');
      return;
    }
    const webui = typeof window !== 'undefined' ? (window as any).webui : null;
    if (!webui || typeof webui.call !== 'function') {
      pre.style.display = 'block';
      pre.textContent = t('project_memory.ab_unavailable', 'A comparação está disponível no aplicativo Arestrix.');
      return;
    }
    try {
      const raw = await webui.call('run_memory_ab', JSON.stringify({
        projectId: this.graph.getProjectId(),
        tasks
      }));
      const data = raw ? JSON.parse(String(raw)) : {};
      const lines: string[] = [];
      lines.push(t('project_memory.ab_title', 'A/B de contexto (v1)'));
      lines.push(`${t('project_memory.ab_fingerprint', 'Fingerprint da cópia congelada')}: ${String(data.fingerprint || '-')}`);
      lines.push(`${t('project_memory.ab_frozen', 'Cópia congelada')}: ${String(data.frozenPath || '-')}`);
      lines.push('');
      for (const task of Array.isArray(data.tasks) ? data.tasks : []) {
        lines.push(`- ${task.id}: ON ${Number(task.onTokens || 0)} tokens / ${Number(task.onChars || 0)} chars · `
          + `${t('project_memory.ab_lessons', 'lições')} ${Number(task.lessons || 0)} · `
          + `${t('project_memory.ab_facts', 'fatos')} ${Number(task.facts || 0)} · `
          + `${t('project_memory.ab_playbooks', 'playbooks')} ${Number(task.playbooks || 0)}`
          + `${task.truncated ? ` · ${t('project_memory.ab_truncated', 'truncado')}` : ''}`);
      }
      lines.push('');
      lines.push(t('project_memory.ab_note', 'v1 mede o custo e o conteúdo do contexto; a taxa de aprovação do validador entra no A/B completo.'));
      pre.style.display = 'block';
      pre.textContent = lines.join('\n');
    } catch (error) {
      pre.style.display = 'block';
      pre.textContent = `${t('project_memory.ab_error', 'Falha na comparação')}: ${String((error as any)?.message || error)}`;
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
