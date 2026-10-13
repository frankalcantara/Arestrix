import { globalEventBus } from '../core/EventBus.ts';
import { t } from '../i18n/index.ts';

export class ConsolePanel {
  private container: HTMLElement;
  private contentEl!: HTMLElement;
  private telemetryEl!: HTMLElement;
  private statusBadge!: HTMLElement;
  private isCollapsed: boolean = false;
  private currentTab: 'build' | 'telemetry' = 'build';
  private defaultHeight: number = 190;
  private isResizing: boolean = false;

  constructor(parent: HTMLElement) {
    this.container = document.createElement('div');
    this.container.className = 'console-panel office-docked-console';
    parent.appendChild(this.container);

    this.render();
    this.bindEvents();
    this.initResizer();
  }

  private render(): void {
    this.container.innerHTML = `
      <!-- Splitter de Redimensionamento do Painel Inferior -->
      <div class="office-console-splitter" id="console-splitter" title="${t('console.splitter_tip')}">
        <div class="splitter-grip"></div>
      </div>

      <div class="console-header office-console-header">
        <div class="office-console-tabs-group">
          <button class="office-console-tab-btn active" id="tab-console-build" data-tab="build">
            <span class="tab-icon">⚡</span> ${t('console.tab_terminal')}
          </button>
          <button class="office-console-tab-btn" id="tab-console-telemetry" data-tab="telemetry">
            <span class="tab-icon">📡</span> ${t('console.tab_telemetry')}
          </button>
          <span class="console-status-badge status-idle" id="console-status">${t('console.status_idle')}</span>
        </div>

        <div class="console-actions office-console-actions">
          <button id="btn-copy-console" class="btn-console-office" title="${t('console.copy_tip')}">
            📋 ${t('console.copy')}
          </button>
          <button id="btn-clear-console" class="btn-console-office" title="${t('console.clear_tip')}">
            🗑️ ${t('console.clear')}
          </button>
          <button id="btn-toggle-console" class="btn-console-office" title="${t('console.toggle_tip')}">
            _ ${t('console.minmax')}
          </button>
        </div>
      </div>

      <div class="console-views-wrapper">
        <div class="console-body office-console-body active" id="console-body">
          <div class="console-line line-info">${t('console.ready_line')}</div>
        </div>
        <div class="console-body office-console-body" id="console-telemetry-body" style="display: none;">
          <div class="console-line line-muted">${t('console.telemetry_waiting')}</div>
        </div>
      </div>
    `;

    this.contentEl = this.container.querySelector('#console-body') as HTMLElement;
    this.telemetryEl = this.container.querySelector('#console-telemetry-body') as HTMLElement;
    this.statusBadge = this.container.querySelector('#console-status') as HTMLElement;

    // Abas
    const tabBuild = this.container.querySelector('#tab-console-build') as HTMLButtonElement;
    const tabTelem = this.container.querySelector('#tab-console-telemetry') as HTMLButtonElement;

    tabBuild?.addEventListener('click', () => this.switchTab('build'));
    tabTelem?.addEventListener('click', () => this.switchTab('telemetry'));

    // Ações
    this.container.querySelector('#btn-clear-console')?.addEventListener('click', () => this.clear());
    this.container.querySelector('#btn-toggle-console')?.addEventListener('click', () => this.toggleCollapse());
    this.container.querySelector('#btn-copy-console')?.addEventListener('click', () => this.copyToClipboard());
  }

  private switchTab(tab: 'build' | 'telemetry'): void {
    this.currentTab = tab;
    const tabBuild = this.container.querySelector('#tab-console-build') as HTMLButtonElement;
    const tabTelem = this.container.querySelector('#tab-console-telemetry') as HTMLButtonElement;

    if (tab === 'build') {
      tabBuild.classList.add('active');
      tabTelem.classList.remove('active');
      this.contentEl.style.display = 'block';
      this.telemetryEl.style.display = 'none';
    } else {
      tabBuild.classList.remove('active');
      tabTelem.classList.add('active');
      this.contentEl.style.display = 'none';
      this.telemetryEl.style.display = 'block';
    }
  }

