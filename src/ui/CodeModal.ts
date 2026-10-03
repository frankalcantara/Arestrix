import { Graph } from '../core/Graph.ts';
import { TaskflowCppGenerator } from '../compiler/TaskflowCppGenerator.ts';

export class CodeModal {
  private modalOverlay: HTMLElement;
  private graph: Graph;
  private currentTab: 'cpp' | 'msvc' | 'cmake' | 'makefile' = 'cpp';

  constructor(graph: Graph) {
    this.graph = graph;

    this.modalOverlay = document.createElement('div');
    this.modalOverlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.modalOverlay);

    this.render();
  }

  show(): void {
    this.render();
    this.modalOverlay.classList.remove('hidden');
  }

  hide(): void {
    this.modalOverlay.classList.add('hidden');
  }

  private render(): void {
    const meta = this.graph.getMetadata();
    const cppCode = TaskflowCppGenerator.generateSource(this.graph);
    const cmakeCode = TaskflowCppGenerator.generateCMakeLists(this.graph);
    const makeCode = TaskflowCppGenerator.generateMakefile(this.graph);
    const msvcScripts = TaskflowCppGenerator.generateMsvcBuildScript(this.graph);

    let activeCode = cppCode;
    let filename = 'main.cpp';

    if (this.currentTab === 'msvc') {
      activeCode = msvcScripts.ps1;
      filename = 'build_msvc.ps1';
    } else if (this.currentTab === 'cmake') {
      activeCode = cmakeCode;
      filename = 'CMakeLists.txt';
    } else if (this.currentTab === 'makefile') {
      activeCode = makeCode;
      filename = 'Makefile';
    }

    this.modalOverlay.innerHTML = `
      <div class="modal-dialog">
        <div class="modal-header">
          <div class="modal-title-group">
            <h2 class="modal-title">Exportador Taskflow C++20</h2>
            <span class="compiler-badge">Compilador Alvo: ${meta.compilerConfig.compiler.toUpperCase()} (Windows MSVC)</span>
          </div>
          <button class="btn-close" id="btn-close-modal">&times;</button>
        </div>

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
            <button id="btn-copy-code" class="btn btn-secondary">📋 Copiar Código</button>
            <button id="btn-download-file" class="btn btn-primary">💾 Baixar ${filename}</button>
          </div>
        </div>
      </div>
    `;

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
        copyBtn.textContent = '✅ Copiado!';
        setTimeout(() => { copyBtn.textContent = '📋 Copiar Código'; }, 2000);
      });
    });

    const downloadBtn = this.modalOverlay.querySelector('#btn-download-file');
    downloadBtn?.addEventListener('click', () => {
      const blob = new Blob([activeCode], { type: 'text/plain;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = filename;
      a.click();
      URL.revokeObjectURL(url);
    });
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
