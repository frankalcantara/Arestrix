import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

/**
 * Arestrix Studio - Diálogo de Importação do Excel (Modernização de Planilhas)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportExcelModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private projectManager: ProjectManager;
  private windowController?: IModalWindowController;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;
    this.projectManager = ProjectManager.getInstance(globalEventBus);

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-import-excel';
      this.modal.style.display = 'none';
      this.modal.addEventListener('click', (e: MouseEvent) => {
        if (e.target === this.modal) this.hide();
      });
      document.body.appendChild(this.modal);
      this.render();
    }
  }

  public show(): void {
    if (this.modal) {
      this.render();
      this.modal.style.display = 'flex';
      this.windowController?.bringToFront();
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal) return;

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Importar Planilha do Excel',
      subtitle: 'Converta planilhas (.xlsx) em persistência no 🛠️ Workspace e agentes no 📦 Projeto.',
      icon: '📊',
      closeBtnId: 'btn-close-excel-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 760px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <!-- Seleção de Arquivo e Banco -->
          <div class="office-group" style="margin-bottom: 0;">
            <div style="display: grid; grid-template-columns: 2fr 1fr; gap: 12px; margin-bottom: 8px;">
              <div>
                <label class="office-label" for="excel-file-path">Arquivo da planilha:</label>
                <div style="display: flex; gap: 6px;">
                  <input type="text" id="excel-file-path" class="office-input" value="data/Planilha_Operacional.xlsx" style="flex: 1;">
                  <button id="btn-load-sample-excel" class="tool-btn btn-secondary" style="font-size: 12px; height: 32px; padding: 4px 12px; white-space: nowrap;">
                    Exemplo
                  </button>
                </div>
              </div>
              <div>
                <label class="office-label" for="excel-db-target">Banco de dados destino:</label>
                <select id="excel-db-target" class="office-select">
                  <option value="sqlite">SQLite WAL (local)</option>
                  <option value="postgres">PostgreSQL Relacional</option>
                </select>
              </div>
            </div>
          </div>

          <!-- Abas / Tabelas Inferidas -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">Tabelas e regras inferidas a partir das abas da planilha:</span>
            <div style="display: flex; flex-direction: column; gap: 6px; font-size: 12.5px;">
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                <strong>Aba "Transacoes"</strong> &mdash; Mapeada para tabela relacional <code>transacoes</code> (14 colunas)
              </div>
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                <strong>Aba "Clientes"</strong> &mdash; Mapeada para tabela relacional <code>clientes</code> (8 colunas)
              </div>
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                <strong>Aba "Regras_Calculo"</strong> &mdash; Mapeada para o agente de regras de negócio
              </div>
            </div>
          </div>

          <!-- Rede de Agentes a Serem Criados -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">Agentes gerados no grafo:</span>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px; font-size: 12px;">
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>1. Ingestão e Limpeza</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">Validação e carga no banco de dados.</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>2. Regras e Cálculos</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">Execução de cálculos em C++20.</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>3. Auditoria de Integridade</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">Validação de consistência e regras fiscais.</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>4. Relatórios e Exportação</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">Geração de artefatos e resumos analíticos.</p>
              </div>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">Pronto para sintetizar o banco e o grafo de agentes.</span>
          <div class="footer-actions">
            <button id="btn-cancel-excel" class="tool-btn btn-secondary">Cancelar</button>
            <button id="btn-synthesize-excel" class="tool-btn btn-action">Importar</button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.modal.querySelector?.('.modal-window') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 580,
        minHeight: 440,
        onClose: () => this.hide()
      });
    }

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal) return;
    this.modal.querySelector('#btn-close-excel-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-excel')?.addEventListener('click', () => this.hide());

    this.modal.querySelector('#btn-load-sample-excel')?.addEventListener('click', () => {
      const input = this.modal?.querySelector('#excel-file-path') as HTMLInputElement;
      if (input) input.value = 'data/Planilha_Financeira_Auditoria_Fiscal.xlsx';
    });

    this.modal.querySelector('#btn-synthesize-excel')?.addEventListener('click', () => {
      this.synthesizeSpreadsheetSystem();
    });
  }

  public synthesizeSpreadsheetSystem(): void {
    this.graph.clear();

    const n1 = this.graph.createNode('node_excel_ingest', '1. Ingestao_Limpeza_Excel', 'agent', -480, 80);
    const n2 = this.graph.createNode('node_excel_calc', '2. Calculo_Regras_Negocio', 'agent', -160, 80);
    const n3 = this.graph.createNode('node_excel_eval', '3. Auditoria_Consistencia', 'condition', 160, 80);
    const n4 = this.graph.createNode('node_excel_report', '4. Consolidacao_Relatorio', 'agent', 480, 160);

    this.graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);
    this.graph.createEdge(n2.id, n2.outputs[0].id, n3.id, n3.inputs[0].id);
    this.graph.createEdge(n3.id, n3.outputs[0].id, n4.id, n4.inputs[0].id);
    this.graph.createEdge(n3.id, n3.outputs[1].id, n1.id, n1.inputs[0].id);

    this.projectManager.updateProjectMetadata({
      database: {
        type: 'sqlite',
        filePath: 'storage/database.sqlite'
      }
    }, this.graph);

    if (this.viewport) {
      setTimeout(() => this.viewport?.zoomToFit(), 120);
    }

    this.hide();
  }
}
