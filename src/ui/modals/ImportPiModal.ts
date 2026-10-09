import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings, INodeMemoryConfig } from '../../core/types.ts';
import { PiProjectImporter, IPiProjectScanResult } from '../../core/pi/PiProjectImporter.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';

export class ImportPiModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private projectImporter: PiProjectImporter;
  private projectManager: ProjectManager;
  private activeTab: 'project' | 'chat' = 'project';
  private currentScanResult: IPiProjectScanResult | null = null;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;
    this.projectImporter = PiProjectImporter.getInstance();
    this.projectManager = ProjectManager.getInstance(globalEventBus);

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

    // Resposta visual imediata ao clique
    this.modal.style.display = 'flex';
    this.render();

    // Se ainda não temos o resultado do scan, carrega de forma assíncrona
    if (!this.currentScanResult) {
      try {
        await this.initKnownProject();
      } catch (err) {
        console.warn('[ImportPiModal] Falha ao escanear projeto Pi, usando fallback:', err);
        this.currentScanResult = this.projectImporter.createFallbackScanResult('C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica');
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
      this.currentScanResult = this.projectImporter.createFallbackScanResult('C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica');
    }
  }

  private render(): void {
    if (!this.modal) return;

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 860px; width: 94%; max-height: 90vh; display: flex; flex-direction: column;">
        <!-- Header -->
        <div class="modal-header" style="border-bottom: 2px solid #0d9488; padding-bottom: 12px;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #0d9488; color: white; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              π
            </div>
            <div>
              <h3 style="margin: 0; font-size: 17px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação Oficial do Pi (Pi.ai / Pi CLI)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Importe workspaces completos de produção ou conversas de raciocínio empático do Pi
              </p>
            </div>
          </div>
          <button id="btn-close-pi-modal" class="modal-close-btn">&times;</button>
        </div>

        <!-- Abas do Modal -->
        <div style="display: flex; gap: 4px; padding: 12px 0 0 0; border-bottom: 1px solid #e2e8f0;">
          <button class="tool-btn ${this.activeTab === 'project' ? 'btn-action' : 'btn-secondary'}" id="pi-tab-project" style="font-weight: 700; font-size: 12px; border-radius: 8px 8px 0 0; ${this.activeTab === 'project' ? 'background: #0d9488; color: white;' : ''}">
            📁 Projeto Completo (Pasta Local do Disco)
          </button>
          <button class="tool-btn ${this.activeTab === 'chat' ? 'btn-action' : 'btn-secondary'}" id="pi-tab-chat" style="font-weight: 700; font-size: 12px; border-radius: 8px 8px 0 0; ${this.activeTab === 'chat' ? 'background: #0d9488; color: white;' : ''}">
            💬 Conversa / Prompt Rápido
          </button>
        </div>

        <!-- Conteúdo do Modal -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 0; display: flex; flex-direction: column; gap: 16px;">
          ${this.activeTab === 'project' ? this.renderProjectTab() : this.renderChatTab()}
        </div>

        <!-- Footer -->
        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding-top: 14px; display: flex; justify-content: space-between; align-items: center;">
          <span style="font-size: 12px; color: #64748b;" id="pi-status-msg">
            ${this.activeTab === 'project' ? 'Pronto para sintetizar o grafo completo do projeto Pi' : 'Pronto para sintetizar agente a partir do diálogo'}
          </span>
          <div style="display: flex; gap: 10px;">
            <button id="btn-cancel-pi" class="tool-btn btn-secondary">Cancelar</button>
            ${this.activeTab === 'project' ? `
              <button id="btn-import-project-now" class="tool-btn btn-action" style="background: #0d9488; color: white; font-weight: 800; padding: 8px 22px;">
                🚀 Importar Projeto e Montar Grafo Visual
              </button>
            ` : `
              <button id="btn-inject-pi-agent" class="tool-btn btn-action" style="background: #0d9488; color: white; font-weight: 700; padding: 8px 18px;">
                🚀 Injetar Agente do Pi no Grafo
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

    return `
      <!-- Card Informativo Super Simples -->
      <div style="background: #f0fdfa; border: 1px solid #99f6e4; border-radius: 10px; padding: 14px; display: flex; justify-content: space-between; align-items: center;">
        <div>
          <div style="display: flex; align-items: center; gap: 6px; margin-bottom: 4px;">
            <span style="background: #0d9488; color: white; font-size: 10px; font-weight: 800; padding: 2px 8px; border-radius: 9999px;">1-CLIQUE</span>
            <span style="font-size: 13px; font-weight: 800; color: #115e59;">Importador Automático de Workspaces Pi</span>
          </div>
          <p style="margin: 0; font-size: 12px; color: #134e4a; line-height: 1.4;">
            Detecta automaticamente as etapas do <code>pipeline.js</code> / <code>orquestrador.js</code>, as skills em <code>.agents/</code>, o catálogo de artigos em <code>plano.md</code> e a pasta de publicação no blog <code>matematica</code>.
          </p>
        </div>
        <button id="btn-auto-detect-pi" class="tool-btn btn-secondary" style="font-weight: 800; font-size: 12px; border-color: #0d9488; color: #0f766e; white-space: nowrap;">
          ⚡ Auto-Detectar pi-matematica
        </button>
      </div>

      <!-- Caminho da Pasta -->
      <div>
        <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
          Diretório Raiz do Projeto Pi:
        </label>
        <div style="display: flex; gap: 8px;">
          <input type="text" id="pi-dir-input" value="${this.escapeHtml(defaultPath)}" style="flex: 1; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px; font-weight: 600;">
          <button id="btn-rescan-pi" class="tool-btn btn-secondary" style="font-weight: 700; font-size: 12px; white-space: nowrap;">
            🔍 Inspecionar Pasta
          </button>
        </div>
      </div>

      <!-- Preview Estrutural do Projeto Detectado -->
      ${scan ? `
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; padding: 16px; box-shadow: 0 2px 8px rgba(0,0,0,0.03);">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; border-bottom: 1px solid #f1f5f9; padding-bottom: 8px;">
            <div style="display: flex; align-items: center; gap: 8px;">
              <span style="font-size: 16px;">📚</span>
              <strong style="font-size: 14px; color: #0f172a;">${this.escapeHtml(scan.projectName)}</strong>
            </div>
            <span style="background: #e0f2fe; color: #0369a1; font-size: 11px; font-weight: 800; padding: 2px 10px; border-radius: 9999px;">
              Provedor: ${scan.defaultProvider.toUpperCase()} (${scan.defaultModel})
            </span>
          </div>

          <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; font-size: 12px; margin-bottom: 14px;">
            <div style="background: #f8fafc; padding: 10px; border-radius: 8px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 11px; font-weight: 700;">SKILLS DETECTADAS</span>
              <strong style="font-size: 14px; color: #0f172a;">${scan.skills.length} Skills</strong>
              <small style="display: block; color: #64748b; margin-top: 2px;">redigir, frontmatter, svgs, animar...</small>
            </div>

            <div style="background: #f8fafc; padding: 10px; border-radius: 8px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 11px; font-weight: 700;">CATÁLOGO DE ARTIGOS</span>
              <strong style="font-size: 14px; color: #0f172a;">${scan.articles.length > 0 ? scan.articles.length : scan.totalArticlesCount} Artigos</strong>
              <small style="display: block; color: #16a34a; font-weight: 700; margin-top: 2px;">ART-001 a ART-195</small>
            </div>

            <div style="background: #f8fafc; padding: 10px; border-radius: 8px; border: 1px solid #e2e8f0;">
              <span style="color: #64748b; display: block; font-size: 11px; font-weight: 700;">DESTINO DE PUBLICAÇÃO</span>
              <strong style="font-size: 12px; color: #0f172a;">Blog Matemática</strong>
              <small style="display: block; color: #64748b; margin-top: 2px; font-family: monospace;">_posts/matematica</small>
            </div>
          </div>

          <!-- Etapas do DAG que serão geradas -->
          <div>
            <span style="display: block; font-size: 11.5px; font-weight: 800; color: #475569; text-transform: uppercase; margin-bottom: 8px;">
              Fluxo Visual Taskflow C++20 a Ser Montado (7 Etapas Concorrentes):
            </span>
            <div style="display: flex; flex-wrap: wrap; gap: 6px; align-items: center; font-size: 11.5px;">
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">1. Redação</span>
              <span>➔</span>
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">2. Frontmatter</span>
              <span>➔</span>
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">3. Planejar SVG</span>
              <span>➔</span>
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">4. Criar SVG</span>
              <span>➔</span>
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">4b. Validar Jev</span>
              <span>➔</span>
              <span style="background: #e0e7ff; color: #3730a3; padding: 4px 10px; border-radius: 6px; font-weight: 700;">5. Animar SVG</span>
              <span>➔</span>
              <span style="background: #dcfce7; color: #166534; padding: 4px 10px; border-radius: 6px; font-weight: 800;">6. Publicar Blog</span>
            </div>
          </div>
        </div>
      ` : `
        <div style="padding: 20px; background: #f8fafc; border-radius: 8px; text-align: center; color: #64748b;">
          Clique em "Inspecionar Pasta" para analisar a estrutura do projeto.
        </div>
      `}
    `;
  }

  private renderChatTab(): string {
    return `
      <!-- Card de Apresentação / Badge de Conversa -->
      <div style="background: #f0fdfa; border: 1px solid #99f6e4; border-radius: 10px; padding: 14px; display: flex; justify-content: space-between; align-items: center;">
        <div>
          <span style="font-size: 12.5px; font-weight: 700; color: #115e59;">Diálogo e Síntese de Agente Individual</span>
          <p style="margin: 2px 0 0 0; font-size: 12px; color: #134e4a;">
            Cole aqui uma conversa ou reflexão do Pi.ai para converter a persona em um nó de agente isolado no grafo.
          </p>
        </div>
        <button id="btn-load-sample-pi" class="tool-btn btn-secondary" style="font-weight: 700; font-size: 11.5px; white-space: nowrap; border-color: #0d9488; color: #0f766e;">
          📋 Carregar Exemplo
        </button>
      </div>

      <!-- Campos de Entrada -->
      <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
        <div>
          <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
            Nome do Agente no Grafo:
          </label>
          <input type="text" id="pi-agent-name" value="Agente_Pi_Mentor" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px; font-weight: 600;">
        </div>

        <div>
          <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
            Estilo de Raciocínio Cognitivo:
          </label>
          <select id="pi-agent-style" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px; font-weight: 600;">
            <option value="empathetic_dialogue">Diálogo Empático e Investigativo (Padrão Pi)</option>
            <option value="strategic_advisor">Conselheiro Estratégico & Crítico</option>
            <option value="step_by_step_tutor">Tutor Passo a Passo Metódico</option>
            <option value="deep_synthesizer">Sintetizador Conciso de Ideias Complexas</option>
          </select>
        </div>
      </div>

      <!-- Histórico / Transcrição da Conversa -->
      <div>
        <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
          Histórico da Conversa / Exportação do Pi.ai:
        </label>
        <textarea id="pi-conversation-input" rows="7" placeholder="Cole o diálogo do Pi aqui..." style="width: 100%; padding: 10px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-family: monospace; font-size: 12px; line-height: 1.4; resize: vertical;"></textarea>
      </div>

      <!-- Configuração de Memória -->
      <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 10px; padding: 14px;">
        <h4 style="font-size: 12.5px; font-weight: 800; color: #1e293b; margin-bottom: 8px;">
          ⚙️ Memória do Agente Pi (Fase 12)
        </h4>
        <div style="display: flex; gap: 20px; align-items: center;">
          <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; font-weight: 600; cursor: pointer;">
            <input type="checkbox" id="pi-memory-enable" checked>
            Ativar Memória do Agente
          </label>
          <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; font-weight: 600; cursor: pointer;">
            <input type="checkbox" id="pi-memory-persist" checked>
            Persistir entre Sessões (Disco)
          </label>
        </div>
      </div>
    `;
  }

  private bindEvents(): void {
    if (!this.modal) return;

    this.modal.querySelector('#btn-close-pi-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-pi')?.addEventListener('click', () => this.hide());

    // Alternador de Abas
    this.modal.querySelector('#pi-tab-project')?.addEventListener('click', () => {
      this.activeTab = 'project';
      this.render();
    });

    this.modal.querySelector('#pi-tab-chat')?.addEventListener('click', () => {
      this.activeTab = 'chat';
      this.render();
    });

    // Auto-detectar
    this.modal.querySelector('#btn-auto-detect-pi')?.addEventListener('click', async () => {
      const input = this.modal?.querySelector('#pi-dir-input') as HTMLInputElement;
      const target = 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
      if (input) input.value = target;
      this.currentScanResult = await this.projectImporter.scanProject(target);
      this.render();
    });

    // Re-scan
    this.modal.querySelector('#btn-rescan-pi')?.addEventListener('click', async () => {
      const input = this.modal?.querySelector('#pi-dir-input') as HTMLInputElement;
      const pathVal = input?.value.trim() || 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
      this.currentScanResult = await this.projectImporter.scanProject(pathVal);
      this.render();
    });

    // Importar Projeto Completo
    this.modal.querySelector('#btn-import-project-now')?.addEventListener('click', async () => {
      await this.executeProjectImport();
    });

    // Botões do Chat
    this.modal.querySelector('#btn-load-sample-pi')?.addEventListener('click', () => {
      const input = this.modal?.querySelector('#pi-conversation-input') as HTMLTextAreaElement;
      if (input) {
        input.value = [
          'User: Pi, como estruturar o pipeline de matemática para concursos?',
          'Pi: Vamos dividir em 6 etapas claras: Redação aprofundada, Frontmatter Jekyll, Planejamento de SVGs conceituais, Criação com validação visual, Animação suave e Publicação direta no blog!'
        ].join('\n');
      }
    });

    this.modal.querySelector('#btn-inject-pi-agent')?.addEventListener('click', () => {
      this.injectPiAgent();
    });
  }

  public async executeProjectImport(dirPath?: string): Promise<boolean> {
    const pathVal = dirPath || (this.modal?.querySelector('#pi-dir-input') as HTMLInputElement)?.value.trim() || 'C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica';
    const scan = this.currentScanResult || await this.projectImporter.scanProject(pathVal);

    const ok = await this.projectImporter.importProject(scan, this.graph, this.projectManager);
    if (ok) {
      if (this.viewport) {
        setTimeout(() => this.viewport?.zoomToFit(), 150);
      }
      this.hide();
      if (typeof alert !== 'undefined') {
        alert(`✅ Projeto "${scan.projectName}" importado com sucesso!\nGrafo concorrente montado com 7 etapas e lote configurado.`);
      }
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
      `# AGENTE PI (INFLECTION COGNITIVE ENGINE) - ${agentName}`,
      `Estilo Cognitivo: ${style}`,
      `Diretrizes Principais: Raciocínio empático, investigação socrática, rigor metodológico e decomposição de problemas complexos.`,
      conversation ? `\n--- HISTÓRICO E CONTEXTO DE ORIGEM (PI.AI) ---\n${conversation}` : '',
      `\n--- INSTRUÇÕES OPERACIONAIS TASKFLOW ---`,
      `- Receba o prompt de entrada na porta 'prompt_in'.`,
      `- Processe com precisão e gere o artefato final na porta 'artifact_out'.`,
      `- Registre aprendizados e regras preventivas no Blackboard de Memória.`
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
      allowedKeys: ['pi_insights', 'lesson_learned', 'architecture_decision'],
      domains: ['strategy', 'architecture']
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
        allowedTools: ['read', 'write', 'bash'],
        customScripts: ['scripts/pi/run_task.py']
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
    if (typeof alert !== 'undefined') {
      alert(`✅ ${agentName} sintetizado com sucesso a partir do Pi e inserido no grafo!`);
    }
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
