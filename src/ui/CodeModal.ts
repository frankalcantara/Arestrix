import { Graph } from '../core/Graph.ts';
import { ProjectManager } from '../core/ProjectManager.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';

type MonacoType = typeof import('monaco-editor');
type IStandaloneCodeEditor = import('monaco-editor').editor.IStandaloneCodeEditor;
type ITextModel = import('monaco-editor').editor.ITextModel;

let monacoInstance: MonacoType | null = null;
async function getMonaco(): Promise<MonacoType | null> {
  if (monacoInstance) return monacoInstance;
  if (typeof window === 'undefined') return null;
  try {
    monacoInstance = await import('monaco-editor');
    if (!(window as any).MonacoEnvironment) {
      (window as any).MonacoEnvironment = {
        getWorker: function () {
          const blob = new Blob([`self.onmessage = function () {};`], { type: 'application/javascript' });
          return new Worker(URL.createObjectURL(blob));
        }
      };
    }
    return monacoInstance;
  } catch (e) {
    console.warn('[CodeModal] Falha ao carregar Monaco Editor:', e);
    return null;
  }
}

export class CodeModal {
  private modalOverlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private currentTab: 'cpp' | 'msvc' | 'cmake' | 'makefile' = 'cpp';
  private theme: 'vs' | 'vs-dark' = 'vs';
  private monacoEditor: IStandaloneCodeEditor | null = null;
  private models: Partial<Record<'cpp' | 'msvc' | 'cmake' | 'makefile', ITextModel>> = {};
  private isRendered: boolean = false;
  private isSwitchingTab: boolean = false;
  private codeCache: { cpp: string; cmake: string; make: string; msvc_ps1: string } = {
    cpp: '',
    cmake: '',
    make: '',
    msvc_ps1: ''
  };

  constructor(graph?: Graph) {
    this.graph = graph || new Graph();

    if (typeof document !== 'undefined' && typeof document.createElement === 'function') {
      this.modalOverlay = document.createElement('div');
      this.modalOverlay.className = 'modal-overlay hidden';
      document.body?.appendChild(this.modalOverlay);
    } else {
      this.modalOverlay = {
        className: 'modal-overlay hidden',
        classList: {
          contains: (cls: string) => cls === 'hidden',
          add: () => {},
          remove: () => {}
        },
        querySelector: () => null,
        querySelectorAll: () => [],
        addEventListener: () => {}
      } as any;
    }

    // Configura MonacoEnvironment seguro para evitar falhas de worker
    if (typeof window !== 'undefined' && !(window as any).MonacoEnvironment) {
      (window as any).MonacoEnvironment = {
        getWorker: function () {
          const blob = new Blob([`self.onmessage = function () {};`], { type: 'application/javascript' });
          return new Worker(URL.createObjectURL(blob));
        }
      };
    }
  }

