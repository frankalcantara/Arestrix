import { globalEventBus } from '../core/EventBus.ts';
import { IArtifactItem, OutputArtifactType } from '../core/types.ts';
import { t } from '../i18n/index.ts';

export class ConsolePanel {
  private container: HTMLElement;
  private contentEl!: HTMLElement;
  private telemetryEl!: HTMLElement;
  private artifactsEl!: HTMLElement;
  private statusBadge!: HTMLElement;
  private isCollapsed: boolean = false;
  private currentTab: 'build' | 'telemetry' | 'artifacts' = 'build';
  private defaultHeight: number = 240;
  private isResizing: boolean = false;

  // Estado dos Artefatos
  private artifacts: IArtifactItem[] = [];
  private selectedArtifactId: string | null = null;
  private artifactViewMode: 'visual' | 'raw' = 'visual';
  private svgZoom: number = 1.0;
  private sheetFilter: string = '';

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
      <div class="office-console-splitter" id="console-splitter" title="${t('console.splitter_tip', 'Arraste para redimensionar ou dê duplo clique para recolher/expandir')}">
        <div class="splitter-grip"></div>
      </div>

      <div class="console-header office-console-header">
        <div class="office-console-tabs-group" role="tablist">
          <button class="office-console-tab-btn active" id="tab-console-build" data-tab="build" role="tab" aria-selected="true">
            <svg class="tab-icon-svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="4 17 10 11 4 5"></polyline><line x1="12" y1="19" x2="20" y2="19"></line></svg>
            <span>${t('console.tab_terminal', 'Terminal MSVC C++20')}</span>
          </button>
          <button class="office-console-tab-btn" id="tab-console-telemetry" data-tab="telemetry" role="tab" aria-selected="false">
            <svg class="tab-icon-svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 12h-4l-3 9L9 3l-3 9H2"></path></svg>
            <span>${t('console.tab_telemetry', 'Telemetria em Tempo Real')}</span>
          </button>
          <button class="office-console-tab-btn" id="tab-console-artifacts" data-tab="artifacts" role="tab" aria-selected="false">
            <svg class="tab-icon-svg" width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"></path><polyline points="3.27 6.96 12 12.01 20.73 6.96"></polyline><line x1="12" y1="22.08" x2="12" y2="12"></line></svg>
            <span>${t('console.tab_artifacts', 'Artefatos')}</span>
            <span class="office-artifact-badge-count" id="artifacts-badge-count" style="display: none;">0</span>
          </button>
          <span class="console-status-badge status-idle" id="console-status"><span class="status-dot">●</span> ${t('console.status_idle', 'OCIOSO')}</span>
        </div>

        <div class="console-actions office-console-actions">
          <button id="btn-copy-console" class="btn-console-office" title="${t('console.copy_tip', 'Copiar para a Área de Transferência')}">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>
            <span>${t('console.copy', 'Copiar')}</span>
          </button>
          <button id="btn-clear-console" class="btn-console-office" title="${t('console.clear_tip', 'Limpar')}">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path></svg>
            <span>${t('console.clear', 'Limpar')}</span>
          </button>
          <button id="btn-toggle-console" class="btn-console-office" title="${t('console.toggle_tip', 'Minimizar/Restaurar')}">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><line x1="5" y1="12" x2="19" y2="12"></line></svg>
            <span>${t('console.minmax', 'Min/Max')}</span>
          </button>
        </div>
      </div>