  private initResizer(): void {
    const splitter = this.container.querySelector('#console-splitter') as HTMLElement;
    if (!splitter) return;

    let startY = 0;
    let startH = 0;

    const onPointerMove = (e: PointerEvent) => {
      if (!this.isResizing) return;
      const dy = startY - e.clientY;
      const newHeight = Math.max(44, Math.min(window.innerHeight * 0.7, startH + dy));
      this.container.style.height = `${newHeight}px`;
      if (newHeight <= 50) {
        this.isCollapsed = true;
        this.container.classList.add('collapsed');
      } else {
        this.isCollapsed = false;
        this.container.classList.remove('collapsed');
        this.defaultHeight = newHeight;
      }
    };

    const onPointerUp = () => {
      if (this.isResizing) {
        this.isResizing = false;
        document.body.style.cursor = '';
        window.removeEventListener('pointermove', onPointerMove);
        window.removeEventListener('pointerup', onPointerUp);
      }
    };

    splitter.addEventListener('pointerdown', (e: PointerEvent) => {
      e.preventDefault();
      this.isResizing = true;
      startY = e.clientY;
      startH = this.container.offsetHeight;
      document.body.style.cursor = 'ns-resize';
      window.addEventListener('pointermove', onPointerMove);
      window.addEventListener('pointerup', onPointerUp);
    });

    splitter.addEventListener('dblclick', () => {
      this.toggleCollapse();
    });
  }

  private copyToClipboard(): void {
    const target = this.currentTab === 'build' ? this.contentEl : this.telemetryEl;
    const text = target.innerText || target.textContent || '';
    if (navigator.clipboard) {
      navigator.clipboard.writeText(text).then(() => {
        const copyBtn = this.container.querySelector('#btn-copy-console') as HTMLButtonElement;
        if (copyBtn) {
          const original = copyBtn.innerHTML;
          copyBtn.innerHTML = '✔ Copiado!';
          setTimeout(() => { copyBtn.innerHTML = original; }, 1500);
        }
      });
    }
  }

  private bindEvents(): void {
    globalEventBus.on('pipeline:compiling', (msg: string) => {
      this.setStatus('● COMPILANDO MSVC...', 'status-compiling');
      this.addLine(`[BUILD] ${msg}`, 'line-warning');
      this.ensureVisible();
    });

    globalEventBus.on('pipeline:running', (msg: string) => {
      this.setStatus('● EXECUTANDO...', 'status-running');
      this.addLine(`[RUN] ${msg}`, 'line-info');
    });

    globalEventBus.on('pipeline:success', (data: { stdout: string; elapsedMs: number }) => {
      this.setStatus(`● SUCESSO (${data.elapsedMs}ms)`, 'status-success');
      this.addLine(`----------------------------------------------------------------------`, 'line-muted');
      this.addLine(data.stdout, 'line-output');
      this.addLine(`----------------------------------------------------------------------`, 'line-muted');
      this.addLine(`[OK] Execução concluída com sucesso em ${data.elapsedMs} ms.`, 'line-success');
    });

    globalEventBus.on('pipeline:error', (data: { stage: string; error: string }) => {
      this.setStatus('● FALHA', 'status-error');
      this.addLine(`[ERRO NO ESTÁGIO: ${data.stage.toUpperCase()}]`, 'line-error');
      this.addLine(data.error, 'line-error');
    });

    // Telemetria Granular de Agentes em Tempo Real (Fase 10)
    globalEventBus.on('agent:telemetry', (ev: any) => {
      this.handleAgentTelemetry(ev);
    });

    globalEventBus.on('locale:changed', () => {
      this.updateLabels();
    });
  }

