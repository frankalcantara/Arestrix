import { Graph } from '../../core/Graph.ts';
import { GraphvizGenerator } from '../../core/GraphvizGenerator.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Diálogo de Exportação e Visualização Graphviz (WASM)
 * 
 * - Renderização 100% offline via WebAssembly (@hpcc-js/wasm), sem depender de CDNs externas.
 * - Prévia interativa do SVG gerado com controle de zoom e navegação.
 * - Exportação direta para arquivos .svg e .dot, cópia para área de transferência e nova aba.
 * - Estrita aderência à regra: Zero sombras/glows em fontes.
 */
export class GraphvizExportModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private renderedSvg: string = '';
  private currentDot: string = '';
  private zoomLevel: number = 1.0;
  private isLoading: boolean = false;
  private errorMessage: string = '';
  private activeTab: 'preview' | 'dot' = 'preview';

  constructor(graph: Graph) {
    this.graph = graph;

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-export-graphviz';
      this.modal.style.display = 'none';
      this.modal.addEventListener('click', (e: MouseEvent) => {
        if (e.target === this.modal) this.hide();
      });
      document.body.appendChild(this.modal);
      this.render();
    }
  }

  public async show(): Promise<void> {
    if (!this.modal) return;
    this.modal.style.display = 'flex';
    // O diálogo foi montado oculto; mede novamente depois de exibi-lo para
    // centralizar com as dimensões reais (largura/altura em % do viewport).
    this.windowController?.center();
    this.windowController?.bringToFront();
    await this.refreshRender();
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  public async refreshRender(): Promise<void> {
    this.isLoading = true;
    this.errorMessage = '';
    this.zoomLevel = 1.0;
    this.updateContentArea();

    try {
      this.currentDot = GraphvizGenerator.generateDot(this.graph);
      this.renderedSvg = await GraphvizGenerator.renderDotToSvg(this.currentDot);
      this.isLoading = false;
      this.updateContentArea();
    } catch (err: any) {
      console.error('[GraphvizExportModal] Erro ao renderizar Graphviz WASM:', err);
      this.isLoading = false;
      this.errorMessage = err?.message || 'Falha ao processar diagrama DOT com Graphviz WASM.';
      this.updateContentArea();
    }

    const dotArea = this.modal?.querySelector('#graphviz-dot-source') as HTMLTextAreaElement;
    if (dotArea) {
      dotArea.value = this.currentDot;
    }
  }

  private render(): void {
    if (!this.modal) return;

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Arestrix Studio - Arquitetura de Grafo (Graphviz)',
      subtitle: 'Visualização e exportação vetorial de arquitetura concorrente Taskflow C++20',
      icon: '🖼️',
      closeBtnId: 'btn-close-graphviz-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 920px; width: 92%; height: 82vh; display: flex; flex-direction: column; background: #ffffff; border-radius: 8px; box-shadow: 0 10px 25px rgba(0,0,0,0.2); overflow: hidden;">
        ${titleBarHtml}

        <!-- Abas Estilo Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'preview' ? 'active' : ''}" data-tab="preview" role="tab" aria-selected="${this.activeTab === 'preview'}">
            ${t('graphviz_export.tab_preview', 'Diagrama SVG')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'dot' ? 'active' : ''}" data-tab="dot" role="tab" aria-selected="${this.activeTab === 'dot'}">
            ${t('graphviz_export.tab_dot', 'Código Fonte DOT')}
          </button>
        </div>

        <!-- Aba 1: Diagrama SVG -->
        <div id="pane-tab-preview" class="tab-pane" style="${this.activeTab === 'preview' ? '' : 'display: none;'} flex: 1; display: flex; flex-direction: column; overflow: hidden;">
          <!-- Barra de Ações Superior Estilo Office -->
          <div style="background: #f8fafc; border-bottom: 1px solid #e2e8f0; padding: 10px 16px; display: flex; justify-content: space-between; align-items: center; gap: 8px; flex-wrap: wrap;">
            <div style="display: flex; gap: 8px; align-items: center;">
              <button id="btn-graphviz-download-svg" class="tool-btn btn-action btn-primary-office" style="height: 32px; padding: 4px 14px; font-size: 12.5px; border-radius: 6px; cursor: pointer; display: inline-flex; align-items: center; justify-content: center;">
                ${t('graphviz_export.btn_download_svg', 'Baixar SVG')}
              </button>
              <button id="btn-graphviz-download-dot" class="tool-btn btn-secondary btn-secondary-office" style="height: 32px; padding: 4px 12px; font-size: 12px; border-radius: 6px; cursor: pointer;">
                ${t('graphviz_export.btn_download_dot', 'Baixar DOT')}
              </button>
              <button id="btn-graphviz-copy-svg" class="tool-btn btn-secondary btn-secondary-office" style="height: 32px; padding: 4px 12px; font-size: 12px; border-radius: 6px; cursor: pointer;">
                ${t('graphviz_export.btn_copy_svg', 'Copiar SVG')}
              </button>
              <button id="btn-graphviz-open-tab" class="tool-btn btn-secondary btn-secondary-office" style="height: 32px; padding: 4px 12px; font-size: 12px; border-radius: 6px; cursor: pointer;">
                ${t('graphviz_export.btn_open_tab', 'Nova Janela')}
              </button>
            </div>

            <!-- Controles de Zoom -->
            <div style="display: flex; gap: 4px; align-items: center;">
              <button id="btn-graphviz-zoom-out" title="Diminuir Zoom" style="width: 28px; height: 28px; border-radius: 4px; border: 1px solid #cbd5e1; background: #ffffff; font-size: 14px; font-weight: 700; cursor: pointer;">−</button>
              <span id="graphviz-zoom-label" style="font-size: 12px; font-weight: 600; color: #475569; min-width: 44px; text-align: center;">100%</span>
              <button id="btn-graphviz-zoom-in" title="Aumentar Zoom" style="width: 28px; height: 28px; border-radius: 4px; border: 1px solid #cbd5e1; background: #ffffff; font-size: 14px; font-weight: 700; cursor: pointer;">+</button>
              <button id="btn-graphviz-zoom-reset" title="Ajustar" style="height: 28px; padding: 0 8px; border-radius: 4px; border: 1px solid #cbd5e1; background: #ffffff; font-size: 11px; font-weight: 600; cursor: pointer; color: #475569;">100%</button>
            </div>
          </div>

          <!-- Área Central de Visualização -->
          <div id="graphviz-modal-viewport" style="flex: 1; overflow: auto; background: #f1f5f9; padding: 24px; display: flex; justify-content: center; align-items: center; position: relative;">
            <!-- Injetado dinamicamente via updateContentArea -->
          </div>
        </div>

        <!-- Aba 2: Código Fonte DOT -->
        <div id="pane-tab-dot" class="tab-pane" style="${this.activeTab === 'dot' ? '' : 'display: none;'} flex: 1; padding: 16px; display: flex; flex-direction: column; overflow: hidden; background: #ffffff;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
            <span style="font-size: 12px; color: #64748b; font-weight: 600;">Especificação DOT gerada a partir da topologia do grafo ativo:</span>
            <button id="btn-graphviz-copy-dot-source" class="tool-btn btn-secondary btn-secondary-office" style="height: 30px; padding: 4px 12px; font-size: 12px;">
              ${t('graphviz_export.btn_copy_dot', 'Copiar DOT')}
            </button>
          </div>
          <textarea id="graphviz-dot-source" class="office-textarea" readonly style="flex: 1; font-family: Consolas, 'Courier New', monospace; font-size: 12px; resize: none; background: #faf9f8; line-height: 1.5; border: 1px solid #d1d1d1; border-radius: 4px; padding: 12px;">${this.escapeHtml(this.currentDot || '')}</textarea>
        </div>

        <!-- Rodapé com Informações -->
        <div class="modal-footer office-modal-footer" style="padding: 8px 16px; display: flex; justify-content: space-between; align-items: center; font-size: 11.5px; color: #64748b;">
          <div>Motor: <strong>Graphviz C++ compilado para WebAssembly (@hpcc-js/wasm)</strong> · 100% offline</div>
          <div><button id="btn-graphviz-close-footer" class="tool-btn btn-secondary btn-secondary-office" style="padding: 4px 14px; font-size: 12px;">Fechar</button></div>
        </div>
      </div>
    `;

    this.windowController = ModalWindowManager.attach({
      dialog: this.modal.querySelector('.modal-window') as HTMLElement,
      minWidth: 540,
      minHeight: 380,
      resizable: true,
      draggable: true,
      onClose: () => this.hide()
    });

    this.bindEvents();
    this.updateContentArea();
  }

  private updateContentArea(): void {
    if (!this.modal) return;
    const vp = this.modal.querySelector('#graphviz-modal-viewport');
    if (!vp) return;

    if (this.isLoading) {
      vp.innerHTML = `
        <div style="display: flex; flex-direction: column; align-items: center; gap: 12px; color: #0f6cbd;">
          <div style="width: 36px; height: 36px; border: 3px solid #cbd5e1; border-top-color: #0f6cbd; border-radius: 50%; animation: spin 0.8s linear infinite;"></div>
          <div style="font-size: 13.5px; font-weight: 600; color: #1e293b;">Renderizando diagrama com Graphviz WASM...</div>
          <style>@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }</style>
        </div>
      `;
      return;
    }

    if (this.errorMessage) {
      vp.innerHTML = `
        <div style="max-width: 500px; background: #ffffff; border: 1px solid #fca5a5; border-radius: 8px; padding: 20px; box-shadow: 0 4px 12px rgba(0,0,0,0.06); text-align: center;">
          <div style="font-size: 28px; margin-bottom: 8px;">⚠️</div>
          <div style="font-size: 14px; font-weight: 700; color: #b91c1c; margin-bottom: 6px;">Não foi possível renderizar o diagrama</div>
          <div style="font-size: 12px; color: #64748b; margin-bottom: 16px; word-break: break-word;">${this.errorMessage}</div>
          <button id="btn-graphviz-retry" style="background: #0f6cbd; color: #ffffff; border: none; padding: 6px 16px; border-radius: 6px; font-size: 12px; font-weight: 600; cursor: pointer;">Tentar novamente</button>
        </div>
      `;
      vp.querySelector('#btn-graphviz-retry')?.addEventListener('click', () => this.refreshRender());
      return;
    }

    if (this.renderedSvg) {
      vp.innerHTML = `
        <div id="graphviz-svg-container" style="transform-origin: center center; transform: scale(${this.zoomLevel}); transition: transform 0.15s ease-out; background: #ffffff; border-radius: 8px; padding: 24px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); border: 1px solid #e2e8f0; display: inline-flex;">
          ${this.renderedSvg}
        </div>
      `;
      const svgEl = vp.querySelector('#graphviz-svg-container svg') as SVGElement | null;
      if (svgEl) {
        svgEl.style.maxWidth = '100%';
        svgEl.style.height = 'auto';
        svgEl.style.display = 'block';
      }
    } else {
      vp.innerHTML = `<div style="font-size: 13px; color: #64748b;">Nenhum vértice ou aresta no grafo para exibir.</div>`;
    }
  }

  private bindEvents(): void {
    if (!this.modal) return;

    // Alternância de abas
    this.modal.querySelectorAll('.modal-tabs .tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn as HTMLElement).getAttribute('data-tab') as 'preview' | 'dot';
        if (!tab) return;
        this.activeTab = tab;
        this.modal?.querySelectorAll('.modal-tabs .tab-btn').forEach(b => {
          const isCurrent = b.getAttribute('data-tab') === tab;
          b.classList.toggle('active', isCurrent);
          b.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
        });
        this.modal?.querySelectorAll('.tab-pane').forEach(p => {
          const el = p as HTMLElement;
          el.style.display = el.id === `pane-tab-${tab}` ? (tab === 'dot' ? 'flex' : 'flex') : 'none';
        });
      });
    });

    this.modal.querySelector('#btn-close-graphviz-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-graphviz-close-footer')?.addEventListener('click', () => this.hide());

    // Copiar DOT da aba de código
    this.modal.querySelector('#btn-graphviz-copy-dot-source')?.addEventListener('click', async () => {
      const dot = this.currentDot || GraphvizGenerator.generateDot(this.graph);
      try {
        if (navigator.clipboard) {
          await navigator.clipboard.writeText(dot);
          this.showToast('✓ Código DOT copiado para a área de transferência!');
        }
      } catch (e) {
        console.warn('Erro ao copiar DOT:', e);
      }
    });

    // 1. Baixar SVG
    this.modal.querySelector('#btn-graphviz-download-svg')?.addEventListener('click', () => {
      if (!this.renderedSvg) return;
      this.downloadFile('arestrix_architecture.svg', this.renderedSvg, 'image/svg+xml;charset=utf-8');
    });

    // 2. Baixar DOT
    this.modal.querySelector('#btn-graphviz-download-dot')?.addEventListener('click', () => {
      const dot = this.currentDot || GraphvizGenerator.generateDot(this.graph);
      this.downloadFile('arestrix_architecture.dot', dot, 'text/vnd.graphviz;charset=utf-8');
    });

    // 3. Copiar SVG
    this.modal.querySelector('#btn-graphviz-copy-svg')?.addEventListener('click', async () => {
      if (!this.renderedSvg) return;
      try {
        if (navigator.clipboard) {
          await navigator.clipboard.writeText(this.renderedSvg);
          this.showToast('✓ Código SVG copiado para a área de transferência!');
        }
      } catch (e) {
        console.warn('Erro ao copiar SVG:', e);
      }
    });

    // 4. Nova Janela Standalone
    this.modal.querySelector('#btn-graphviz-open-tab')?.addEventListener('click', () => {
      this.openStandaloneTab();
    });

    // 5. Controles de Zoom
    const updateZoom = (nextZoom: number) => {
      this.zoomLevel = Math.max(0.25, Math.min(nextZoom, 3.0));
      const lbl = this.modal?.querySelector('#graphviz-zoom-label');
      if (lbl) lbl.textContent = `${Math.round(this.zoomLevel * 100)}%`;
      const svgBox = this.modal?.querySelector('#graphviz-svg-container') as HTMLElement | null;
      if (svgBox) {
        svgBox.style.transform = `scale(${this.zoomLevel})`;
      }
    };

    this.modal.querySelector('#btn-graphviz-zoom-in')?.addEventListener('click', () => {
      updateZoom(this.zoomLevel + 0.2);
    });

    this.modal.querySelector('#btn-graphviz-zoom-out')?.addEventListener('click', () => {
      updateZoom(this.zoomLevel - 0.2);
    });

    this.modal.querySelector('#btn-graphviz-zoom-reset')?.addEventListener('click', () => {
      updateZoom(1.0);
    });
  }

  public openStandaloneTab(): void {
    const svgContent = this.renderedSvg || '';

    const html = `<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8">
  <title>Arestrix Graphviz Export</title>
  <style>
    body { margin: 0; padding: 0; background: #f8fafc; font-family: Inter, system-ui, -apple-system, sans-serif; display: flex; flex-direction: column; height: 100vh; }
    header { background: #ffffff; padding: 14px 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.08); display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #e2e8f0; }
    h1 { margin: 0; font-size: 16px; color: #0f172a; font-weight: 700; }
    .actions { display: flex; gap: 8px; }
    .btn { background: #0f6cbd; color: white; border: none; padding: 8px 14px; border-radius: 6px; font-weight: 600; font-size: 13px; cursor: pointer; transition: background 0.15s; }
    .btn:hover { background: #115ea3; }
    .btn-sec { background: #f1f5f9; color: #334155; border: 1px solid #cbd5e1; }
    .btn-sec:hover { background: #e2e8f0; }
    #graph-container { flex: 1; overflow: auto; padding: 24px; display: flex; justify-content: center; align-items: center; }
    #graph-container svg { max-width: 100%; height: auto; box-shadow: 0 4px 12px rgba(0,0,0,0.06); background: #ffffff; border-radius: 8px; padding: 16px; border: 1px solid #e2e8f0; }
  </style>
</head>
<body>
  <header>
    <h1>Arestrix Studio - Arquitetura de Grafo (Taskflow C++20)</h1>
    <div class="actions">
      <button class="btn btn-sec" id="btn-copy">Copiar SVG</button>
      <button class="btn" id="btn-download">Baixar SVG</button>
    </div>
  </header>
  <div id="graph-container">
    ${svgContent || '<div style="color: #64748b;">Nenhum elemento no grafo para renderizar.</div>'}
  </div>
  <script>
    document.getElementById("btn-download").addEventListener("click", () => {
      const svgEl = document.querySelector("#graph-container svg");
      const svgData = svgEl ? svgEl.outerHTML : "";
      if (!svgData) return;
      const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = "arestrix_architecture.svg";
      a.click();
      URL.revokeObjectURL(url);
    });
    document.getElementById("btn-copy").addEventListener("click", async () => {
      const svgEl = document.querySelector("#graph-container svg");
      const svgData = svgEl ? svgEl.outerHTML : "";
      if (!svgData) return;
      try {
        await navigator.clipboard.writeText(svgData);
        alert("SVG copiado para a área de transferência!");
      } catch (_) {}
    });
  </script>
</body>
</html>`;

    const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    window.open(url, '_blank');
  }

  private downloadFile(filename: string, content: string, mimeType: string): void {
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  }

  private showToast(msg: string): void {
    if (typeof document === 'undefined') return;
    const toast = document.createElement('div');
    toast.textContent = msg;
    toast.style.position = 'fixed';
    toast.style.bottom = '24px';
    toast.style.left = '50%';
    toast.style.transform = 'translateX(-50%)';
    toast.style.background = '#065f46';
    toast.style.color = '#ffffff';
    toast.style.padding = '8px 16px';
    toast.style.borderRadius = '6px';
    toast.style.fontSize = '12.5px';
    toast.style.fontWeight = '600';
    toast.style.boxShadow = '0 4px 12px rgba(0,0,0,0.15)';
    toast.style.zIndex = '99999';
    toast.style.transition = 'opacity 0.2s';
    document.body.appendChild(toast);
    setTimeout(() => {
      toast.style.opacity = '0';
      setTimeout(() => toast.remove(), 200);
    }, 2200);
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

