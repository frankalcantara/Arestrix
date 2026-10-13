import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { INodeData, INodeSkill } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { GitHubSkillsRegistry } from '../../core/skills/GitHubSkillsRegistry.ts';
import { EventBus } from '../../core/EventBus.ts';
import { WorkspaceManager } from '../../core/workspace/WorkspaceManager.ts';
import { WorkspaceSkillAuditorAgent } from '../../core/workspace/WorkspaceSkillAuditorAgent.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Diálogo de Habilidades & Skills do Agente (Padrão Microsoft Office)
 * 
 * Permite ao nó individual do agente:
 * 1. Escolher e importar Habilidades Pré-Aprovadas do Workspace com auditoria e certificação.
 * 2. Edição manual completa do SKILL.md (Editor Markdown com visualizador e campo de prompt do agente).
 * 3. Escolher e importar habilidades open source a partir do catálogo global fornecido
 *    pelos repositórios do GitHub configurados no sistema.
 * 
 * NOTA: A gestão dos repositórios autorizados do GitHub é global do sistema (via GitHubReposModal).
 */
export class NodeSkillsModal extends BaseModal {
  private graph: Graph;
  private registry: GitHubSkillsRegistry;
  private workspaceManager: WorkspaceManager;
  private workspaceAuditor: WorkspaceSkillAuditorAgent;
  private bus?: EventBus;
  private activeNodeId: string | null = null;
  private activeTab: 'workspace' | 'editor' | 'github' = 'workspace';
  private markdownViewMode: 'edit' | 'preview' = 'edit';
  private selectedCategory: string = 'todas';
  private searchQuery: string = '';
  private workspaceCategoryFilter: string = 'todas';
  private workspaceSearchQuery: string = '';
  private notificationMessage: string | null = null;
  private notificationType: 'success' | 'info' | 'warning' = 'success';

  constructor(graph: Graph, bus?: EventBus) {
    super({
      id: 'node-skills-modal',
      title: t('node_skills.title', 'Habilidades & Skills do Agente (SKILL.md)'),
      icon: '📜',
      minWidth: 800,
      minHeight: 580,
      dialogClass: 'node-skills-modal-dialog'
    });

    this.graph = graph;
    this.bus = bus;
    this.registry = GitHubSkillsRegistry.getInstance();
    this.workspaceManager = WorkspaceManager.getInstance(bus);
    this.workspaceAuditor = WorkspaceSkillAuditorAgent.getInstance();

    // Quando os repositórios globais forem alterados, re-renderiza se estiver aberto na aba github
    this.bus?.on('skills:repositories-updated', () => {
      if (this.isOpen && this.activeTab === 'github' && this.activeNodeId) {
        this.render();
      }
    });
  }

  public override open(nodeId?: string, initialTab: 'workspace' | 'editor' | 'github' = 'workspace'): void {
    if (nodeId) {
      const node = this.graph.getNode(nodeId);
      if (!node) return;

      this.activeNodeId = nodeId;
      this.ensureNodeHasSkill(node);
      this.config.subtitle = t('node_skills.subtitle', '📦 Vértice do Projeto: <strong style="color: #0f6cbd;">{{label}}</strong> (ID: <code>{{id}}</code>) | Catálogo do 🛠️ Workspace', { label: this.escapeHtml(node.label), id: node.id });
    }

    this.activeTab = initialTab;
    this.notificationMessage = null;

    super.open();
  }

  public override hide(): void {
    super.hide();
    this.notificationMessage = null;
    this.activeNodeId = null;
  }

  private ensureNodeHasSkill(node: INodeData): void {
    if (!node.agentSettings) {
      node.agentSettings = {
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        skills: []
      };
    }

    if (!node.agentSettings.skills || node.agentSettings.skills.length === 0) {
      const defaultSkill: INodeSkill = {
        id: `skill-${node.id.replace(/[^a-zA-Z0-9_-]/g, '_')}`,
        name: node.label || 'Habilidade Especializada',
        description: `Habilidade operacional padrão para o agente ${node.label}`,
        markdownContent: `---\nname: "${node.label}"\ndescription: "Execução especializada de tarefas"\nlicense: "MIT"\ntools:\n  - write\n  - edit\n  - read\n---\n\n# ${node.label}\n\nInstruções operacionais e diretrizes para execução do agente.\n\n## Diretrizes\n1. Atuação com máxima precisão e objetividade.\n2. Cumprimento estrito das regras de formatação e integridade dos artefatos.`,
        systemPrompt: `Você é o agente responsável por "${node.label}". Execute suas tarefas com precisão técnica e rigor conceitual.`,
        tools: ['write', 'edit', 'read'],
        license: 'MIT',
        updatedAt: new Date().toISOString()
      };

      node.agentSettings.skills = [defaultSkill];
      node.agentSettings.activeSkillId = defaultSkill.id;
      this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
    } else if (!node.agentSettings.activeSkillId) {
      node.agentSettings.activeSkillId = node.agentSettings.skills[0].id;
      this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
    }
  }

  private getActiveSkill(node: INodeData): INodeSkill {
    const skills = node.agentSettings?.skills || [];
    const activeId = node.agentSettings?.activeSkillId;
    return skills.find(s => s.id === activeId) || skills[0];
  }

