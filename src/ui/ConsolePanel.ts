import { globalEventBus } from '../core/EventBus.ts';

export class ConsolePanel {
  private container: HTMLElement;
  private contentEl!: HTMLElement;
  private statusBadge!: HTMLElement;
  private isCollapsed: boolean = false;

  constructor(parent: HTMLElement) {
    this.container = document.createElement('div');
    this.container.className = 'console-panel';
    parent.appendChild(this.container);

    this.render();
    this.bindEvents();
  }

  private render(): void {
    this.container.innerHTML = `
      <div class="console-header">
        <div class="console-title-group">
          <span class="console-title">TERMINAL DE COMPILAÇÃO & EXECUÇÃO (MSVC C++20)</span>
          <span class="console-status-badge" id="console-status">OCIOSO</span>
        </div>
        <div class="console-actions">
          <button id="btn-clear-console" class="btn-console" title="Limpar Console">🗑️ Limpar</button>
          <button id="btn-toggle-console" class="btn-console" title="Recolher / Expandir Console">_ Min/Max</button>
        </div>
      </div>
      <div class="console-body" id="console-body">
        <div class="console-line line-info">[Arestrix] Sistema pronto. Clique em "▶ Executar Pipeline" ou pressione F5 para compilar com MSVC e executar os agentes.</div>
      </div>
    `;

    this.contentEl = this.container.querySelector('#console-body') as HTMLElement;
    this.statusBadge = this.container.querySelector('#console-status') as HTMLElement;

    this.container.querySelector('#btn-clear-console')?.addEventListener('click', () => this.clear());
    this.container.querySelector('#btn-toggle-console')?.addEventListener('click', () => this.toggleCollapse());
  }

  private bindEvents(): void {
    globalEventBus.on('pipeline:compiling', (msg: string) => {
      this.setStatus('COMPILANDO MSVC...', 'status-compiling');
      this.addLine(`[BUILD] ${msg}`, 'line-warning');
      this.ensureVisible();
    });

    globalEventBus.on('pipeline:running', (msg: string) => {
      this.setStatus('EXECUTANDO...', 'status-running');
      this.addLine(`[RUN] ${msg}`, 'line-info');
    });

    globalEventBus.on('pipeline:success', (data: { stdout: string; elapsedMs: number }) => {
      this.setStatus(`SUCESSO (${data.elapsedMs}ms)`, 'status-success');
      this.addLine(`----------------------------------------------------------------------`, 'line-muted');
      this.addLine(data.stdout, 'line-output');
      this.addLine(`----------------------------------------------------------------------`, 'line-muted');
      this.addLine(`[OK] Execução concluída em ${data.elapsedMs} ms.`, 'line-success');
    });

    globalEventBus.on('pipeline:error', (data: { stage: string; error: string }) => {
      this.setStatus('FALHA', 'status-error');
      this.addLine(`[ERRO NO ESTÁGIO: ${data.stage.toUpperCase()}]`, 'line-error');
      this.addLine(data.error, 'line-error');
    });

    // Telemetria Granular de Agentes em Tempo Real (Fase 10)
    globalEventBus.on('agent:telemetry', (ev: any) => {
      this.handleAgentTelemetry(ev);
    });
  }

  private handleAgentTelemetry(ev: any): void {
    const timeStr = new Date(ev.timestamp || Date.now()).toLocaleTimeString('pt-BR');
    const p = ev.payload || {};
    const prefix = `[${timeStr}] [${ev.nodeId}]`;

    switch (ev.type) {
      case 'thinking': {
        const kb = p.thinkingChars ? (p.thinkingChars / 1024).toFixed(1) : '0';
        this.addLine(`${prefix} 🧠 Raciocinando (thinking)… ${kb} KB gerados`, 'line-thinking');
        break;
      }
      case 'tool_start': {
        this.addLine(`${prefix} 🔧 ${p.toolName || 'tool'} ${p.toolArgs || ''}`, 'line-tool-start');
        break;
      }
      case 'tool_end': {
        const ok = p.toolSuccess !== false;
        this.addLine(`${prefix} ${ok ? '✔' : '✖'} ${p.toolName || 'tool'} ${ok ? 'concluído' : 'FALHOU'}`, ok ? 'line-tool-ok' : 'line-tool-err');
        break;
      }
      case 'rate_limit': {
        this.addLine(`${prefix} ⚠️ [RATE LIMIT 429] Aguardando ${p.retrySeconds || 15}s para retry... ${p.detail || ''}`, 'line-rate-limit');
        break;
      }
      case 'status': {
        this.addLine(`${prefix} ⏱ Status: ${p.status} ${p.detail ? '- ' + p.detail : ''}`, 'line-info');
        break;
      }
      case 'log': {
        if (p.logLine) {
          this.addLine(`${prefix} ${p.logLine}`, 'line-default');
        }
        break;
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

  setStatus(text: string, className: string): void {
    this.statusBadge.textContent = text;
    this.statusBadge.className = `console-status-badge ${className}`;
  }

  clear(): void {
    this.contentEl.innerHTML = '';
    this.setStatus('OCIOSO', 'status-idle');
  }

  toggleCollapse(): void {
    this.isCollapsed = !this.isCollapsed;
    if (this.isCollapsed) {
      this.container.classList.add('collapsed');
    } else {
      this.container.classList.remove('collapsed');
    }
  }

  ensureVisible(): void {
    if (this.isCollapsed) {
      this.toggleCollapse();
    }
  }
}
