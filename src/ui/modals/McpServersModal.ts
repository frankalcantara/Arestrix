import { Graph } from '../../core/Graph.ts';
import { IMcpServerConfig, IMcpCallResult } from '../../core/types.ts';

export class McpServersModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private servers: IMcpServerConfig[] = [];
  private selectedServerId: string | null = null;

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public show(): void {
    this.overlay.classList.remove('hidden');
    this.refreshServers();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  public async refreshServers(): Promise<void> {
    try {
      if ((window as any).webui && (window as any).webui.call) {
        const raw = await (window as any).webui.call('get_mcp_servers');
        if (raw) {
          this.servers = JSON.parse(raw);
        }
      } else {
        // Fallback / Presets padrão caso esteja rodando standalone
        if (this.servers.length === 0) {
          this.servers = [
            {
              id: 'mock-filesystem',
              name: 'Filesystem Explorer (MCP)',
              transport: 'stdio',
              command: 'mock',
              args: ['--dir', '.'],
              isEnabled: true,
              cachedTools: [
                { name: 'read_file', description: 'Lê o conteúdo completo de um arquivo local UTF-8', serverId: 'mock-filesystem' },
                { name: 'write_file', description: 'Cria ou sobrescreve um arquivo no disco', serverId: 'mock-filesystem' },
                { name: 'list_directory', description: 'Lista pastas e arquivos no diretório', serverId: 'mock-filesystem' }
              ]
            },
            {
              id: 'mock-fetch',
              name: 'Web & Fetch Gateway (MCP)',
              transport: 'stdio',
              command: 'mock',
              args: ['--timeout', '10'],
              isEnabled: true,
              cachedTools: [
                { name: 'fetch_url', description: 'Efetua uma requisição HTTP GET para uma URL externa', serverId: 'mock-fetch' }
              ]
            }
          ];
        }
      }
    } catch (e) {
      console.warn('[McpServersModal] Falha ao listar servidores MCP do backend:', e);
    }

    this.render();
  }

  private render(): void {
    this.overlay.innerHTML = `
      <div class="modal-dialog project-memory-modal-dialog" style="max-width: 900px;">
        <div class="modal-header">
          <div>
            <h2 class="modal-title">Conectores MCP (Model Context Protocol)</h2>
            <div style="font-size: 12px; color: #616161; margin-top: 2px;">Projeto: <strong>${this.escapeHtml(this.graph.getProjectId())}</strong> &mdash; Servidores MCP e ferramentas padronizadas</div>
          </div>
          <button class="modal-close-btn" id="btn-close-mcp-modal" title="Fechar">&times;</button>
        </div>

        <div class="modal-body-scrollable">
          <!-- Toolbar de Ações MCP -->
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px;">
            <div style="display: flex; gap: 8px; align-items: center;">
              <span style="font-size: 11px; font-weight: 600; color: #0f6cbd; background: #eef6fc; padding: 2px 8px; border-radius: 2px; border: 1px solid #c7e0f4;">
                ${this.servers.length} SERVIDORES REGISTRADOS
              </span>
              <span style="font-size: 11px; font-weight: 600; color: #107c41; background: #f1fbf4; padding: 2px 8px; border-radius: 2px; border: 1px solid #c3edd3;">
                ${this.getTotalToolsCount()} FERRAMENTAS DISPONÍVEIS
              </span>
            </div>
            <div style="display: flex; gap: 8px;">
              <button id="btn-mcp-refresh" class="tool-btn btn-secondary">
                Atualizar
              </button>
              <button id="btn-mcp-add-new" class="tool-btn btn-action">
                + Novo Servidor
              </button>
            </div>
          </div>

          <!-- Formulário de Adição/Edição (Oculto por padrão) -->
          <div id="mcp-form-container" class="settings-section-card hidden" style="margin-bottom: 20px; border: 1px solid var(--border-color);">
            <div class="section-card-header">
              <div class="section-card-titles">
                <h3 class="section-card-title" id="mcp-form-title">Registrar Novo Servidor MCP</h3>
                <p class="section-card-desc">Configure o transporte stdio ou SSE</p>
              </div>
            </div>

            <!-- Presets rápidos -->
            <div style="margin: 12px 0;">
              <label class="form-label">Modelos Pré-definidos:</label>
              <div style="display: flex; gap: 8px; flex-wrap: wrap;">
                <button type="button" class="btn-preset-mcp tool-btn btn-secondary" data-preset="filesystem">Filesystem Local</button>
                <button type="button" class="btn-preset-mcp tool-btn btn-secondary" data-preset="fetch">Web Fetch</button>
                <button type="button" class="btn-preset-mcp tool-btn btn-secondary" data-preset="sqlite">SQLite Database</button>
                <button type="button" class="btn-preset-mcp tool-btn btn-secondary" data-preset="github">GitHub (npx)</button>
              </div>
            </div>

            <div class="form-row-grid" style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
              <div class="form-group">
                <label class="form-label" for="mcp-input-id">ID do Servidor (slug único)</label>
                <input type="text" id="mcp-input-id" class="form-input" placeholder="ex: local-filesystem" />
              </div>
              <div class="form-group">
                <label class="form-label" for="mcp-input-name">Nome Amigável</label>
                <input type="text" id="mcp-input-name" class="form-input" placeholder="ex: Filesystem Explorer" />
              </div>
            </div>

            <div class="form-group" style="margin-top: 12px;">
              <label class="form-label">Transporte:</label>
              <div style="display: flex; gap: 16px;">
                <label style="display: flex; align-items: center; gap: 6px; cursor: pointer;">
                  <input type="radio" name="mcp-transport" value="stdio" checked />
                  <strong>stdio (Processo Local)</strong>
                </label>
                <label style="display: flex; align-items: center; gap: 6px; cursor: pointer;">
                  <input type="radio" name="mcp-transport" value="sse" />
                  <strong>SSE (HTTP / Server-Sent Events)</strong>
                </label>
              </div>
            </div>

            <div id="mcp-stdio-fields" style="margin-top: 12px;">
              <div class="form-group">
                <label class="form-label" for="mcp-input-command">Comando Executável</label>
                <input type="text" id="mcp-input-command" class="form-input" placeholder="ex: npx, uvx, python, mock" />
              </div>
              <div class="form-group" style="margin-top: 8px;">
                <label class="form-label" for="mcp-input-args">Argumentos (separados por espaço)</label>
                <input type="text" id="mcp-input-args" class="form-input" placeholder="ex: -y @modelcontextprotocol/server-filesystem ./workspace" />
              </div>
            </div>

            <div id="mcp-sse-fields" class="hidden" style="margin-top: 12px;">
              <div class="form-group">
                <label class="form-label" for="mcp-input-url">URL do Endpoint SSE</label>
                <input type="text" id="mcp-input-url" class="form-input" placeholder="ex: http://localhost:8000/sse" />
              </div>
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 8px; margin-top: 16px;">
              <button id="btn-cancel-mcp-form" class="btn-action-pill" style="background: var(--surface-bg); color: var(--text-primary); border: 1px solid var(--border-color);">
                Cancelar
              </button>
              <button id="btn-save-mcp-server" class="btn-action-pill" style="background: var(--gradient-blue); color: #fff;">
                💾 Salvar Conector
              </button>
            </div>
          </div>

          <!-- Lista de Servidores Registrados -->
          <div class="mcp-servers-list" style="display: flex; flex-direction: column; gap: 14px;">
            ${this.renderServersList()}
          </div>

          <!-- Playground de Teste de Ferramenta -->
          <div class="settings-section-card" style="margin-top: 24px;">
            <div class="section-card-header">
              <span class="section-card-icon">⚡</span>
              <div class="section-card-titles">
                <h3 class="section-card-title">Playground de Execução Rápida MCP</h3>
                <p class="section-card-desc">Invoque uma ferramenta remota diretamente para validar parâmetros e respostas</p>
              </div>
            </div>

            <div class="form-row-grid" style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 12px;">
              <div class="form-group">
                <label class="form-label" for="playground-select-server">Servidor Alvo</label>
                <select id="playground-select-server" class="form-input">
                  ${this.servers.map(s => `<option value="${s.id}">${this.escapeHtml(s.name)} (${s.id})</option>`).join('')}
                </select>
              </div>
              <div class="form-group">
                <label class="form-label" for="playground-select-tool">Ferramenta</label>
                <select id="playground-select-tool" class="form-input">
                  ${this.getToolsOptionsForSelectedServer()}
                </select>
              </div>
            </div>

            <div class="form-group" style="margin-top: 12px;">
              <label class="form-label" for="playground-args">Argumentos JSON</label>
              <textarea id="playground-args" class="form-input" style="height: 60px; font-family: monospace; font-size: 12px;" placeholder='{"path": "package.json"}'></textarea>
            </div>

            <div style="display: flex; justify-content: flex-end; margin-top: 10px;">
              <button id="btn-run-mcp-playground" class="btn-action-pill" style="background: var(--gradient-blue); color: white;">
                ▶ Invocar Ferramenta MCP
              </button>
            </div>

            <div id="playground-result-container" class="hidden" style="margin-top: 12px; background: rgba(0,0,0,0.15); border-radius: 8px; padding: 12px; font-family: monospace; font-size: 12px; max-height: 140px; overflow-y: auto;">
              <div style="font-weight: 700; color: var(--text-primary); margin-bottom: 4px;">Resposta:</div>
              <div id="playground-result-output" style="white-space: pre-wrap; word-break: break-all; color: var(--text-secondary);"></div>
            </div>
          </div>
        </div>
      </div>
    `;

    this.bindInternalEvents();
  }

  private getTotalToolsCount(): number {
    let count = 0;
    for (const s of this.servers) {
      if (s.cachedTools) count += s.cachedTools.length;
    }
    return count;
  }

  private renderServersList(): string {
    if (this.servers.length === 0) {
      return `
        <div style="padding: 24px; text-align: center; color: var(--text-secondary); background: var(--surface-bg); border-radius: 8px;">
          Nenhum servidor MCP configurado no momento. Clique em "+ Novo Servidor" ou utilize um dos presets para começar.
        </div>
      `;
    }

    return this.servers.map(s => {
      const tools = s.cachedTools || [];
      const isStdio = s.transport === 'stdio';
      const cmdInfo = isStdio 
        ? `${s.command || ''} ${(s.args || []).join(' ')}`
        : `SSE: ${s.url || ''}`;

      return `
        <div class="settings-section-card" data-server-id="${s.id}" style="border: 1px solid var(--border-color); padding: 14px;">
          <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 12px;">
            <div>
              <div style="display: flex; align-items: center; gap: 8px;">
                <strong style="font-size: 15px; color: var(--text-primary); font-weight: 700;">${this.escapeHtml(s.name)}</strong>
                <span class="inheritance-status-badge ${s.isEnabled ? 'badge-inherited' : 'badge-overridden'}" style="font-weight: 700; font-size: 11px;">
                  ${s.isEnabled ? '🟢 ATIVO' : '⚪ DESABILITADO'}
                </span>
                <span class="inheritance-status-badge badge-memory-persistent" style="font-weight: 700; font-size: 11px;">
                  ${s.transport.toUpperCase()}
                </span>
              </div>
              <div style="margin-top: 4px; font-family: monospace; font-size: 12px; color: var(--text-secondary);">
                ID: <strong>${this.escapeHtml(s.id)}</strong> | Exec: <code>${this.escapeHtml(cmdInfo)}</code>
              </div>
            </div>

            <div style="display: flex; gap: 6px;">
              <button class="btn-test-server tool-btn btn-action" data-id="${s.id}" title="Testa a inicialização e atualiza lista de ferramentas">
                🔍 Testar & Descobrir
              </button>
              <button class="btn-delete-server tool-btn btn-danger-pill" data-id="${s.id}" title="Remover servidor">
                🗑️
              </button>
            </div>
          </div>

          <!-- Tools tags -->
          <div style="margin-top: 12px; padding-top: 10px; border-top: 1px solid rgba(255,255,255,0.06);">
            <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px;">
              <span style="font-size: 12px; font-weight: 700; color: var(--text-primary);">
                Ferramentas Disponíveis (${tools.length}):
              </span>
            </div>
            <div style="display: flex; gap: 8px; flex-wrap: wrap;">
              ${tools.length === 0 
                ? `<span style="font-size: 12px; color: var(--text-secondary); font-style: italic;">Nenhuma ferramenta em cache. Clique em "Testar & Descobrir" para sincronizar.</span>`
                : tools.map(t => `
                  <span class="tool-checkbox-pill checked" style="cursor: default; padding: 4px 10px; font-size: 11px;" title="${this.escapeHtml(t.description)}">
                    <strong style="color: var(--accent-color); font-weight: 700;">${this.escapeHtml(t.name)}</strong>
                  </span>
                `).join('')
              }
            </div>
          </div>
        </div>
      `;
    }).join('');
  }

  private getToolsOptionsForSelectedServer(): string {
    const selId = this.selectedServerId || (this.servers[0] ? this.servers[0].id : null);
    if (!selId) return '<option value="">Nenhum servidor</option>';

    const s = this.servers.find(srv => srv.id === selId);
    if (!s || !s.cachedTools || s.cachedTools.length === 0) {
      return '<option value="">Nenhuma ferramenta disponível</option>';
    }

    return s.cachedTools.map(t => `<option value="${t.name}">${this.escapeHtml(t.name)} - ${this.escapeHtml(t.description.substring(0, 40))}...</option>`).join('');
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) {
        this.hide();
      }
    });
  }

  private bindInternalEvents(): void {
    const btnClose = this.overlay.querySelector('#btn-close-mcp-modal');
    if (btnClose) btnClose.addEventListener('click', () => this.hide());

    const btnRefresh = this.overlay.querySelector('#btn-mcp-refresh');
    if (btnRefresh) btnRefresh.addEventListener('click', () => this.refreshServers());

    const btnAddNew = this.overlay.querySelector('#btn-mcp-add-new');
    const formContainer = this.overlay.querySelector('#mcp-form-container') as HTMLElement;
    const btnCancelForm = this.overlay.querySelector('#btn-cancel-mcp-form');

    if (btnAddNew && formContainer) {
      btnAddNew.addEventListener('click', () => {
        formContainer.classList.remove('hidden');
        (this.overlay.querySelector('#mcp-input-id') as HTMLInputElement).focus();
      });
    }

    if (btnCancelForm && formContainer) {
      btnCancelForm.addEventListener('click', () => {
        formContainer.classList.add('hidden');
      });
    }

    // Radio de transporte
    const transportRadios = this.overlay.querySelectorAll('input[name="mcp-transport"]');
    const stdioFields = this.overlay.querySelector('#mcp-stdio-fields') as HTMLElement;
    const sseFields = this.overlay.querySelector('#mcp-sse-fields') as HTMLElement;

    transportRadios.forEach(radio => {
      radio.addEventListener('change', (e) => {
        const val = (e.target as HTMLInputElement).value;
        if (val === 'stdio') {
          stdioFields?.classList.remove('hidden');
          sseFields?.classList.add('hidden');
        } else {
          stdioFields?.classList.add('hidden');
          sseFields?.classList.remove('hidden');
        }
      });
    });

    // Presets
    const presetBtns = this.overlay.querySelectorAll('.btn-preset-mcp');
    presetBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        const preset = btn.getAttribute('data-preset');
        this.applyPreset(preset);
      });
    });

    // Salvar servidor
    const btnSave = this.overlay.querySelector('#btn-save-mcp-server');
    if (btnSave) {
      btnSave.addEventListener('click', () => this.handleSaveServer());
    }

    // Botões de teste e delete em cada card
    this.overlay.querySelectorAll('.btn-test-server').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const id = (e.currentTarget as HTMLElement).getAttribute('data-id');
        if (id) this.handleTestServer(id);
      });
    });

    this.overlay.querySelectorAll('.btn-delete-server').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const id = (e.currentTarget as HTMLElement).getAttribute('data-id');
        if (id) this.handleDeleteServer(id);
      });
    });

    // Playground
    const selServer = this.overlay.querySelector('#playground-select-server') as HTMLSelectElement;
    const selTool = this.overlay.querySelector('#playground-select-tool') as HTMLSelectElement;

    if (selServer && selTool) {
      selServer.addEventListener('change', () => {
        this.selectedServerId = selServer.value;
        selTool.innerHTML = this.getToolsOptionsForSelectedServer();
      });
    }

    const btnRunPlayground = this.overlay.querySelector('#btn-run-mcp-playground');
    if (btnRunPlayground) {
      btnRunPlayground.addEventListener('click', () => this.handleRunPlayground());
    }
  }

  private applyPreset(preset: string | null): void {
    const idInput = this.overlay.querySelector('#mcp-input-id') as HTMLInputElement;
    const nameInput = this.overlay.querySelector('#mcp-input-name') as HTMLInputElement;
    const cmdInput = this.overlay.querySelector('#mcp-input-command') as HTMLInputElement;
    const argsInput = this.overlay.querySelector('#mcp-input-args') as HTMLInputElement;

    if (!preset || !idInput) return;

    if (preset === 'filesystem') {
      idInput.value = 'local-filesystem';
      nameInput.value = 'Filesystem Explorer (MCP)';
      cmdInput.value = 'npx';
      argsInput.value = '-y @modelcontextprotocol/server-filesystem .';
    } else if (preset === 'fetch') {
      idInput.value = 'remote-fetch';
      nameInput.value = 'Web & Fetch Gateway (MCP)';
      cmdInput.value = 'uvx';
      argsInput.value = 'mcp-server-fetch';
    } else if (preset === 'sqlite') {
      idInput.value = 'local-sqlite';
      nameInput.value = 'SQLite Database (MCP)';
      cmdInput.value = 'npx';
      argsInput.value = '-y @modelcontextprotocol/server-sqlite --db-path ./database.db';
    } else if (preset === 'github') {
      idInput.value = 'github-connector';
      nameInput.value = 'GitHub Integration (MCP)';
      cmdInput.value = 'npx';
      argsInput.value = '-y @modelcontextprotocol/server-github';
    }
  }

  private async handleSaveServer(): Promise<void> {
    const id = (this.overlay.querySelector('#mcp-input-id') as HTMLInputElement).value.trim();
    const name = (this.overlay.querySelector('#mcp-input-name') as HTMLInputElement).value.trim();
    const transport = (this.overlay.querySelector('input[name="mcp-transport"]:checked') as HTMLInputElement)?.value as 'stdio' | 'sse' || 'stdio';
    const command = (this.overlay.querySelector('#mcp-input-command') as HTMLInputElement)?.value.trim() || '';
    const argsRaw = (this.overlay.querySelector('#mcp-input-args') as HTMLInputElement)?.value.trim() || '';
    const url = (this.overlay.querySelector('#mcp-input-url') as HTMLInputElement)?.value.trim() || '';

    if (!id || !name) {
      alert('Por favor, informe ID e Nome para o servidor MCP.');
      return;
    }

    const args = argsRaw ? argsRaw.split(' ').filter(a => a.length > 0) : [];

    const newServer: IMcpServerConfig = {
      id,
      name,
      transport,
      command,
      args,
      url,
      isEnabled: true,
      cachedTools: []
    };

    try {
      if ((window as any).webui && (window as any).webui.call) {
        await (window as any).webui.call('save_mcp_server', JSON.stringify(newServer));
      } else {
        const idx = this.servers.findIndex(s => s.id === id);
        if (idx >= 0) {
          this.servers[idx] = newServer;
        } else {
          this.servers.push(newServer);
        }
      }
    } catch (e) {
      console.error('[MCP] Erro ao salvar servidor:', e);
    }

    await this.refreshServers();
  }

  private async handleTestServer(id: string): Promise<void> {
    try {
      if ((window as any).webui && (window as any).webui.call) {
        const resRaw = await (window as any).webui.call('test_mcp_server', id);
        if (resRaw) {
          const res = JSON.parse(resRaw);
          if (res.success) {
            alert(`Conexão com servidor "${id}" estabelecida com sucesso!\nFerramentas descobertas: ${res.count}`);
          } else {
            alert(`Falha no teste com servidor "${id}":\n${res.error || 'Erro desconhecido'}`);
          }
        }
      } else {
        alert(`Servidor "${id}" testado com sucesso (modo simulado).`);
      }
    } catch (e) {
      alert('Erro durante teste do servidor: ' + e);
    }

    await this.refreshServers();
  }

  private async handleDeleteServer(id: string): Promise<void> {
    if (!confirm(`Deseja realmente remover o conector MCP "${id}"?`)) return;

    try {
      if ((window as any).webui && (window as any).webui.call) {
        await (window as any).webui.call('remove_mcp_server', id);
      } else {
        this.servers = this.servers.filter(s => s.id !== id);
      }
    } catch (e) {
      console.error('[MCP] Erro ao remover servidor:', e);
    }

    await this.refreshServers();
  }

  private async handleRunPlayground(): Promise<void> {
    const selServer = this.overlay.querySelector('#playground-select-server') as HTMLSelectElement;
    const selTool = this.overlay.querySelector('#playground-select-tool') as HTMLSelectElement;
    const argsInput = this.overlay.querySelector('#playground-args') as HTMLTextAreaElement;
    const resBox = this.overlay.querySelector('#playground-result-container') as HTMLElement;
    const resOut = this.overlay.querySelector('#playground-result-output') as HTMLElement;

    const serverId = selServer?.value;
    const toolName = selTool?.value;
    if (!serverId || !toolName) {
      alert('Selecione um servidor e uma ferramenta para executar.');
      return;
    }

    let parsedArgs = {};
    try {
      const txt = argsInput.value.trim();
      if (txt) parsedArgs = JSON.parse(txt);
    } catch (e) {
      alert('Erro de sintaxe no JSON de argumentos.');
      return;
    }

    resBox.classList.remove('hidden');
    resOut.textContent = 'Executando chamada MCP...';

    try {
      if ((window as any).webui && (window as any).webui.call) {
        const payload = JSON.stringify({ serverId, toolName, arguments: parsedArgs });
        const respRaw = await (window as any).webui.call('call_mcp_tool', payload);
        if (respRaw) {
          const resp: IMcpCallResult = JSON.parse(respRaw);
          resOut.textContent = resp.isError 
            ? `[ERRO] ${resp.errorMessage || 'Falha na execução'}` 
            : `[SUCESSO] ${resp.content} (Duração: ${resp.durationSeconds?.toFixed(3)}s)`;
        }
      } else {
        resOut.textContent = `[SIMULAÇÃO OK] Ferramenta ${toolName} executada no servidor ${serverId}. Args: ${JSON.stringify(parsedArgs)}`;
      }
    } catch (e) {
      resOut.textContent = `[EXCEÇÃO] ${e}`;
    }
  }

  private escapeHtml(str: string): string {
    return (str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