  protected renderBody(): string {
    if (!this.activeNodeId) return `<div style="padding: 20px;">${t('node_skills.no_active_vertex', 'Nenhum vértice ativo selecionado.')}</div>`;
    const node = this.graph.getNode(this.activeNodeId);
    if (!node) return `<div style="padding: 20px;">${t('node_skills.vertex_not_found', 'Vértice não encontrado.')}</div>`;

    const currentSkill = this.getActiveSkill(node);
    const skillsList = node.agentSettings?.skills || [];
    const githubSkills = this.registry.searchSkills(this.searchQuery, this.selectedCategory);
    const reposList = this.registry.getRepositories();
    const activeReposCount = reposList.filter(r => r.isEnabled).length;
    const approvedSkills = this.workspaceManager.getApprovedSkills().filter(s => {
      const matchCat = this.workspaceCategoryFilter === 'todas' || s.category === this.workspaceCategoryFilter;
      const matchSearch = !this.workspaceSearchQuery ||
        s.name.toLowerCase().includes(this.workspaceSearchQuery.toLowerCase()) ||
        s.description.toLowerCase().includes(this.workspaceSearchQuery.toLowerCase()) ||
        s.tools.some(t => t.toLowerCase().includes(this.workspaceSearchQuery.toLowerCase()));
      return matchCat && matchSearch;
    });
    const approvedMcps = this.workspaceManager.getApprovedMcps();
    const currentAudit = this.workspaceAuditor.auditSkill(currentSkill);

    return `
      <!-- Abas de Navegação Particionadas por Escopo -->
      <div class="office-modal-tab-bar">
        <button class="office-modal-tab-btn ${this.activeTab === 'workspace' ? 'active' : ''}" data-tab="workspace">
          ${t('node_skills.tab_workspace', '🛡️ Habilidades Pré-Aprovadas do Workspace')}
        </button>
        <button class="office-modal-tab-btn ${this.activeTab === 'editor' ? 'active' : ''}" data-tab="editor">
          ${t('node_skills.tab_editor', '📝 [Projeto] Habilidade do Agente & Editor')}
        </button>
        <button class="office-modal-tab-btn ${this.activeTab === 'github' ? 'active' : ''}" data-tab="github">
          ${t('node_skills.tab_github', '🌐 [Studio] Importar do Catálogo (GitHub Open Source)')}
        </button>
      </div>

      ${this.notificationMessage ? `
        <div class="office-callout ${this.notificationType === 'success' ? 'callout-success' : 'callout-warning'}" style="margin: 12px 20px 0 20px; padding: 10px 14px; border-radius: 4px; font-size: 12px;">
          <span>${this.escapeHtml(this.notificationMessage)}</span>
        </div>
      ` : ''}

      <div style="padding: 16px 20px;">
        <!-- ABA 1: HABILIDADES PRÉ-APROVADAS DO WORKSPACE -->
        <div class="skills-tab-pane ${this.activeTab === 'workspace' ? 'active' : ''}" id="pane-workspace">
          <div class="office-info-callout" style="margin-bottom: 14px; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 4px; padding: 12px 14px; display: flex; align-items: flex-start; justify-content: space-between; gap: 12px;">
            <div style="display: flex; align-items: flex-start; gap: 10px;">
              <span style="font-size: 20px; line-height: 1;">🛡️</span>
              <div style="font-size: 12px; color: #166534; line-height: 1.45;">
                <strong>${t('node_skills.callout_ws_title', 'Habilidades Pré-Aprovadas do Workspace:')}</strong> ${t('node_skills.callout_ws_desc', 'Catálogo oficial homologado com governança estrita e certificação de conformidade. Todas as habilidades cumprem integralmente as diretrizes do Arestrix (incluindo política de <strong>Zero Sombras em Tipografia Vetorial SVG</strong> e integridade de artefatos). Instaladas nativamente em <code>.workspace/skills</code> na sua máquina.')}
              </div>
            </div>
            <div style="font-size: 11px; font-weight: 700; color: #15803d; background: #dcfce7; padding: 4px 8px; border-radius: 4px; border: 1px solid #86efac; white-space: nowrap;">
              ${t('node_skills.badge_approved_count', '{{count}} Homologadas', { count: approvedSkills.length })}
            </div>
          </div>

          <div style="display: flex; gap: 10px; margin-bottom: 12px; align-items: center;">
            <div style="flex: 1; position: relative;">
              <input type="text" id="input-search-workspace-skills" class="form-input" placeholder="${t('node_skills.placeholder_search_ws', 'Buscar habilidades pré-aprovadas por nome, ferramenta ou palavra-chave...')}" value="${this.escapeHtml(this.workspaceSearchQuery)}" />
            </div>
            <select id="select-workspace-category-filter" class="form-select" style="max-width: 220px;">
              <option value="todas" ${this.workspaceCategoryFilter === 'todas' ? 'selected' : ''}>${t('node_skills.cat_all', 'Todas as Categorias')}</option>
              <option value="Redação & Conteúdo" ${this.workspaceCategoryFilter === 'Redação & Conteúdo' ? 'selected' : ''}>${t('node_skills.cat_writing', 'Redação & Conteúdo')}</option>
              <option value="Matemática & Ciência" ${this.workspaceCategoryFilter === 'Matemática & Ciência' ? 'selected' : ''}>${t('node_skills.cat_math', 'Matemática & Ciência')}</option>
              <option value="Design & Visual" ${this.workspaceCategoryFilter === 'Design & Visual' ? 'selected' : ''}>${t('node_skills.cat_design', 'Design & Visual (SVG)')}</option>
              <option value="Engenharia de Software" ${this.workspaceCategoryFilter === 'Engenharia de Software' ? 'selected' : ''}>${t('node_skills.cat_software', 'Engenharia C++20')}</option>
              <option value="Dados & Armazenamento" ${this.workspaceCategoryFilter === 'Dados & Armazenamento' ? 'selected' : ''}>${t('node_skills.cat_data', 'Dados & SQL')}</option>
              <option value="Engenharia de Prompt" ${this.workspaceCategoryFilter === 'Engenharia de Prompt' ? 'selected' : ''}>${t('node_skills.cat_prompt', 'Prompt XML')}</option>
              <option value="Governança & Auditoria" ${this.workspaceCategoryFilter === 'Governança & Auditoria' ? 'selected' : ''}>${t('node_skills.cat_governance', 'Governança & Auditoria')}</option>
            </select>
          </div>

          <!-- Grid de Habilidades Pré-Aprovadas -->
          <div class="workspace-skills-grid" style="display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 12px; margin-bottom: 20px;">
            ${approvedSkills.length === 0 ? `
              <div style="grid-column: 1 / -1; text-align: center; padding: 30px; color: #64748b; background: #f8fafc; border: 1px dashed #cbd5e1; border-radius: 4px; font-size: 13px;">
                ${t('node_skills.empty_ws_search', 'Nenhuma habilidade pré-aprovada corresponde aos filtros atuais.')}
              </div>
            ` : approvedSkills.map(item => `
              <div class="workspace-skill-card" style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 6px; padding: 14px; display: flex; flex-direction: column; justify-content: space-between; transition: all 0.15s ease;">
                <div>
                  <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 8px; margin-bottom: 6px;">
                    <span style="font-size: 14px; font-weight: 700; color: #0f6cbd;">${this.escapeHtml(item.name)}</span>
                    <span style="background: #e8f5e9; color: #1e7e34; border: 1px solid #c8e6c9; border-radius: 3px; font-size: 10px; font-weight: 700; padding: 2px 6px; white-space: nowrap;">
                      ${t('node_skills.badge_pre_approved', '🛡️ PRÉ-APROVADA')}
                    </span>
                  </div>

                  <div style="font-size: 11px; color: #64748b; margin-bottom: 8px; display: flex; justify-content: space-between;">
                    <span>📂 ${this.escapeHtml(item.category)} &bull; v${this.escapeHtml(item.version)}</span>
                    <span style="font-weight: 600;">${this.escapeHtml(item.license)}</span>
                  </div>

                  <div style="font-size: 12px; color: #334155; line-height: 1.4; margin-bottom: 10px;">
                    ${this.escapeHtml(item.description)}
                  </div>

                  <div style="display: flex; gap: 4px; flex-wrap: wrap; margin-bottom: 10px;">
                    ${item.tools.map(t => `<code style="font-size: 10px; background: #f1f5f9; color: #475569; padding: 1px 5px; border-radius: 3px;">${t}</code>`).join('')}
                  </div>

                  <div style="font-size: 11px; color: #0f766e; background: #f0fdf4; padding: 4px 8px; border-radius: 4px; margin-bottom: 12px; border: 1px solid #dcfce7;">
                    ${t('node_skills.compliance_note', '✓ Zero Sombras em Textos &bull; Risco de Auditoria: 0/100')}
                  </div>
                </div>

                <div style="display: flex; justify-content: flex-end; border-top: 1px solid #f1f5f9; padding-top: 10px;">
                  <button class="btn btn-primary btn-sm btn-import-workspace-skill" data-skill-id="${item.id}" style="font-weight: 700;">
                    ${t('node_skills.btn_import_to_agent', '📥 Importar para este Agente')}
                  </button>
                </div>
              </div>
            `).join('')}
          </div>

          <!-- Painel Informativo de MCPs Pré-aprovados -->
          <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 12px 16px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
              <strong style="font-size: 12px; color: #334155;">${t('node_skills.panel_mcps_title', '🔌 Servidores MCP Pré-Aprovados do Workspace ({{count}})', { count: approvedMcps.length })}</strong>
              <span style="font-size: 11px; color: #64748b;">${t('node_skills.panel_mcps_desc', 'Instalados em <code>.workspace/mcps</code>')}</span>
            </div>
            <div style="display: grid; grid-template-columns: repeat(auto-fill, minmax(220px, 1fr)); gap: 8px;">
              ${approvedMcps.map(m => `
                <div style="padding: 6px 10px; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 4px;">
                  <div style="font-size: 11px; font-weight: 700; color: #0f172a;">${this.escapeHtml(m.name)}</div>
                  <div style="font-size: 10px; color: #64748b;">${this.escapeHtml(m.description)}</div>
                </div>
              `).join('')}
            </div>
          </div>
        </div>

        <!-- ABA 2: EDITOR MANUAL DE SKILL E PROMPT DO VÉRTICE -->
        <div class="skills-tab-pane ${this.activeTab === 'editor' ? 'active' : ''}" id="pane-editor">
          <!-- Callout do Agente de Auditoria do Workspace -->
          <div class="office-callout" style="margin-bottom: 14px; padding: 10px 14px; border-radius: 4px; display: flex; justify-content: space-between; align-items: center; ${currentAudit.status === 'approved' ? 'background: #f0fdf4; border: 1px solid #bbf7d0;' : 'background: #fffbeb; border: 1px solid #fde68a;'}">
            <div style="display: flex; align-items: center; gap: 10px;">
              <span style="font-size: 18px;">${currentAudit.status === 'approved' ? '🛡️' : '⚠️'}</span>
              <div style="font-size: 12px; line-height: 1.4; ${currentAudit.status === 'approved' ? 'color: #166534;' : 'color: #92400e;'}">
                <strong>${t('node_skills.audit_title', 'Auditoria do Workspace:')} ${currentAudit.status === 'approved' ? t('node_skills.audit_approved', 'APROVADA E HOMOLOGADA') : t('node_skills.audit_alert', 'ALERTA DE CONFORMIDADE')}</strong> (${t('node_skills.audit_risk_score', 'Pontuação de Risco: {{score}}/100', { score: currentAudit.riskScore })})<br>
                <span style="font-size: 11px; ${currentAudit.status === 'approved' ? 'color: #15803d;' : 'color: #b45309;'}">
                  ${currentAudit.warnings.length > 0 ? currentAudit.warnings.join(' &bull; ') : t('node_skills.audit_ok_text', '✓ Habilidade cumpre a regra de Zero Sombras em Fontes e lista de ferramentas autorizadas.')}
                </span>
              </div>
            </div>
            <span style="font-size: 10px; font-weight: 700; padding: 2px 8px; border-radius: 4px; white-space: nowrap; ${currentAudit.status === 'approved' ? 'background: #dcfce7; color: #15803d; border: 1px solid #86efac;' : 'background: #fef3c7; color: #b45309; border: 1px solid #fcd34d;'}">
              ${currentAudit.status === 'approved' ? t('node_skills.badge_audit_ok', 'AUDITADA: OK') : t('node_skills.badge_requires_attention', 'REQUER ATENÇÃO')}
            </span>
          </div>

          <div class="office-section-card" style="margin-bottom: 14px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
              <label class="form-label" style="margin: 0; font-weight: 700;">${t('node_skills.label_focus_skill', 'Habilidade em Foco:')}</label>
              <div style="display: flex; gap: 8px;">
                <select id="select-active-skill" class="form-select" style="min-width: 240px;">
                  ${skillsList.map(s => `
                    <option value="${s.id}" ${s.id === currentSkill.id ? 'selected' : ''}>
                      ${this.escapeHtml(s.name)} (${s.license || 'MIT'})
                    </option>
                  `).join('')}
                </select>
                <button id="btn-create-new-skill" class="btn btn-secondary" title="${t('node_skills.tip_create_skill', 'Criar nova habilidade em branco para este agente')}">
                  ${t('node_skills.btn_create_skill', '+ Nova')}
                </button>
              </div>
            </div>

            <div class="form-row-grid" style="grid-template-columns: 2fr 1fr; gap: 12px; margin-bottom: 12px;">
              <div class="form-group" style="margin: 0;">
                <label class="form-label" for="skill-name-input">${t('node_skills.label_skill_name', 'Nome da Habilidade')}</label>
                <input type="text" id="skill-name-input" class="form-input" value="${this.escapeHtml(currentSkill.name)}" />
              </div>
              <div class="form-group" style="margin: 0;">
                <label class="form-label" for="skill-license-input">${t('node_skills.label_skill_license', 'Licença de Uso')}</label>
                <input type="text" id="skill-license-input" class="form-input" value="${this.escapeHtml(currentSkill.license || 'MIT')}" />
              </div>
            </div>

            <div class="form-group" style="margin-bottom: 12px;">
              <label class="form-label" for="skill-desc-input">${t('node_skills.label_skill_desc', 'Descrição Operacional')}</label>
              <input type="text" id="skill-desc-input" class="form-input" value="${this.escapeHtml(currentSkill.description)}" />
            </div>

            <div class="form-group" style="margin-bottom: 12px;">
              <label class="form-label">${t('node_skills.label_authorized_tools', 'Ferramentas Autorizadas para esta Habilidade')}</label>
              <div class="tools-checkbox-row" style="display: flex; flex-wrap: wrap; gap: 12px; padding: 8px 12px; background: #faf9f8; border: 1px solid #edebe9; border-radius: 4px;">
                ${['write', 'edit', 'read', 'bash', 'validate_svg', 'codemode'].map(tool => `
                  <label style="display: flex; align-items: center; gap: 6px; font-size: 13px; cursor: pointer;">
                    <input type="checkbox" class="chk-skill-tool" value="${tool}" ${currentSkill.tools?.includes(tool) ? 'checked' : ''} />
                    <code>${tool}</code>
                  </label>
                `).join('')}
              </div>
            </div>

            <div class="form-group" style="margin-bottom: 14px;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                <label class="form-label" for="skill-prompt-input" style="margin: 0;">${t('node_skills.label_system_prompt', 'Prompt Operacional & Regras do Agente (System Prompt)')}</label>
                <span style="font-size: 11px; color: #64748b;">${t('node_skills.prompt_injection_hint', 'Injetado diretamente na sessão LLM do agente')}</span>
              </div>
              <textarea id="skill-prompt-input" class="form-textarea code-font" rows="4" placeholder="${t('node_skills.placeholder_system_prompt', 'Instruções de sistema, tom de voz, restrições e regras...')}">${this.escapeHtml(currentSkill.systemPrompt || '')}</textarea>
            </div>

            <!-- Editor Markdown com Abas [Editar] / [Visualizar] -->
            <div class="form-group" style="margin-bottom: 0;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                <label class="form-label" style="margin: 0; font-weight: 700;">${t('node_skills.label_markdown_content', 'Conteúdo Integral do SKILL.md (Markdown)')}</label>
                <div class="office-subtab-group" style="display: flex; gap: 4px;">
                  <button class="office-subtab-btn ${this.markdownViewMode === 'edit' ? 'active' : ''}" id="btn-mode-md-edit">
                    ${t('node_skills.btn_edit_markdown', '✏️ Editar Markdown')}
                  </button>
                  <button class="office-subtab-btn ${this.markdownViewMode === 'preview' ? 'active' : ''}" id="btn-mode-md-preview">
                    ${t('node_skills.btn_preview_markdown', '👁️ Visualizar Prévia')}
                  </button>
                </div>
              </div>

              ${this.markdownViewMode === 'edit' ? `
                <textarea id="skill-markdown-input" class="form-textarea code-font" rows="12" style="font-size: 12px; line-height: 1.5;" placeholder="${t('node_skills.placeholder_markdown', '# Nome da Skill\n\nInstruções detalhadas em Markdown...')}">${this.escapeHtml(currentSkill.markdownContent || '')}</textarea>
              ` : `
                <div class="office-markdown-preview" style="min-height: 250px; max-height: 380px; overflow-y: auto; padding: 14px 18px; background: #ffffff; border: 1px solid #d1d5db; border-radius: 4px;">
                  ${this.renderMarkdownToHtml(currentSkill.markdownContent || '')}
                </div>
              `}
            </div>
          </div>

          <div style="display: flex; justify-content: flex-end; gap: 10px; margin-top: 14px;">
            <button class="btn btn-secondary" id="btn-delete-current-skill" ${skillsList.length <= 1 ? 'disabled' : ''}>
              ${t('node_skills.btn_delete_skill', 'Excluir Habilidade')}
            </button>
            <button class="btn btn-primary" id="btn-save-skill-changes" style="min-width: 140px;">
              ${t('node_skills.btn_save_changes', 'Salvar Alterações')}
            </button>
          </div>
        </div>

        <!-- ABA 2: IMPORTAR DO GITHUB (OPEN SOURCE) -->
        <div class="skills-tab-pane ${this.activeTab === 'github' ? 'active' : ''}" id="pane-github">
          <div class="office-info-callout" style="margin-bottom: 14px; background: #f0f7ff; border: 1px solid #c7e0f4; border-radius: 4px; padding: 10px 14px; display: flex; align-items: flex-start; justify-content: space-between; gap: 12px;">
            <div style="display: flex; align-items: flex-start; gap: 10px;">
              <span style="font-size: 18px; line-height: 1;">ℹ️</span>
              <div style="font-size: 12px; color: #242424; line-height: 1.4;">
                <strong>${t('node_skills.callout_gh_title', 'Catálogo Curado de Skills Open Source:')}</strong> ${t('node_skills.callout_gh_desc', 'Escolha quais habilidades importar para este agente. As opções disponíveis são fornecidas pelos <strong>{{count}} repositórios globais</strong> ativos do sistema.', { count: activeReposCount })}
              </div>
            </div>
            <button class="btn btn-secondary btn-sm" id="btn-open-global-repos-from-node" style="white-space: nowrap; font-weight: 600; font-size: 11px;">
              ${t('node_skills.btn_configure_repos', '⚙️ Configurar Projetos GitHub...')}
            </button>
          </div>

          <div style="display: flex; gap: 10px; margin-bottom: 12px; align-items: center;">
            <div style="flex: 1; position: relative;">
              <input type="text" id="input-search-github-skills" class="form-input" placeholder="${t('node_skills.placeholder_search_gh', 'Buscar habilidades por nome, descrição ou ferramenta...')}" value="${this.escapeHtml(this.searchQuery)}" />
            </div>
            <select id="select-category-filter" class="form-select" style="max-width: 200px;">
              <option value="todas" ${this.selectedCategory === 'todas' ? 'selected' : ''}>${t('node_skills.cat_all', 'Todas as Categorias')}</option>
              <option value="Redação & Conteúdo" ${this.selectedCategory === 'Redação & Conteúdo' ? 'selected' : ''}>${t('node_skills.cat_writing', 'Redação & Conteúdo')}</option>
              <option value="Matemática & Ciência" ${this.selectedCategory === 'Matemática & Ciência' ? 'selected' : ''}>${t('node_skills.cat_math', 'Matemática & Ciência')}</option>
              <option value="Design & Visual" ${this.selectedCategory === 'Design & Visual' ? 'selected' : ''}>${t('node_skills.cat_design', 'Design & Visual (SVG)')}</option>
              <option value="Engenharia de Software" ${this.selectedCategory === 'Engenharia de Software' ? 'selected' : ''}>${t('node_skills.cat_software', 'Engenharia C++20')}</option>
              <option value="Dados & Armazenamento" ${this.selectedCategory === 'Dados & Armazenamento' ? 'selected' : ''}>${t('node_skills.cat_data', 'Dados & SQL')}</option>
              <option value="Engenharia de Prompt" ${this.selectedCategory === 'Engenharia de Prompt' ? 'selected' : ''}>${t('node_skills.cat_prompt', 'Prompt XML')}</option>
            </select>
          </div>

          <!-- Grid de Habilidades do GitHub -->
          <div class="github-skills-grid" style="display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 12px;">
            ${githubSkills.length === 0 ? `
              <div style="grid-column: 1 / -1; text-align: center; padding: 40px 20px; color: #64748b; background: #f8fafc; border: 1px dashed #cbd5e1; border-radius: 4px;">
                <p style="margin: 0 0 10px 0; font-size: 13px;">${t('node_skills.empty_gh_search', 'Nenhuma habilidade encontrada para o termo pesquisado ou não há repositórios ativos.')}</p>
                <button class="btn btn-secondary btn-sm" id="btn-empty-open-global-repos" style="font-weight: 600;">
                  ${t('node_skills.btn_empty_open_repos', '⚙️ Abrir Configuração Global de Repositórios GitHub')}
                </button>
              </div>
            ` : githubSkills.map(item => `
              <div class="github-skill-card" style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 12px 14px; display: flex; flex-direction: column; justify-content: space-between; transition: border-color 0.15s ease;">
                <div>
                  <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 8px; margin-bottom: 6px;">
                    <span style="font-size: 14px; font-weight: 700; color: #0f6cbd;">${this.escapeHtml(item.name)}</span>
                    <span class="office-license-badge" style="background: #e8f5e9; color: #1e7e34; border: 1px solid #c8e6c9; border-radius: 3px; font-size: 10px; font-weight: 700; padding: 2px 6px;">
                      ${this.escapeHtml(item.license)}
                    </span>
                  </div>

                  <div style="font-size: 11px; color: #64748b; margin-bottom: 8px;">
                    📁 <em>${this.escapeHtml(item.repoName)}</em> &bull; <span>${this.escapeHtml(item.category)}</span>
                  </div>

                  <div style="font-size: 12px; color: #242424; line-height: 1.4; margin-bottom: 10px;">
                    ${this.escapeHtml(item.description)}
                  </div>

                  <div style="display: flex; gap: 4px; flex-wrap: wrap; margin-bottom: 12px;">
                    ${item.tools.map(t => `<code style="font-size: 10px; background: #f3f4f6; color: #374151; padding: 1px 4px; border-radius: 2px;">${t}</code>`).join('')}
                  </div>
                </div>

                <div style="display: flex; justify-content: space-between; align-items: center; border-top: 1px solid #f3f4f6; padding-top: 8px;">
                  ${item.githubUrl ? `
                    <a href="${item.githubUrl}" target="_blank" rel="noopener noreferrer" style="font-size: 11px; color: #0f6cbd; text-decoration: none;">
                      ${t('node_skills.view_on_github', 'Ver no GitHub ↗')}
                    </a>
                  ` : '<span></span>'}
                  <button class="btn btn-secondary btn-sm btn-import-github-skill" data-skill-id="${item.id}" style="font-weight: 600;">
                    ${t('node_skills.btn_import_to_agent', '📥 Importar para este Agente')}
                  </button>
                </div>
              </div>
            `).join('')}
          </div>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    if (!this.activeNodeId) return '';
    const node = this.graph.getNode(this.activeNodeId);
    const skillsList = node?.agentSettings?.skills || [];
    const reposList = this.registry.getRepositories();
    const activeReposCount = reposList.filter(r => r.isEnabled).length;

    return `
      <div class="modal-footer" style="display: flex; justify-content: space-between; align-items: center; padding: 12px 20px; background: #faf9f8; border-top: 1px solid #edebe9;">
        <div style="font-size: 12px; color: #64748b;">
          ${t('node_skills.footer_total_skills', 'Total de Habilidades deste Agente: <strong>{{skillsCount}}</strong> | Repositórios Globais Ativos: <strong>{{reposCount}}</strong>', { skillsCount: skillsList.length, reposCount: activeReposCount })}
        </div>
        <button class="btn btn-secondary" id="btn-footer-close-skills" style="min-width: 90px;">
          ${t('node_skills.btn_close', 'Fechar')}
        </button>
      </div>
    `;
  }

  protected override bindEvents(): void {
    if (!this.activeNodeId) return;
    const node = this.graph.getNode(this.activeNodeId);
    if (!node) return;
    const currentSkill = this.getActiveSkill(node);

    // Fechar
    this.dialog.querySelector('#btn-footer-close-skills')?.addEventListener('click', () => this.hide());

    // Botão para abrir o modal global de repositórios
    const openGlobalReposHandler = () => {
      this.bus?.emit('modal:open-github-repos');
    };
    this.dialog.querySelector('#btn-open-global-repos-from-node')?.addEventListener('click', openGlobalReposHandler);
    this.dialog.querySelector('#btn-empty-open-global-repos')?.addEventListener('click', openGlobalReposHandler);

    // Troca de Abas Principais
    this.dialog.querySelectorAll('.office-modal-tab-btn').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const targetTab = (e.currentTarget as HTMLElement).getAttribute('data-tab') as any;
        if (targetTab && (targetTab === 'workspace' || targetTab === 'editor' || targetTab === 'github')) {
          this.activeTab = targetTab;
          this.render();
        }
      });
    });

    // Busca e Filtros da Aba de Habilidades Pré-Aprovadas do Workspace
    const wsSearchInput = this.dialog.querySelector('#input-search-workspace-skills') as HTMLInputElement;
    wsSearchInput?.addEventListener('input', () => {
      this.workspaceSearchQuery = wsSearchInput.value;
      this.render();
      const newWsSearch = this.dialog.querySelector('#input-search-workspace-skills') as HTMLInputElement;
      if (newWsSearch) {
        newWsSearch.focus();
        newWsSearch.setSelectionRange(newWsSearch.value.length, newWsSearch.value.length);
      }
    });

    const wsCategorySelect = this.dialog.querySelector('#select-workspace-category-filter') as HTMLSelectElement;
    wsCategorySelect?.addEventListener('change', () => {
      this.workspaceCategoryFilter = wsCategorySelect.value;
      this.render();
    });

    // Botões de Importar Habilidade Pré-Aprovada do Workspace para este Vértice
    this.dialog.querySelectorAll<HTMLButtonElement>('.btn-import-workspace-skill').forEach(btn => {
      btn.addEventListener('click', () => {
        const skillId = btn.getAttribute('data-skill-id');
        if (!skillId) return;

        const approvedItem = this.workspaceManager.getApprovedSkills().find(s => s.id === skillId);
        if (!approvedItem) return;

        const nodeSkill = this.workspaceManager.convertToNodeSkill(approvedItem);
        if (!node.agentSettings) node.agentSettings = { inheritSandbox: false, inheritModel: false, inheritCodeMode: false };
        if (!node.agentSettings.skills) node.agentSettings.skills = [];

        // Substitui ou adiciona
        const existingIdx = node.agentSettings.skills.findIndex(s => s.id === nodeSkill.id);
        if (existingIdx !== -1) {
          node.agentSettings.skills[existingIdx] = nodeSkill;
        } else {
          node.agentSettings.skills.push(nodeSkill);
        }
        node.agentSettings.activeSkillId = nodeSkill.id;

        if (node.agentSettings.model) {
          node.agentSettings.model.promptTemplate = nodeSkill.systemPrompt;
        }

        const verdict = this.workspaceManager.auditSkill(nodeSkill, true);
        this.bus?.emit('skill:import-requested', { skill: nodeSkill, nodeId: node.id, isCurated: true });

        this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
        this.notificationMessage = t('node_skills.msg_imported_ws', '🛡️ Habilidade "{{name}}" importada com sucesso e certificada pelo Workspace Auditor! (Risco: {{risk}}/100)', { name: nodeSkill.name, risk: verdict.riskScore });
        this.notificationType = 'success';
        this.activeTab = 'editor';
        this.render();
      });
    });

    // Sub-abas do Editor Markdown (Editar / Visualizar)
    this.dialog.querySelector('#btn-mode-md-edit')?.addEventListener('click', () => {
      this.markdownViewMode = 'edit';
      this.render();
    });

    this.dialog.querySelector('#btn-mode-md-preview')?.addEventListener('click', () => {
      const ta = this.dialog.querySelector('#skill-markdown-input') as HTMLTextAreaElement;
      if (ta) currentSkill.markdownContent = ta.value;
      this.markdownViewMode = 'preview';
      this.render();
    });

    // Troca de Habilidade Ativa
    const selSkill = this.dialog.querySelector('#select-active-skill') as HTMLSelectElement;
    selSkill?.addEventListener('change', () => {
      if (node.agentSettings) {
        node.agentSettings.activeSkillId = selSkill.value;
        this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
        this.render();
      }
    });

    // Criar Nova Habilidade em Branco
    this.dialog.querySelector('#btn-create-new-skill')?.addEventListener('click', () => {
      const newId = `skill-${Date.now()}`;
      const newSkill: INodeSkill = {
        id: newId,
        name: 'Nova Habilidade Especializada',
        description: 'Descrição das competências e artefatos gerados pelo agente',
        markdownContent: `# Nova Habilidade\n\nInstruções operacionais.\n\n## Diretrizes\n- Escreva com precisão e clareza.`,
        systemPrompt: 'Você é um agente com competência especializada.',
        tools: ['write', 'edit', 'read'],
        license: 'MIT',
        updatedAt: new Date().toISOString()
      };