      <div class="console-views-wrapper">
        <div class="console-body office-console-body active" id="console-body">
          <div class="console-line line-info">${t('console.ready_line', 'Taskflow C++20 Engine pronto. Pressione F5 para compilar e executar o pipeline.')}</div>
        </div>
        <div class="console-body office-console-body" id="console-telemetry-body" style="display: none;">
          <div class="console-line line-muted">${t('console.telemetry_waiting', 'Aguardando inicialização de agentes para transmissão de telemetria...')}</div>
        </div>
        <div class="console-body office-console-body office-artifacts-body" id="console-artifacts-body" style="display: none; padding: 0;">
          <!-- O conteúdo dos artefatos é renderizado dinamicamente por renderArtifactsView() -->
        </div>
      </div>
    `;

    this.contentEl = this.container.querySelector('#console-body') as HTMLElement;
    this.telemetryEl = this.container.querySelector('#console-telemetry-body') as HTMLElement;
    this.artifactsEl = this.container.querySelector('#console-artifacts-body') as HTMLElement;
    this.statusBadge = this.container.querySelector('#console-status') as HTMLElement;

    // Abas
    const tabBuild = this.container.querySelector('#tab-console-build') as HTMLButtonElement;
    const tabTelem = this.container.querySelector('#tab-console-telemetry') as HTMLButtonElement;
    const tabArt = this.container.querySelector('#tab-console-artifacts') as HTMLButtonElement;

    tabBuild?.addEventListener('click', () => this.switchTab('build'));
    tabTelem?.addEventListener('click', () => this.switchTab('telemetry'));
    tabArt?.addEventListener('click', () => this.switchTab('artifacts'));

    // Ações
    this.container.querySelector('#btn-clear-console')?.addEventListener('click', () => this.clear());
    this.container.querySelector('#btn-toggle-console')?.addEventListener('click', () => this.toggleCollapse());
    this.container.querySelector('#btn-copy-console')?.addEventListener('click', () => this.copyToClipboard());
  }

  public switchTab(tab: 'build' | 'telemetry' | 'artifacts'): void {
    this.currentTab = tab;
    const tabBuild = this.container.querySelector('#tab-console-build') as HTMLButtonElement;
    const tabTelem = this.container.querySelector('#tab-console-telemetry') as HTMLButtonElement;
    const tabArt = this.container.querySelector('#tab-console-artifacts') as HTMLButtonElement;

    tabBuild?.classList.toggle('active', tab === 'build');
    tabTelem?.classList.toggle('active', tab === 'telemetry');
    tabArt?.classList.toggle('active', tab === 'artifacts');

    tabBuild?.setAttribute('aria-selected', tab === 'build' ? 'true' : 'false');
    tabTelem?.setAttribute('aria-selected', tab === 'telemetry' ? 'true' : 'false');
    tabArt?.setAttribute('aria-selected', tab === 'artifacts' ? 'true' : 'false');

    this.contentEl.style.display = tab === 'build' ? 'block' : 'none';
    this.telemetryEl.style.display = tab === 'telemetry' ? 'block' : 'none';
    this.artifactsEl.style.display = tab === 'artifacts' ? 'flex' : 'none';

    if (tab === 'artifacts') {
      this.ensureVisible();
      if (this.artifacts.length === 0) {
        void this.loadArtifacts();
      } else {
        this.renderArtifactsView();
      }
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
    if (this.currentTab === 'artifacts') {
      const art = this.selectedArtifact();
      if (art && art.content) {
        navigator.clipboard?.writeText(art.content).then(() => {
          this.flashCopyBtn();
        });
      }
      return;
    }
    const target = this.currentTab === 'build' ? this.contentEl : this.telemetryEl;
    const text = target.innerText || target.textContent || '';
    if (navigator.clipboard) {
      navigator.clipboard.writeText(text).then(() => {
        this.flashCopyBtn();
      });
    }
  }

  private flashCopyBtn(): void {
    const copyBtn = this.container.querySelector('#btn-copy-console') as HTMLButtonElement;
    if (copyBtn) {
      const original = copyBtn.innerHTML;
      copyBtn.innerHTML = t('console.copied', 'Copiado!');
      setTimeout(() => { copyBtn.innerHTML = original; }, 1500);
    }
  }

  private bindEvents(): void {
    globalEventBus.on('pipeline:compiling', (msg: string) => {
      this.setStatus(t('console.status_compiling_msvc', '● COMPILANDO'), 'status-compiling');
      this.addLine(`[BUILD] ${msg}`, 'line-warning');
      this.ensureVisible();
    });

    globalEventBus.on('pipeline:running', (msg: string) => {
      this.setStatus(t('console.status_executing', '● EXECUTANDO'), 'status-running');
      this.addLine(`[RUN] ${msg}`, 'line-info');
    });

    globalEventBus.on('pipeline:log', (data: { line: string; isError?: boolean }) => {
      this.addLine(data.line, data.isError ? 'line-error' : 'line-output');

      // Detecção de entregáveis estruturados emitidos pelo pipeline [OUTPUT_DELIVERY]
      if (data.line.includes('[OUTPUT_DELIVERY]')) {
        try {
          const jsonStr = data.line.substring(data.line.indexOf('[OUTPUT_DELIVERY]') + '[OUTPUT_DELIVERY]'.length).trim();
          const parsed = JSON.parse(jsonStr);
          this.handleDeliveredArtifact(parsed);
        } catch (_) {}
      }
    });

    globalEventBus.on('pipeline:success', (data: { stdout: string; elapsedMs: number }) => {
      this.setStatus(t('console.status_success', { ms: data.elapsedMs }), 'status-success');
      if (data.stdout) {
        this.addLine(`----------------------------------------------------------------------`, 'line-muted');
        this.addLine(data.stdout, 'line-output');
        this.addLine(`----------------------------------------------------------------------`, 'line-muted');
      }
      this.addLine(t('console.exec_success', { ms: data.elapsedMs }), 'line-success');

      // Atualiza lista de artefatos após execução bem sucedida
      void this.loadArtifacts(true);
    });

    globalEventBus.on('pipeline:error', (data: { stage: string; error: string }) => {
      this.setStatus(t('console.status_failure', '● ERRO'), 'status-error');
      this.addLine(`[${t('console.stage_error', 'FALHA NA ETAPA')}: ${data.stage.toUpperCase()}]`, 'line-error');
      this.addLine(data.error, 'line-error');
    });

    // Telemetria Granular de Agentes em Tempo Real (Fase 10)
    globalEventBus.on('agent:telemetry', (ev: any) => {
      this.handleAgentTelemetry(ev);
    });

    // Abertura explícita solicitada pelo usuário (ex: botão na Ribbon ou Inspector)
    globalEventBus.on('console:open-artifacts', (payload?: { nodeId?: string; path?: string }) => {
      this.ensureVisible();
      this.switchTab('artifacts');
      if (payload?.nodeId) {
        const found = this.artifacts.find(a => a.nodeId === payload.nodeId);
        if (found) {
          this.selectArtifact(found.id);
          return;
        }
      }
      if (payload?.path) {
        const found = this.artifacts.find(a => a.path === payload.path || a.relativePath === payload.path);
        if (found) {
          this.selectArtifact(found.id);
          return;
        }
      }
      void this.loadArtifacts(true);
    });

    globalEventBus.on('locale:changed', () => {
      this.updateLabels();
      if (this.currentTab === 'artifacts') {
        this.renderArtifactsView();
      }
    });
  }

  private updateLabels(): void {
    const tabBuild = this.container.querySelector('#tab-console-build');
    if (tabBuild) tabBuild.innerHTML = `<span class="tab-icon">⚡</span> ${t('console.tab_terminal', 'Terminal MSVC C++20')}`;
    const tabTelem = this.container.querySelector('#tab-console-telemetry');
    if (tabTelem) tabTelem.innerHTML = `<span class="tab-icon">📡</span> ${t('console.tab_telemetry', 'Telemetria em Tempo Real')}`;
    const tabArt = this.container.querySelector('#tab-console-artifacts');
    if (tabArt) {
      const badge = this.artifacts.length > 0 ? `<span class="office-artifact-badge-count">${this.artifacts.length}</span>` : '';
      tabArt.innerHTML = `<span class="tab-icon">📦</span> ${t('console.tab_artifacts', 'Artefatos')} ${badge}`;
    }
    const copyBtn = this.container.querySelector('#btn-copy-console');
    if (copyBtn) copyBtn.innerHTML = `📋 ${t('console.copy', 'Copiar')}`;
    const clearBtn = this.container.querySelector('#btn-clear-console');
    if (clearBtn) clearBtn.innerHTML = `🗑️ ${t('console.clear', 'Limpar')}`;
  }

  // --- Sistema de Artefatos & Entregáveis (Fase Integrada) ---------------------

  private handleDeliveredArtifact(delivery: any): void {
    const src = delivery.source || '';
    const dst = delivery.destination || '';
    const p = dst || src;
    if (!p) return;

    const id = delivery.nodeId || `art_${Date.now()}`;
    const name = delivery.label || p.split(/[/\\]/).pop() || 'artefato';
    const type: OutputArtifactType = delivery.type || this.inferArtifactType(p);

    const existingIdx = this.artifacts.findIndex(a => a.path === p || a.id === id);
    const item: IArtifactItem = {
      id,
      nodeId: delivery.nodeId,
      name,
      path: p,
      relativePath: p,
      type
    };

    if (existingIdx >= 0) {
      this.artifacts[existingIdx] = { ...this.artifacts[existingIdx], ...item };
    } else {
      this.artifacts.push(item);
    }

    this.updateArtifactsBadge();
    void this.fetchArtifactContent(item);
  }

  private inferArtifactType(path: string): OutputArtifactType {
    const lower = path.toLowerCase();
    if (lower.endsWith('.svg')) return 'svg';
    if (lower.endsWith('.csv') || lower.endsWith('.tsv') || lower.endsWith('.xlsx')) return 'sheet';
    if (lower.endsWith('.pdf')) return 'pdf';
    if (lower.endsWith('.md')) return 'markdown';
    if (lower.endsWith('.json')) return 'json';
    if (lower.endsWith('.txt') || lower.endsWith('.log')) return 'text';
    if (lower.endsWith('.png') || lower.endsWith('.jpg') || lower.endsWith('.webp')) return 'image';
    return 'auto';
  }

  public async loadArtifacts(autoSelectFirst: boolean = false): Promise<void> {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    if (w && w.webui && typeof w.webui.call === 'function') {
      try {
        const raw = await w.webui.call('list_artifacts');
        const parsed = JSON.parse(raw);
        if (parsed.ok && Array.isArray(parsed.artifacts)) {
          const list: IArtifactItem[] = parsed.artifacts;
          // Mescla sem perder conteúdos já cacheados
          for (const item of list) {
            const existing = this.artifacts.find(a => a.path === item.path || a.id === item.id);
            if (existing && existing.content && !item.content) {
              item.content = existing.content;
            }
          }
          this.artifacts = list;
        }
      } catch (err) {
        console.warn('Falha ao listar artefatos via C++ backend:', err);
      }
    }

    // Se nenhum estiver na lista mas existir um scene.svg na pasta workspace/ do projeto conhecido
    if (this.artifacts.length === 0) {
      // Adiciona scene.svg padrão do Exemplo 1 se estiver presente
      this.artifacts.push({
        id: 'default_svg',
        name: 'scene.svg',
        path: 'workspace/scene.svg',
        relativePath: 'workspace/scene.svg',
        type: 'svg',
        description: 'Vetor gráfico animado SVG'
      });
    }

    this.updateArtifactsBadge();

    if (autoSelectFirst || !this.selectedArtifactId) {
      if (this.artifacts.length > 0) {
        this.selectedArtifactId = this.artifacts[0].id;
        await this.fetchArtifactContent(this.artifacts[0]);
      }
    }

    this.renderArtifactsView();
  }

  private updateArtifactsBadge(): void {
    const badge = this.container.querySelector('#artifacts-badge-count') as HTMLElement;
    if (!badge) return;
    if (this.artifacts.length > 0) {
      badge.textContent = String(this.artifacts.length);
      badge.style.display = 'inline-block';
    } else {
      badge.style.display = 'none';
    }
  }

  private selectedArtifact(): IArtifactItem | undefined {
    return this.artifacts.find(a => a.id === this.selectedArtifactId) ?? this.artifacts[0];
  }

  private async selectArtifact(id: string): Promise<void> {
    this.selectedArtifactId = id;
    const art = this.selectedArtifact();
    if (art && !art.content) {
      await this.fetchArtifactContent(art);
    }
    this.renderArtifactsView();
  }

  private async fetchArtifactContent(art: IArtifactItem): Promise<void> {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    if (w && w.webui && typeof w.webui.call === 'function') {
      try {
        const raw = await w.webui.call('get_artifact', art.path || art.relativePath);
        const parsed = JSON.parse(raw);
        if (parsed.ok && parsed.content) {
          art.content = parsed.content;
          art.sizeBytes = parsed.sizeBytes ?? art.sizeBytes;
        }
      } catch (_) {}
    }

    // Fallback de leitura via fetch caso rodando em dev server ou arquivo acessível
    if (!art.content) {
      try {
        const resp = await fetch(art.path || art.relativePath);
        if (resp.ok) {
          art.content = await resp.text();
        }
      } catch (_) {}
    }
  }

  private renderArtifactsView(): void {
    if (!this.artifactsEl) return;

    if (this.artifacts.length === 0) {
      this.artifactsEl.innerHTML = `
        <div class="office-artifacts-empty-state">
          <div class="empty-state-icon">📦</div>
          <div class="empty-state-title">${t('console.artifacts_empty_title', 'Nenhum entregável disponível')}</div>
          <div class="empty-state-desc">${t('console.artifacts_empty_desc', 'Adicione um nó de Saída (Output) ao pipeline ou execute o projeto (F5) para gerar e inspecionar os arquivos produzidos.')}</div>
          <button class="btn-console-office" id="btn-refresh-artifacts-empty" style="margin-top: 10px;">
            🔄 ${t('common.refresh', 'Atualizar')}
          </button>
        </div>
      `;
      this.artifactsEl.querySelector('#btn-refresh-artifacts-empty')?.addEventListener('click', () => {
        void this.loadArtifacts(true);
      });
      return;
    }

    const current = this.selectedArtifact();
    const hasMultiple = this.artifacts.length > 1;

    this.artifactsEl.innerHTML = `
      <div class="office-artifacts-layout">
        ${hasMultiple ? `
          <!-- Barra lateral com lista de arquivos entregáveis -->
          <aside class="office-artifacts-sidebar">
            <div class="office-artifacts-sidebar-header">
              <span>${t('console.artifacts_list_header', 'Entregáveis')} (${this.artifacts.length})</span>
              <button class="btn-icon-subtle" id="btn-refresh-artifacts" title="${t('common.refresh', 'Atualizar')}">🔄</button>
            </div>
            <div class="office-artifacts-nav">
              ${this.artifacts.map(a => `
                <button class="office-artifact-nav-item ${a.id === current?.id ? 'active' : ''}" data-artifact-id="${this.escapeHtml(a.id)}">
                  <span class="artifact-icon">${this.getArtifactIcon(a.type)}</span>
                  <div class="artifact-meta">
                    <span class="artifact-name" title="${this.escapeHtml(a.name)}">${this.escapeHtml(a.name)}</span>
                    <span class="artifact-sub">${this.formatBytes(a.sizeBytes)} • ${a.type.toUpperCase()}</span>
                  </div>
                </button>
              `).join('')}
            </div>
          </aside>
        ` : ''}