  private updateLabels(): void {
    const tabBuild = this.container.querySelector('#tab-console-build');
    if (tabBuild) tabBuild.innerHTML = `<span class="tab-icon">⚡</span> ${t('console.tab_terminal')}`;
    const tabTelem = this.container.querySelector('#tab-console-telemetry');
    if (tabTelem) tabTelem.innerHTML = `<span class="tab-icon">📡</span> ${t('console.tab_telemetry')}`;
    const copyBtn = this.container.querySelector('#btn-copy-console');
    if (copyBtn) copyBtn.innerHTML = `📋 ${t('console.copy')}`;
    const clearBtn = this.container.querySelector('#btn-clear-console');
    if (clearBtn) clearBtn.innerHTML = `🗑️ ${t('console.clear')}`;
  }

  private handleAgentTelemetry(ev: any): void {
    const timeStr = new Date(ev.timestamp || Date.now()).toLocaleTimeString('pt-BR');
    const p = ev.payload || {};
    const prefix = `[${timeStr}] [${ev.nodeId}]`;

    let text = '';
    let cls = 'line-default';

    switch (ev.type) {
      case 'thinking': {
        const kb = p.thinkingChars ? (p.thinkingChars / 1024).toFixed(1) : '0';
        text = `${prefix} 🧠 Raciocinando (thinking)… ${kb} KB gerados`;
        cls = 'line-thinking';
        break;
      }
      case 'tool_start': {
        text = `${prefix} 🔧 ${p.toolName || 'tool'} ${p.toolArgs || ''}`;
        cls = 'line-tool-start';
        break;
      }
      case 'tool_end': {
        const ok = p.toolSuccess !== false;
        text = `${prefix} ${ok ? '✔' : '✖'} ${p.toolName || 'tool'} ${ok ? 'concluído' : 'FALHOU'}`;
        cls = ok ? 'line-tool-ok' : 'line-tool-err';
        break;
      }
      case 'rate_limit': {
        text = `${prefix} ⚠️ [RATE LIMIT 429] Aguardando ${p.retrySeconds || 15}s para retry... ${p.detail || ''}`;
        cls = 'line-rate-limit';
        break;
      }
      case 'status': {
        text = `${prefix} ⏱ Status: ${p.status} ${p.detail ? '- ' + p.detail : ''}`;
        cls = 'line-info';
        break;
      }
      case 'log': {
        if (p.logLine) {
          text = `${prefix} ${p.logLine}`;
          cls = 'line-default';
        }
        break;
      }
    }

    if (text) {
      this.addTelemetryLine(text, cls);
      // Se não for build, adiciona também uma linha resumida no build se estiver ativo
      if (ev.type === 'tool_end' || ev.type === 'rate_limit') {
        this.addLine(text, cls);
      }
    }
  }

  addLine(text: string, className: string = 'line-default'): void {
    const line = document.createElement('div');
    line.className = `console-line ${className}`;
    line.textContent = text;
    this.contentEl.appendChild(line);
    this.contentEl.scrollTop = this.contentEl.scrollHeight;
  }

  addTelemetryLine(text: string, className: string = 'line-default'): void {
    const line = document.createElement('div');
    line.className = `console-line ${className}`;
    line.textContent = text;
    this.telemetryEl.appendChild(line);
    this.telemetryEl.scrollTop = this.telemetryEl.scrollHeight;
  }

  setStatus(text: string, className: string): void {
    this.statusBadge.textContent = text;
    this.statusBadge.className = `console-status-badge ${className}`;
  }

  clear(): void {
    if (this.currentTab === 'build') {
      this.contentEl.innerHTML = '';
    } else {
      this.telemetryEl.innerHTML = '';
    }
    this.setStatus('● OCIOSO', 'status-idle');
  }

  toggleCollapse(): void {
    this.isCollapsed = !this.isCollapsed;
    if (this.isCollapsed) {
      this.container.classList.add('collapsed');
      this.container.style.height = '42px';
    } else {
      this.container.classList.remove('collapsed');
      this.container.style.height = `${this.defaultHeight}px`;
    }
  }

  ensureVisible(): void {
    if (this.isCollapsed) {
      this.toggleCollapse();
    }
  }
}