      if (!node.agentSettings) node.agentSettings = { inheritSandbox: false, inheritModel: false, inheritCodeMode: false };
      if (!node.agentSettings.skills) node.agentSettings.skills = [];
      node.agentSettings.skills.push(newSkill);
      node.agentSettings.activeSkillId = newSkill.id;

      this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
      this.notificationMessage = t('node_skills.msg_created_success', 'Nova habilidade criada com sucesso.');
      this.notificationType = 'success';
      this.render();
    });

    // Salvar Alterações na Habilidade Ativa
    this.dialog.querySelector('#btn-save-skill-changes')?.addEventListener('click', () => {
      const nameInput = this.dialog.querySelector('#skill-name-input') as HTMLInputElement;
      const descInput = this.dialog.querySelector('#skill-desc-input') as HTMLInputElement;
      const licenseInput = this.dialog.querySelector('#skill-license-input') as HTMLInputElement;
      const promptInput = this.dialog.querySelector('#skill-prompt-input') as HTMLTextAreaElement;
      const mdInput = this.dialog.querySelector('#skill-markdown-input') as HTMLTextAreaElement;

      const checkedTools: string[] = [];
      this.dialog.querySelectorAll<HTMLInputElement>('.chk-skill-tool:checked').forEach(chk => {
        checkedTools.push(chk.value);
      });

      currentSkill.name = nameInput?.value.trim() || currentSkill.name;
      currentSkill.description = descInput?.value.trim() || currentSkill.description;
      currentSkill.license = licenseInput?.value.trim() || 'MIT';
      currentSkill.systemPrompt = promptInput?.value || '';
      if (mdInput) currentSkill.markdownContent = mdInput.value;
      currentSkill.tools = checkedTools;
      currentSkill.updatedAt = new Date().toISOString();

      const updatedSettings = {
        inheritSandbox: node.agentSettings?.inheritSandbox ?? false,
        inheritModel: node.agentSettings?.inheritModel ?? false,
        inheritCodeMode: node.agentSettings?.inheritCodeMode ?? false,
        ...node.agentSettings,
        skills: [...(node.agentSettings?.skills || [])]
      };

      const cmd = new UpdateNodeCommand(
        this.graph,
        node.id,
        { agentSettings: node.agentSettings },
        { agentSettings: updatedSettings },
        t('node_skills.cmd_update_skill', 'Atualizar habilidade {{name}}', { name: currentSkill.name })
      );
      this.graph.commandManager.execute(cmd);

      this.notificationMessage = t('node_skills.msg_saved_success', 'Habilidade "{{name}}" e SKILL.md salvos com sucesso!', { name: currentSkill.name });
      this.notificationType = 'success';
      this.render();
    });

    // Excluir Habilidade
    this.dialog.querySelector('#btn-delete-current-skill')?.addEventListener('click', () => {
      if (!node.agentSettings?.skills || node.agentSettings.skills.length <= 1) return;
      if (confirm(t('node_skills.confirm_delete', 'Deseja excluir a habilidade "{{name}}" deste agente?', { name: currentSkill.name }))) {
        node.agentSettings.skills = node.agentSettings.skills.filter(s => s.id !== currentSkill.id);
        node.agentSettings.activeSkillId = node.agentSettings.skills[0].id;
        this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
        this.notificationMessage = t('node_skills.msg_removed_success', 'Habilidade removida.');
        this.notificationType = 'info';
        this.render();
      }
    });

    // Busca e Filtros do GitHub
    const searchInput = this.dialog.querySelector('#input-search-github-skills') as HTMLInputElement;
    searchInput?.addEventListener('input', () => {
      this.searchQuery = searchInput.value;
      this.render();
      const newSearch = this.dialog.querySelector('#input-search-github-skills') as HTMLInputElement;
      if (newSearch) {
        newSearch.focus();
        newSearch.setSelectionRange(newSearch.value.length, newSearch.value.length);
      }
    });

    const categorySelect = this.dialog.querySelector('#select-category-filter') as HTMLSelectElement;
    categorySelect?.addEventListener('change', () => {
      this.selectedCategory = categorySelect.value;
      this.render();
    });

    // Botões de Importar Skill do GitHub para este Vértice
    this.dialog.querySelectorAll<HTMLButtonElement>('.btn-import-github-skill').forEach(btn => {
      btn.addEventListener('click', () => {
        const skillId = btn.getAttribute('data-skill-id');
        if (!skillId) return;

        const githubItem = this.registry.getDefaultSkills().find(s => s.id === skillId);
        if (!githubItem) return;

        const nodeSkill = this.registry.convertToNodeSkill(githubItem);
        if (!node.agentSettings) node.agentSettings = { inheritSandbox: false, inheritModel: false, inheritCodeMode: false };
        if (!node.agentSettings.skills) node.agentSettings.skills = [];

        // Substitui ou adiciona
        const existingIdx = node.agentSettings.skills.findIndex(s => s.id === nodeSkill.id);
        if (existingIdx !== -1) {
          node.agentSettings.skills[existingIdx] = nodeSkill;
        } else {
          node.agentSettings.skills.push(nodeSkill);
        }
        node.agentSettings.activeSkillId = nodeSkill.id;

        // Atualiza modelo com o prompt se o agente não tiver prompt customizado
        if (node.agentSettings.model) {
          node.agentSettings.model.promptTemplate = nodeSkill.systemPrompt;
        }

        this.graph.updateNode(node.id, { agentSettings: node.agentSettings });
        this.notificationMessage = t('node_skills.msg_imported_gh', 'Habilidade "{{name}}" importada com sucesso do repositório open source!', { name: nodeSkill.name });
        this.notificationType = 'success';
        this.activeTab = 'editor';
        this.render();
      });
    });
  }

  /**
   * Renderizador simples e seguro de Markdown para HTML, com formatação limpa e moderna
   * e estrito respeito à proibição de sombras/glows em texto.
   */
  private renderMarkdownToHtml(markdown: string): string {
    if (!markdown) return `<p style="color: #64748b;">${t('node_skills.empty_markdown', 'Nenhum conteúdo markdown disponível.')}</p>`;

    // Escapa HTML básico
    let html = markdown
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');

    // Títulos
    html = html.replace(/^### (.*$)/gim, '<h4 style="font-size: 13px; font-weight: 700; color: #1e293b; margin: 12px 0 6px 0;">$1</h4>');
    html = html.replace(/^## (.*$)/gim, '<h3 style="font-size: 14px; font-weight: 700; color: #0f6cbd; margin: 14px 0 6px 0;">$1</h3>');
    html = html.replace(/^# (.*$)/gim, '<h2 style="font-size: 16px; font-weight: 700; color: #0f172a; margin: 0 0 10px 0; border-bottom: 1px solid #e2e8f0; padding-bottom: 6px;">$1</h2>');

    // Negrito e Itálico
    html = html.replace(/\*\*(.*?)\*\*/gim, '<strong style="font-weight: 700; color: #0f172a;">$1</strong>');
    html = html.replace(/\*(.*?)\*/gim, '<em>$1</em>');

    // Blocos de código delimitados
    html = html.replace(/```([a-zA-Z0-9_]*)\n([\s\S]*?)```/gim, (_match, _lang, code) => {
      return `<pre style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 4px; padding: 10px 12px; font-family: Consolas, monospace; font-size: 11px; overflow-x: auto; margin: 10px 0; color: #1e293b;"><code>${code}</code></pre>`;
    });

    // Código inline
    html = html.replace(/`([^`]+)`/gim, '<code style="background: #f1f5f9; color: #0f172a; padding: 2px 5px; border-radius: 3px; font-family: Consolas, monospace; font-size: 11px;">$1</code>');

    // Listas
    html = html.replace(/^\s*-\s+(.*$)/gim, '<li style="margin-left: 20px; font-size: 12px; color: #334155; line-height: 1.5;">$1</li>');
    html = html.replace(/^\s*([0-9]+)\.\s+(.*$)/gim, '<li style="margin-left: 20px; font-size: 12px; color: #334155; line-height: 1.5;">$2</li>');

    // Parágrafos
    html = html.replace(/\n\n/gim, '</p><p style="margin: 8px 0; font-size: 12px; line-height: 1.5; color: #334155;">');

    return `<div style="font-family: 'Segoe UI', Inter, sans-serif;">${html}</div>`;
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
