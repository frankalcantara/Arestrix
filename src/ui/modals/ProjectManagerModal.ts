import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { IProjectManifest } from '../../core/types.ts';
import { hashProjectName } from '../../core/hash.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

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
      title: 'Gerenciador de Projetos',
      subtitle: `Projeto Ativo: <strong>${this.escapeHtml(cur.name)}</strong>`,
      icon: '📂',
      closeBtnId: 'btn-close-proj-manager'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog project-memory-modal-dialog" style="max-width: 860px;">
        ${titleBarHtml}

        <!-- Abas de Navegação Estilo Office -->
        <div class="modal-tabs">
          <button class="tab-btn ${this.activeTab === 'recents' ? 'active' : ''}" id="tab-btn-recents">
            Projetos Recentes (${recents.length})
          </button>
          <button class="tab-btn ${this.activeTab === 'create' ? 'active' : ''}" id="tab-btn-create">
            Novo Projeto
          </button>
          <button class="tab-btn ${this.activeTab === 'settings' ? 'active' : ''}" id="tab-btn-settings">
            Configurações do Projeto Ativo
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
        <span style="font-size: 12px; color: #616161;">Selecione um projeto aberto anteriormente para carregar suas pipelines e configurações do workspace</span>
        <div style="display: flex; gap: 8px; flex-shrink: 0;">
          <button id="btn-browse-project" class="tool-btn btn-secondary" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px;">
            Abrir Outro Diretório...
          </button>
          <button id="btn-go-to-create" class="tool-btn btn-action" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px;">
            + Criar Novo Projeto
          </button>
        </div>
      </div>

      <div style="display: flex; flex-direction: column; gap: 10px;">
        ${recents.length === 0 ? `
          <div style="padding: 32px; text-align: center; color: #616161; background: #faf9f8; border: 1px solid #edebe9; border-radius: 4px;">
            Nenhum projeto registrado no histórico recente. Clique em "+ Criar Novo Projeto" para começar.
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
                      ATIVO
                    </span>
                  ` : ''}
                </div>
                <div style="margin-top: 5px; font-size: 12px; color: #64748b; word-break: break-all; overflow-wrap: anywhere;">
                  Caminho: <code style="word-break: break-all; overflow-wrap: anywhere; max-width: 100%; display: inline-block;">${this.escapeHtml(r.rootPath)}</code>
                </div>
              </div>

              <div style="flex-shrink: 0; display: flex; align-items: center; gap: 8px;">
                <select class="recent-project-action-select recents-modal-action-select" data-id="${this.escapeHtml(r.projectId)}" data-path="${this.escapeHtml(r.rootPath)}" data-name="${this.escapeHtml(r.name)}">
                  <option value="" disabled selected>Opções ▾</option>
                  <option value="open">📂 Abrir Projeto</option>
                  <option value="manage">⚙️ Gerenciar</option>
                  <option value="delete">🗑️ Apagar Projeto...</option>
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
            <h3 class="section-card-title">Criar Novo Projeto <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">📦 Escopo do Projeto</span></h3>
            <p class="section-card-desc">Configuração do manifesto com isolamento de pipelines, memória e diretório no workspace</p>
          </div>
        </div>

        <div class="form-group" style="margin-top: 16px;">
          <label class="form-label" for="new-proj-name" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">Nome do Projeto *</label>
          <input type="text" id="new-proj-name" class="form-input" placeholder="ex: Fechamento Contábil Q3" style="width: 100%; box-sizing: border-box;" />
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" for="new-proj-path" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">Diretório Raiz do Workspace * <span style="font-size: 10px; color: #0f6cbd; font-weight: normal;">(Armazenamento Físico 🛠️)</span></label>
          <div style="display: flex; gap: 8px;">
            <input type="text" id="new-proj-path" class="form-input" value="${this.escapeHtml(defaultPath)}" placeholder="Selecione ou digite o diretório físico do workspace..." style="flex: 1; box-sizing: border-box;" />
            <button type="button" id="btn-browse-new-proj-path" class="tool-btn btn-secondary" style="white-space: nowrap; height: 36px; padding: 0 16px; font-size: 13px; flex-shrink: 0;">
              Procurar...
            </button>
          </div>
          <div class="form-hint" style="margin-top: 4px; font-size: 11px; color: #64748b;">
            O Arestrix criará automaticamente as pastas de infraestrutura <code>.workspace/</code>, <code>pipelines/</code>, <code>storage/</code> e <code>output/</code> neste diretório.
          </div>
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" for="new-proj-desc" style="font-weight: 600; color: #323130; margin-bottom: 6px; display: block;">Descrição do Projeto</label>
          <textarea id="new-proj-desc" class="form-input" style="height: 60px; width: 100%; box-sizing: border-box;" placeholder="Finalidade, escopo e regras deste pipeline..."></textarea>
        </div>

        <div class="form-group" style="margin-top: 14px;">
          <label class="form-label" style="font-weight: 600; color: #323130; margin-bottom: 8px; display: block;">Template Inicial de Pipeline:</label>
          <div style="grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 6px; display: grid;">
            <label class="template-choice-card selected" style="display: flex; flex-direction: row; align-items: center; gap: 12px; padding: 14px 16px; border: 1.5px solid #0f6cbd; border-radius: 8px; background-color: #f0f7fd; cursor: pointer; transition: all 0.15s ease; user-select: none; box-sizing: border-box;">
              <input type="radio" name="new-proj-template" value="empty" checked style="accent-color: #0f6cbd; width: 18px; height: 18px; margin: 0; cursor: pointer; flex-shrink: 0;" />
              <div class="template-choice-info">
                <span class="template-choice-title">Pipeline Vazio</span>
                <span class="template-choice-desc">Inicia com um grafo limpo e pronto para modelagem.</span>
              </div>
            </label>
            <label class="template-choice-card" style="display: flex; flex-direction: row; align-items: center; gap: 12px; padding: 14px 16px; border: 1px solid #cbd5e1; border-radius: 8px; background-color: #ffffff; cursor: pointer; transition: all 0.15s ease; user-select: none; box-sizing: border-box;">
              <input type="radio" name="new-proj-template" value="agents" style="accent-color: #0f6cbd; width: 18px; height: 18px; margin: 0; cursor: pointer; flex-shrink: 0;" />
              <div class="template-choice-info">
                <span class="template-choice-title">Pipeline Multi-Agente IA</span>
                <span class="template-choice-desc">Arquitetura multi-agente com orquestração inicial.</span>
              </div>
            </label>
          </div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 24px;">
          <button id="btn-cancel-create-proj" class="tool-btn btn-secondary" style="height: 36px; padding: 0 18px; font-size: 13px;">
            Cancelar
          </button>
          <button id="btn-submit-create-proj" class="tool-btn btn-action" style="height: 36px; padding: 0 20px; font-size: 13px; font-weight: 600;">
            🚀 Criar e Ativar Projeto
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
            <h3 class="section-card-title">Propriedades do Projeto Ativo <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">📦 Escopo do Projeto</span></h3>
            <p class="section-card-desc" style="word-break: break-all; overflow-wrap: anywhere;">Manifesto: <code style="word-break: break-all; overflow-wrap: anywhere; max-width: 100%; display: inline-block;">${this.escapeHtml(current.rootPath)}/arestrix.project.json</code> (armazenado no diretório do 🛠️ Workspace)</p>
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
            <label class="form-label" for="edit-proj-output-dir">Pasta de Saída / Output <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px;">📦 Escopo do Projeto</span></label>
            <input type="text" id="edit-proj-output-dir" class="form-input" value="${this.escapeHtml(current.outputDir || 'Output')}" placeholder="Output" />
            <div class="form-hint">Diretório relativo ao projeto onde código e artefatos de saída são gerados (padrão: Output).</div>
          </div>
          <div class="form-group">
            <label class="form-label" for="edit-proj-db-path">Arquivo do Banco de Dados Relacional (SQLite) <span style="font-size: 10px; color: #0f6cbd;">🛠️ Workspace</span></label>
            <input type="text" id="edit-proj-db-path" class="form-input" value="${this.escapeHtml(current.database?.filePath || 'storage/database.sqlite')}" />
            <div class="form-hint">Caminho físico relativo à raiz do workspace no disco.</div>
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label">Pipelines Cadastrados</label>
          <div style="background: rgba(0,0,0,0.05); padding: 8px 12px; border-radius: 6px; font-family: monospace; font-size: 12px;">
            ${(current.pipelines || []).map(p => `<div>• <strong>${this.escapeHtml(p.name)}</strong> (${this.escapeHtml(p.file)}) ${p.isPrimary ? '<span class="badge-inherited" style="font-size: 9px; padding: 1px 4px;">PRINCIPAL</span>' : ''}</div>`).join('')}
          </div>
        </div>

        <div class="form-group" style="margin-top: 12px;">
          <label class="form-label" for="edit-proj-env">Variáveis de Ambiente Locais do Projeto (.env)</label>
          <textarea id="edit-proj-env" class="form-input" style="height: 70px; font-family: monospace; font-size: 12px;" placeholder="VAR_LOCAL=valor\nMODO=producao">${this.escapeHtml(envEntries)}</textarea>
          <div class="form-hint">Variáveis injetadas no ambiente de execução deste projeto (chaves de LLM globais são gerenciadas no 🏢 Studio).</div>
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
          <button id="btn-save-proj-settings" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
            💾 Salvar Alterações do Projeto
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
              if (typeof alert !== 'undefined') alert(`Projeto '${pName}' carregado com sucesso!`);
              this.hide();
            } else {
              if (typeof alert !== 'undefined') alert(`Falha ao abrir projeto: ${pName}`);
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
            ? confirm(`Tem certeza que deseja apagar o projeto inteiro "${pName}" e todos os seus arquivos do disco?\n\nEsta operação é permanente e não poderá ser desfeita.`)
            : true;

          if (confirmed) {
            await this.projectManager.deleteProject(pId || pPath, true, this.graph);
            if (typeof alert !== 'undefined') alert(`🗑️ Projeto "${pName}" apagado com sucesso.`);
            this.render();
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
          alert(`Projeto carregado com sucesso!`);
          this.hide();
        } else {
          alert(`Falha ao carregar projeto: ${path}`);
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
          const manual = prompt('Informe o caminho do diretório do workspace físico:', pathInput?.value || '');
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
        alert('Por favor, preencha o Nome do Projeto.');
        return;
      }
      if (!path) {
        alert('Por favor, informe o Diretório do workspace físico.');
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
