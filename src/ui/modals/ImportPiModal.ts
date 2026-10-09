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

export class ImportPiModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private projectImporter: PiProjectImporter;
  private projectManager: ProjectManager;
  private envManager: EnvironmentManager;

  private activeTab: 'project' | 'chat' = 'project';
  private currentScanResult: IPiProjectScanResult | null = null;
  private aiValidationResult: IImportAIValidationResult | null = null;
  private isValidatingAI: boolean = false;

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
    this.render();

    if (!this.currentScanResult) {
      try {
        await this.initKnownProject();
      } catch (err) {
        console.warn('[ImportPiModal] Falha ao escanear projeto Pi:', err);
      }
      this.render();
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private async initKnownProject(): Promise<void> {
    try {
      const known = await this.projectImporter.getKnownProjects();
      const defaultPath = known.length > 0 ? known[0] : 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
      this.currentScanResult = await this.projectImporter.scanProject(defaultPath);
    } catch (e) {
      console.warn('[ImportPiModal] initKnownProject erro:', e);
    }
  }

  private render(): void {
    if (!this.modal) return;

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 880px; width: 94%; max-height: 90vh; display: flex; flex-direction: column; background: #ffffff; border-radius: 8px; border: 1px solid #cbd5e1; box-shadow: 0 16px 40px rgba(0,0,0,0.18);">
        <!-- Header Estilo Office -->
        <div class="modal-header" style="border-bottom: 2px solid #0d9488; padding: 14px 18px; display: flex; justify-content: space-between; align-items: center; background: #f8fafc;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #0d9488; color: white; border-radius: 6px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              π
            </div>
            <div>
              <h3 style="margin: 0; font-size: 16px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação Genérica de Projetos Pi (Pi.ai / Pi CLI)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Varredura profunda de Agentes, Skills, MCP e Orquestração com Garantia por IA
              </p>
            </div>
          </div>
          <button id="btn-close-pi-modal" class="modal-close-btn" style="background: transparent; border: none; font-size: 20px; color: #64748b; cursor: pointer;">&times;</button>
        </div>

        <!-- Abas do Modal -->
        <div style="display: flex; gap: 4px; padding: 8px 18px 0 18px; background: #f8fafc; border-bottom: 1px solid #e2e8f0;">
          <button class="tool-btn ${this.activeTab === 'project' ? 'btn-action' : 'btn-secondary'}" id="pi-tab-project" style="font-weight: 700; font-size: 12px; border-radius: 6px 6px 0 0; ${this.activeTab === 'project' ? 'background: #0d9488; color: white;' : ''}">
            📁 Projeto Completo (Qualquer Diretório Local)
          </button>
          <button class="tool-btn ${this.activeTab === 'chat' ? 'btn-action' : 'btn-secondary'}" id="pi-tab-chat" style="font-weight: 700; font-size: 12px; border-radius: 6px 6px 0 0; ${this.activeTab === 'chat' ? 'background: #0d9488; color: white;' : ''}">
            💬 Diálogo Individual do Pi
          </button>
        </div>

        <!-- Conteúdo do Modal -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px; display: flex; flex-direction: column; gap: 16px;">
          ${this.activeTab === 'project' ? this.renderProjectTab() : this.renderChatTab()}
        </div>

        <!-- Footer -->
        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding: 12px 18px; display: flex; justify-content: space-between; align-items: center; background: #f8fafc;">
          <span style="font-size: 12px; color: #64748b;" id="pi-status-msg">
            ${this.activeTab === 'project' 
              ? (this.aiValidationResult ? '✅ Estrutura validada pela IA e pronta para sintetização' : 'Pronto para analisar e sintetizar') 
              : 'Pronto para sintetizar agente a partir do diálogo'}
          </span>
          <div style="display: flex; gap: 10px;">
            <button id="btn-cancel-pi" class="tool-btn btn-secondary">Cancelar</button>
            ${this.activeTab === 'project' ? `
              <button id="btn-import-project-now" class="tool-btn btn-action" style="background: #0d9488; color: white; font-weight: 800; padding: 8px 22px;">
                🚀 Sintetizar no Grafo do Arestrix
              </button>
            ` : `
              <button id="btn-inject-pi-agent" class="tool-btn btn-action" style="background: #0d9488; color: white; font-weight: 700; padding: 8px 18px;">
                🚀 Injetar Agente no Grafo
              </button>
            `}
          </div>
        </div>
      </div>
    `;

    this.bindEvents();
  }

  private renderProjectTab(): string {
    const scan = this.currentScanResult;
    const defaultPath = scan?.rootPath || 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
    const ai = this.aiValidationResult;

    return `
      <!-- 1. Seleção do Diretório do Projeto -->
      <div>
        <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
          Diretório Raiz de Qualquer Projeto Pi:
        </label>
        <div style="display: flex; gap: 8px;">
          <input type="text" id="pi-dir-input" value="${this.escapeHtml(defaultPath)}" placeholder="C:/Caminho/Para/Seu/Projeto/Pi" style="flex: 1; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600;">
          <button id="btn-rescan-pi" class="tool-btn btn-secondary" style="font-weight: 700; font-size: 12px; white-space: nowrap;">
            🔍 1. Analisar Recursos Estáticos
          </button>
        </div>
      </div>

      <!-- 2. Resumo dos Recursos Estáticos Detectados -->
      ${scan ? `
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; border-bottom: 1px solid #f1f5f9; padding-bottom: 8px;">
            <div style="display: flex; align-items: center; gap: 8px;">
              <span style="font-size: 18px;">📦</span>
              <strong style="font-size: 14px; color: #0f172a;">${this.escapeHtml(scan.projectName)}</strong>
            </div>
            <span style="background: #f0fdfa; color: #0d9488; border: 1px solid #99f6e4; font-size: 11px; font-weight: 800; padding: 2px 10px; border-radius: 4px;">
              ${scan.defaultProvider.toUpperCase()} / ${scan.defaultModel}
            </span>
          </div>

          <!-- Grid de Métricas de Recursos -->
          <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; font-size: 12px; margin-bottom: 14px;">
            <div style="background: #f8fafc; padding: 10px; border-radius: 6px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 10.5px; font-weight: 700;">AGENTES / SKILLS</span>
              <strong style="font-size: 15px; color: #0f172a;">${scan.skills.length}</strong>
              <small style="display: block; color: #64748b; margin-top: 2px;">descobertas em .agents/</small>
            </div>

            <div style="background: #f8fafc; padding: 10px; border-radius: 6px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 10.5px; font-weight: 700;">FERRAMENTAS / MCP</span>
              <strong style="font-size: 15px; color: #0f172a;">${scan.mcpTools.length + scan.defaultTools.length}</strong>
              <small style="display: block; color: #64748b; margin-top: 2px;">scripts e mcp servers</small>
            </div>

            <div style="background: #f8fafc; padding: 10px; border-radius: 6px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 10.5px; font-weight: 700;">ETAPAS DO PIPELINE</span>
              <strong style="font-size: 15px; color: #0f172a;">${scan.stages.length}</strong>
              <small style="display: block; color: #64748b; margin-top: 2px;">${scan.orchestrationSourceFile || 'deduzidas'}</small>
            </div>

            <div style="background: #f8fafc; padding: 10px; border-radius: 6px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 10.5px; font-weight: 700;">ITENS DE LOTE</span>
              <strong style="font-size: 15px; color: #0f172a;">${scan.workItems?.length || 0}</strong>
              <small style="display: block; color: #16a34a; font-weight: 700; margin-top: 2px;">itens de trabalho</small>
            </div>
          </div>

          <!-- Etapas Reais Descobertas Dinamicamente -->
          <div>
            <span style="display: block; font-size: 11px; font-weight: 800; color: #475569; text-transform: uppercase; margin-bottom: 6px;">
              Fluxo Sequencial Descoberto no Projeto:
            </span>
            <div style="display: flex; flex-wrap: wrap; gap: 6px; align-items: center; font-size: 11.5px;">
              ${scan.stages.map((st, i) => `
                <span style="background: ${st.isDeterministic ? '#f1f5f9' : '#eff6ff'}; color: ${st.isDeterministic ? '#475569' : '#1d4ed8'}; border: 1px solid ${st.isDeterministic ? '#cbd5e1' : '#bfdbfe'}; padding: 3px 8px; border-radius: 4px; font-weight: 700;">
                  ${this.escapeHtml(st.name)}
                </span>
                ${i < scan.stages.length - 1 ? '<span style="color: #94a3b8; font-weight: 800;">➔</span>' : ''}
              `).join('')}
            </div>
          </div>
        </div>

        <!-- 3. Validação e Garantia com o Modelo Padrão do Arestrix (IA) -->
        <div style="background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 8px; padding: 14px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
            <div style="display: flex; align-items: center; gap: 8px;">
              <span style="font-size: 18px;">🧠</span>
              <strong style="font-size: 13.5px; color: #166534;">
                2. Garantia de Importação pelo Modelo Padrão (IA Central do Arestrix)
              </strong>
            </div>
            <div style="display: flex; gap: 8px;">
              <button id="btn-env-guidance" class="tool-btn btn-secondary" style="font-size: 11px; font-weight: 700; padding: 4px 10px; border-color: #cbd5e1;">
                🔑 Gerenciar DEEPSEEK_API_KEY
              </button>
              <button id="btn-validate-ai" class="tool-btn btn-action" style="background: #16a34a; color: white; font-size: 12px; font-weight: 800; padding: 5px 14px;" ${this.isValidatingAI ? 'disabled' : ''}>
                ${this.isValidatingAI ? '⏳ Analisando...' : '🔍 Validar com Modelo Padrão'}
              </button>
            </div>
          </div>

          <p style="margin: 0 0 10px 0; font-size: 12px; color: #15803d; line-height: 1.45;">
            O modelo padrão analisa os scripts, prompts e dependências para auditar a orquestração e garantir que nenhum recurso, ferramenta MCP ou regra de validação foi omitida.
          </p>

          ${ai ? `
            <div style="background: #ffffff; border: 1px solid #86efac; border-radius: 6px; padding: 12px; font-size: 12px;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                <span style="background: #dcfce7; color: #15803d; font-weight: 800; padding: 2px 8px; border-radius: 4px; font-size: 11px;">
                  Índice de Confiabilidade: ${ai.confidenceScore}%
                </span>
                ${ai.missingEnvKey ? `
                  <span style="background: #fef3c7; color: #92400e; font-weight: 700; padding: 2px 8px; border-radius: 4px; font-size: 11px;">
                    ⚠️ Validação Estática (Chave ${ai.missingEnvKey} ausente no ambiente)
                  </span>
                ` : `
                  <span style="background: #e0f2fe; color: #0284c7; font-weight: 700; padding: 2px 8px; border-radius: 4px; font-size: 11px;">
                    ✅ Validação Semântica por IA Concluída
                  </span>
                `}
              </div>
              <p style="margin: 0 0 8px 0; color: #1f2937; line-height: 1.4;">
                <strong>Diagnóstico da IA:</strong> ${this.escapeHtml(ai.summary)}
              </p>
              ${ai.auditFindings && ai.auditFindings.length > 0 ? `
                <div style="border-top: 1px solid #f3f4f6; padding-top: 6px; margin-top: 6px;">
                  <strong style="color: #4b5563; font-size: 11px;">Apontamentos de Integridade:</strong>
                  <ul style="margin: 4px 0 0 0; padding-left: 16px; color: #4b5563; font-size: 11.5px; line-height: 1.4;">
                    ${ai.auditFindings.map(f => `<li>${this.escapeHtml(f)}</li>`).join('')}
                  </ul>
                </div>
              ` : ''}
            </div>
          ` : `
            <div style="background: rgba(255,255,255,0.7); border: 1px dashed #86efac; border-radius: 6px; padding: 10px; font-size: 11.5px; color: #166534; text-align: center;">
              Clique em "Validar com Modelo Padrão" para submeter a estrutura à IA central antes da sintetização.
            </div>
          `}
        </div>
      ` : `
        <div style="padding: 24px; background: #f8fafc; border-radius: 8px; text-align: center; color: #64748b; border: 1px dashed #cbd5e1;">
          Informe o caminho da pasta e clique em "1. Analisar Recursos Estáticos" para iniciar.
        </div>
      `}
    `;
  }

  private renderChatTab(): string {
    return `
      <div style="background: #f0fdfa; border: 1px solid #99f6e4; border-radius: 8px; padding: 12px;">
        <span style="font-size: 12.5px; font-weight: 700; color: #115e59;">Diálogo e Síntese de Agente Individual</span>
        <p style="margin: 2px 0 0 0; font-size: 12px; color: #134e4a;">
          Cole um diálogo ou prompt do Pi para instanciar um Agente no canvas do Arestrix.
        </p>
      </div>

      <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
        <div>
          <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
            Nome do Agente:
          </label>
          <input type="text" id="pi-agent-name" value="Agente_Pi_Mentor" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600;">
        </div>
        <div>
          <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
            Estilo Cognitivo:
          </label>
          <select id="pi-agent-style" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600;">
            <option value="empathetic_dialogue">Empático / Investigativo</option>
            <option value="rigorous_math">Raciocínio Matemático Estrito</option>
            <option value="system_architect">Arquiteto de Sistemas Concorrentes</option>
          </select>
        </div>
      </div>

      <div>
        <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
          Transcrição da Conversa / Instruções:
        </label>
        <textarea id="pi-conversation-input" rows="6" placeholder="Cole o diálogo do Pi aqui..." style="width: 100%; padding: 10px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-family: monospace; font-size: 12px; line-height: 1.4; resize: vertical;"></textarea>
      </div>

      <div style="display: flex; gap: 16px; align-items: center; font-size: 12px;">
        <label style="display: flex; align-items: center; gap: 6px; font-weight: 600; cursor: pointer;">
          <input type="checkbox" id="pi-memory-enable" checked>
          Ativar Blackboard de Memória Semântica
        </label>
        <label style="display: flex; align-items: center; gap: 6px; font-weight: 600; cursor: pointer;">
          <input type="checkbox" id="pi-memory-persist" checked>
          Persistir no Disco
        </label>
      </div>
    `;
  }

  private bindEvents(): void {
    if (!this.modal) return;

    this.modal.querySelector('#btn-close-pi-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-pi')?.addEventListener('click', () => this.hide());

    // Abas
    this.modal.querySelector('#pi-tab-project')?.addEventListener('click', () => {
      this.activeTab = 'project';
      this.render();
    });

    this.modal.querySelector('#pi-tab-chat')?.addEventListener('click', () => {
      this.activeTab = 'chat';
      this.render();
    });

    // 1. Analisar recursos estáticos da pasta
    this.modal.querySelector('#btn-rescan-pi')?.addEventListener('click', async () => {
      const input = this.modal?.querySelector('#pi-dir-input') as HTMLInputElement;
      const pathVal = input?.value.trim() || 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
      const scanBtn = this.modal?.querySelector('#btn-rescan-pi') as HTMLButtonElement;
      if (scanBtn) {
        scanBtn.disabled = true;
        scanBtn.textContent = '🔍 Varrendo...';
      }

      this.currentScanResult = await this.projectImporter.scanProject(pathVal);
      this.aiValidationResult = null; // Reseta validação anterior ao trocar/reescanear pasta
      this.render();
    });

    // 2. Botão de Orientação de Variável de Ambiente
    this.modal.querySelector('#btn-env-guidance')?.addEventListener('click', () => {
      EnvGuidanceModal.getInstance().show('DEEPSEEK_API_KEY', async () => {
        if (this.currentScanResult) {
          await this.executeAIValidation();
        }
      });
    });

    // 3. Validar com Modelo Padrão por IA
    this.modal.querySelector('#btn-validate-ai')?.addEventListener('click', async () => {
      await this.executeAIValidation();
    });

    // 4. Importar Projeto Completo e Sintetizar Grafo
    this.modal.querySelector('#btn-import-project-now')?.addEventListener('click', async () => {
      await this.executeProjectImport();
    });

    // Injetar Agente no Chat
    this.modal.querySelector('#btn-inject-pi-agent')?.addEventListener('click', () => {
      this.injectPiAgent();
    });
  }

  private async executeAIValidation(): Promise<void> {
    if (!this.currentScanResult) return;

    // Checa se a chave existe no ambiente antes de chamar
    const hasKey = await this.envManager.hasEnvVar('DEEPSEEK_API_KEY');
    if (!hasKey) {
      EnvGuidanceModal.getInstance().show('DEEPSEEK_API_KEY', async () => {
        await this.executeAIValidation();
      });
      return;
    }

    this.isValidatingAI = true;
    this.render();

    this.aiValidationResult = await this.projectImporter.validateWithDefaultIntelligence(this.currentScanResult);
    this.isValidatingAI = false;
    this.render();
  }

  public async executeProjectImport(dirPath?: string): Promise<boolean> {
    const pathVal = dirPath || (this.modal?.querySelector('#pi-dir-input') as HTMLInputElement)?.value.trim() || 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
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
    const memPersist = this.modal?.querySelector('#pi-memory-persist') as HTMLInputElement;

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

    const memoryConfig: INodeMemoryConfig = {
      enabled: memEnable ? memEnable.checked : true,
      persistent: memPersist ? memPersist.checked : true,
      readBlackboard: true,
      writeBlackboard: true,
      semanticLessons: true,
      recordLessons: true,
      allowedKeys: ['pi_insights'],
      domains: ['strategy']
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
