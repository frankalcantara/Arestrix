import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { IProjectManifest } from '../../core/types.ts';

export class ProjectManagerModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private projectManager: ProjectManager;
  private activeTab: 'recents' | 'create' | 'settings' = 'recents';

  constructor(graph: Graph) {
    this.graph = graph;
    this.projectManager = ProjectManager.getInstance();

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public async show(tab: 'recents' | 'create' | 'settings' = 'recents'): Promise<void> {
    this.activeTab = tab;
    await this.projectManager.init();
    this.render();
    this.overlay.classList.remove('hidden');
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const cur = this.projectManager.getCurrentProject();
    const recents = this.projectManager.listRecentProjects();

    this.overlay.innerHTML = `
      <div class="modal-dialog project-memory-modal-dialog" style="max-width: 860px;">
        <div class="modal-header">
          <div class="modal-title-group">
            <div class="modal-header-icon">📁</div>
            <div>
              <h2 class="modal-title">Gerenciador de Projetos & Workspaces</h2>
              <div class="modal-subtitle">Projeto Ativo: <strong>${this.escapeHtml(cur.name)}</strong> (ID: <code>${this.escapeHtml(cur.projectId)}</code>)</div>
            </div>
          </div>
          <button class="modal-close-btn" id="btn-close-proj-manager">&times;</button>
        </div>

        <!-- Abas de Navegação -->
        <div style="display: flex; gap: 8px; border-bottom: 1px solid var(--border-color); padding: 0 24px; background: rgba(0,0,0,0.03);">
          <button class="tool-btn btn-secondary ${this.activeTab === 'recents' ? 'btn-active' : ''}" id="tab-btn-recents" style="border-radius: 8px 8px 0 0; border-bottom: none; font-weight: 700;">
            📁 Projetos Recentes (${recents.length})
          </button>
          <button class="tool-btn btn-secondary ${this.activeTab === 'create' ? 'btn-active' : ''}" id="tab-btn-create" style="border-radius: 8px 8px 0 0; border-bottom: none; font-weight: 700;">
            ✨ Novo Projeto
          </button>
          <button class="tool-btn btn-secondary ${this.activeTab === 'settings' ? 'btn-active' : ''}" id="tab-btn-settings" style="border-radius: 8px 8px 0 0; border-bottom: none; font-weight: 700;">
            ⚙️ Configurações do Projeto Ativo
          </button>
        </div>

        <div class="modal-body-scrollable">
          ${this.activeTab === 'recents' ? this.renderRecentsTab(recents, cur) : ''}
          ${this.activeTab === 'create' ? this.renderCreateTab() : ''}
          ${this.activeTab === 'settings' ? this.renderSettingsTab(cur) : ''}
        </div>
      </div>
    `;

    this.bindInternalEvents(cur);
  }

  private renderRecentsTab(recents: any[], current: IProjectManifest): string {
    return `
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
        <span class="section-card-desc">Selecione um projeto aberto anteriormente para carregar seu workspace e pipelines</span>
        <div style="display: flex; gap: 8px;">
          <button id="btn-browse-project" class="btn-action-pill" style="background: var(--surface-bg); color: var(--text-primary); border: 1px solid var(--border-color);">
            📂 Abrir Outro Diretório...
          </button>
          <button id="btn-go-to-create" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
            + Criar Novo Projeto
          </button>
        </div>
      </div>

      <div style="display: flex; flex-direction: column; gap: 10px;">
        ${recents.length === 0 ? `
          <div style="padding: 32px; text-align: center; color: var(--text-secondary); background: var(--surface-bg); border-radius: 8px;">
            Nenhum projeto registrado no histórico recente. Clique em "+ Criar Novo Projeto" para começar.
          </div>
        ` : recents.map(r => {
          const isCurrent = r.projectId === current.projectId;
          return `
            <div class="settings-section-card" style="padding: 14px; border: 1px solid ${isCurrent ? 'var(--accent-color)' : 'var(--border-color)'}; display: flex; justify-content: space-between; align-items: center;">
              <div>
                <div style="display: flex; align-items: center; gap: 8px;">
                  <strong style="font-size: 15px; color: var(--text-primary); font-weight: 700;">${this.escapeHtml(r.name)}</strong>
                  ${isCurrent ? `
                    <span class="inheritance-status-badge badge-inherited" style="font-weight: 700; font-size: 10px;">
                      🟢 ATIVO
                    </span>
                  ` : ''}
                </div>
                <div style="margin-top: 4px; font-family: monospace; font-size: 11px; color: var(--text-secondary);">
                  ID: <strong>${this.escapeHtml(r.projectId)}</strong> | Caminho: <code>${this.escapeHtml(r.rootPath)}</code>
                </div>
              </div>

              <div>
                ${isCurrent ? `
                  <button class="tool-btn btn-secondary" disabled style="opacity: 0.6; cursor: default;">
                    Em Execução
                  </button>
                ` : `
                  <button class="btn-open-recent-project tool-btn btn-action" data-path="${this.escapeHtml(r.rootPath)}" data-id="${this.escapeHtml(r.projectId)}">
                    Abrir Workspace
                  </button>
                `}
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;
  }

  private renderCreateTab(): string {
    return `
      <div class="settings-section-card" style="padding: 16px;">
        <div class="section-card-header">
          <span class="section-card-icon">🚀</span>
          <div class="section-card-titles">
            <h3 class="section-card-title">Criar Novo Workspace de Projeto</h3>
            <p class="section-card-desc">Configuração do manifesto .forgeproj com isolamento de pipelines, memória e sandboxes</p>
          </div>
        </div>

        <div class="form-row-grid" style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 14px;">
          <div class="form-group">
            <label class="form-label" for="new-proj-name">Nome do Projeto *</label>
            <input type="text" id="new-proj-name" class="form-input" placeholder="ex: Fechamento Contábil Q3" />
          </div>
          <div class="form-group">
            <label class="form-label" for="new-proj-id">Identificador Único (slug) *</label>
            <input type="text" id="new-proj-id" class="form-input" placeholder="ex: fechamento_contabil_q3" />
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="new-proj-path">Diretório Raiz do Workspace *</label>
          <input type="text" id="new-proj-path" class="form-input" placeholder="C:\\ArestrixProjects\\FechamentoContabil" />
          <div class="form-hint">O Arestrix criará automaticamente as subpastas <code>pipelines/</code>, <code>storage/</code> e <code>output/</code>.</div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="new-proj-desc">Descrição do Projeto</label>
          <textarea id="new-proj-desc" class="form-input" style="height: 60px;" placeholder="Finalidade, escopo e regras deste pipeline..."></textarea>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label">Template Inicial de Pipeline:</label>
          <div style="display: flex; gap: 10px; margin-top: 6px;">
            <label class="tool-checkbox-pill checked">
              <input type="radio" name="new-proj-template" value="empty" checked />
              <span class="tool-pill-name">Pipeline Vazio</span>
            </label>
            <label class="tool-checkbox-pill">
              <input type="radio" name="new-proj-template" value="agents" />
              <span class="tool-pill-name">Pipeline Multi-Agente IA</span>
            </label>
          </div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 20px;">
          <button id="btn-cancel-create-proj" class="btn-action-pill" style="background: var(--surface-bg); color: var(--text-primary); border: 1px solid var(--border-color);">
            Cancelar
          </button>
          <button id="btn-submit-create-proj" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
            🚀 Criar e Ativar Workspace
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
            <h3 class="section-card-title">Propriedades do Workspace Ativo</h3>
            <p class="section-card-desc">Manifesto: <code>${this.escapeHtml(current.rootPath)}/arestrix.project.json</code></p>
          </div>
        </div>

        <div class="form-row-grid" style="display: grid; grid-template-columns: 2fr 1fr; gap: 12px; margin-top: 14px;">
          <div class="form-group">
            <label class="form-label" for="edit-proj-name">Nome do Projeto</label>
            <input type="text" id="edit-proj-name" class="form-input" value="${this.escapeHtml(current.name)}" />
          </div>
          <div class="form-group">
            <label class="form-label" for="edit-proj-ver">Versão</label>
            <input type="text" id="edit-proj-ver" class="form-input" value="${this.escapeHtml(current.version)}" />
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="edit-proj-desc">Descrição</label>
          <textarea id="edit-proj-desc" class="form-input" style="height: 60px;">${this.escapeHtml(current.description)}</textarea>
        </div>

        <div class="form-row-grid" style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 12px;">
          <div class="form-group">
            <label class="form-label" for="edit-proj-db-path">Arquivo do Banco de Dados Relacional (SQLite)</label>
            <input type="text" id="edit-proj-db-path" class="form-input" value="${this.escapeHtml(current.database?.filePath || 'storage/database.sqlite')}" />
            <div class="form-hint">Caminho relativo à raiz do workspace.</div>
          </div>
          <div class="form-group">
            <label class="form-label">Pipelines Cadastrados</label>
            <div style="background: rgba(0,0,0,0.1); padding: 8px 12px; border-radius: 6px; font-family: monospace; font-size: 12px;">
              ${(current.pipelines || []).map(p => `<div>• <strong>${this.escapeHtml(p.name)}</strong> (${this.escapeHtml(p.file)}) ${p.isPrimary ? '<span class="badge-inherited" style="font-size: 9px; padding: 1px 4px;">PRINCIPAL</span>' : ''}</div>`).join('')}
            </div>
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="edit-proj-env">Variáveis de Ambiente do Workspace (.env)</label>
          <textarea id="edit-proj-env" class="form-input" style="height: 70px; font-family: monospace; font-size: 12px;" placeholder="CHAVE_API=valor\nMODO=producao">${this.escapeHtml(envEntries)}</textarea>
          <div class="form-hint">Variáveis injetadas no ambiente dos subprocessos e sandboxes deste projeto.</div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
          <button id="btn-save-proj-settings" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
            💾 Salvar Alterações do Workspace
          </button>
        </div>
      </div>
    `;
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  private bindInternalEvents(current: IProjectManifest): void {
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

    // Abrir projeto recente
    this.overlay.querySelectorAll('.btn-open-recent-project').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        const path = (e.currentTarget as HTMLElement).getAttribute('data-path');
        if (path) {
          const ok = await this.projectManager.loadProject(path, this.graph);
          if (ok) {
            alert(`Workspace '${path}' carregado com sucesso!`);
            this.hide();
          } else {
            alert(`Falha ao abrir workspace: ${path}`);
          }
        }
      });
    });

    // Abrir outro diretório
    this.overlay.querySelector('#btn-browse-project')?.addEventListener('click', async () => {
      const path = prompt('Informe o caminho do diretório ou do arquivo arestrix.project.json:');
      if (path && path.trim()) {
        const ok = await this.projectManager.loadProject(path.trim(), this.graph);
        if (ok) {
          alert(`Workspace carregado com sucesso!`);
          this.hide();
        } else {
          alert(`Falha ao carregar workspace: ${path}`);
        }
      }
    });

    // Nome -> slug automático no formulário de criação
    const newNameInput = this.overlay.querySelector('#new-proj-name') as HTMLInputElement;
    const newIdInput = this.overlay.querySelector('#new-proj-id') as HTMLInputElement;
    const newPathInput = this.overlay.querySelector('#new-proj-path') as HTMLInputElement;

    if (newNameInput && newIdInput && newPathInput) {
      newNameInput.addEventListener('input', () => {
        const slug = newNameInput.value.toLowerCase().replace(/[^a-z0-9_-]/g, '_');
        newIdInput.value = slug;
        newPathInput.value = `./projects/${slug}`;
      });
    }

    // Criar projeto
    this.overlay.querySelector('#btn-submit-create-proj')?.addEventListener('click', async () => {
      const name = (this.overlay.querySelector('#new-proj-name') as HTMLInputElement)?.value.trim();
      const id = (this.overlay.querySelector('#new-proj-id') as HTMLInputElement)?.value.trim();
      const path = (this.overlay.querySelector('#new-proj-path') as HTMLInputElement)?.value.trim();
      const desc = (this.overlay.querySelector('#new-proj-desc') as HTMLTextAreaElement)?.value.trim();

      if (!name || !id || !path) {
        alert('Por favor, preencha Nome, ID e Diretório Raiz do projeto.');
        return;
      }

      const ok = await this.projectManager.createProject({
        name,
        projectId: id,
        rootPath: path,
        description: desc
      }, this.graph);

      if (ok) {
        alert(`Projeto '${name}' criado e ativado com sucesso!`);
        this.hide();
      } else {
        alert('Falha ao criar diretório e manifesto do projeto.');
      }
    });

    // Salvar configurações do projeto ativo
    this.overlay.querySelector('#btn-save-proj-settings')?.addEventListener('click', async () => {
      const name = (this.overlay.querySelector('#edit-proj-name') as HTMLInputElement)?.value.trim() || current.name;
      const ver = (this.overlay.querySelector('#edit-proj-ver') as HTMLInputElement)?.value.trim() || current.version;
      const desc = (this.overlay.querySelector('#edit-proj-desc') as HTMLTextAreaElement)?.value.trim() || current.description;
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
        database: { type: 'sqlite', filePath: dbPath },
        environment: envMap
      }, this.graph);

      await this.projectManager.saveCurrentProject();
      alert('Configurações do workspace salvas com sucesso!');
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