        <!-- Painel principal de visualização do artefato selecionado -->
        <main class="office-artifact-viewer-main">
          ${current ? this.renderArtifactViewer(current) : ''}
        </main>
      </div>
    `;

    // Conectar eventos dos botões da sidebar
    this.artifactsEl.querySelectorAll<HTMLElement>('.office-artifact-nav-item').forEach(item => {
      item.addEventListener('click', () => {
        const id = item.dataset.artifactId;
        if (id) void this.selectArtifact(id);
      });
    });

    this.artifactsEl.querySelector('#btn-refresh-artifacts')?.addEventListener('click', () => {
      void this.loadArtifacts();
    });

    // Conectar eventos da toolbar do visualizador
    this.bindViewerEvents(current);
  }

  private renderArtifactViewer(art: IArtifactItem): string {
    const isSvg = art.type === 'svg' || art.path.endsWith('.svg');
    const isSheet = art.type === 'sheet' || art.path.endsWith('.csv') || art.path.endsWith('.tsv') || art.path.endsWith('.xlsx');
    const isPdf = art.type === 'pdf' || art.path.endsWith('.pdf');
    const isJson = art.type === 'json' || art.path.endsWith('.json');
    const isText = art.type === 'text' || art.type === 'markdown' || art.path.endsWith('.txt') || art.path.endsWith('.md');

    return `
      <!-- Toolbar do Artefato -->
      <header class="office-artifact-toolbar">
        <div class="office-artifact-toolbar-left">
          <span class="artifact-current-icon">${this.getArtifactIcon(art.type)}</span>
          <span class="artifact-current-title">${this.escapeHtml(art.name)}</span>
          <span class="artifact-current-path" title="${this.escapeHtml(art.path)}">${this.escapeHtml(art.relativePath || art.path)}</span>
          ${art.sizeBytes ? `<span class="artifact-size-pill">${this.formatBytes(art.sizeBytes)}</span>` : ''}
        </div>

