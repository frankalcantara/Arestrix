import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings, INodeMemoryConfig } from '../../core/types.ts';
import { PiProjectImporter, IPiProjectScanResult } from '../../core/pi/PiProjectImporter.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import type { IImportAIValidationResult } from '../../core/ai/DefaultIntelligenceService.ts';
import { EnvGuidanceModal } from './EnvGuidanceModal.ts';
import { EnvironmentManager } from '../../core/env/EnvironmentManager.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

declare const process: any;

/**
 * Arestrix Studio - Diálogo de Importação de Projetos Pi
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportPiModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private projectImporter: PiProjectImporter;
  private projectManager: ProjectManager;
  private envManager: EnvironmentManager;
  private windowController?: IModalWindowController;

  private activeTab: 'project' | 'chat' = 'project';
  private currentScanResult: IPiProjectScanResult | null = null;
  private aiValidationResult: IImportAIValidationResult | null = null;
  private isValidatingAI: boolean = false;
  private isScanning: boolean = false;
  private selectedDirPath: string = 'C:/Projetos';
  private shouldValidateWithAI: boolean = true;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;
    this.projectImporter = PiProjectImporter.getInstance();
    this.projectManager = ProjectManager.getInstance(globalEventBus);
    this.envManager = EnvironmentManager.getInstance();

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-import-pi';
      this.modal.style.display = 'none';
      this.modal.addEventListener('click', (e: MouseEvent) => {
        if (e.target === this.modal) {
          this.hide();
        }
      });
      document.body.appendChild(this.modal);
      this.render();
    }
  }

  public async show(): Promise<void> {
    if (!this.modal) return;
    this.modal.style.display = 'flex';
    this.windowController?.bringToFront();

    if (!this.selectedDirPath) {
      this.selectedDirPath = 'C:/Projetos';
    }

    this.render();

    if (!this.currentScanResult) {
      await this.rescanPath(this.selectedDirPath);
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  /**
   * Varre um diretório arbitrário e carrega suas informações
   */
  public async rescanPath(pathVal: string): Promise<void> {
    if (!pathVal) return;
    this.selectedDirPath = pathVal.replace(/\\/g, '/');
    this.isScanning = true;
    this.render();

    try {
      this.currentScanResult = await this.projectImporter.scanProject(this.selectedDirPath);
      this.aiValidationResult = null;

      if (this.shouldValidateWithAI && this.currentScanResult?.isValid) {
        const hasKey = await this.envManager.hasEnvVar('DEEPSEEK_API_KEY');
        if (hasKey) {
          this.aiValidationResult = await this.projectImporter.validateWithDefaultIntelligence(this.currentScanResult);
        }
      }
    } catch (err) {
      console.warn('[ImportPiModal] Falha ao escanear diretório:', err);
    } finally {
      this.isScanning = false;
      this.render();
    }
  }

  /**
   * Abre o seletor nativo do Windows (IFileOpenDialog no C++) com foco em primeiro plano
   */
  private async browseFolderNative(): Promise<void> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const picked = await (window as any).webui.call('pick_folder');
        if (picked && typeof picked === 'string' && picked.trim().length > 0) {
          const clean = picked.trim().replace(/\\/g, '/');
          this.selectedDirPath = clean;
          await this.rescanPath(clean);
          return;
        }
      }
    } catch (err) {
      console.warn('[ImportPiModal] Falha ao abrir seletor nativo C++:', err);
    }

    // Fallback: Dispara seletor HTML5 de diretório se estiver no navegador
    const pickerInput = this.modal?.querySelector('#pi-folder-picker') as HTMLInputElement;
    if (pickerInput) {
      pickerInput.click();
    }
  }

  private render(): void {
    if (!this.modal) return;

    try {
      const titleBarHtml = ModalWindowManager.createTitleBarHTML({
        title: t('import_modals.pi_title'),
        subtitle: t('import_modals.pi_subtitle'),
        icon: '🥧',
        closeBtnId: 'btn-close-pi-modal'
      });

      this.modal.innerHTML = `
        <div class="modal-window" style="max-width: 760px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
          ${titleBarHtml}

          <!-- Abas Estilo Office Pivot -->
          <div class="modal-tabs">
            <button class="tab-btn ${this.activeTab === 'project' ? 'active' : ''}" id="pi-tab-project">
              ${t('import_modals.tab_full_project')}
            </button>
            <button class="tab-btn ${this.activeTab === 'chat' ? 'active' : ''}" id="pi-tab-chat">
              ${t('import_modals.tab_pi_chat')}
            </button>
          </div>

          <!-- Corpo do Diálogo -->
          <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 14px;">
            ${this.activeTab === 'project' ? this.renderProjectTab() : this.renderChatTab()}
          </div>

          <!-- Rodapé Estilo Office -->
          <div class="modal-footer office-modal-footer">
            <span class="footer-info" id="pi-status-msg">
              ${this.isScanning ? t('import_modals.status_analyzing') : (this.activeTab === 'project' 
                ? (this.currentScanResult?.isValid ? t('import_modals.status_ready_project') : t('import_modals.status_select_folder')) 
                : t('import_modals.status_ready_chat'))}
            </span>
            <div class="footer-actions">
              <button id="btn-cancel-pi" class="tool-btn btn-secondary btn-secondary-office">${t('import_modals.btn_cancel')}</button>
              ${this.activeTab === 'project' ? `
                <button id="btn-import-project-now" class="tool-btn btn-action btn-primary-office" ${(!this.currentScanResult || !this.currentScanResult.isValid || this.isScanning) ? 'disabled' : ''}>
                  ${t('import_modals.btn_import')}
                </button>
              ` : `
                <button id="btn-inject-pi-agent" class="tool-btn btn-action btn-primary-office">
                  ${t('import_modals.btn_import')}
                </button>
              `}
            </div>
          </div>
        </div>
      `;

      const dialog = this.modal.querySelector?.('.modal-window') as HTMLElement;
      if (dialog) {
        this.windowController = ModalWindowManager.attach({
          dialog,
          minWidth: 580,
          minHeight: 440,
          onClose: () => this.hide()
        });
      }

      this.bindEvents();
    } catch (err) {
      console.error('[ImportPiModal] Erro ao renderizar modal:', err);
    }
  }

  private renderProjectTab(): string {
    const scan = this.currentScanResult;
    const currentPath = this.selectedDirPath || scan?.rootPath || 'C:/Projetos';
    const ai = this.aiValidationResult;

    const skillsCount = scan?.skills?.length || 0;
    const mcpCount = (scan?.mcpTools?.length || 0) + (scan?.defaultTools?.length || 0);
    const stagesCount = scan?.stages?.length || 0;
    const workItemsCount = scan?.workItems?.length || scan?.articles?.length || 0;
    const defaultProvider = (scan?.defaultProvider || 'deepseek').toUpperCase();
    const defaultModel = scan?.defaultModel || 'deepseek-v4-pro';
    const stagesList = scan?.stages || [];

    return `
      <!-- 1. Localização do Projeto -->
      <div class="office-group" style="margin-bottom: 0;">
        <label class="office-label" for="pi-dir-input">${t('import_modals.label_project_folder')}</label>
        <div style="display: flex; gap: 8px; align-items: center;">
          <input type="text" id="pi-dir-input" class="office-input" value="${this.escapeHtml(currentPath)}" placeholder="C:/Caminho/Do/Projeto" style="flex: 1;">
          <button id="btn-browse-folder" class="tool-btn btn-secondary" style="white-space: nowrap; font-size: 12px; height: 32px; padding: 4px 14px;" title="${t('import_modals.btn_browse_tip')}">
            ${t('import_modals.btn_browse')}
          </button>
          <input type="file" id="pi-folder-picker" webkitdirectory directory style="display: none;">
        </div>
      </div>

      <!-- 2. Resumo da Estrutura Detectada -->
      ${scan && scan.isValid ? `
        <div class="office-group" style="margin-bottom: 0;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; border-bottom: 1px solid #edebe9; padding-bottom: 6px;">
            <strong style="font-size: 13px; color: #242424; font-weight: 600;">
              ${this.escapeHtml(scan.projectName || 'Projeto Pi')}
            </strong>
            <span style="font-size: 11px; color: #616161; font-weight: 600;">
              ${defaultProvider} (${defaultModel})
            </span>
          </div>

          <!-- Tabela de Propriedades do Projeto -->
          <table style="width: 100%; border-collapse: collapse; font-size: 12.5px; color: #242424;">
            <tbody>
              <tr style="border-bottom: 1px solid #f3f2f1;">
                <td style="padding: 6px 4px; color: #616161; width: 180px; font-weight: 600;">${t('import_modals.row_skills_agents')}</td>
                <td style="padding: 6px 4px;">${t('import_modals.skills_count_desc', { count: skillsCount })}</td>
              </tr>
              <tr style="border-bottom: 1px solid #f3f2f1;">
                <td style="padding: 6px 4px; color: #616161; font-weight: 600;">${t('import_modals.row_tools_mcp')}</td>
                <td style="padding: 6px 4px;">${t('import_modals.tools_count_desc', { count: mcpCount })}</td>
              </tr>
              <tr style="border-bottom: 1px solid #f3f2f1;">
                <td style="padding: 6px 4px; color: #616161; font-weight: 600;">${t('import_modals.row_pipeline_stages')}</td>
                <td style="padding: 6px 4px;">
                  <span style="font-weight: 600;">${t('import_modals.stages_count_desc', { count: stagesCount })}</span>
                  ${stagesList.length > 0 ? `
                    <div style="margin-top: 4px; display: flex; flex-wrap: wrap; gap: 4px;">
                      ${stagesList.map(st => `
                        <span style="background: #ffffff; border: 1px solid #d1d1d1; padding: 1px 6px; border-radius: 2px; font-size: 11px; color: #323130;">
                          ${this.escapeHtml(st.name)}
                        </span>
                      `).join('')}
                    </div>
                  ` : ''}
                </td>
              </tr>
              <tr>
                <td style="padding: 6px 4px; color: #616161; font-weight: 600;">${t('import_modals.row_work_items')}</td>
                <td style="padding: 6px 4px;">${t('import_modals.items_count_desc', { count: workItemsCount })}</td>
              </tr>
            </tbody>
          </table>
        </div>

        <!-- 3. Validação e Auditoria com o Modelo Padrão -->
        <div class="office-group" style="margin-bottom: 0;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12.5px; font-weight: 600; color: #242424; cursor: pointer;">
              <input type="checkbox" id="chk-validate-ai" ${this.shouldValidateWithAI ? 'checked' : ''} style="cursor: pointer;">
              ${t('import_modals.chk_validate_ai')}
            </label>
            ${!ai && !this.isValidatingAI ? `
              <button id="btn-run-ai-check" class="tool-btn btn-secondary" style="font-size: 11px; height: 26px; padding: 2px 10px;">
                ${t('import_modals.btn_validate_now')}
              </button>
            ` : ''}
          </div>

          ${this.isValidatingAI ? `
            <div style="font-size: 12px; color: #616161; padding: 6px 0;">
              ${t('import_modals.ai_consulting')}
            </div>
          ` : ai ? `
            <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px 10px; margin-top: 6px; font-size: 12px;">
              <div style="display: flex; justify-content: space-between; margin-bottom: 4px;">
                <strong style="color: #0f6cbd;">${t('import_modals.ai_confidence', { score: ai.confidenceScore })}</strong>
                ${ai.missingEnvKey ? `
                  <a href="#" id="link-env-guidance" style="color: #0f6cbd; text-decoration: underline; font-size: 11px;">
                    ${t('import_modals.ai_configure_key', { key: ai.missingEnvKey })}
                  </a>
                ` : `
                  <span style="color: #107c41; font-weight: 600; font-size: 11px;">${t('import_modals.ai_validated_success')}</span>
                `}
              </div>
              <p style="margin: 0; color: #323130; line-height: 1.4;">
                ${this.escapeHtml(ai.summary || t('import_modals.ai_default_summary'))}
              </p>
            </div>
          ` : `
            <div style="font-size: 11.5px; color: #616161;">
              ${t('import_modals.ai_explanation')}
            </div>
          `}
        </div>
      ` : `
        <div style="padding: 32px 16px; text-align: center; color: #616161; background: #faf9f8; border: 1px solid #edebe9; border-radius: 4px;">
          ${this.isScanning ? t('import_modals.scanning_folder') : t('import_modals.empty_folder_prompt')}
        </div>
      `}
    `;
  }

  private renderChatTab(): string {
    return `
      <div class="office-group" style="margin-bottom: 0;">
        <label class="office-label" for="pi-agent-name">${t('import_modals.label_agent_name')}</label>
        <input type="text" id="pi-agent-name" class="office-input" value="Agente_Pi_Mentor" style="margin-bottom: 12px;">

        <label class="office-label" for="pi-agent-style">${t('import_modals.label_cognitive_style')}</label>
        <select id="pi-agent-style" class="office-select" style="margin-bottom: 12px;">
          <option value="empathetic_dialogue">${t('import_modals.style_empathetic')}</option>
          <option value="rigorous_analytical">${t('import_modals.style_rigorous')}</option>
          <option value="creative_synthesizer">${t('import_modals.style_creative')}</option>
        </select>

        <label class="office-label" for="pi-conversation-input">${t('import_modals.label_conversation_history')}</label>
        <textarea id="pi-conversation-input" class="office-textarea" rows="6" placeholder="${t('import_modals.placeholder_conversation')}"></textarea>
      </div>

      <div class="office-group" style="margin-bottom: 0;">
        <label class="office-label">${t('import_modals.label_persistence_options')}</label>
        <div style="display: flex; gap: 16px; margin-top: 6px;">
          <label style="display: flex; align-items: center; gap: 6px; font-size: 12.5px; color: #323130; cursor: pointer;">
            <input type="checkbox" id="pi-memory-enable" checked style="cursor: pointer;">
            ${t('import_modals.chk_heterogeneous_memory')}
          </label>
        </div>
      </div>
    `;
  }

  private bindEvents(): void {
    if (!this.modal) return;

    this.modal.querySelector('#btn-close-pi-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-pi')?.addEventListener('click', () => this.hide());

    // Abas
    this.modal.querySelector('#pi-tab-project')?.addEventListener('click', (e) => {
      e.preventDefault();
      const input = this.modal?.querySelector('#pi-dir-input') as HTMLInputElement;
      if (input && input.value) {
        this.selectedDirPath = input.value.trim();
      }
      this.activeTab = 'project';
      this.render();
    });

    this.modal.querySelector('#pi-tab-chat')?.addEventListener('click', (e) => {
      e.preventDefault();
      const input = this.modal?.querySelector('#pi-dir-input') as HTMLInputElement;
      if (input && input.value) {
        this.selectedDirPath = input.value.trim();
      }
      this.activeTab = 'chat';
      this.render();
    });

    // Procurar pasta
    this.modal.querySelector('#btn-browse-folder')?.addEventListener('click', async (e) => {
      e.preventDefault();
      await this.browseFolderNative();
    });

    // Input com Enter ou Blur
    const dirInput = this.modal.querySelector('#pi-dir-input') as HTMLInputElement;
    if (dirInput) {
      dirInput.addEventListener('keydown', async (e: KeyboardEvent) => {
        if (e.key === 'Enter') {
          e.preventDefault();
          if (dirInput.value.trim()) {
            await this.rescanPath(dirInput.value.trim());
          }
        }
      });
      dirInput.addEventListener('change', async () => {
        if (dirInput.value.trim() && dirInput.value.trim() !== this.selectedDirPath) {
          await this.rescanPath(dirInput.value.trim());
        }
      });
    }

    // Input fallback HTML5
    this.modal.querySelector('#pi-folder-picker')?.addEventListener('change', async (e) => {
      const input = e.target as HTMLInputElement;
      if (input.files && input.files.length > 0) {
        const rel = input.files[0].webkitRelativePath;
        const topFolder = rel ? rel.split('/')[0] : '';
        if (topFolder) {
          const current = this.selectedDirPath || 'C:/Projetos';
          const newPath = `${current.replace(/[\/\\][^\/\\]*$/, '')}/${topFolder}`;
          await this.rescanPath(newPath);
        }
      }
    });

    // Checkbox de validação IA
    const chkAi = this.modal.querySelector('#chk-validate-ai') as HTMLInputElement;
    if (chkAi) {
      chkAi.addEventListener('change', () => {
        this.shouldValidateWithAI = chkAi.checked;
      });
    }

    // Botão validar agora
    this.modal.querySelector('#btn-run-ai-check')?.addEventListener('click', async (e) => {
      e.preventDefault();
      await this.executeAIValidation();
    });

    // Link configurar chave de ambiente
    this.modal.querySelector('#link-env-guidance')?.addEventListener('click', (e) => {
      e.preventDefault();
      EnvGuidanceModal.getInstance().show('DEEPSEEK_API_KEY', async () => {
        if (this.currentScanResult) {
          await this.executeAIValidation();
        }
      });
    });

    // Importar projeto
    this.modal.querySelector('#btn-import-project-now')?.addEventListener('click', async (e) => {
      e.preventDefault();
      await this.executeProjectImport();
    });

    // Injetar agente de chat
    this.modal.querySelector('#btn-inject-pi-agent')?.addEventListener('click', (e) => {
      e.preventDefault();
      this.injectPiAgent();
    });
  }

  private async executeAIValidation(): Promise<void> {
    if (!this.currentScanResult) return;

    const hasKey = await this.envManager.hasEnvVar('DEEPSEEK_API_KEY');
    if (!hasKey) {
      EnvGuidanceModal.getInstance().show('DEEPSEEK_API_KEY', async () => {
        await this.executeAIValidation();
      });
      return;
    }

    this.isValidatingAI = true;
    this.render();

    try {
      this.aiValidationResult = await this.projectImporter.validateWithDefaultIntelligence(this.currentScanResult);
    } catch (err) {
      console.error('[ImportPiModal] Erro ao validar com inteligência padrão:', err);
    } finally {
      this.isValidatingAI = false;
      this.render();
    }
  }

  public async executeProjectImport(dirPath?: string): Promise<boolean> {
    const pathVal = dirPath || (this.modal?.querySelector('#pi-dir-input') as HTMLInputElement)?.value.trim() || this.selectedDirPath || 'C:/Projetos';
    const scan = this.currentScanResult || await this.projectImporter.scanProject(pathVal);

    const ok = await this.projectImporter.importProject(
      scan, 
      this.graph, 
      this.projectManager, 
      undefined, 
      this.aiValidationResult || undefined
    );

    if (ok) {
      if (this.viewport) {
        setTimeout(() => this.viewport?.zoomToFit(), 150);
      }
      this.hide();
      return true;
    }
    return false;
  }

  public injectPiAgent(options?: { name?: string; style?: string; conversation?: string }): void {
    const nameInput = this.modal?.querySelector('#pi-agent-name') as HTMLInputElement;
    const styleSelect = this.modal?.querySelector('#pi-agent-style') as HTMLSelectElement;
    const convInput = this.modal?.querySelector('#pi-conversation-input') as HTMLTextAreaElement;
    const memEnable = this.modal?.querySelector('#pi-memory-enable') as HTMLInputElement;

    const agentName = options?.name || nameInput?.value?.trim() || 'Agente_Pi_Mentor';
    const style = options?.style || styleSelect?.value || 'empathetic_dialogue';
    const conversation = options?.conversation || convInput?.value?.trim() || '';

    const systemPrompt = [
      `# AGENTE PI - ${agentName}`,
      `Estilo Cognitivo: ${style}`,
      `Diretrizes: Raciocínio lógico e modular.`,
      conversation ? `\n--- CONTEXTO ORIGINAL ---\n${conversation}` : ''
    ].join('\n');

    const nodeId = `pi_agent_${Date.now()}`;
    const center = this.viewport ? this.viewport.getCameraCenter() : { x: 0, y: 0 };
    const x = Math.round(center.x / 10) * 10;
    const y = Math.round(center.y / 10) * 10;

    const node = this.graph.buildNode(nodeId, agentName, 'agent', x, y);

    const memoryEnabled = memEnable ? memEnable.checked : true;
    const memoryTheme = 'strategy';
    const memoryConfig: INodeMemoryConfig = {
      enabled: memoryEnabled,
      persistent: false,
      mode: memoryEnabled ? 'learn' : 'off',
      theme: memoryTheme,
      readThemes: [],
      taskType: '',
      readBlackboard: false,
      writeBlackboard: false,
      semanticLessons: memoryEnabled,
      recordLessons: memoryEnabled,
      domains: [memoryTheme]
    };

    const agentSettings: INodeAgentSettings = {
      inheritSandbox: false,
      sandbox: {
        isolationLevel: 'container',
        network: 'egress_llm_only',
        maxMemoryMB: 1024,
        timeoutSeconds: 300,
        readOnlyMounts: ['/workspace/context'],
        readWriteMounts: ['/workspace/output']
      },
      inheritModel: false,
      model: {
        provider: 'local',
        model: 'pi-3-empathic',
        temperature: 0.7,
        promptTemplate: systemPrompt
      },
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read', 'write'],
        customScripts: []
      },
      inheritMemory: false,
      memory: memoryConfig
    };

    node.agentSettings = agentSettings;

    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    if (this.viewport) {
      this.viewport.selectNode(nodeId);
    }

    this.hide();
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
