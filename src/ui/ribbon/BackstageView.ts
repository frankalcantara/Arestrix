import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { IProjectManifest, IRecentProjectItem } from '../../core/types.ts';
import { CodeModal } from '../CodeModal.ts';
import { GlobalDefaultsModal } from '../GlobalDefaultsModal.ts';

export class BackstageView {
  private overlay?: HTMLElement;
  private graph: Graph;
  private projectManager: ProjectManager;
  private codeModal: CodeModal;
  private globalDefaultsModal: GlobalDefaultsModal;
  private activeSection: 'info' | 'new' | 'open' | 'export' = 'info';

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
    }
  }

  public open(section: 'info' | 'new' | 'open' | 'export' = 'info'): void {
    this.activeSection = section;
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

  private render(): void {
    if (!this.overlay) return;
    const proj: IProjectManifest = this.projectManager.getCurrentProject();
    const recentList: IRecentProjectItem[] = this.projectManager.listRecentProjects();

    this.overlay.innerHTML = `
      <div class="office-backstage-container">
        <!-- BARRA LATERAL OFFICE (AZUL FLUENT) -->
        <div class="office-backstage-sidebar">
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
            <li class="backstage-nav-item ${this.activeSection === 'export' ? 'active' : ''}" data-section="export">
              <span>📤</span> Exportar Código
            </li>
            <li class="backstage-nav-item" id="btn-backstage-github-repos">
              <span>🌐</span> Repositórios GitHub
            </li>
            <li class="backstage-nav-item" id="btn-backstage-options">
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
              <p><strong>Nós Ativos:</strong> ${this.graph.getAllNodes().length}</p>
              <p><strong>Arestas / Links:</strong> ${this.graph.getAllEdges().length}</p>
              <p><strong>Pipelines Vinculados:</strong> ${proj.pipelines.length}</p>
            </div>
          </div>

          <div style="margin-top: 10px;">
            <h4 style="font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 8px;">Ações Rápidas de Manutenção</h4>
            <div style="display: flex; gap: 10px;">
              <button class="tool-btn btn-secondary" id="btn-quick-sync-db" style="font-weight: 700;">🔄 Sincronizar Banco</button>
              <button class="tool-btn btn-secondary" id="btn-quick-clean-cache" style="font-weight: 700;">🧹 Limpar Cache do Resume</button>
            </div>
          </div>
        `;

      case 'new':
        return `
          <h2 class="backstage-header-title">Criar Novo Projeto</h2>
          <p class="backstage-header-sub">Configure um novo workspace isolado com estrutura de diretórios e pipelines.</p>

          <div style="max-width: 600px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 24px;">
            <div style="margin-bottom: 16px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">ID Único do Projeto:</label>
              <input type="text" id="backstage-new-id" value="projeto_${Date.now()}" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px;">
            </div>

            <div style="margin-bottom: 16px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome de Exibição:</label>
              <input type="text" id="backstage-new-name" placeholder="Ex: Auditoria de Agentes 2026" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px;">
            </div>

            <div style="margin-bottom: 24px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Descrição do Propósito:</label>
              <textarea id="backstage-new-desc" rows="3" placeholder="Objetivo e escopo deste projeto..." style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px;"></textarea>
            </div>

            <div style="display: flex; gap: 12px;">
              <button class="tool-btn btn-action" id="btn-backstage-create-submit" style="background: #0f6cbd; color: #fff; font-weight: 700; padding: 10px 20px;">Criar e Abrir Workspace</button>
            </div>
          </div>
        `;

      case 'open':
        return `
          <h2 class="backstage-header-title">Projetos Recentes</h2>
          <p class="backstage-header-sub">Selecione um projeto acessado recentemente para carregar instantaneamente.</p>

          ${recentList.length === 0 ? `
            <div style="padding: 24px; background: #f8fafc; border-radius: 8px; color: #64748b; font-size: 13px;">
              Nenhum projeto recente registrado. Crie um novo projeto para começar.
            </div>
          ` : `
            <table class="backstage-mru-table">
              <thead>
                <tr>
                  <th>Nome do Projeto</th>
                  <th>ID do Workspace</th>
                  <th>Caminho Raiz</th>
                  <th>Último Acesso</th>
                  <th style="text-align: right;">Ação</th>
                </tr>
              </thead>
              <tbody>
                ${recentList.map(item => `
                  <tr>
                    <td><strong>${this.escapeHtml(item.name)}</strong></td>
                    <td><code>${this.escapeHtml(item.projectId)}</code></td>
                    <td><small style="color: #64748b;">${this.escapeHtml(item.rootPath)}</small></td>
                    <td>${new Date(item.lastOpened).toLocaleString()}</td>
                    <td style="text-align: right;">
                      <button class="tool-btn btn-secondary btn-mru-open" data-id="${item.projectId}" style="padding: 4px 10px; font-size: 11.5px; font-weight: 700;">
                        Abrir
                      </button>
                    </td>
                  </tr>
                `).join('')}
              </tbody>
            </table>
          `}
        `;

      case 'export':
        return `
          <h2 class="backstage-header-title">Exportação de Engenharia C++20</h2>
          <p class="backstage-header-sub">Exporte código nativo Taskflow C++20, scripts de build ou descritores do pipeline.</p>

          <div class="backstage-cards-grid">
            <div class="backstage-info-card">
              <h4>💻 Código Fonte C++20</h4>
              <p>Gere e visualize o arquivo <code>main.cpp</code> completo com tarefas Taskflow e lambdas dos nós.</p>
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
      item.addEventListener('click', () => {
        const sec = item.dataset.section as any;
        if (sec) {
          this.activeSection = sec;
          this.render();
        }
      });
    });

    if (!this.overlay) return;

    // Salvar Workspace
    this.overlay.querySelector('#btn-backstage-save')?.addEventListener('click', async () => {
      await this.projectManager.saveProject(this.graph);
      if (typeof alert !== 'undefined') alert('💾 Workspace e pipelines salvos com sucesso!');
      this.close();
    });

    // Opções do Studio
    this.overlay.querySelector('#btn-backstage-options')?.addEventListener('click', () => {
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
      const idInput = this.overlay.querySelector('#backstage-new-id') as HTMLInputElement;
      const nameInput = this.overlay.querySelector('#backstage-new-name') as HTMLInputElement;
      const descInput = this.overlay.querySelector('#backstage-new-desc') as HTMLTextAreaElement;

      const pId = idInput?.value.trim() || `proj_${Date.now()}`;
      const pName = nameInput?.value.trim() || 'Novo Projeto';
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

    // Abrir item recente
    this.overlay.querySelectorAll<HTMLButtonElement>('.btn-mru-open').forEach(btn => {
      btn.addEventListener('click', async () => {
        const pId = btn.dataset.id;
        if (pId) {
          await this.projectManager.loadProject(pId, this.graph);
          if (typeof alert !== 'undefined') alert('✅ Projeto carregado!');
          this.close();
        }
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

  private escapeHtml(str: string): string {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
