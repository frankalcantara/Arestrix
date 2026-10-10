import { Graph } from '../core/Graph.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';

export class CodeModal {
  private modalOverlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private currentTab: 'cpp' | 'msvc' | 'cmake' | 'makefile' = 'cpp';
  
  private codeCache: any = { cpp: '', cmake: '', make: '', msvc_ps1: '' };

  constructor(graph: Graph) {
    this.graph = graph;

    this.modalOverlay = document.createElement('div');
    this.modalOverlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.modalOverlay);
  }

  async show(): Promise<void> {
    // Busca do C++ as strings prontas!
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        const response = await webui.call("get_cpp_preview", "");
        this.codeCache = JSON.parse(response);
      }
    } catch (e) {
      console.error("Erro ao buscar preview do C++", e);
    }
    
    this.render();
    this.modalOverlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  hide(): void {
    this.modalOverlay.classList.add('hidden');
  }

  private render(): void {
    const meta = this.graph.getMetadata();
    
    let activeCode = this.codeCache.cpp;
    let filename = 'main.cpp';

    if (this.currentTab === 'msvc') {
      activeCode = this.codeCache.msvc_ps1;
      filename = 'build_msvc.ps1';
    } else if (this.currentTab === 'cmake') {
      activeCode = this.codeCache.cmake;
      filename = 'CMakeLists.txt';
    } else if (this.currentTab === 'makefile') {
      activeCode = this.codeCache.make;
      filename = 'Makefile';
    }

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Exportador Taskflow C++20',
      subtitle: `Compilador Alvo: ${meta.compilerConfig.compiler.toUpperCase()} (Windows MSVC)`,
      icon: '⚡',
      closeBtnId: 'btn-close-modal'
    });

    this.modalOverlay.innerHTML = `
      <div class="modal-dialog">
        ${titleBarHtml}

        <div class="modal-tabs">
          <button class="tab-btn ${this.currentTab === 'cpp' ? 'active' : ''}" data-tab="cpp">main.cpp (C++20)</button>
          <button class="tab-btn ${this.currentTab === 'msvc' ? 'active' : ''}" data-tab="msvc">build_msvc.ps1 (MSVC cl.exe)</button>
          <button class="tab-btn ${this.currentTab === 'cmake' ? 'active' : ''}" data-tab="cmake">CMakeLists.txt</button>
          <button class="tab-btn ${this.currentTab === 'makefile' ? 'active' : ''}" data-tab="makefile">Makefile</button>
        </div>

        <div class="modal-body">
          <div class="code-container">
            <pre><code class="code-block" id="modal-code-content">${this.escapeHtml(activeCode)}</code></pre>
          </div>
        </div>

        <div class="modal-footer">
          <div class="footer-info">
            <span>Arquivo: <strong>${filename}</strong></span>
          </div>
          <div class="footer-actions">
            <button id="btn-copy-code" class="btn btn-secondary">📋 Copiar</button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.modalOverlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 540,
        minHeight: 380,
        onClose: () => this.hide()
      });
    }

    // Eventos
    this.modalOverlay.querySelector('#btn-close-modal')?.addEventListener('click', () => this.hide());
    this.modalOverlay.addEventListener('click', (e) => {
      if (e.target === this.modalOverlay) this.hide();
    });

    const tabs = this.modalOverlay.querySelectorAll('.tab-btn');
    tabs.forEach(tab => {
      tab.addEventListener('click', (e) => {
        const target = (e.currentTarget as HTMLElement).getAttribute('data-tab') as any;
        this.currentTab = target;
        this.render();
      });
    });

    const copyBtn = this.modalOverlay.querySelector('#btn-copy-code');
    copyBtn?.addEventListener('click', () => {
      navigator.clipboard.writeText(activeCode).then(() => {
        const ogText = copyBtn.innerHTML;
        copyBtn.innerHTML = '✅ Copiado!';
        setTimeout(() => copyBtn.innerHTML = ogText, 2000);
      });
    });
  }

  private escapeHtml(unsafe: string): string {
    if (!unsafe) return '';
    return unsafe
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }
}
