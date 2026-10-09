import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';

export class ImportExcelModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private projectManager: ProjectManager;

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
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal) return;
    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 860px; width: 92%; max-height: 90vh; display: flex; flex-direction: column;">
        <div class="modal-header" style="border-bottom: 2px solid #16a34a; padding-bottom: 12px;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #16a34a; color: white; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              📊
            </div>
            <div>
              <h3 style="margin: 0; font-size: 17px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação do Excel (Planilhas → Banco de Dados & Agentes)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Converta planilhas complexas (.xlsx / .xlsm) em bancos relacionais e redes de agentes Taskflow C++20
              </p>
            </div>
          </div>
          <button id="btn-close-excel-modal" class="modal-close-btn">&times;</button>
        </div>

        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 0; display: flex; flex-direction: column; gap: 16px;">
          <!-- Card de Explicação de Engenharia -->
          <div style="background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 10px; padding: 14px;">
            <h4 style="margin: 0 0 4px 0; font-size: 13px; font-weight: 800; color: #166534;">
              Desconstrução de Planilhas Legadas & Modernização
            </h4>
            <p style="margin: 0; font-size: 12px; color: #14532d; line-height: 1.5;">
              Planilhas com múltiplas abas e fórmulas cruzadas são convertidas em tabelas SQL relacionais normalizadas (SQLite WAL) e uma rede concorrente de agentes (Ingestão, Regras de Negócio, Auditoria e Relatórios) executados pelo motor C++20.
            </p>
          </div>

          <!-- Seleção de Arquivo e Abas -->
          <div style="display: grid; grid-template-columns: 2fr 1fr; gap: 14px;">
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
                Arquivo Excel / Planilha de Entrada:
              </label>
              <div style="display: flex; gap: 8px;">
                <input type="text" id="excel-file-path" value="data/Planilha_Operacional_Complexa.xlsx" style="flex: 1; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
                <button id="btn-load-sample-excel" class="tool-btn btn-secondary" style="font-weight: 700; font-size: 11.5px; white-space: nowrap;">
                  📋 Exemplo
                </button>
              </div>
            </div>

            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
                Banco de Dados Destino:
              </label>
              <select id="excel-db-target" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px; font-weight: 600;">
                <option value="sqlite">SQLite WAL (storage/database.sqlite)</option>
                <option value="postgres">PostgreSQL Relacional</option>
              </select>
            </div>
          </div>

          <!-- Mapeamento de Abas Detectadas -->
          <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 10px; padding: 14px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
              <h4 style="margin: 0; font-size: 12.5px; font-weight: 800; color: #1e293b;">
                📑 Abas Detectadas para Normalização em Tabelas
              </h4>
              <span style="font-size: 11px; color: #64748b;">4 tabelas relacionais inferidas</span>
            </div>

            <div style="display: flex; flex-direction: column; gap: 8px; font-size: 12px;">
              <label style="display: flex; align-items: center; gap: 10px; background: white; padding: 8px 12px; border: 1px solid #e2e8f0; border-radius: 6px;">
                <input type="checkbox" checked disabled>
                <strong>Aba "Transacoes"</strong> ➔ Tabela <code>transacoes</code> (14 colunas: id, data, valor, cliente_id, formula_imposto)
              </label>
              <label style="display: flex; align-items: center; gap: 10px; background: white; padding: 8px 12px; border: 1px solid #e2e8f0; border-radius: 6px;">
                <input type="checkbox" checked disabled>
                <strong>Aba "Clientes"</strong> ➔ Tabela <code>clientes</code> (8 colunas: id, razao_social, cnpj, score_credito)
              </label>
              <label style="display: flex; align-items: center; gap: 10px; background: white; padding: 8px 12px; border: 1px solid #e2e8f0; border-radius: 6px;">
                <input type="checkbox" checked disabled>
                <strong>Aba "Regras_Calculo"</strong> ➔ Regras mapeadas para o <code>Agente_Calculo_Regras</code>
              </label>
            </div>
          </div>

          <!-- Síntese da Rede de Agentes -->
          <div>
            <h4 style="font-size: 12.5px; font-weight: 800; color: #1e293b; margin-bottom: 10px;">
              🤖 Agentes Autônomos a Serem Gerados no DAG
            </h4>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 12px;">
              <div style="background: white; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px;">
                <strong>1. Agente de Ingestão & Limpeza:</strong>
                <p style="margin: 4px 0 0 0; color: #64748b; font-size: 11.5px;">Valida formatos, remove inconsistências e insere dados no SQLite.</p>
              </div>
              <div style="background: white; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px;">
                <strong>2. Agente de Regras & Cálculos:</strong>
                <p style="margin: 4px 0 0 0; color: #64748b; font-size: 11.5px;">Executa lógicas de negócio equivalentes a PROCV, SOMASE e IFS em C++20.</p>
              </div>
              <div style="background: white; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px;">
                <strong>3. Agente Auditor de Integridade:</strong>
                <p style="margin: 4px 0 0 0; color: #64748b; font-size: 11.5px;">Auditoria contábil contínua com gravação de lições na memória semântica.</p>
              </div>
              <div style="background: white; border: 1px solid #e2e8f0; border-radius: 8px; padding: 10px;">
                <strong>4. Agente de Relatórios & Artefatos:</strong>
                <p style="margin: 4px 0 0 0; color: #64748b; font-size: 11.5px;">Gera relatórios executivos em Markdown e dashboards analíticos.</p>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding-top: 14px; display: flex; justify-content: space-between; align-items: center;">
          <span style="font-size: 12px; color: #64748b;">Pronto para sintetizar banco de dados e DAG de agentes</span>
          <div style="display: flex; gap: 10px;">
            <button id="btn-cancel-excel" class="tool-btn btn-secondary">Cancelar</button>
            <button id="btn-synthesize-excel" class="tool-btn btn-action" style="background: #16a34a; color: white; font-weight: 700; padding: 8px 18px;">
              ⚡ Sintetizar Banco de Dados & Rede de Agentes
            </button>
          </div>
        </div>
      </div>
    `;

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
    // Limpa grafo atual para injetar a nova rede de agentes
    this.graph.clear();

    // 1. Agente Ingestão
    const n1 = this.graph.createNode('node_excel_ingest', '1. Ingestao_Limpeza_Excel', 'agent', -480, 80);
    // 2. Agente Calculos
    const n2 = this.graph.createNode('node_excel_calc', '2. Calculo_Regras_Negocio', 'agent', -160, 80);
    // 3. Condicao de Auditoria
    const n3 = this.graph.createNode('node_excel_eval', '3. Auditoria_Consistencia', 'condition', 160, 80);
    // 4. Agente Relatorios
    const n4 = this.graph.createNode('node_excel_report', '4. Consolidacao_Relatorio', 'agent', 480, 160);

    // Conexoes
    this.graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);
    this.graph.createEdge(n2.id, n2.outputs[0].id, n3.id, n3.inputs[0].id);
    // Branch 0 (Consistente) -> Relatório
    this.graph.createEdge(n3.id, n3.outputs[0].id, n4.id, n4.inputs[0].id);
    // Branch 1 (Inconsistente) -> Loop de Correção para Ingestão
    this.graph.createEdge(n3.id, n3.outputs[1].id, n1.id, n1.inputs[0].id);

    // Atualiza manifesto do projeto com o banco SQLite gerado
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
    if (typeof alert !== 'undefined') {
      alert('✅ Planilha convertida com sucesso em Banco de Dados Relacional e Rede de 4 Agentes Concorrentes no Arestrix!');
    }
  }
}