        <div class="office-artifact-toolbar-right">
          ${(isSvg || isSheet) ? `
            <div class="office-view-mode-toggle">
              <button class="office-mode-btn ${this.artifactViewMode === 'visual' ? 'active' : ''}" id="btn-mode-visual">
                ${isSvg ? '🖼️ Visual' : '📊 Tabela'}
              </button>
              <button class="office-mode-btn ${this.artifactViewMode === 'raw' ? 'active' : ''}" id="btn-mode-raw">
                📝 Código
              </button>
            </div>
          ` : ''}

          ${isSvg && this.artifactViewMode === 'visual' ? `
            <div class="office-zoom-controls">
              <button class="btn-console-office" id="btn-svg-zoom-out" title="${t('console.zoom_out', 'Reduzir')}">−</button>
              <span class="zoom-level-text" id="svg-zoom-label">${Math.round(this.svgZoom * 100)}%</span>
              <button class="btn-console-office" id="btn-svg-zoom-in" title="${t('console.zoom_in', 'Ampliar')}>+</button>
              <button class="btn-console-office" id="btn-svg-zoom-reset" title="${t('console.zoom_reset', '100%')}">100%</button>
            </div>
          ` : ''}

          <button class="btn-console-office" id="btn-open-explorer" title="${t('console.open_explorer_tip', 'Abrir pasta no Windows Explorer')}">
            📂 ${t('console.open_explorer', 'No Explorer')}
          </button>
          <button class="btn-console-office" id="btn-copy-artifact" title="${t('common.copy', 'Copiar Conteúdo')}">
            📋 ${t('common.copy', 'Copiar')}
          </button>
          <button class="btn-console-office" id="btn-download-artifact" title="${t('common.download', 'Salvar Como...')}">
            💾 ${t('common.download', 'Baixar')}
          </button>
        </div>
      </header>