  public async show(initialTab: 'cpp' | 'msvc' | 'cmake' | 'makefile' = 'cpp'): Promise<void> {
    this.currentTab = initialTab;

    // 1. Tenta buscar do backend C++ (WebUI)
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined' && typeof webui.call === 'function') {
        // @ts-ignore
        const response = await webui.call('get_cpp_preview', '');
        if (response && response.trim()) {
          this.codeCache = JSON.parse(response);
        }
      }
    } catch (e) {
      console.warn('[CodeModal] WebUI indisponível, gerando C++20 localmente:', e);
    }

    // 2. Fallback dinâmico: gera código C++20 Taskflow a partir do grafo atual
    this.ensureFallbackCode();

    // 3. Renderiza a estrutura da janela uma única vez no DOM
    if (!this.isRendered) {
      this.render();
      this.isRendered = true;
    }

    this.modalOverlay.classList.remove('hidden');

    // 4. Inicializa ou sincroniza os modelos no Monaco Editor
    await this.initOrUpdateMonaco();

    // 5. Ativa e sincroniza a aba solicitada
    this.switchTab(initialTab);

    // 6. Maximiza para ocupar toda a área útil da tela
    if (this.windowController) {
      this.windowController.bringToFront();
      try {
        this.windowController.maximize();
      } catch (_) {}
    }

    // 7. Garante recálculo fluido do layout do Monaco
    setTimeout(() => {
      this.monacoEditor?.layout();
    }, 60);
  }

  public hide(): void {
    if (this.monacoEditor) {
      this.syncCurrentTabCode(this.monacoEditor.getValue());
    }
    this.modalOverlay.classList.add('hidden');
  }

  public destroy(): void {
    if (this.monacoEditor) {
      try {
        this.monacoEditor.dispose();
      } catch (_) {}
      this.monacoEditor = null;
    }
    if (this.models) {
      Object.values(this.models).forEach(m => {
        try { m?.dispose?.(); } catch (_) {}
      });
      this.models = {};
    }
    this.isRendered = false;
    if (this.modalOverlay && this.modalOverlay.parentNode) {
      this.modalOverlay.parentNode.removeChild(this.modalOverlay);
    }
  }

  public switchTab(tab: 'cpp' | 'msvc' | 'cmake' | 'makefile'): void {
    // 1. Salva o código digitado no editor na aba anterior
    if (this.monacoEditor) {
      this.syncCurrentTabCode(this.monacoEditor.getValue());
    }

    this.currentTab = tab;
    const activeCode = this.getActiveCode();
    const lang = this.getLanguageForTab(tab);

    // 2. Atualiza classes visuais dos botões de abas
    this.modalOverlay.querySelectorAll?.('.code-tab-btn')?.forEach(btn => {
      const btnTab = btn.getAttribute?.('data-tab');
      btn.classList?.toggle?.('active', btnTab === tab);
    });

    // 3. Atualiza os textos do rodapé
    this.updateFooterInfo(activeCode);

    // 4. Alterna o modelo no Monaco Editor mantendo o container DOM estável
    if (this.monacoEditor && monacoInstance) {
      let targetModel = this.models[tab];
      if (!targetModel) {
        targetModel = monacoInstance.editor.createModel(activeCode, lang);
        this.models[tab] = targetModel;
      }

      this.isSwitchingTab = true;
      this.monacoEditor.setModel(targetModel);
      this.isSwitchingTab = false;
      this.monacoEditor.layout();
    } else {
      // Fallback sem Monaco (testes/headless)
      const fallbackCode = this.modalOverlay.querySelector?.('#modal-code-content');
      if (fallbackCode) fallbackCode.textContent = activeCode;
    }
  }

  public toggleTheme(): void {
    this.theme = this.theme === 'vs' ? 'vs-dark' : 'vs';
    const isDark = this.theme === 'vs-dark';

    const dialog = this.modalOverlay.querySelector?.('.modal-dialog');
    if (dialog) {
      dialog.classList?.toggle?.('theme-dark', isDark);
    }

    const themeBtn = this.modalOverlay.querySelector?.('#btn-toggle-editor-theme');
    if (themeBtn) {
      themeBtn.innerHTML = isDark ? '☀️ Tema Claro' : '🌙 Tema Escuro';
    }

    if (monacoInstance) {
      monacoInstance.editor.setTheme(this.theme);
    }
  }

  private ensureFallbackCode(): void {
    const meta = this.graph.getMetadata();
    const nodes = this.graph.getAllNodes();
    const edges = this.graph.getAllEdges();
    const graphName = (meta.graphName || 'TaskflowPipeline').replace(/[^a-zA-Z0-9_]/g, '_');

    if (!this.codeCache.cpp || !this.codeCache.cpp.trim()) {
      let code = `// =========================================================================\n`;
      code += `// Arestrix Studio - Pipeline Concorrente Taskflow C++20\n`;
      code += `// Pipeline: ${meta.graphName || 'TaskflowPipeline'}\n`;
      code += `// Descrição: ${meta.description || 'Execução Paralela de Agentes com Taskflow'}\n`;
      code += `// Padrão C++: ISO C++20 (/std:c++20 /O2)\n`;
      code += `// =========================================================================\n\n`;
      code += `#include <taskflow/taskflow.hpp>\n`;
      code += `#include <iostream>\n`;
      code += `#include <string>\n`;
      code += `#include <vector>\n`;
      code += `#include <chrono>\n`;
      code += `#include <memory>\n\n`;

      code += `int main() {\n`;
      code += `    std::cout << "[Arestrix] Inicializando Motor Concorrente Taskflow C++20...\\n";\n\n`;
      code += `    tf::Executor executor;\n`;
      code += `    tf::Taskflow taskflow("${graphName}");\n\n`;

      if (nodes.length === 0) {
        code += `    // Exemplo de tarefa Taskflow (grafo vazio no momento)\n`;
        code += `    auto task_init = taskflow.emplace([]() {\n`;
        code += `        std::cout << "[Task] Executando rotina C++20 Taskflow...\\n";\n`;
        code += `    }).name("InitTask");\n\n`;
      } else {
        code += `    // 1. Definição de Vértices / Tarefas do Grafo\n`;
        nodes.forEach(node => {
          const varName = `task_${node.id.replace(/[^a-zA-Z0-9_]/g, '_')}`;
          const safeLabel = node.label.replace(/"/g, '\\"');
          code += `    auto ${varName} = taskflow.emplace([]() {\n`;
          code += `        std::cout << "[${safeLabel}] Executando nó de tipo '${node.type}'...\\n";\n`;
          code += `    }).name("${safeLabel}");\n`;
        });
        code += `\n`;

        if (edges.length > 0) {
          code += `    // 2. Dependências e Precedências de Execução\n`;
          edges.forEach(edge => {
            const srcVar = `task_${edge.sourceNodeId.replace(/[^a-zA-Z0-9_]/g, '_')}`;
            const tgtVar = `task_${edge.targetNodeId.replace(/[^a-zA-Z0-9_]/g, '_')}`;
            code += `    ${srcVar}.precede(${tgtVar});\n`;
          });
          code += `\n`;
        }
      }

      code += `    std::cout << "[Arestrix] Disparando execução no thread pool MSVC...\\n";\n`;
      code += `    auto start = std::chrono::steady_clock::now();\n`;
      code += `    executor.run(taskflow).wait();\n`;
      code += `    auto end = std::chrono::steady_clock::now();\n`;
      code += `    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n\n`;
      code += `    std::cout << "[Arestrix] Pipeline finalizado com sucesso em " << elapsed << " ms.\\n";\n`;
      code += `    return 0;\n`;
      code += `}\n`;
      this.codeCache.cpp = code;
    }

    if (!this.codeCache.msvc_ps1 || !this.codeCache.msvc_ps1.trim()) {
      let ps1 = `# =========================================================================\n`;
      ps1 += `# Arestrix Studio - Script de Compilação Rápida MSVC (C++20 Taskflow)\n`;
      ps1 += `# =========================================================================\n\n`;
      ps1 += `$ErrorActionPreference = "Stop"\n`;
      ps1 += `Write-Host "Iniciando compilação MSVC C++20..." -ForegroundColor Cyan\n\n`;
      ps1 += `# Flags recomendadas: /std:c++20, /O2 (otimização máxima), /EHsc (exceções RAII), /utf-8\n`;
      ps1 += `$Includes = "/I include /I ../include /I vendor"\n`;
      ps1 += `$Flags = "/std:c++20 /O2 /EHsc /utf-8 /W3 $Includes"\n`;
      ps1 += `$Output = "/Fe:Output/${graphName}.exe"\n\n`;
      ps1 += `if (-not (Test-Path "Output")) { New-Item -ItemType Directory -Path "Output" | Out-Null }\n\n`;
      ps1 += `Write-Host "Invocando cl.exe..." -ForegroundColor Yellow\n`;
      ps1 += `& cl.exe $Flags main.cpp $Output /link /nologo\n\n`;
      ps1 += `if ($LASTEXITCODE -eq 0) {\n`;
      ps1 += `    Write-Host "✅ Compilado com sucesso: Output/${graphName}.exe" -ForegroundColor Green\n`;
      ps1 += `} else {\n`;
      ps1 += `    Write-Host "❌ Falha na compilação do pipeline." -ForegroundColor Red\n`;
      ps1 += `}\n`;
      this.codeCache.msvc_ps1 = ps1;
    }

    if (!this.codeCache.cmake || !this.codeCache.cmake.trim()) {
      let cmake = `cmake_minimum_required(VERSION 3.20)\n`;
      cmake += `project(${graphName} LANGUAGES CXX)\n\n`;
      cmake += `set(CMAKE_CXX_STANDARD 20)\n`;
      cmake += `set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n`;
      cmake += `# Procura Taskflow moderno\n`;
      cmake += `find_package(Taskflow REQUIRED)\n\n`;
      cmake += `add_executable(${graphName} main.cpp)\n`;
      cmake += `target_link_libraries(${graphName} PRIVATE Taskflow::Taskflow)\n`;
      this.codeCache.cmake = cmake;
    }

    if (!this.codeCache.make || !this.codeCache.make.trim()) {
      let make = `CXX = cl.exe\n`;
      make += `CXXFLAGS = /std:c++20 /O2 /EHsc /utf-8 /I include\n`;
      make += `TARGET = Output/${graphName}.exe\n\n`;
      make += `all: $(TARGET)\n\n`;
      make += `$(TARGET): main.cpp\n`;
      make += `\t@if not exist Output mkdir Output\n`;
      make += `\t$(CXX) $(CXXFLAGS) main.cpp /Fe:$@ /link /nologo\n\n`;
      make += `clean:\n`;
      make += `\t@if exist Output rd /s /q Output\n`;
      this.codeCache.make = make;
    }
  }

  public getActiveCode(): string {
    if (this.monacoEditor) {
      const currentModel = this.monacoEditor.getModel();
      if (currentModel) return currentModel.getValue();
    }
    const targetModel = this.models[this.currentTab];
    if (targetModel) return targetModel.getValue();

    switch (this.currentTab) {
      case 'msvc':
        return this.codeCache.msvc_ps1;
      case 'cmake':
        return this.codeCache.cmake;
      case 'makefile':
        return this.codeCache.make;
      case 'cpp':
      default:
        return this.codeCache.cpp;
    }
  }

  public getAllCodes(): { cpp: string; cmake: string; make: string; msvc_ps1: string } {
    if (this.monacoEditor) {
      this.syncCurrentTabCode(this.monacoEditor.getValue());
    }
    return {
      cpp: this.models.cpp?.getValue?.() ?? this.codeCache.cpp,
      msvc_ps1: this.models.msvc?.getValue?.() ?? this.codeCache.msvc_ps1,
      cmake: this.models.cmake?.getValue?.() ?? this.codeCache.cmake,
      make: this.models.makefile?.getValue?.() ?? this.codeCache.make
    };
  }

  private getFilename(): string {
    switch (this.currentTab) {
      case 'msvc':
        return 'build_msvc.ps1';
      case 'cmake':
        return 'CMakeLists.txt';
      case 'makefile':
        return 'Makefile';
      case 'cpp':
      default:
        return 'main.cpp';
    }
  }

  public getOutputDir(): string {
    return ProjectManager.getInstance().getProjectOutputDir();
  }

  private getLanguageForTab(tab: string): string {
    switch (tab) {
      case 'msvc':
        return 'powershell';
      case 'cmake':
        return 'ini';
      case 'makefile':
        return 'makefile';
      case 'cpp':
      default:
        return 'cpp';
    }
  }

  private updateFooterInfo(activeCode: string): void {
    const fnEl = this.modalOverlay.querySelector?.('#code-file-name');
    if (fnEl) fnEl.textContent = this.getFilename();
    const langEl = this.modalOverlay.querySelector?.('#code-file-language');
    if (langEl) langEl.textContent = this.getLanguageForTab(this.currentTab).toUpperCase();
    this.updateLineCounter(activeCode);
  }

  private updateLineCounter(code: string): void {
    const counter = this.modalOverlay.querySelector?.('#code-line-counter');
    if (counter) {
      counter.textContent = `${code.split('\n').length} linhas (${code.length} bytes)`;
    }
  }

  private render(): void {
    const meta = this.graph.getMetadata();
    const filename = this.getFilename();
    const activeCode = this.getActiveCode();
    const isDark = this.theme === 'vs-dark';

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Exportador Taskflow C++20',
      subtitle: `Editor Monaco Integrado (Microsoft) | Compilador: ${meta.compilerConfig.compiler.toUpperCase()} (MSVC C++20 Taskflow)`,
      icon: '⚡',
      closeBtnId: 'btn-close-code-modal'
    });

    this.modalOverlay.innerHTML = `
      <div class="modal-dialog code-modal-fullscreen-dialog ${isDark ? 'theme-dark' : ''}">
        ${titleBarHtml}

        <!-- Barra de Abas Estilo IDE / Office -->
        <div class="modal-tabs-bar">
          <div class="modal-tabs-group">
            <button class="code-tab-btn ${this.currentTab === 'cpp' ? 'active' : ''}" data-tab="cpp">
              📄 main.cpp <span style="font-size: 10px; opacity: 0.8;">(C++20 Taskflow)</span>
            </button>
            <button class="code-tab-btn ${this.currentTab === 'msvc' ? 'active' : ''}" data-tab="msvc">
              📜 build_msvc.ps1 <span style="font-size: 10px; opacity: 0.8;">(PowerShell MSVC)</span>
            </button>
            <button class="code-tab-btn ${this.currentTab === 'cmake' ? 'active' : ''}" data-tab="cmake">
              ⚙️ CMakeLists.txt <span style="font-size: 10px; opacity: 0.8;">(CMake)</span>
            </button>
            <button class="code-tab-btn ${this.currentTab === 'makefile' ? 'active' : ''}" data-tab="makefile">
              🛠️ Makefile <span style="font-size: 10px; opacity: 0.8;">(NMake)</span>
            </button>
          </div>

          <div style="display: flex; align-items: center; gap: 8px;">
            <button id="btn-toggle-editor-theme" class="tool-btn btn-secondary" style="height: 30px; padding: 0 12px; font-size: 11.5px; font-weight: 600;">
              ${isDark ? '☀️ Tema Claro' : '🌙 Tema Escuro'}
            </button>
          </div>
        </div>

        <!-- Viewport do Editor Monaco Fullscreen -->
        <div class="code-editor-viewport">
          <div id="monaco-code-editor-container" style="width: 100%; height: 100%; position: absolute; inset: 0;"></div>
          <!-- Fallback semântico para ambientes sem suporte completo ao canvas/DOM -->
          <pre style="display: none;"><code id="modal-code-content" class="code-block">${this.escapeHtml(activeCode)}</code></pre>
        </div>

        <!-- Barra de Rodapé com Ações e Informações -->
        <div class="modal-footer">
          <div class="footer-info" style="display: flex; align-items: center; gap: 14px;">
            <span>Arquivo: <strong id="code-file-name">${filename}</strong></span>
            <span style="opacity: 0.4;">|</span>
            <span>Linguagem: <strong id="code-file-language">${this.getLanguageForTab(this.currentTab).toUpperCase()}</strong></span>
            <span style="opacity: 0.4;">|</span>
            <span id="code-line-counter" style="color: #64748b;">${activeCode.split('\n').length} linhas (${activeCode.length} bytes)</span>
          </div>

          <div class="footer-actions" style="display: flex; align-items: center; gap: 8px;">
            <button id="btn-open-vscode" class="tool-btn btn-secondary" style="height: 32px; padding: 0 14px; font-size: 12px; font-weight: 600; display: inline-flex; align-items: center; gap: 6px;">
              <span>💻</span> Abrir no VS Code
            </button>
            <button id="btn-download-code-file" class="tool-btn btn-secondary" style="height: 32px; padding: 0 14px; font-size: 12px; font-weight: 600;">
              💾 Baixar Arquivo
            </button>
            <button id="btn-copy-code" class="tool-btn btn-secondary" style="height: 32px; padding: 0 14px; font-size: 12px; font-weight: 600;">
              📋 Copiar Código
            </button>
            <button id="btn-generate-disk" class="tool-btn btn-action" style="height: 32px; padding: 0 16px; font-size: 12px; font-weight: 700; background: #0f6cbd; color: #ffffff;">
              🚀 Gerar
            </button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.modalOverlay.querySelector?.('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 640,
        minHeight: 440,
        allowMaximize: true,
        onClose: () => this.hide(),
        onResize: () => {
          this.monacoEditor?.layout();
        },
        onMaximize: () => {
          setTimeout(() => this.monacoEditor?.layout(), 50);
        }
      });
    }

    this.bindEvents();
  }

  private async initOrUpdateMonaco(): Promise<void> {
    const container = this.modalOverlay.querySelector?.('#monaco-code-editor-container') as HTMLElement;
    if (!container) return;

    const monaco = await getMonaco();
    if (!monaco) {
      const fallbackPre = this.modalOverlay.querySelector?.('pre') as HTMLElement;
      if (fallbackPre && fallbackPre.style) fallbackPre.style.display = 'block';
      const fallbackCode = this.modalOverlay.querySelector?.('#modal-code-content') as HTMLElement;
      if (fallbackCode) fallbackCode.textContent = this.getActiveCode();
      return;
    }

    try {
      if (!this.monacoEditor) {
        // 1. Cria os modelos para as 4 abas
        this.models.cpp = monaco.editor.createModel(this.codeCache.cpp, 'cpp');
        this.models.msvc = monaco.editor.createModel(this.codeCache.msvc_ps1, 'powershell');
        this.models.cmake = monaco.editor.createModel(this.codeCache.cmake, 'ini');
        this.models.makefile = monaco.editor.createModel(this.codeCache.make, 'makefile');

        // 2. Instancia o Monaco Editor com o modelo da aba atual
        const currentModel = this.models[this.currentTab] || this.models.cpp;
        this.monacoEditor = monaco.editor.create(container, {
          model: currentModel,
          theme: this.theme,
          readOnly: false,
          automaticLayout: true,
          lineNumbers: 'on',
          minimap: { enabled: true },
          scrollBeyondLastLine: false,
          fontSize: 13.5,
          fontFamily: "'Cascadia Code', 'Fira Code', 'Consolas', 'Courier New', monospace",
          tabSize: 2,
          renderWhitespace: 'selection',
          wordWrap: 'on',
          folding: true,
          cursorBlinking: 'smooth',
          smoothScrolling: true
        });

        // 3. Registra sincronização de digitação do usuário
        this.monacoEditor.onDidChangeModelContent(() => {
          if (this.isSwitchingTab) return;
          const val = this.monacoEditor?.getValue() || '';
          this.syncCurrentTabCode(val);
          this.updateLineCounter(val);
        });
      } else {
        // Atualiza os modelos caso novos códigos tenham sido carregados no cache
        if (this.models.cpp && this.codeCache.cpp && this.models.cpp.getValue() !== this.codeCache.cpp) {
          this.models.cpp.setValue(this.codeCache.cpp);
        }
        if (this.models.msvc && this.codeCache.msvc_ps1 && this.models.msvc.getValue() !== this.codeCache.msvc_ps1) {
          this.models.msvc.setValue(this.codeCache.msvc_ps1);
        }
        if (this.models.cmake && this.codeCache.cmake && this.models.cmake.getValue() !== this.codeCache.cmake) {
          this.models.cmake.setValue(this.codeCache.cmake);
        }
        if (this.models.makefile && this.codeCache.make && this.models.makefile.getValue() !== this.codeCache.make) {
          this.models.makefile.setValue(this.codeCache.make);
        }

        monaco.editor.setTheme(this.theme);
        this.monacoEditor.layout();
      }
    } catch (e) {
      console.warn('[CodeModal] Inicialização do Monaco em fallback gracioso:', e);
      const fallbackPre = this.modalOverlay.querySelector?.('pre') as HTMLElement;
      if (fallbackPre && fallbackPre.style) fallbackPre.style.display = 'block';
      const fallbackCode = this.modalOverlay.querySelector?.('#modal-code-content') as HTMLElement;
      if (fallbackCode) fallbackCode.textContent = this.getActiveCode();
    }
  }

  private syncCurrentTabCode(code: string): void {
    switch (this.currentTab) {
      case 'msvc':
        this.codeCache.msvc_ps1 = code;
        break;
      case 'cmake':
        this.codeCache.cmake = code;
        break;
      case 'makefile':
        this.codeCache.make = code;
        break;
      case 'cpp':
      default:
        this.codeCache.cpp = code;
        break;
    }
  }

  private bindEvents(): void {
    // Fechar Modal
    this.modalOverlay.querySelector?.('#btn-close-code-modal')?.addEventListener('click', () => this.hide());
    this.modalOverlay.addEventListener?.('click', (e) => {
      if (e.target === this.modalOverlay) this.hide();
    });

    // Tecla Escape para fechar
    const onKeyDown = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && !this.modalOverlay.classList.contains('hidden')) {
        this.hide();
      }
    };
    if (typeof window !== 'undefined') {
      window.addEventListener('keydown', onKeyDown);
    }

    // Abas de Código
    const tabBtns = this.modalOverlay.querySelectorAll?.('.code-tab-btn');
    tabBtns?.forEach(btn => {
      btn.addEventListener?.('click', (e) => {
        const target = (e.currentTarget as HTMLElement)?.getAttribute?.('data-tab') as any;
        if (target) {
          this.switchTab(target);
        }
      });
    });

    // Alternar Tema (Claro / Escuro)
    this.modalOverlay.querySelector?.('#btn-toggle-editor-theme')?.addEventListener('click', () => {
      this.toggleTheme();
    });

    // Copiar Código
    const copyBtn = this.modalOverlay.querySelector?.('#btn-copy-code') as HTMLButtonElement;
    copyBtn?.addEventListener?.('click', () => {
      const code = this.getActiveCode();
      if (typeof navigator !== 'undefined' && navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(code).then(() => {
          const og = copyBtn.innerHTML;
          copyBtn.innerHTML = '✅ Copiado!';
          setTimeout(() => { copyBtn.innerHTML = og; }, 2000);
        });
      } else if (typeof alert !== 'undefined') {
        alert('Código copiado para a área de transferência!');
      }
    });

    // Baixar Arquivo
    this.modalOverlay.querySelector?.('#btn-download-code-file')?.addEventListener('click', () => {
      const code = this.getActiveCode();
      const filename = this.getFilename();
      if (typeof Blob !== 'undefined' && typeof URL !== 'undefined') {
        const blob = new Blob([code], { type: 'text/plain;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
      }
    });

    // Abrir no VS Code
    this.modalOverlay.querySelector?.('#btn-open-vscode')?.addEventListener('click', async () => {
      const outputDir = this.getOutputDir();
      const filename = this.getFilename();
      const targetPath = `${outputDir}/${filename}`;
      try {
        let opened = false;
        // @ts-ignore
        if (typeof webui !== 'undefined' && typeof webui.call === 'function') {
          // @ts-ignore
          opened = await webui.call('open_in_vscode', targetPath);
        }
        if (!opened && typeof window !== 'undefined') {
          // Fallback via protocolo registrado vscode:// no SO
          window.location.href = `vscode://file/${encodeURIComponent(targetPath)}`;
        }
      } catch (err) {
        console.warn('[CodeModal] Erro ao abrir no VS Code:', err);
        if (typeof window !== 'undefined') {
          window.location.href = `vscode://file/${encodeURIComponent(targetPath)}`;
        }
      }
    });

    // Gerar na Pasta de Saída do Projeto via WebUI
    this.modalOverlay.querySelector?.('#btn-generate-disk')?.addEventListener('click', async () => {
      const outputDir = this.getOutputDir();
      try {
        // @ts-ignore
        if (typeof webui !== 'undefined' && typeof webui.call === 'function') {
          // @ts-ignore
          const ok = await webui.call('generate_cpp_code', JSON.stringify({ outputDir }));
          if (ok) {
            alert(`✅ Arquivos gerados com sucesso na pasta '${outputDir}'!`);
          } else {
            alert(`⚠️ O backend C++ relatou erro ao gerar arquivos na pasta '${outputDir}'.`);
          }
        } else {
          // Fallback: faz download direto do arquivo ativo
          const blob = new Blob([this.getActiveCode()], { type: 'text/plain;charset=utf-8' });
          const url = URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.href = url;
          a.download = this.getFilename();
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(url);
          alert(`Arquivos preparados! O arquivo ${this.getFilename()} foi baixado (pasta do projeto: '${outputDir}').`);
        }
      } catch (err) {
        alert('Erro ao gerar código no disco: ' + err);
      }
    });
  }

  private escapeHtml(unsafe: string): string {
    if (!unsafe) return '';
    return unsafe
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
