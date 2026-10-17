import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

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
      title: t('import_modals.excel_title'),
      subtitle: t('import_modals.excel_subtitle'),
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
                <label class="office-label" for="excel-file-path">${t('import_modals.excel_file_label')}</label>
                <div style="display: flex; gap: 6px;">
                  <input type="text" id="excel-file-path" class="office-input" value="data/Planilha_Operacional.xlsx" style="flex: 1;">
                  <button id="btn-load-sample-excel" class="tool-btn btn-secondary" style="font-size: 12px; height: 32px; padding: 4px 12px; white-space: nowrap;">
                    ${t('import_modals.excel_btn_sample')}
                  </button>
                </div>
              </div>
              <div>
                <label class="office-label" for="excel-db-target">${t('import_modals.excel_db_target_label')}</label>
                <select id="excel-db-target" class="office-select">
                  <option value="sqlite">${t('import_modals.db_sqlite_wal')}</option>
                  <option value="postgres">${t('import_modals.db_postgres')}</option>
                </select>
              </div>
            </div>
          </div>

          <!-- Abas / Tabelas Inferidas -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">${t('import_modals.excel_tables_title')}</span>
            <div style="display: flex; flex-direction: column; gap: 6px; font-size: 12.5px;">
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                ${t('import_modals.excel_tab_transacoes')}
              </div>
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                ${t('import_modals.excel_tab_clientes')}
              </div>
              <div style="background: #ffffff; padding: 6px 10px; border: 1px solid #d1d1d1; border-radius: 2px;">
                ${t('import_modals.excel_tab_regras')}
              </div>
            </div>
          </div>

          <!-- Rede de Agentes a Serem Criados -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">${t('import_modals.excel_agents_title')}</span>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 8px; font-size: 12px;">
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>${t('import_modals.excel_agent1_title')}</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">${t('import_modals.excel_agent1_desc')}</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>${t('import_modals.excel_agent2_title')}</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">${t('import_modals.excel_agent2_desc')}</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>${t('import_modals.excel_agent3_title')}</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">${t('import_modals.excel_agent3_desc')}</p>
              </div>
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px;">
                <strong>${t('import_modals.excel_agent4_title')}</strong>
                <p style="margin: 2px 0 0 0; color: #616161;">${t('import_modals.excel_agent4_desc')}</p>
              </div>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">${t('import_modals.excel_footer_info')}</span>
          <div class="footer-actions">
            <button id="btn-cancel-excel" class="tool-btn btn-secondary">${t('import_modals.btn_cancel')}</button>
            <button id="btn-synthesize-excel" class="tool-btn btn-action">${t('import_modals.btn_import')}</button>
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

    this.graph.autoLayout(false);

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
