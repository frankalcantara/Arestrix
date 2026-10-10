import { Graph } from '../../core/Graph.ts';
import { INodeData, ICodeModeConfig, CodeModeTool } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

export class NodeToolsModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;

    this.render(node);
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(node: INodeData): void {
    const globalDefaults = this.graph.getGlobalDefaults();
    const settings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    const isInherited = settings.inheritCodeMode !== false;
    const effectiveTools: ICodeModeConfig = isInherited
      ? globalDefaults.codeMode
      : {
          ...globalDefaults.codeMode,
          ...(settings.codeMode || {})
        };

    const allowed = effectiveTools.allowedTools || [];
    const allowedMcp = effectiveTools.allowedMcpTools || [];

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Ferramentas Code Mode',
      subtitle: `Nó: <strong>${this.escapeHtml(node.label)}</strong> (ID: ${node.id})`,
      icon: '🔧',
      closeBtnId: 'btn-close-tools-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}
        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-tools" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">Herdar Padrões Globais de Ferramentas</span>
                <span class="inheritance-desc">Usa o conjunto de ferramentas procedurais configurado na Toolbar (${(globalDefaults.codeMode.allowedTools || []).join(', ')})</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? 'HERDADO DO PIPELINE' : 'CUSTOMIZADO PARA ESTE NÓ'}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="tools-form-wrapper">
            <div class="tools-checkbox-grid">
              <label class="tool-checkbox-pill ${allowed.includes('write') ? 'checked' : ''}">
                <input type="checkbox" class="node-tool-check" value="write" ${allowed.includes('write') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                <span class="tool-pill-name">tools.write</span>
                <span class="tool-pill-desc">Permite salvar novos arquivos</span>
              </label>

              <label class="tool-checkbox-pill ${allowed.includes('edit') ? 'checked' : ''}">
                <input type="checkbox" class="node-tool-check" value="edit" ${allowed.includes('edit') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                <span class="tool-pill-name">tools.edit</span>
                <span class="tool-pill-desc">Modificações cirúrgicas em arquivos</span>
              </label>

              <label class="tool-checkbox-pill ${allowed.includes('read') ? 'checked' : ''}">
                <input type="checkbox" class="node-tool-check" value="read" ${allowed.includes('read') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                <span class="tool-pill-name">tools.read</span>
                <span class="tool-pill-desc">Leitura do conteúdo de arquivos</span>
              </label>

              <label class="tool-checkbox-pill ${allowed.includes('bash') ? 'checked' : ''}">
                <input type="checkbox" class="node-tool-check" value="bash" ${allowed.includes('bash') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                <span class="tool-pill-name">tools.bash</span>
                <span class="tool-pill-desc">Execução de comandos bash / shell</span>
              </label>

              <label class="tool-checkbox-pill ${allowed.includes('validate_svg') ? 'checked' : ''}">
                <input type="checkbox" class="node-tool-check" value="validate_svg" ${allowed.includes('validate_svg') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                <span class="tool-pill-name">tools.validate_svg</span>
                <span class="tool-pill-desc">Validador de sintaxe e renderização</span>
              </label>
            </div>

            <!-- Ferramentas MCP (Model Context Protocol) -->
            <div style="margin-top: 18px; border-top: 1px solid var(--border-color); padding-top: 14px;">
              <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px;">
                <label class="form-label" style="margin: 0; font-weight: 700;">🔌 Ferramentas MCP (Model Context Protocol)</label>
                <span style="font-size: 11px; color: var(--text-secondary);">Conectores remotos/locais</span>
              </div>
              <div class="tools-checkbox-grid">
                ${this.renderMcpToolsList(allowedMcp, isInherited)}
              </div>
            </div>

            <div class="form-group" style="margin-top: 16px;">
              <label class="form-label" for="node-custom-scripts">Scripts Utilitários Autorizados</label>
              <input type="text" id="node-custom-scripts" class="form-input" value="${this.escapeHtml((effectiveTools.customScripts || []).join(', '))}" placeholder="python scripts/audit_exercises.py" ${isInherited ? 'disabled' : ''} />
              <div class="form-hint">Scripts Python/Node que o agente pode disparar via terminal com timeout supervisionado.</div>
            </div>
          </div>

          <!-- Validação de Artefato & Resume Engine (Fase 11) -->
          <div class="settings-section-card" style="margin-top: 16px;">
            <div class="section-card-header">
              <span class="section-card-icon">🛡️</span>
              <div class="section-card-titles">
                <h3 class="section-card-title">Validação de Artefato & Resume Engine</h3>
                <p class="section-card-desc">Critérios para considerar esta etapa concluída e permitir auto-recuperação</p>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group full-width">
                <label class="form-label" for="node-artifact-pattern">Padrão de Caminho do Artefato</label>
                <input type="text" id="node-artifact-pattern" class="form-input" value="${this.escapeHtml((settings.validationRules && settings.validationRules[0]?.targetPathPattern) || '')}" placeholder="aulas/{itemId}.md" />
                <div class="form-hint">Suporta interpolação com <code>{itemId}</code> ou <code>{id}</code>.</div>
              </div>

              <div class="form-group">
                <label class="form-label" for="node-artifact-min-size">Tamanho Mínimo (Bytes)</label>
                <input type="number" id="node-artifact-min-size" class="form-input" value="${(settings.validationRules && settings.validationRules[0]?.minSizeBytes) || 0}" min="0" step="100" />
              </div>

              <div class="form-group">
                <label class="form-label" for="node-artifact-regex">Regex de Conteúdo (Opcional)</label>
                <input type="text" id="node-artifact-regex" class="form-input" value="${this.escapeHtml((settings.validationRules && settings.validationRules[0]?.contentPattern) || '')}" placeholder="^---[\\s\\S]+---" />
              </div>

              <div class="form-group full-width">
                <label class="clean-checkbox-label">
                  <input type="checkbox" id="chk-node-serial-batch" ${settings.isSerialAcrossBatch ? 'checked' : ''} />
                  <span><strong>Execução Serial no Lote:</strong> Gargalo de concorrência — processa apenas 1 artigo por vez nesta etapa.</span>
                </label>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-cancel-tools-modal">Cancelar</button>
          <button class="btn btn-primary" id="btn-save-tools-modal">Salvar Configuração</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        onClose: () => this.hide()
      });
    }

    this.bindDialogEvents(node);
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  private bindDialogEvents(node: INodeData): void {
    this.overlay.querySelector('#btn-close-tools-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-cancel-tools-modal')?.addEventListener('click', () => this.hide());

    const inheritCheck = this.overlay.querySelector('#chk-inherit-tools') as HTMLInputElement;
    const formWrapper = this.overlay.querySelector('#tools-form-wrapper') as HTMLElement;

    inheritCheck?.addEventListener('change', () => {
      const isInherited = inheritCheck.checked;
      const inputs = formWrapper.querySelectorAll<HTMLInputElement>('input');
      inputs.forEach(input => {
        input.disabled = isInherited;
      });

      if (isInherited) {
        formWrapper.classList.add('form-disabled-opacity');
      } else {
        formWrapper.classList.remove('form-disabled-opacity');
      }
    });

    this.overlay.querySelectorAll('.node-tool-check, .node-mcp-tool-check').forEach(chk => {
      chk.addEventListener('change', (e) => {
        const input = e.target as HTMLInputElement;
        const pill = input.closest('.tool-checkbox-pill');
        if (pill) {
          if (input.checked) pill.classList.add('checked');
          else pill.classList.remove('checked');
        }
      });
    });

    this.overlay.querySelector('#btn-save-tools-modal')?.addEventListener('click', () => {
      this.saveNodeTools(node, inheritCheck.checked);
      this.hide();
    });
  }

  private renderMcpToolsList(allowedMcp: string[], isInherited: boolean): string {
    const knownMcpTools = [
      { name: 'read_file', desc: 'Lê arquivos locais via MCP', server: 'filesystem' },
      { name: 'write_file', desc: 'Grava arquivos locais via MCP', server: 'filesystem' },
      { name: 'list_directory', desc: 'Lista diretórios via MCP', server: 'filesystem' },
      { name: 'fetch_url', desc: 'Requisição HTTP remota via MCP', server: 'fetch' }
    ];

    return knownMcpTools.map(t => {
      const isChecked = allowedMcp.includes(t.name);
      return `
        <label class="tool-checkbox-pill ${isChecked ? 'checked' : ''}">
          <input type="checkbox" class="node-mcp-tool-check" value="${t.name}" ${isChecked ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
          <span class="tool-pill-name">mcp.${this.escapeHtml(t.name)}</span>
          <span class="tool-pill-desc">${this.escapeHtml(t.desc)} (${this.escapeHtml(t.server)})</span>
        </label>
      `;
    }).join('');
  }

  private saveNodeTools(node: INodeData, inherit: boolean): void {
    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    let newToolsData: Partial<ICodeModeConfig> | undefined;

    if (!inherit) {
      const allowedTools: CodeModeTool[] = [];
      this.overlay.querySelectorAll<HTMLInputElement>('.node-tool-check:checked').forEach(chk => {
        allowedTools.push(chk.value as CodeModeTool);
      });

      const allowedMcpTools: string[] = [];
      this.overlay.querySelectorAll<HTMLInputElement>('.node-mcp-tool-check:checked').forEach(chk => {
        allowedMcpTools.push(chk.value);
      });

      const scriptsRaw = (this.overlay.querySelector('#node-custom-scripts') as HTMLInputElement)?.value || '';
      const customScripts = scriptsRaw.split(',').map(s => s.trim()).filter(Boolean);

      newToolsData = {
        allowedTools,
        customScripts,
        allowedMcpTools
      };
    }

    // Regras de validação de artefato e encadeamento em lote
    const artPattern = (this.overlay.querySelector('#node-artifact-pattern') as HTMLInputElement)?.value.trim() || '';
    const minBytes = parseInt((this.overlay.querySelector('#node-artifact-min-size') as HTMLInputElement)?.value, 10) || 0;
    const artRegex = (this.overlay.querySelector('#node-artifact-regex') as HTMLInputElement)?.value.trim() || '';
    const isSerial = (this.overlay.querySelector('#chk-node-serial-batch') as HTMLInputElement)?.checked || false;

    const validationRules = artPattern ? [{
      targetPathPattern: artPattern,
      minSizeBytes: minBytes,
      contentPattern: artRegex,
      checkLogExitCode: true
    }] : undefined;

    const updatedSettings = {
      ...currentSettings,
      inheritCodeMode: inherit,
      codeMode: inherit ? undefined : newToolsData,
      validationRules,
      isSerialAcrossBatch: isSerial
    };

    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      `Alterar ferramentas e validação de "${node.label}"`
    );
    this.graph.commandManager.execute(cmd);
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
