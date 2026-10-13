import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { IProjectManifest } from '../../core/types.ts';
import { hashProjectName } from '../../core/hash.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

export class ProjectManagerModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private projectManager: ProjectManager;
  private windowController?: IModalWindowController;
  private activeTab: 'recents' | 'create' | 'settings' = 'recents';
  private defaultWorkspaceDir: string = '';

  constructor(graph: Graph) {
    this.graph = graph;
    this.projectManager = ProjectManager.getInstance();

    if (typeof document !== 'undefined') {
      this.overlay = document.createElement('div');
      this.overlay.className = 'modal-overlay hidden';
      document.body.appendChild(this.overlay);
      this.bindEvents();
    } else {
      this.overlay = {} as any;
    }
  }

  public async show(tab: 'recents' | 'create' | 'settings' = 'recents'): Promise<void> {
    this.activeTab = tab;
    await this.projectManager.init();
    await this.resolveDefaultWorkspaceDir();
    this.render();
    if (this.overlay && this.overlay.classList) {
      this.overlay.classList.remove('hidden');
    }
    this.windowController?.bringToFront();
  }

  private async resolveDefaultWorkspaceDir(): Promise<void> {
    try {
      this.defaultWorkspaceDir = await this.projectManager.getDefaultWorkspaceDirectory();
    } catch (_) {
      this.defaultWorkspaceDir = '';
    }
  }

  public hide(): void {
    if (this.overlay && this.overlay.classList) {
      this.overlay.classList.add('hidden');
    }
  }

  private render(): void {
    if (!this.overlay || typeof this.overlay.querySelector !== 'function') return;
    const cur = this.projectManager.getCurrentProject();
    const recents = this.projectManager.listRecentProjects();

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('project_manager_modal.title'),
      subtitle: t('project_manager_modal.subtitle_html', { name: this.escapeHtml(cur.name) }),
      icon: '📂',
      closeBtnId: 'btn-close-proj-manager'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog project-memory-modal-dialog" style="max-width: 860px;">
        ${titleBarHtml}

        <!-- Abas de Navegação Estilo Office -->
        <div class="modal-tabs">
          <button class="tab-btn ${this.activeTab === 'recents' ? 'active' : ''}" id="tab-btn-recents">
            ${t('project_manager_modal.tab_recents_count', { count: recents.length })}
          </button>
          <button class="tab-btn ${this.activeTab === 'create' ? 'active' : ''}" id="tab-btn-create">
            ${t('project_manager_modal.tab_create')}
          </button>
          <button class="tab-btn ${this.activeTab === 'settings' ? 'active' : ''}" id="tab-btn-settings">
            ${t('project_manager_modal.tab_settings')}
          </button>
        </div>

        <div class="modal-body-scrollable">
          ${this.activeTab === 'recents' ? this.renderRecentsTab(recents, cur) : ''}
          ${this.activeTab === 'create' ? this.renderCreateTab() : ''}
          ${this.activeTab === 'settings' ? this.renderSettingsTab(cur) : ''}
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 560,
        minHeight: 400,
        onClose: () => this.hide()
      });
    }

    this.bindInternalEvents(cur);
  }

  private renderRecentsTab(recents: any[], current: IProjectManifest): string {
    return `
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; gap: 12px;">
        <span style="font-size: 12px; color: #616161;">${t('project_manager_modal.recents_hint')}</span>
        <div style="display: flex; gap: 8px; flex-shrink: 0;">
          <button id="btn-browse-project" class="tool-btn btn-secondary" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px;">
            ${t('project_manager_modal.btn_browse_other')}
          </button>
          <button id="btn-go-to-create" class="tool-btn btn-action" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px;">
            ${t('project_manager_modal.btn_go_to_create')}
          </button>
        </div>
      </div>

      <div style="display: flex; flex-direction: column; gap: 10px;">
        ${recents.length === 0 ? `
          <div style="padding: 32px; text-align: center; color: #616161; background: #faf9f8; border: 1px solid #edebe9; border-radius: 4px;">
            ${t('project_manager_modal.empty_recents')}
          </div>
        ` : recents.map(r => {
          const isCurrent = r.projectId === current.projectId;
          return `
            <div style="padding: 14px 18px; border: 1px solid ${isCurrent ? '#0f6cbd' : '#edebe9'}; border-radius: 6px; background: #ffffff; display: flex; justify-content: space-between; align-items: center; gap: 16px;">
              <div style="flex: 1; min-width: 0;">
                <div style="display: flex; align-items: center; gap: 8px;">
                  <strong style="font-size: 14px; color: #242424; font-weight: 600;">${this.escapeHtml(r.name)}</strong>
                  ${isCurrent ? `
                    <span style="background: #eef6fc; color: #0f6cbd; border: 1px solid #c7e0f4; padding: 2px 8px; border-radius: 2px; font-weight: 600; font-size: 10px;">
                      ${t('project_manager_modal.badge_active')}
                    </span>
                  ` : ''}
                </div>
                <div style="margin-top: 5px; font-size: 12px; color: #64748b; word-break: break-all; overflow-wrap: anywhere;">
                  ${t('project_manager_modal.path_prefix')} <code style="word-break: break-all; overflow-wrap: anywhere; max-width: 100%; display: inline-block;">${this.escapeHtml(r.rootPath)}</code>
                </div>
              </div>

              <div style="flex-shrink: 0; display: flex; align-items: center; gap: 8px;">
                <select class="recent-project-action-select recents-modal-action-select" data-id="${this.escapeHtml(r.projectId)}" data-path="${this.escapeHtml(r.rootPath)}" data-name="${this.escapeHtml(r.name)}">
                  <option value="" disabled selected>${t('project_manager_modal.opt_options')}</option>
                  <option value="open">${t('project_manager_modal.opt_open')}</option>
                  <option value="manage">${t('project_manager_modal.opt_manage')}</option>
                  <option value="delete">${t('project_manager_modal.opt_delete')}</option>
                </select>
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;
  }

  private renderCreateTab(): string {
    const defaultPath = this.defaultWorkspaceDir || '';

    return `
      <div class="settings-section-card" style="padding: 20px;">
        <div class="section-card-header">
          <span class="section-card-icon">🚀</span>
          <div class="section-card-titles">
            <h3 class="section-card-title">${t('project_manager_modal.create_header_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('project_manager_modal.badge_project_scope')}</span></h3>
            <p class="section-card-desc">${t('project_manager_modal.create_header_desc')}</p>
          </div>
        </div>

        <div class="form-group" style="margin-top: 16px;">
          <label class="form-label" for="new-proj-name" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">${t('project_manager_modal.name_required_label')}</label>
          <input type="text" id="new-proj-name" class="form-input" placeholder="${t('project_manager_modal.name_placeholder')}" style="width: 100%; box-sizing: border-box;" />
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" for="new-proj-path" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">${t('project_manager_modal.root_required_label')} <span style="font-size: 10px; color: #0f6cbd; font-weight: normal;">${t('project_manager_modal.root_storage_hint')}</span></label>
          <div style="display: flex; gap: 8px;">
            <input type="text" id="new-proj-path" class="form-input" value="${this.escapeHtml(defaultPath)}" placeholder="${t('project_manager_modal.root_placeholder')}" style="flex: 1; box-sizing: border-box;" />
            <button type="button" id="btn-browse-new-proj-path" class="tool-btn btn-secondary" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px; flex-shrink: 0;">
              ${t('project_manager_modal.btn_browse')}
            </button>
          </div>
          <div class="form-hint" style="margin-top: 4px; font-size: 11px; color: #64748b;">
            ${t('project_manager_modal.infra_hint')}
          </div>
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" for="new-proj-desc" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">${t('project_manager_modal.desc_label')}</label>
          <textarea id="new-proj-desc" class="form-input" style="height: 60px; width: 100%; box-sizing: border-box;" placeholder="${t('project_manager_modal.desc_placeholder')}"></textarea>
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" style="font-weight: 600; color: #323130; margin-bottom: 8px; display: block;">${t('project_manager_modal.template_label')}</label>
          <div style="grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 6px; display: grid;">
            <label class="template-choice-card selected" style="display: flex; flex-direction: row; align-items: center; gap: 12px; padding: 14px 16px; border: 1.5px solid #0f6cbd; border-radius: 8px; background-color: #f0f7fd; cursor: pointer; transition: all 0.15s ease; user-select: none; box-sizing: border-box;">
              <input type="radio" name="new-proj-template" value="empty" checked style="accent-color: #0f6cbd; width: 18px; height: 18px; margin: 0; cursor: pointer; flex-shrink: 0;" />
              <div class="template-choice-info">
                <span class="template-choice-title">${t('project_manager_modal.template_empty_title')}</span>
                <span class="template-choice-desc">${t('project_manager_modal.template_empty_desc')}</span>
              </div>
            </label>
            <label class="template-choice-card" style="display: flex; flex-direction: row; align-items: center; gap: 12px; padding: 14px 16px; border: 1px solid #cbd5e1; border-radius: 8px; background-color: #ffffff; cursor: pointer; transition: all 0.15s ease; user-select: none; box-sizing: border-box;">
              <input type="radio" name="new-proj-template" value="agents" style="accent-color: #0f6cbd; width: 18px; height: 18px; margin: 0; cursor: pointer; flex-shrink: 0;" />
              <div class="template-choice-info">
                <span class="template-choice-title">${t('project_manager_modal.template_agents_title')}</span>
                <span class="template-choice-desc">${t('project_manager_modal.template_agents_desc')}</span>
              </div>
            </label>
          </div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 24px;">
          <button id="btn-cancel-create-proj" class="tool-btn btn-secondary" style="height: 36px; padding: 0 18px; font-size: 13px;">
            ${t('project_manager_modal.btn_cancel')}
          </button>
          <button id="btn-submit-create-proj" class="tool-btn btn-action" style="height: 36px; padding: 0 20px; font-size: 13px; font-weight: 600;">
            ${t('project_manager_modal.btn_submit_create')}
          </button>
        </div>
      </div>
    `;
  }

  private renderSettingsTab(current: IProjectManifest): string {
    const envEntries = Object.entries(current.environment || {}).map(([k, v]) => `${k}=${v}`).join('\n');

    return `
      <div class="settings-section-card" style="padding: 16px;">
        <div class="section-card-header">
          <span class="section-card-icon">⚙️</span>
          <div class="section-card-titles">
            <h3 class="section-card-title">${t('project_manager_modal.settings_header_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('project_manager_modal.badge_project_scope')}</span></h3>
            <p class="section-card-desc" style="word-break: break-all; overflow-wrap: anywhere;">${t('project_manager_modal.settings_manifest_desc', { path: this.escapeHtml(current.rootPath) })}</p>
          </div>
        </div>

        <div class="form-row-grid" style="display: grid; grid-template-columns: 2fr 1fr; gap: 12px; margin-top: 14px;">
          <div class="form-group">
            <label class="form-label" for="edit-proj-name">${t('project_manager_modal.settings_name_label')}</label>
            <input type="text" id="edit-proj-name" class="form-input" value="${this.escapeHtml(current.name)}" />
          </div>
          <div class="form-group">
            <label class="form-label" for="edit-proj-ver">${t('project_manager_modal.settings_ver_label')}</label>
            <input type="text" id="edit-proj-ver" class="form-input" value="${this.escapeHtml(current.version)}" />
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="edit-proj-desc">${t('project_manager_modal.settings_desc_label')}</label>
          <textarea id="edit-proj-desc" class="form-input" style="height: 60px;">${this.escapeHtml(current.description)}</textarea>
        </div>

        <div class="form-row-grid" style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 12px;">
          <div class="form-group">
            <label class="form-label" for="edit-proj-output-dir">${t('project_manager_modal.settings_output_dir_label')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px;">${t('project_manager_modal.badge_project_scope')}</span></label>
            <input type="text" id="edit-proj-output-dir" class="form-input" value="${this.escapeHtml(current.outputDir || 'Output')}" placeholder="Output" />
            <div class="form-hint">${t('project_manager_modal.settings_output_dir_hint')}</div>
          </div>
          <div class="form-group">
            <label class="form-label" for="edit-proj-db-path">${t('project_manager_modal.settings_db_path_label')} <span style="font-size: 10px; color: #0f6cbd;">${t('project_manager_modal.badge_workspace_scope')}</span></label>
            <input type="text" id="edit-proj-db-path" class="form-input" value="${this.escapeHtml(current.database?.filePath || 'storage/database.sqlite')}" />
            <div class="form-hint">${t('project_manager_modal.settings_db_path_hint')}</div>
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label">${t('project_manager_modal.settings_pipelines_label')}</label>
          <div style="background: rgba(0,0,0,0.05); padding: 8px 12px; border-radius: 6px; font-family: monospace; font-size: 12px;">
            ${(current.pipelines || []).map(p => `<div>• <strong>${this.escapeHtml(p.name)}</strong> (${this.escapeHtml(p.file)}) ${p.isPrimary ? `<span class="badge-inherited" style="font-size: 9px; padding: 1px 4px;">${t('project_manager_modal.settings_badge_primary')}</span>` : ''}</div>`).join('')}
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="edit-proj-env">${t('project_manager_modal.settings_env_label')}</label>
          <textarea id="edit-proj-env" class="form-input" style="height: 70px; font-family: monospace; font-size: 12px;" placeholder="${t('project_manager_modal.settings_env_placeholder')}">${this.escapeHtml(envEntries)}</textarea>
          <div class="form-hint">${t('project_manager_modal.settings_env_hint')}</div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
          <button id="btn-save-proj-settings" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
            ${t('project_manager_modal.btn_save_changes')}
          </button>
        </div>
      </div>
    `;
  }

  private bindEvents(): void {
    if (!this.overlay || typeof this.overlay.addEventListener !== 'function') return;
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  private bindInternalEvents(current: IProjectManifest): void {
    if (!this.overlay || typeof this.overlay.querySelector !== 'function') return;
    this.overlay.querySelector('#btn-close-proj-manager')?.addEventListener('click', () => this.hide());

    this.overlay.querySelector('#tab-btn-recents')?.addEventListener('click', () => {
      this.activeTab = 'recents';
      this.render();
    });

    this.overlay.querySelector('#tab-btn-create')?.addEventListener('click', () => {
      this.activeTab = 'create';
      this.render();
    });

    this.overlay.querySelector('#tab-btn-settings')?.addEventListener('click', () => {
      this.activeTab = 'settings';
      this.render();
    });

    this.overlay.querySelector('#btn-go-to-create')?.addEventListener('click', () => {
      this.activeTab = 'create';
      this.render();
    });

    // Ações de projeto recente no Modal (Dropdown Estilo Office)
    this.overlay.querySelectorAll<HTMLSelectElement>('.recents-modal-action-select').forEach(select => {
      select.addEventListener('change', async () => {
        const action = select.value;
        const pId = select.dataset.id || '';
        const pPath = select.dataset.path || '';
        const pName = select.dataset.name || 'Projeto';
        const pTarget = pPath || pId;

        // Restaura dropdown para o placeholder
        select.value = '';

        if (action === 'open') {
          if (pTarget) {
            const ok = await this.projectManager.loadProject(pTarget, this.graph);
            if (ok) {
              if (typeof alert !== 'undefined') alert(t('project_manager_modal.alert_loaded', { name: pName }));
              this.hide();
            } else {
              if (typeof alert !== 'undefined') alert(t('project_manager_modal.alert_load_fail', { name: pName }));
            }
          }
        } else if (action === 'manage') {
          if (pTarget) {
            const cur = this.projectManager.getCurrentProject();
            if (cur.projectId !== pId && cur.rootPath !== pPath) {
              await this.projectManager.loadProject(pTarget, this.graph);
            }
          }
          this.activeTab = 'settings';
          this.render();
        } else if (action === 'delete') {
          const confirmed = typeof confirm !== 'undefined'
            ? confirm(t('project_manager_modal.confirm_delete', { name: pName }))
            : true;

          if (confirmed) {
            await this.projectManager.deleteProject(pId || pPath, true, this.graph);
            if (typeof alert !== 'undefined') alert(t('project_manager_modal.alert_deleted', { name: pName }));
            this.render();
          }
        }
      });
    });

    // Abrir outro diretório
    this.overlay.querySelector('#btn-browse-project')?.addEventListener('click', async () => {
      const path = prompt(t('project_manager_modal.prompt_browse_path'));
      if (path && path.trim()) {
        const ok = await this.projectManager.loadProject(path.trim(), this.graph);
        if (ok) {
          alert(t('project_manager_modal.alert_loaded_simple'));
          this.hide();
        } else {
          alert(t('project_manager_modal.alert_load_fail_path', { path }));
        }
      }
    });

    // Seleção de pasta para o novo workspace (Procurar...)
    this.overlay.querySelector('#btn-browse-new-proj-path')?.addEventListener('click', async () => {
      try {
        if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
          const picked = await (window as any).webui.call('pick_folder');
          if (picked && picked.trim()) {
            const pathInput = this.overlay.querySelector('#new-proj-path') as HTMLInputElement;
            if (pathInput) pathInput.value = picked.trim();
          }
        } else {
          const pathInput = this.overlay.querySelector('#new-proj-path') as HTMLInputElement;
          const manual = prompt(t('project_manager_modal.prompt_workspace_dir'), pathInput?.value || '');
          if (manual && manual.trim()) {
            if (pathInput) pathInput.value = manual.trim();
          }
        }
      } catch (e) {
        console.warn('Falha ao abrir seletor de pasta:', e);
      }
    });

    // Toggle visual dos cards de template
    const templateRadios = this.overlay.querySelectorAll<HTMLInputElement>('input[name="new-proj-template"]');
    templateRadios.forEach(radio => {
      radio.addEventListener('change', () => {
        this.overlay.querySelectorAll('.template-choice-card').forEach(card => {
          card.classList.remove('selected');
          (card as HTMLElement).style.borderColor = '#cbd5e1';
          (card as HTMLElement).style.backgroundColor = '#ffffff';
        });
        const parentCard = radio.closest('.template-choice-card') as HTMLElement;
        if (parentCard) {
          parentCard.classList.add('selected');
          parentCard.style.borderColor = '#0f6cbd';
          parentCard.style.backgroundColor = '#f0f7fd';
        }
      });
    });

    // Cancelar criação e retornar à lista de recentes
    this.overlay.querySelector('#btn-cancel-create-proj')?.addEventListener('click', () => {
      this.activeTab = 'recents';
      this.render();
    });

    // Criar projeto com ID derivado de hash sobre o Nome do Projeto
    this.overlay.querySelector('#btn-submit-create-proj')?.addEventListener('click', async () => {
      const name = (this.overlay.querySelector('#new-proj-name') as HTMLInputElement)?.value.trim();
      const path = (this.overlay.querySelector('#new-proj-path') as HTMLInputElement)?.value.trim();
      const desc = (this.overlay.querySelector('#new-proj-desc') as HTMLTextAreaElement)?.value.trim();

      if (!name) {
        alert(t('project_manager_modal.alert_fill_name'));
        return;
      }
      if (!path) {
        alert(t('project_manager_modal.alert_fill_path'));
        return;
      }

      const id = hashProjectName(name);

      const ok = await this.projectManager.createProject({
        name,
        projectId: id,
        rootPath: path,
        description: desc
      }, this.graph);

      if (ok) {
        alert(t('project_manager_modal.alert_created', { name }));
        this.hide();
      } else {
        alert(t('project_manager_modal.alert_create_fail'));
      }
    });

    // Salvar configurações do projeto ativo
    this.overlay.querySelector('#btn-save-proj-settings')?.addEventListener('click', async () => {
      const name = (this.overlay.querySelector('#edit-proj-name') as HTMLInputElement)?.value.trim() || current.name;
      const ver = (this.overlay.querySelector('#edit-proj-ver') as HTMLInputElement)?.value.trim() || current.version;
      const desc = (this.overlay.querySelector('#edit-proj-desc') as HTMLTextAreaElement)?.value.trim() || current.description;
      const outputDir = (this.overlay.querySelector('#edit-proj-output-dir') as HTMLInputElement)?.value.trim() || 'Output';
      const dbPath = (this.overlay.querySelector('#edit-proj-db-path') as HTMLInputElement)?.value.trim() || 'storage/database.sqlite';
      const envRaw = (this.overlay.querySelector('#edit-proj-env') as HTMLTextAreaElement)?.value || '';

      const envMap: Record<string, string> = {};
      envRaw.split('\n').forEach(line => {
        const trimmed = line.trim();
        if (trimmed && !trimmed.startsWith('#')) {
          const eqIdx = trimmed.indexOf('=');
          if (eqIdx > 0) {
            envMap[trimmed.substring(0, eqIdx).trim()] = trimmed.substring(eqIdx + 1).trim();
          }
        }
      });

      this.projectManager.updateProjectMetadata({
        name,
        version: ver,
        description: desc,
        outputDir,
        database: { type: 'sqlite', filePath: dbPath },
        environment: envMap
      }, this.graph);

      await this.projectManager.saveCurrentProject();
      alert(t('project_manager_modal.alert_settings_saved'));
      this.hide();
    });
  }

  private escapeHtml(text: string): string {
    return (text || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