      <!-- Conteúdo do Visualizador -->
      <div class="office-artifact-stage">
        ${this.renderArtifactContent(art, isSvg, isSheet, isPdf, isJson, isText)}
      </div>
    `;
  }

  private renderArtifactContent(
    art: IArtifactItem, 
    isSvg: boolean, 
    isSheet: boolean, 
    isPdf: boolean, 
    isJson: boolean, 
    isText: boolean
  ): string {
    if (!art.content && !isPdf) {
      return `
        <div class="artifact-loading-state">
          <div class="loading-spinner">⌛</div>
          <div>Carregando conteúdo do arquivo (${this.escapeHtml(art.path)})...</div>
        </div>
      `;
    }

    if (isSvg) {
      if (this.artifactViewMode === 'raw') {
        return `<pre class="office-raw-code-view"><code>${this.escapeHtml(art.content || '')}</code></pre>`;
      }
      return `
        <div class="office-svg-viewport" id="svg-interactive-stage">
          <div class="office-svg-scaler" style="transform: scale(${this.svgZoom}); transform-origin: center center;">
            ${art.content || '<div class="line-muted">SVG Vazio</div>'}
          </div>
        </div>
      `;
    }

    if (isSheet) {
      if (this.artifactViewMode === 'raw') {
        return `<pre class="office-raw-code-view"><code>${this.escapeHtml(art.content || '')}</code></pre>`;
      }
      return this.renderSheetGrid(art.content || '');
    }

    if (isPdf) {
      return `
        <div class="office-pdf-container">
          <div class="office-pdf-card">
            <span style="font-size: 32px;">📄</span>
            <div style="font-weight: 700; font-size: 14px; margin-top: 8px;">Documento PDF Gerado</div>
            <div style="color: #64748b; font-size: 12px; margin-top: 4px;">${this.escapeHtml(art.path)}</div>
            <button class="btn-console-office" id="btn-open-pdf-native" style="margin-top: 14px; padding: 6px 14px; font-weight: 700;">
              📂 Abrir PDF no Visualizador Padrão
            </button>
          </div>
        </div>
      `;
    }

    if (isJson) {
      let formatted = art.content || '';
      try {
        formatted = JSON.stringify(JSON.parse(formatted), null, 2);
      } catch (_) {}
      return `<pre class="office-raw-code-view"><code>${this.escapeHtml(formatted)}</code></pre>`;
    }

    if (isText || art.type === 'markdown') {
      return `
        <div class="office-text-document-view">
          <pre class="office-markdown-view"><code>${this.escapeHtml(art.content || '')}</code></pre>
        </div>
      `;
    }

    return `<pre class="office-raw-code-view"><code>${this.escapeHtml(art.content || '')}</code></pre>`;
  }

  private renderSheetGrid(csvContent: string): string {
    const lines = csvContent.split(/\r?\n/).filter(l => l.trim().length > 0);
    if (lines.length === 0) {
      return `<div class="line-muted" style="padding: 20px;">Tabela vazia.</div>`;
    }

    // Detectar delimitador (vírgula, ponto-e-vírgula ou tab)
    const firstLine = lines[0];
    let delim = ',';
    if (firstLine.includes(';') && (firstLine.split(';').length > firstLine.split(',').length)) {
      delim = ';';
    } else if (firstLine.includes('\t')) {
      delim = '\t';
    }

    const headers = firstLine.split(delim).map(h => h.trim().replace(/^["']|["']$/g, ''));
    const rows = lines.slice(1).map(l => l.split(delim).map(c => c.trim().replace(/^["']|["']$/g, '')));

    const filter = this.sheetFilter.toLowerCase();
    const filteredRows = filter 
      ? rows.filter(r => r.some(cell => cell.toLowerCase().includes(filter)))
      : rows;

    return `
      <div class="office-sheet-wrapper">
        <div class="office-sheet-toolbar">
          <input type="text" id="sheet-search-input" class="office-sheet-search" placeholder="Buscar nas linhas..." value="${this.escapeHtml(this.sheetFilter)}" />
          <span class="sheet-count-label">${filteredRows.length} de ${rows.length} linhas</span>
        </div>
        <div class="office-sheet-scroll">
          <table class="office-sheet-table">
            <thead>
              <tr>
                <th style="width: 40px; text-align: center;">#</th>
                ${headers.map(h => `<th>${this.escapeHtml(h)}</th>`).join('')}
              </tr>
            </thead>
            <tbody>
              ${filteredRows.map((row, i) => `
                <tr>
                  <td style="color: #94a3b8; text-align: center; font-size: 10px;">${i + 1}</td>
                  ${row.map(cell => `<td>${this.escapeHtml(cell)}</td>`).join('')}
                </tr>
              `).join('')}
            </tbody>
          </table>
        </div>
      </div>
    `;
  }

  private bindViewerEvents(art?: IArtifactItem): void {
    if (!art) return;

    // Alternância de modo (Visual <-> Código)
    this.artifactsEl.querySelector('#btn-mode-visual')?.addEventListener('click', () => {
      this.artifactViewMode = 'visual';
      this.renderArtifactsView();
    });
    this.artifactsEl.querySelector('#btn-mode-raw')?.addEventListener('click', () => {
      this.artifactViewMode = 'raw';
      this.renderArtifactsView();
    });

    // Zoom do SVG
    this.artifactsEl.querySelector('#btn-svg-zoom-in')?.addEventListener('click', () => {
      this.svgZoom = Math.min(4.0, this.svgZoom + 0.25);
      this.updateSvgTransform();
    });
    this.artifactsEl.querySelector('#btn-svg-zoom-out')?.addEventListener('click', () => {
      this.svgZoom = Math.max(0.25, this.svgZoom - 0.25);
      this.updateSvgTransform();
    });
    this.artifactsEl.querySelector('#btn-svg-zoom-reset')?.addEventListener('click', () => {
      this.svgZoom = 1.0;
      this.updateSvgTransform();
    });

    // Abrir no Explorer
    this.artifactsEl.querySelector('#btn-open-explorer')?.addEventListener('click', () => {
      void this.openInExplorer(art);
    });

    // Abrir PDF no nativo
    this.artifactsEl.querySelector('#btn-open-pdf-native')?.addEventListener('click', () => {
      void this.openInExplorer(art);
    });

    // Copiar
    this.artifactsEl.querySelector('#btn-copy-artifact')?.addEventListener('click', () => {
      if (art.content && navigator.clipboard) {
        navigator.clipboard.writeText(art.content).then(() => {
          this.flashCopyBtn();
        });
      }
    });

    // Baixar
    this.artifactsEl.querySelector('#btn-download-artifact')?.addEventListener('click', () => {
      this.triggerDownload(art);
    });

    // Busca na planilha
    const searchInput = this.artifactsEl.querySelector('#sheet-search-input') as HTMLInputElement;
    searchInput?.addEventListener('input', () => {
      this.sheetFilter = searchInput.value;
      const stage = this.artifactsEl.querySelector('.office-artifact-stage');
      if (stage) {
        stage.innerHTML = this.renderSheetGrid(art.content || '');
        this.bindSheetSearch(art);
      }
    });
  }

  private bindSheetSearch(art: IArtifactItem): void {
    const searchInput = this.artifactsEl.querySelector('#sheet-search-input') as HTMLInputElement;
    if (!searchInput) return;
    searchInput.focus();
    searchInput.setSelectionRange(searchInput.value.length, searchInput.value.length);
    searchInput.addEventListener('input', () => {
      this.sheetFilter = searchInput.value;
      const stage = this.artifactsEl.querySelector('.office-artifact-stage');
      if (stage) {
        stage.innerHTML = this.renderSheetGrid(art.content || '');
        this.bindSheetSearch(art);
      }
    });
  }

  private updateSvgTransform(): void {
    const scaler = this.artifactsEl.querySelector('.office-svg-scaler') as HTMLElement;
    const label = this.artifactsEl.querySelector('#svg-zoom-label');
    if (scaler) {
      scaler.style.transform = `scale(${this.svgZoom})`;
    }
    if (label) {
      label.textContent = `${Math.round(this.svgZoom * 100)}%`;
    }
  }

  private async openInExplorer(art: IArtifactItem): Promise<void> {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    if (w && w.webui && typeof w.webui.call === 'function') {
      try {
        await w.webui.call('open_output_folder', art.path || art.relativePath || '');
      } catch (err) {
        console.warn('Erro ao abrir pasta de artefatos no Explorer:', err);
      }
    }
  }

  private triggerDownload(art: IArtifactItem): void {
    const content = art.content || '';
    const blob = new Blob([content], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = art.name || 'artefato';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  private getArtifactIcon(type: OutputArtifactType): string {
    switch (type) {
      case 'svg': return '🖼️';
      case 'sheet': return '📊';
      case 'pdf': return '📄';
      case 'markdown': return '📝';
      case 'json': return '🧱';
      case 'text': return '📃';
      case 'image': return '🌄';
      default: return '📦';
    }
  }

  private formatBytes(bytes?: number): string {
    if (!bytes || bytes <= 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
  }

  // --- Telemetria e Terminal MSVC ---------------------------------------------

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
    } else if (this.currentTab === 'telemetry') {
      this.telemetryEl.innerHTML = '';
      globalEventBus.emit('telemetry:reset');
    }
    this.setStatus(t('console.status_idle', '● OCIOSO'), 'status-idle');
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

  private escapeHtml(text: string): string {
    return (text || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
