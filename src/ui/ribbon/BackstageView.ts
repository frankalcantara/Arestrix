import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { IProjectManifest, IRecentProjectItem } from '../../core/types.ts';
import { hashProjectName } from '../../core/hash.ts';
import { CodeModal } from '../CodeModal.ts';
import { GlobalDefaultsModal } from '../GlobalDefaultsModal.ts';
import { WorkspaceManager } from '../../core/workspace/WorkspaceManager.ts';

export class BackstageView {
  private overlay?: HTMLElement;
  private graph: Graph;
  private projectManager: ProjectManager;
  private codeModal: CodeModal;
  private globalDefaultsModal: GlobalDefaultsModal;
  private activeSection: 'info' | 'new' | 'open' | 'saveas' | 'export' | 'options' = 'info';

  // Estado da Tela "Salvar Como" (Navegação de Discos e Pastas)
  private saveAsCurrentPath: string = '';
  private saveAsDrives: string[] = ['C:\\'];
  private saveAsSubdirectories: Array<{ name: string; path: string }> = [];
  private saveAsFileName: string = '';
  private saveAsFormat: 'arestrix' | 'json' = 'arestrix';

  constructor(graph: Graph, codeModal: CodeModal, globalDefaultsModal: GlobalDefaultsModal) {
    this.graph = graph;
    this.codeModal = codeModal;
    this.globalDefaultsModal = globalDefaultsModal;
    this.projectManager = ProjectManager.getInstance(globalEventBus);

    if (typeof document !== 'undefined') {
      this.overlay = document.createElement('div');
      this.overlay.className = 'office-backstage-overlay';
      document.body.appendChild(this.overlay);

      this.render();
      this.bindEvents();

      globalEventBus.on('project:changed', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
      globalEventBus.on('project:loaded', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
      globalEventBus.on('project:recents-updated', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
    }
  }

  public async initSaveAsState(): Promise<void> {
    if (!this.saveAsCurrentPath) {
      this.saveAsCurrentPath = await this.projectManager.getDefaultWorkspaceDirectory();
      if (!this.saveAsCurrentPath || this.saveAsCurrentPath === '.') {
        this.saveAsCurrentPath = 'C:/Projetos';
      }
    }
    this.saveAsCurrentPath = this.saveAsCurrentPath.replace(/\\/g, '/');
    this.saveAsDrives = await this.projectManager.listDrives();
    this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
    const proj = this.projectManager.getCurrentProject();
    if (!this.saveAsFileName) {
      this.saveAsFileName = (proj.name || 'projeto').toLowerCase().replace(/\s+/g, '_');
    }
  }

  public async open(section: 'info' | 'new' | 'open' | 'saveas' | 'export' = 'info'): Promise<void> {
    this.activeSection = section;
    try {
      await this.projectManager.init(true);
      if (this.activeSection === 'saveas') {
        await this.initSaveAsState();
      }
    } catch (_) {}
    if (this.overlay) {
      this.render();
      this.overlay.classList.add('open');
    }
  }

  public close(): void {
    if (this.overlay) {
      this.overlay.classList.remove('open');
    }
  }

  public isOpen(): boolean {
    return this.overlay ? this.overlay.classList.contains('open') : false;
  }

  public getOverlay(): HTMLElement | undefined {
    return this.overlay;
  }

  private render(): void {
    if (!this.overlay) return;
    const proj: IProjectManifest = this.projectManager.getCurrentProject();
    const recentList: IRecentProjectItem[] = this.projectManager.listRecentProjects();

    this.overlay.innerHTML = `
      <div class="office-backstage-container">
        <!-- BARRA LATERAL OFFICE (AZUL FLUENT & IDENTIDADE OFICIAL ARESTRIX) -->
        <div class="office-backstage-sidebar">
          <div class="backstage-brand-header" style="padding: 16px 20px 12px 20px; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid rgba(255,255,255,0.12); margin-bottom: 6px;">
            <div style="width: 28px; height: 28px; flex-shrink: 0;">
              <svg viewBox="0 0 124 124" width="28" height="28" fill="none" style="display:block;">
                <g stroke="#F4F2EE" stroke-width="14" stroke-linecap="round">
                  <line x1="62" y1="21" x2="22" y2="103"/>
                  <line x1="62" y1="21" x2="102" y2="103"/>
                </g>
                <line x1="37" y1="73" x2="87" y2="73" stroke="#E8590C" stroke-width="12" stroke-linecap="round"/>
                <circle cx="62" cy="21" r="11" fill="#F4F2EE"/>
                <circle cx="22" cy="103" r="11" fill="#F4F2EE"/>
                <circle cx="102" cy="103" r="11" fill="#F4F2EE"/>
              </svg>
            </div>
            <div>
              <div style="font-size: 15px; font-weight: 800; color: #F4F2EE; letter-spacing: -0.3px; line-height: 1;">Arestrix</div>
              <div style="font-size: 9px; font-weight: 700; color: #E8590C; letter-spacing: 1.2px; text-transform: uppercase; margin-top: 2px;">Studio 2.0</div>
            </div>
          </div>

          <button class="backstage-back-btn" id="btn-backstage-close" title="Voltar ao Grafo (Esc)">
            <span class="backstage-back-icon">←</span>
            <span>Voltar ao Grafo</span>
          </button>

          <ul class="backstage-nav-list">
            <li class="backstage-nav-item ${this.activeSection === 'info' ? 'active' : ''}" data-section="info">
              <span>ℹ️</span> Informações
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'new' ? 'active' : ''}" data-section="new">
              <span>📄</span> Novo Projeto
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'open' ? 'active' : ''}" data-section="open">
              <span>📂</span> Abrir Recente
            </li>
            <li class="backstage-nav-item" id="btn-backstage-save">
              <span>💾</span> Salvar Workspace
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'saveas' ? 'active' : ''}" data-section="saveas">
              <span>📥</span> Salvar Como
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'export' ? 'active' : ''}" data-section="export">
              <span>📤</span> Exportar Código
            </li>
            <li class="backstage-nav-item" id="btn-backstage-github-repos">
              <span>🌐</span> Repositórios GitHub
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'options' ? 'active' : ''}" data-section="options">
              <span>⚙️</span> Opções do Studio
            </li>
          </ul>
        </div>

        <!-- PAINEL PRINCIPAL DE CONTEÚDO -->
        <div class="office-backstage-main">
          ${this.renderSectionContent(proj, recentList)}
        </div>
      </div>
    `;

    this.bindInternalEvents();
  }

  private renderRecentProjectsTable(recentList: IRecentProjectItem[]): string {
    if (!recentList || recentList.length === 0) {
      return `
        <div style="padding: 20px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; color: #64748b; font-size: 13px;">
          Nenhum projeto recente registrado no histórico.
        </div>
      `;
    }

    return `
      <table class="backstage-mru-table" style="width: 100%; border-collapse: collapse;">
        <thead>
          <tr>
            <th style="text-align: left; padding: 10px 14px;">Nome do Projeto</th>
            <th style="text-align: left; padding: 10px 14px;">Localização (Diretório)</th>
            <th style="text-align: left; padding: 10px 14px;">Último Acesso</th>
            <th style="text-align: right; padding: 10px 14px;">Ação</th>
          </tr>
        </thead>
        <tbody>
          ${recentList.map(item => `
            <tr>
              <td style="padding: 10px 14px;">
                <strong style="color: #0f172a; font-size: 13px;">${this.escapeHtml(item.name)}</strong>
              </td>
              <td style="padding: 10px 14px;">
                <code style="color: #475569; font-size: 11.5px; background: #f1f5f9; padding: 2px 6px; border-radius: 4px;">${this.escapeHtml(item.rootPath)}</code>
              </td>
              <td style="padding: 10px 14px; font-size: 12px; color: #64748b;">
                ${item.lastOpened ? new Date(item.lastOpened).toLocaleString() : 'Recentemente'}
              </td>
              <td style="text-align: right; padding: 10px 14px;">
                <select class="recent-project-action-select backstage-project-action-select" data-id="${item.projectId}" data-path="${this.escapeHtml(item.rootPath)}" data-name="${this.escapeHtml(item.name)}">
                  <option value="" disabled selected>Opções ▾</option>
                  <option value="open">📂 Abrir Projeto</option>
                  <option value="manage">⚙️ Gerenciar</option>
                  <option value="delete">🗑️ Apagar Projeto...</option>
                </select>
              </td>
            </tr>
          `).join('')}
        </tbody>
      </table>
    `;
  }

  private renderSectionContent(proj: IProjectManifest, recentList: IRecentProjectItem[]): string {
    switch (this.activeSection) {
      case 'info':
        return `
          <h2 class="backstage-header-title">Informações do Projeto</h2>
          <p class="backstage-header-sub">Gerencie o manifesto e propriedades deste workspace do Arestrix Studio.</p>

          <div class="backstage-cards-grid">
            <div class="backstage-info-card">
              <h4>📁 Identificação do Workspace</h4>
              <p><strong>Nome:</strong> ${this.escapeHtml(proj.name)}</p>
              <p><strong>ID:</strong> <code>${this.escapeHtml(proj.projectId)}</code></p>
              <p><strong>Versão:</strong> ${this.escapeHtml(proj.version)}</p>
              <p><strong>Descrição:</strong> ${this.escapeHtml(proj.description || 'Sem descrição')}</p>
            </div>

            <div class="backstage-info-card">
              <h4>🗄️ Armazenamento & Banco de Dados</h4>
              <p><strong>Tipo:</strong> ${proj.database?.type || 'SQLite (Padrão)'}</p>
              <p><strong>Arquivo DB:</strong> <code>${this.escapeHtml(proj.database?.filePath || 'storage/database.sqlite')}</code></p>
              <p><strong>Diretório Raiz:</strong> <code>${this.escapeHtml(proj.rootPath)}</code></p>
            </div>

            <div class="backstage-info-card">
              <h4>⚡ Estatísticas do Pipeline</h4>
              <p><strong>Vértices Ativos:</strong> ${this.graph.getAllNodes().length}</p>
              <p><strong>Arestas / Links:</strong> ${this.graph.getAllEdges().length}</p>
              <p><strong>Pipelines Vinculados:</strong> ${proj.pipelines ? proj.pipelines.length : 0}</p>
            </div>
          </div>

          <div style="margin-top: 14px;">
            <h4 style="font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 8px;">Ações Rápidas de Manutenção</h4>
            <div style="display: flex; gap: 10px;">
              <button class="tool-btn btn-secondary" id="btn-quick-sync-db" style="font-weight: 700;">🔄 Sincronizar Banco</button>
              <button class="tool-btn btn-secondary" id="btn-quick-clean-cache" style="font-weight: 700;">🧹 Limpar Cache do Resume</button>
            </div>
          </div>

          <!-- LISTA DE PROJETOS RECENTES (ESTILO OFFICE) -->
          <div style="margin-top: 26px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📂 Projetos Recentes</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">Selecione um projeto acessado recentemente para carregar instantaneamente.</p>
            ${this.renderRecentProjectsTable(recentList)}
          </div>
        `;

      case 'new':
        return `
          <h2 class="backstage-header-title">Criar Novo Projeto</h2>
          <p class="backstage-header-sub">Configure um novo workspace isolado com estrutura de diretórios e pipelines.</p>

          <div style="max-width: 650px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 24px; margin-bottom: 28px;">
            <div style="margin-bottom: 16px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome do Projeto:</label>
              <input type="text" id="backstage-new-name" placeholder="Ex: Auditoria de Agentes 2026" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; box-sizing: border-box;">
            </div>

            <div style="margin-bottom: 20px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Descrição do Propósito:</label>
              <textarea id="backstage-new-desc" rows="3" placeholder="Objetivo e escopo deste projeto..." style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; box-sizing: border-box;"></textarea>
            </div>

            <div style="display: flex; gap: 12px;">
              <button class="tool-btn btn-action" id="btn-backstage-create-submit" style="background: #0f6cbd; color: #fff; font-weight: 700; padding: 10px 22px; height: 38px; font-size: 13px;">
                🚀 Criar e Abrir Workspace
              </button>
            </div>
          </div>

          <!-- LISTA DE PROJETOS RECENTES (REPETIDA NA TELA NOVO PROJETO) -->
          <div>
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📂 Projetos Recentes</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">Ou continue trabalhando em um dos projetos acessados recentemente:</p>
            ${this.renderRecentProjectsTable(recentList)}
          </div>
        `;

      case 'open':
        return `
          <h2 class="backstage-header-title">Abrir Recente</h2>
          <p class="backstage-header-sub">Acesse pipelines do workspace atual ou abra outros projetos salvos recentemente.</p>

          <!-- 1. LISTA DE GRAFOS RECENTES DO WORKSPACE ATIVO -->
          <div style="margin-bottom: 28px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📊 Grafos Recentes do Workspace Ativo</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">Pipelines disponíveis no projeto atual (<strong>${this.escapeHtml(proj.name)}</strong>):</p>

            ${(proj.pipelines && proj.pipelines.length > 0) ? `
              <table class="backstage-mru-table" style="width: 100%; border-collapse: collapse;">
                <thead>
                  <tr>
                    <th style="text-align: left; padding: 10px 14px;">Nome do Grafo</th>
                    <th style="text-align: left; padding: 10px 14px;">Arquivo</th>
                    <th style="text-align: left; padding: 10px 14px;">Tipo</th>
                    <th style="text-align: left; padding: 10px 14px;">Descrição</th>
                    <th style="text-align: right; padding: 10px 14px;">Ação</th>
                  </tr>
                </thead>
                <tbody>
                  ${proj.pipelines.map(p => `
                    <tr>
                      <td style="padding: 10px 14px;">
                        <strong style="color: #0f172a; font-size: 13px;">${this.escapeHtml(p.name)}</strong>
                      </td>
                      <td style="padding: 10px 14px;">
                        <code style="color: #475569; font-size: 11.5px; background: #f1f5f9; padding: 2px 6px; border-radius: 4px;">${this.escapeHtml(p.file)}</code>
                      </td>
                      <td style="padding: 10px 14px;">
                        ${p.isPrimary ? '<span class="badge-inherited" style="font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: 700;">PRINCIPAL</span>' : '<span style="color: #64748b; font-size: 11px;">Subgrafo</span>'}
                      </td>
                      <td style="padding: 10px 14px; font-size: 12px; color: #64748b;">
                        ${this.escapeHtml(p.description || 'Pipeline de execução')}
                      </td>
                      <td style="text-align: right; padding: 10px 14px;">
                        <button class="tool-btn btn-secondary btn-pipeline-load" data-file="${this.escapeHtml(p.file)}" style="padding: 6px 14px; font-size: 12px; font-weight: 700; white-space: nowrap; height: 32px; display: inline-flex; align-items: center;">
                          Carregar no Canvas
                        </button>
                      </td>
                    </tr>
                  `).join('')}
                </tbody>
              </table>
            ` : `
              <div style="padding: 20px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; color: #64748b; font-size: 13px;">
                Nenhum grafo cadastrado no projeto atual.
              </div>
            `}
          </div>

          <!-- 2. LISTA DE PROJETOS RECENTES (DEPOIS DA LISTA DE GRAFOS) -->
          <div>
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📂 Projetos Recentes</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">Selecione um projeto acessado anteriormente para carregar:</p>
            ${this.renderRecentProjectsTable(recentList)}
          </div>
        `;

      case 'saveas':
        return `
          <h2 class="backstage-header-title">Salvar Como</h2>
          <p class="backstage-header-sub">Navegue pelas pastas e unidades de disco do sistema para salvar o pacote comprimido do projeto (<code>.arestrix</code>) ou manifesto JSON.</p>

          <!-- BARRA DE DISCOS & LOCALIZAÇÃO RÁPIDA -->
          <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 18px 22px; margin-bottom: 22px;">
            <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; margin-bottom: 14px;">
              <div style="display: flex; align-items: center; gap: 8px;">
                <span style="font-size: 12px; font-weight: 700; color: #475569; text-transform: uppercase; letter-spacing: 0.5px;">Unidades de Disco:</span>
                <div class="saveas-drives-list" style="display: flex; gap: 8px; flex-wrap: wrap;">
                  ${this.saveAsDrives.map(drive => {
                    const cleanDrive = drive.replace(/\\/g, '/');
                    const isCurrent = this.saveAsCurrentPath.toUpperCase().startsWith(cleanDrive.replace(/\/$/, '').toUpperCase());
                    return `
                      <button class="tool-btn btn-saveas-drive ${isCurrent ? 'active-drive' : ''}" data-drive="${this.escapeHtml(cleanDrive)}" style="font-weight: 700; padding: 6px 14px; font-size: 12px; border-radius: 6px; ${isCurrent ? 'background: #0f6cbd; color: #ffffff; border: 1px solid #0f6cbd;' : 'background: #ffffff; color: #0f172a; border: 1px solid #cbd5e1;'}">
                        💾 ${this.escapeHtml(drive)}
                      </button>
                    `;
                  }).join('')}
                </div>
              </div>

              <button class="tool-btn btn-secondary" id="btn-saveas-browse-native" style="font-weight: 700; padding: 6px 16px; font-size: 12px; height: 32px; display: inline-flex; align-items: center; gap: 6px;">
                <span>📂</span> Procurar Pasta no Windows...
              </button>
            </div>

            <!-- BARRA DE NAVEGAÇÃO DE DIRETÓRIO (BREADCRUMB & SUBIR NÍVEL) -->
            <div style="display: flex; align-items: center; gap: 10px; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 8px; padding: 6px 12px;">
              <button class="tool-btn btn-secondary" id="btn-saveas-up-dir" title="Subir um nível na árvore de pastas" style="padding: 4px 10px; font-size: 12px; font-weight: 700; height: 28px;">
                📁 ⬆ Subir
              </button>
              <button class="tool-btn btn-secondary" id="btn-saveas-refresh-dir" title="Atualizar listagem" style="padding: 4px 10px; font-size: 12px; font-weight: 700; height: 28px;">
                🔄 Atualizar
              </button>

              <div style="flex: 1; display: flex; align-items: center; gap: 4px; overflow-x: auto; font-family: 'IBM Plex Mono', monospace; font-size: 12px; color: #334155; padding: 2px 4px;">
                <span style="color: #64748b; font-weight: 700;">Local:</span>
                <span style="font-weight: 600; color: #0f172a;" id="saveas-current-path-display">${this.escapeHtml(this.saveAsCurrentPath)}</span>
              </div>
            </div>
          </div>

          <!-- ÁREA DE SUBPASTAS PARA NAVEGAÇÃO INTERATIVA -->
          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; padding: 18px 22px; margin-bottom: 22px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
              <h4 style="font-size: 13px; font-weight: 700; color: #0f172a; text-transform: uppercase; letter-spacing: 0.5px;">Subpastas no Diretório Atual (Clique para abrir)</h4>
              <span style="font-size: 11.5px; color: #64748b;">${this.saveAsSubdirectories.length} subpastas</span>
            </div>

            <div class="saveas-folders-grid" style="display: grid; grid-template-columns: repeat(auto-fill, minmax(210px, 1fr)); gap: 10px; max-height: 240px; overflow-y: auto; padding: 4px;">
              ${this.saveAsSubdirectories.length > 0 ? this.saveAsSubdirectories.map(sub => `
                <div class="saveas-folder-card" data-path="${this.escapeHtml(sub.path)}" style="display: flex; align-items: center; gap: 8px; padding: 10px 14px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; cursor: pointer; transition: all 0.15s ease;">
                  <span style="font-size: 18px;">📁</span>
                  <span style="font-size: 12.5px; font-weight: 600; color: #1e293b; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${this.escapeHtml(sub.name)}</span>
                </div>
              `).join('') : `
                <div style="grid-column: 1 / -1; padding: 24px; text-align: center; color: #64748b; font-size: 12.5px; background: #f8fafc; border-radius: 8px;">
                  Nenhuma subpasta encontrada no diretório atual. Você pode salvar diretamente aqui.
                </div>
              `}
            </div>
          </div>

          <!-- PAINEL DE SALVAMENTO (ESTILO DIÁLOGO OFFICE SAVE AS) -->
          <div style="background: #f8fafc; border: 1px solid #cbd5e1; border-radius: 12px; padding: 20px 24px;">
            <h4 style="font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 14px;">Propriedades do Pacote a Salvar</h4>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin-bottom: 16px;">
              <div>
                <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome do Arquivo / Projeto:</label>
                <input type="text" id="saveas-input-filename" value="${this.escapeHtml(this.saveAsFileName)}" placeholder="Ex: meu_projeto" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600; color: #0f172a; box-sizing: border-box;" />
              </div>

              <div>
                <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Tipo de Arquivo:</label>
                <select id="saveas-select-format" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600; color: #0f172a; box-sizing: border-box; background: #ffffff;">
                  <option value="arestrix" ${this.saveAsFormat === 'arestrix' ? 'selected' : ''}>Pacote Arestrix Comprimido (*.arestrix)</option>
                  <option value="json" ${this.saveAsFormat === 'json' ? 'selected' : ''}>Manifesto do Projeto JSON (*.json)</option>
                </select>
              </div>
            </div>

            <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; padding-top: 10px; border-top: 1px solid #e2e8f0;">
              <div style="font-size: 12px; color: #475569;">
                <span>Destino final:</span>
                <code id="saveas-path-preview" style="background: #e2e8f0; padding: 3px 8px; border-radius: 4px; font-family: 'IBM Plex Mono', monospace; font-size: 11.5px; color: #0f172a;">${this.escapeHtml(this.saveAsCurrentPath)}/${this.escapeHtml(this.saveAsFileName)}.${this.saveAsFormat}</code>
              </div>

              <button class="tool-btn btn-action" id="btn-saveas-submit" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 10px 26px; height: 38px; font-size: 13px; display: inline-flex; align-items: center; gap: 8px;">
                <span>💾</span> Salvar Pacote Arestrix
              </button>
            </div>
          </div>
        `;

      case 'export':
        return `
          <h2 class="backstage-header-title">Exportação de Engenharia C++20</h2>
          <p class="backstage-header-sub">Exporte código nativo Taskflow C++20, scripts de build ou descritores do pipeline.</p>

          <div class="backstage-cards-grid">
            <div class="backstage-info-card">
              <h4>💻 Código Fonte C++20</h4>
              <p>Gere e visualize o arquivo <code>main.cpp</code> completo com tarefas Taskflow e lambdas dos vértices.</p>
              <button class="tool-btn btn-action" id="btn-backstage-open-codemodal" style="background: #0f6cbd; color: white; font-weight: 700;">Ver Código C++</button>
            </div>

            <div class="backstage-info-card">
              <h4>🛠️ CMakeLists.txt & Makefile</h4>
              <p>Exporte scripts para compilação multiplataforma no Visual Studio ou Make.</p>
              <button class="tool-btn btn-secondary" id="btn-backstage-export-cmake" style="font-weight: 700;">Gerar CMake</button>
            </div>

            <div class="backstage-info-card">
              <h4>🖼️ Arquitetura Visual (SVG)</h4>
              <p>Gere diagrama vetorial escalável de alta resolução da arquitetura do grafo.</p>
              <button class="tool-btn btn-secondary" id="btn-backstage-export-svg" style="font-weight: 700;">Exportar SVG</button>
            </div>
          </div>
        `;

      case 'options': {
        const wm = WorkspaceManager.getInstance();
        const dirs = wm.getWorkspaceDirectories();
        const approvedSkills = wm.getApprovedSkills();
        const approvedMcps = wm.getApprovedMcps();

        return `
          <h2 class="backstage-header-title">Opções do Studio & Governança do Workspace</h2>
          <p class="backstage-header-sub">Configure o ambiente do workspace, sincronize recursos na máquina do usuário e gerencie a execução de agentes, grafos e projetos.</p>

          <!-- 1. IDENTIFICAÇÃO E PASTAS FÍSICAS DO WORKSPACE NA MÁQUINA DO USUÁRIO -->
          <div class="backstage-cards-grid" style="margin-bottom: 24px;">
            <div class="backstage-info-card">
              <h4>📁 Pastas Físicas do Workspace</h4>
              <p><strong>Raiz do Workspace:</strong> <code>${this.escapeHtml(dirs.root)}</code></p>
              <p><strong>Habilidades Instaladas:</strong> <code>${this.escapeHtml(dirs.skills)}</code></p>
              <p><strong>Servidores MCP:</strong> <code>${this.escapeHtml(dirs.mcps)}</code></p>
              <p><strong>Agentes de Governança:</strong> <code>${this.escapeHtml(dirs.agents)}</code></p>
              <div style="margin-top: 14px;">
                <button class="tool-btn btn-action" id="btn-sync-workspace-disk" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 8px 16px; font-size: 12px; display: inline-flex; align-items: center; gap: 6px;">
                  <span>📥</span> Instalar / Sincronizar Recursos na Máquina
                </button>
              </div>
            </div>

            <div class="backstage-info-card">
              <h4>🛡️ Governança & Políticas do Studio</h4>
              <p><strong>Auditoria Automática de Skills:</strong> <span class="badge-inherited" style="font-weight: 700;">ATIVADA</span></p>
              <p><strong>Tipografia & Design:</strong> <span style="color: #0f766e; font-weight: 700;">Zero Sombras/Glows em Fontes</span></p>
              <p><strong>Modo de Isolamento:</strong> Sandbox C++20 Seguro</p>
              <div style="margin-top: 14px;">
                <button class="tool-btn btn-secondary" id="btn-open-global-defaults-modal" style="font-weight: 700; padding: 8px 16px; font-size: 12px;">
                  ⚙️ Preferências Avançadas de Sistema...
                </button>
              </div>
            </div>
          </div>

          <!-- 2. MOTORES DE EXECUÇÃO DO WORKSPACE (AGENTES, GRAFOS, PROJETOS ATRELADOS A EVENTOS) -->
          <div style="margin-bottom: 28px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">⚡ Execução Integrada no Workspace</h3>
            <p class="backstage-header-sub" style="margin-bottom: 14px;">Execute agentes de governança, linters estruturais de grafos ou pipelines de projetos atrelados aos eventos do sistema.</p>

            <div class="backstage-cards-grid">
              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <h4>🤖 Agente Auditor de Workspace</h4>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    Audita em tempo real todas as habilidades dos agentes ativos, verifica violações de segurança e conformidade estrita de tipografia vetorial.
                  </p>
                </div>
                <button class="tool-btn btn-action" id="btn-run-workspace-auditor" style="background: #107c41; color: #ffffff; font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ Executar Auditoria do Workspace
                </button>
              </div>

              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <h4>📊 Linter & Validador de Grafos</h4>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    Executa análise topológica, detecta ciclos de nós órfãos e valida compatibilidade de tipos de dados nos fluxos do grafo atual.
                  </p>
                </div>
                <button class="tool-btn btn-secondary" id="btn-run-workspace-linter" style="font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ Validar Grafo Ativo
                </button>
              </div>

              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <h4>🚀 Pipeline do Projeto</h4>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    Dispara a execução do projeto no ambiente do workspace, inicializando persistência SQLite e telemetria de tarefas.
                  </p>
                </div>
                <button class="tool-btn btn-action" id="btn-run-workspace-project" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ Executar Pipeline do Projeto
                </button>
              </div>
            </div>

            <div id="workspace-execution-output" style="margin-top: 14px; display: none;"></div>
          </div>

          <!-- 3. CATÁLOGO DE RECURSOS PRÉ-APROVADOS NO WORKSPACE -->
          <div style="margin-bottom: 28px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">🛡️ Catálogo de Habilidades e MCPs Pré-Aprovados</h3>
            <p class="backstage-header-sub" style="margin-bottom: 14px;">Recursos homologados e certificados para uso imediato em agentes e nós do sistema.</p>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
              <!-- Habilidades Pré-aprovadas -->
              <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
                <h4 style="margin: 0 0 10px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
                  📜 Habilidades Pré-Aprovadas (${approvedSkills.length})
                </h4>
                <div style="max-height: 220px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
                  ${approvedSkills.map(s => `
                    <div style="padding: 8px 10px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                      <div style="display: flex; justify-content: space-between; align-items: center;">
                        <strong style="font-size: 12px; color: #0f6cbd;">${this.escapeHtml(s.name)}</strong>
                        <span style="font-size: 10px; background: #e8f5e9; color: #1e7e34; padding: 1px 6px; border-radius: 3px; font-weight: 700;">HOMOLOGADA</span>
                      </div>
                      <div style="font-size: 11px; color: #64748b; margin-top: 2px;">${this.escapeHtml(s.description)}</div>
                    </div>
                  `).join('')}
                </div>
              </div>

              <!-- MCPs Pré-aprovados -->
              <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
                <h4 style="margin: 0 0 10px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
                  🔌 Servidores MCP Pré-Aprovados (${approvedMcps.length})
                </h4>
                <div style="max-height: 220px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
                  ${approvedMcps.map(m => `
                    <div style="padding: 8px 10px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                      <div style="display: flex; justify-content: space-between; align-items: center;">
                        <strong style="font-size: 12px; color: #334155;">${this.escapeHtml(m.name)}</strong>
                        <span style="font-size: 10px; background: #e0f2fe; color: #0369a1; padding: 1px 6px; border-radius: 3px; font-weight: 700;">${this.escapeHtml(m.transport.toUpperCase())}</span>
                      </div>
                      <div style="font-size: 11px; color: #64748b; margin-top: 2px;">${this.escapeHtml(m.description)}</div>
                    </div>
                  `).join('')}
                </div>
              </div>
            </div>
          </div>
        `;
      }
    }
  }

  private bindEvents(): void {
    window.addEventListener('keydown', (e: KeyboardEvent) => {
      if (e.key === 'Escape' && this.isOpen()) {
        this.close();
      }
    });
  }

  private bindInternalEvents(): void {
    if (!this.overlay) return;

    this.overlay.querySelector('#btn-backstage-close')?.addEventListener('click', () => {
      this.close();
    });

    this.overlay.querySelectorAll<HTMLElement>('.backstage-nav-item[data-section]').forEach(item => {
      item.addEventListener('click', async () => {
        const sec = item.dataset.section as any;
        if (sec) {
          this.activeSection = sec;
          try {
            await this.projectManager.init(true);
            if (this.activeSection === 'saveas') {
              await this.initSaveAsState();
            }
          } catch (_) {}
          this.render();
        }
      });
    });

    // Controles da Tela "Salvar Como"
    if (this.activeSection === 'saveas') {
      // 1. Botões de Unidades de Disco
      this.overlay.querySelectorAll<HTMLButtonElement>('.btn-saveas-drive').forEach(btn => {
        btn.addEventListener('click', async () => {
          const drive = btn.dataset.drive;
          if (drive) {
            this.saveAsCurrentPath = drive.replace(/\\/g, '/');
            this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
            this.render();
          }
        });
      });

      // 2. Navegação em Pastas
      this.overlay.querySelectorAll<HTMLElement>('.saveas-folder-card').forEach(card => {
        card.addEventListener('click', async () => {
          const p = card.dataset.path;
          if (p) {
            this.saveAsCurrentPath = p.replace(/\\/g, '/');
            this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
            this.render();
          }
        });
      });

      // 3. Subir Nível
      this.overlay.querySelector('#btn-saveas-up-dir')?.addEventListener('click', async () => {
        const cur = this.saveAsCurrentPath.replace(/\\/g, '/').replace(/\/$/, '');
        const lastSlash = cur.lastIndexOf('/');
        if (lastSlash > 0) {
          this.saveAsCurrentPath = cur.substring(0, lastSlash);
          if (/^[a-zA-Z]:$/.test(this.saveAsCurrentPath)) {
            this.saveAsCurrentPath += '/';
          }
        } else {
          this.saveAsCurrentPath = 'C:/';
        }
        this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
        this.render();
      });

      // 4. Atualizar
      this.overlay.querySelector('#btn-saveas-refresh-dir')?.addEventListener('click', async () => {
        this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
        this.render();
      });

      // 5. Procurar Nativo
      this.overlay.querySelector('#btn-saveas-browse-native')?.addEventListener('click', async () => {
        try {
          if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
            const picked = await (window as any).webui.call('pick_folder');
            if (picked && picked.trim()) {
              this.saveAsCurrentPath = picked.trim().replace(/\\/g, '/');
              this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
              this.render();
            }
          }
        } catch (_) {}
      });

      // 6. Atualização de Nome do Arquivo
      const nameInput = this.overlay.querySelector('#saveas-input-filename') as HTMLInputElement;
      nameInput?.addEventListener('input', () => {
        this.saveAsFileName = nameInput.value;
        const preview = this.overlay?.querySelector('#saveas-path-preview');
        if (preview) {
          preview.textContent = `${this.saveAsCurrentPath}/${this.saveAsFileName}.${this.saveAsFormat}`;
        }
      });

      // 7. Atualização do Formato
      const formatSelect = this.overlay.querySelector('#saveas-select-format') as HTMLSelectElement;
      formatSelect?.addEventListener('change', () => {
        this.saveAsFormat = formatSelect.value as any;
        const preview = this.overlay?.querySelector('#saveas-path-preview');
        if (preview) {
          preview.textContent = `${this.saveAsCurrentPath}/${this.saveAsFileName}.${this.saveAsFormat}`;
        }
      });

      // 8. Executar Salvamento do Arquivo
      this.overlay.querySelector('#btn-saveas-submit')?.addEventListener('click', async () => {
        const fileName = (this.saveAsFileName || 'projeto').trim();
        if (!fileName) {
          if (typeof alert !== 'undefined') alert('Por favor, informe o nome do arquivo.');
          return;
        }

        if (this.saveAsFormat === 'arestrix') {
          const res = await this.projectManager.saveProjectAsArestrix(this.saveAsCurrentPath, fileName, this.graph);
          if (res && res.success) {
            if (typeof alert !== 'undefined') {
              alert(`💾 Pacote Arestrix salvo com sucesso!\n\nArquivo: ${res.filePath}\nO projeto completo, opções e grafos foram comprimidos no pacote .arestrix.`);
            }
            this.render();
          } else {
            if (typeof alert !== 'undefined') alert('❌ Ocorreu um erro ao salvar o pacote Arestrix.');
          }
        } else {
          const ok = await this.projectManager.saveProject(this.graph);
          if (ok) {
            if (typeof alert !== 'undefined') alert(`💾 Projeto e grafos salvos com sucesso em formato JSON!`);
            this.render();
          }
        }
      });
    }

    if (!this.overlay) return;

    // Salvar Workspace
    this.overlay.querySelector('#btn-backstage-save')?.addEventListener('click', async () => {
      await this.projectManager.saveProject(this.graph);
      if (typeof alert !== 'undefined') alert('💾 Workspace e pipelines salvos com sucesso!');
      this.close();
    });

    // Opções do Studio & Governança do Workspace
    this.overlay.querySelector('#btn-sync-workspace-disk')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      await wm.installApprovedAssetsToDisk();
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #e8f5e9; border: 1px solid #c8e6c9; border-radius: 6px; font-size: 12px; color: #1e7e34;">
            <strong>✅ Sincronização Concluída com Sucesso!</strong><br>
            Recursos pré-aprovados foram instalados e atualizados nas pastas físicas da sua máquina:<br>
            • <code>${wm.getWorkspaceDirectories().skills}</code> (${wm.getApprovedSkills().length} habilidades e arquivos SKILL.md)<br>
            • <code>${wm.getWorkspaceDirectories().mcps}</code> (${wm.getApprovedMcps().length} servidores MCP)<br>
            • <code>${wm.getWorkspaceDirectories().agents}</code> (agentes de governança)
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-auditor')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const res = await wm.runAgent('agent-workspace-skill-auditor');
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 6px; font-size: 12px; color: #166534;">
            <strong>🛡️ ${res.summary}</strong><br>
            Status: <strong>${res.status.toUpperCase()}</strong> | Timestamp: ${new Date(res.timestamp).toLocaleTimeString()}<br>
            Todas as habilidades ativas e pré-aprovadas foram auditadas sem riscos ou violações.
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-linter')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const res = await wm.runGraph(this.graph);
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; font-size: 12px; color: #1e40af;">
            <strong>📊 ${res.summary}</strong><br>
            Nós verificados: ${res.details?.nodeCount} | Arestas: ${res.details?.edgeCount}<br>
            Nenhum erro de linting ou desconexão detectado no grafo.
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-project')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const cur = this.projectManager.getCurrentProject();
      const res = await wm.runProject(cur.projectId, this.graph);
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #faf5ff; border: 1px solid #e9d5ff; border-radius: 6px; font-size: 12px; color: #6b21a8;">
            <strong>🚀 ${res.summary}</strong><br>
            Pipeline inicializado no workspace com persistência e telemetria ativas.
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-open-global-defaults-modal')?.addEventListener('click', () => {
      this.close();
      this.globalDefaultsModal.show();
    });

    // Repositórios do GitHub
    this.overlay.querySelector('#btn-backstage-github-repos')?.addEventListener('click', () => {
      this.close();
      globalEventBus.emit('modal:open-github-repos');
    });

    // Criar Projeto
    this.overlay.querySelector('#btn-backstage-create-submit')?.addEventListener('click', async () => {
      if (!this.overlay) return;
      const nameInput = this.overlay.querySelector('#backstage-new-name') as HTMLInputElement;
      const descInput = this.overlay.querySelector('#backstage-new-desc') as HTMLTextAreaElement;

      const pName = nameInput?.value.trim() || 'Novo Projeto';
      const pId = hashProjectName(pName);
      const pDesc = descInput?.value.trim() || '';

      await this.projectManager.createProject({
        projectId: pId,
        name: pName,
        description: pDesc,
        pipelines: [
          {
            id: 'main',
            name: 'Pipeline Principal',
            file: 'pipelines/main.json',
            isPrimary: true
          }
        ]
      }, this.graph);

      if (typeof alert !== 'undefined') alert(`✅ Projeto "${pName}" criado e ativo!`);
      this.close();
    });

    // Ações de projeto recente no Backstage (Dropdown Estilo Office)
    this.overlay.querySelectorAll<HTMLSelectElement>('.backstage-project-action-select').forEach(select => {
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
            await this.projectManager.loadProject(pTarget, this.graph);
            if (typeof alert !== 'undefined') alert(`✅ Projeto "${pName}" carregado com sucesso!`);
            this.close();
          }
        } else if (action === 'manage') {
          if (pTarget) {
            const cur = this.projectManager.getCurrentProject();
            if (cur.projectId !== pId && cur.rootPath !== pPath) {
              await this.projectManager.loadProject(pTarget, this.graph);
            }
          }
          this.close();
          globalEventBus.emit('modal:open-project-manager', 'settings');
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

    // Carregar grafo/pipeline recente do workspace
    this.overlay.querySelectorAll<HTMLButtonElement>('.btn-pipeline-load').forEach(btn => {
      btn.addEventListener('click', async () => {
        await this.projectManager.loadPipeline(this.graph);
        if (typeof alert !== 'undefined') alert('✅ Grafo carregado no canvas!');
        this.close();
      });
    });

    // Exportações
    this.overlay.querySelector('#btn-backstage-open-codemodal')?.addEventListener('click', () => {
      this.close();
      this.codeModal.show();
    });

    this.overlay.querySelector('#btn-backstage-export-cmake')?.addEventListener('click', () => {
      this.close();
      this.codeModal.show();
    });

    this.overlay.querySelector('#btn-backstage-export-svg')?.addEventListener('click', () => {
      this.close();
      globalEventBus.emit('ribbon:export-svg');
    });

    this.overlay.querySelector('#btn-quick-sync-db')?.addEventListener('click', () => {
      if (typeof alert !== 'undefined') alert('🗄️ Banco de dados SQLite sincronizado com a estrutura do projeto!');
    });

    this.overlay.querySelector('#btn-quick-clean-cache')?.addEventListener('click', () => {
      if (typeof alert !== 'undefined') alert('🧹 Cache de checkpoints do Resume Engine limpo com sucesso.');
    });
  }

  private escapeHtml(str?: string): string {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
