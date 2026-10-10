import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { analyzeInBackground, AnalysisError } from '../../core/excel/ExcelAnalysisClient.ts';
import { buildRegionGraph } from '../../core/excel/RegionGraphBuilder.ts';
import { Risk, WorkbookModel } from '../../core/excel/WorkbookModel.ts';
import { ExcelPromptStore } from '../../core/excel/ExcelImportPrompt.ts';
import { DefaultIntelligenceService, IExcelReviewResult } from '../../core/ai/DefaultIntelligenceService.ts';

type Tab = 'regions' | 'deps' | 'risks';

const ROLE_LABEL: Record<string, string> = {
  input_table: 'Tabela de entrada',
  calculated_table: 'Tabela calculada',
  parameters: 'Parâmetros',
  report: 'Relatório',
  unknown: 'Indefinido'
};

const SEVERITY_LABEL: Record<Risk['severity'], string> = { high: 'Alta', medium: 'Média', low: 'Baixa' };
const SEVERITY_COLOR: Record<Risk['severity'], string> = { high: '#a4262c', medium: '#8a5a00', low: '#616161' };

export function escapeHtml(text: string): string {
  return text.replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);
}

/**
 * Arestrix Studio - Raio-X de planilhas Excel (Fase 1, somente leitura).
 * Tudo local: o arquivo é lido no navegador, sem rede e sem LLM.
 */
export class ImportExcelModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;

  private model: WorkbookModel | null = null;
  private error: string | null = null;
  private progress: { stage: string; fraction: number } | null = null;
  private tab: Tab = 'regions';
  private promptDraft: string | null = null; // texto em edição, ainda não salvo
  private promptOpen = false;
  private reviewing = false;
  private review: IExcelReviewResult | null = null;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;

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
    if (this.modal) this.modal.style.display = 'none';
  }

  public getModel(): WorkbookModel | null {
    return this.model;
  }

  /** Analisa os bytes de um .xlsx e atualiza a tela. */
  public async analyzeBytes(bytes: Uint8Array, fileName: string): Promise<void> {
    this.model = null;
    this.error = null;
    this.progress = { stage: 'lendo arquivo', fraction: 0 };
    this.render();
    try {
      this.model = await analyzeInBackground(bytes, fileName, (stage, fraction) => {
        this.progress = { stage, fraction };
        this.updateProgress();
      });
    } catch (e) {
      this.error = e instanceof AnalysisError ? e.message : `Falha na análise: ${(e as Error).message}`;
    }
    this.progress = null;
    this.render();
  }

  /** Cria no grafo um nó por região e uma aresta por dependência. */
  public generateRegionGraph(): void {
    if (!this.model) return;
    buildRegionGraph(this.model, this.graph);
    if (this.viewport) setTimeout(() => this.viewport?.zoomToFit(), 120);
    this.hide();
  }

  public getPrompt(): string {
    return this.promptDraft ?? ExcelPromptStore.get();
  }

  public savePrompt(text: string): void {
    ExcelPromptStore.set(text);
    this.promptDraft = null;
  }

  public resetPrompt(): void {
    ExcelPromptStore.reset();
    this.promptDraft = null;
  }

  /** Pede ao modelo padrão que revise o Raio-X, usando o prompt (editado ou padrão). Nada é aplicado sozinho. */
  public async runReview(): Promise<IExcelReviewResult | null> {
    if (!this.model || this.reviewing) return null;
    this.reviewing = true;
    this.review = null;
    this.render();
    this.review = await DefaultIntelligenceService.getInstance().reviewExcelImport(this.model, this.getPrompt());
    this.reviewing = false;
    this.render();
    return this.review;
  }

  public exportJson(): string {
    return JSON.stringify(this.model, null, 2);
  }

  private downloadJson(): void {
    if (!this.model) return;
    const blob = new Blob([this.exportJson()], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `${this.model.source.fileName.replace(/\.[^.]+$/, '')}.raiox.json`;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  private updateProgress(): void {
    const bar = this.modal?.querySelector<HTMLElement>('#excel-progress-bar');
    const label = this.modal?.querySelector<HTMLElement>('#excel-progress-label');
    if (!this.progress || !bar || !label) return;
    bar.style.width = `${Math.round(this.progress.fraction * 100)}%`;
    label.textContent = `Analisando: ${this.progress.stage}`;
  }

  private card(inner: string): string {
    return `<div style="background:#ffffff;padding:6px 10px;border:1px solid #d1d1d1;border-radius:2px;">${inner}</div>`;
  }

  private renderRegions(m: WorkbookModel): string {
    return m.sheets
      .map(s => {
        const flags = [s.state !== 'visible' ? 'oculta' : '', s.protected ? 'protegida' : ''].filter(Boolean).join(', ');
        const regions = s.regions
          .map(r => {
            const cols = r.header ? r.header.columns.map(c => `${escapeHtml(c.name)} <em>(${c.inferredType})</em>`).join(' · ') : '';
            const formulas = r.formulaPatterns.length ? ` · ${r.formulaPatterns.reduce((n, p) => n + p.count, 0)} fórmula(s) em ${r.formulaPatterns.length} padrão(ões)` : '';
            return `<div style="margin:4px 0 0 12px;"><code>${escapeHtml(r.range)}</code> &mdash; <strong>${ROLE_LABEL[r.role]}</strong>${r.title ? ` &ldquo;${escapeHtml(r.title)}&rdquo;` : ''}, ${r.rowCount} linha(s)${formulas}${cols ? `<div style="color:#616161;">${cols}</div>` : ''}</div>`;
          })
          .join('');
        return this.card(`<strong>${escapeHtml(s.name)}</strong>${flags ? ` <em>(${flags})</em>` : ''} <span style="color:#616161;">${escapeHtml(s.dimensions)}</span>${regions || '<div style="margin-left:12px;color:#616161;">Sem dados.</div>'}`);
      })
      .join('');
  }

  private renderDeps(m: WorkbookModel): string {
    if (!m.dependencies.length) return this.card('Nenhuma região lê dados de outra.');
    return m.dependencies
      .map(d => this.card(`<code>${escapeHtml(d.from)}</code> &rarr; <code>${escapeHtml(d.to)}</code> &mdash; ${d.formulaCount} fórmula(s)`))
      .join('');
  }

  private renderRisks(m: WorkbookModel): string {
    if (!m.risks.length) return this.card('Nenhum risco encontrado.');
    return m.risks
      .map(r => this.card(`<strong style="color:${SEVERITY_COLOR[r.severity]};">${SEVERITY_LABEL[r.severity]}</strong> &middot; <code>${r.code}</code> &middot; <code>${escapeHtml(r.where)}</code><div>${escapeHtml(r.message)}</div>`))
      .join('');
  }

  private renderPromptEditor(): string {
    const customized = ExcelPromptStore.isCustomized();
    const stale = ExcelPromptStore.isDefaultNewerThanCustom();
    const status = this.promptDraft !== null ? 'alterações não salvas' : customized ? 'personalizado' : 'padrão';
    return `
      <details id="excel-prompt-details" class="office-group" style="margin-bottom:0;" ${this.promptOpen ? 'open' : ''}>
        <summary class="office-group-title" style="cursor:pointer;">Prompt do agente de importação (${status})</summary>
        <p style="margin:6px 0;font-size:12px;color:#616161;">Este é o prompt que o modelo padrão recebe para revisar a importação. Use <code>{{WORKBOOK_SUMMARY}}</code> onde o Raio-X deve entrar.${stale ? ' <strong style="color:#8a5a00;">O prompt padrão foi atualizado depois da sua edição; considere restaurar.</strong>' : ''}</p>
        <textarea id="excel-prompt-text" class="office-input" spellcheck="false" style="width:100%;min-height:200px;font-family:Consolas,monospace;font-size:12px;">${escapeHtml(this.getPrompt())}</textarea>
        <div style="display:flex;gap:6px;margin-top:6px;">
          <button id="btn-save-excel-prompt" class="tool-btn btn-action" style="font-size:12px;height:28px;padding:2px 12px;">Salvar prompt</button>
          <button id="btn-reset-excel-prompt" class="tool-btn btn-secondary" style="font-size:12px;height:28px;padding:2px 12px;">Restaurar padrão</button>
        </div>
      </details>`;
  }

  private renderReview(): string {
    if (this.reviewing) return this.card('Consultando o modelo padrão…');
    const r = this.review;
    if (!r) return '';
    if (!r.isSuccess) return this.card(`<span style="color:#a4262c;">${escapeHtml(r.error ?? 'A revisão não foi concluída.')}</span>`);
    const items = (title: string, rows: string[]): string => (rows.length ? `<div><strong>${title}</strong><ul style="margin:2px 0 0 18px;padding:0;">${rows.map(x => `<li>${x}</li>`).join('')}</ul></div>` : '');
    return this.card(`
      <div><strong>Revisão do modelo</strong> &middot; confiança ${r.confidenceScore}% &mdash; ${escapeHtml(r.summary)}</div>
      ${items('Papéis sugeridos', r.regionCorrections.map(c => `<code>${escapeHtml(c.regionId)}</code> &rarr; ${ROLE_LABEL[c.role]}: ${escapeHtml(c.reason)}`))}
      ${items('Tabelas sugeridas', r.tableSuggestions.map(t => `<code>${escapeHtml(t.regionId)}</code> &rarr; <code>${escapeHtml(t.tableName)}</code>${t.primaryKey ? ` (chave: ${escapeHtml(t.primaryKey)})` : ''}${t.notes ? ` &mdash; ${escapeHtml(t.notes)}` : ''}`))}
      ${items('Apontamentos', r.findings.map(escapeHtml))}
      ${items('Perguntas para você', r.questionsForUser.map(escapeHtml))}
      <div style="color:#616161;margin-top:4px;">Sugestões apenas: nada foi alterado no Raio-X.</div>`);
  }

  private renderBody(): string {
    const picker = `
      <div class="office-group" style="margin-bottom:0;">
        <label class="office-label" for="excel-file-input">Arquivo da planilha (.xlsx):</label>
        <input type="file" id="excel-file-input" accept=".xlsx,.xlsm" class="office-input">
        <p style="margin:6px 0 0 0;font-size:12px;color:#616161;">A análise é local: o conteúdo da planilha não sai desta máquina e nada é alterado no arquivo original.</p>
      </div>`;

    if (this.progress) {
      return `${picker}<div class="office-group" style="margin-bottom:0;"><span id="excel-progress-label" class="office-group-title">Analisando: ${escapeHtml(this.progress.stage)}</span>
        <div style="height:6px;background:#e1e1e1;border-radius:2px;"><div id="excel-progress-bar" style="height:6px;width:${Math.round(this.progress.fraction * 100)}%;background:#0f6cbd;border-radius:2px;"></div></div></div>`;
    }
    if (this.error) {
      return `${picker}<div class="office-group" style="margin-bottom:0;color:#a4262c;font-size:13px;">${escapeHtml(this.error)}</div>`;
    }
    const m = this.model;
    if (!m) return picker;

    const high = m.risks.filter(r => r.severity === 'high').length;
    const tabBtn = (t: Tab, label: string): string =>
      `<button class="tool-btn ${this.tab === t ? 'btn-action' : 'btn-secondary'}" data-excel-tab="${t}" style="font-size:12px;height:28px;padding:2px 12px;">${label}</button>`;
    const content = this.tab === 'regions' ? this.renderRegions(m) : this.tab === 'deps' ? this.renderDeps(m) : this.renderRisks(m);

    return `${picker}
      <div class="office-group" style="margin-bottom:0;font-size:13px;">
        <strong>${escapeHtml(m.source.fileName)}</strong> &mdash; ${m.stats.sheets} aba(s), ${m.stats.regions} região(ões), ${m.stats.cells} célula(s), ${m.stats.formulaCells} fórmula(s)${m.hasMacros ? ' &middot; <strong style="color:#a4262c;">com macros</strong>' : ''}
      </div>
      <div style="display:flex;gap:6px;">${tabBtn('regions', 'Abas e regiões')}${tabBtn('deps', `Dependências (${m.dependencies.length})`)}${tabBtn('risks', `Riscos (${m.risks.length}${high ? `, ${high} alta(s)` : ''})`)}</div>
      <div style="display:flex;flex-direction:column;gap:6px;font-size:12.5px;">${content}</div>`;
  }

  private renderBodyWithExtras(): string {
    const canReview = this.model !== null && !this.progress;
    const reviewBlock = canReview
      ? `<div class="office-group" style="margin-bottom:0;">
          <button id="btn-review-excel" class="tool-btn btn-secondary" ${this.reviewing ? 'disabled' : ''} style="font-size:12px;height:28px;padding:2px 12px;">Revisar com IA</button>
          <p style="margin:6px 0 0 0;font-size:12px;color:#616161;">Envia ao modelo padrão a <strong>estrutura</strong> (nomes de abas, títulos, cabeçalhos, rótulos de linha, fórmulas em R1C1 e riscos). Os valores das células e o nome do arquivo não são enviados.</p>
        </div>${this.renderReview()}`
      : '';
    return `${this.renderBody()}${reviewBlock}${this.renderPromptEditor()}`;
  }

  private render(): void {
    if (!this.modal) return;
    const ready = this.model !== null;
    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 820px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        <div class="modal-header">
          <div>
            <h3 class="modal-title">Raio-X de Planilha do Excel</h3>
            <p style="margin: 2px 0 0 0; font-size: 12px; color: #616161; font-family: 'Segoe UI', sans-serif;">
              Descubra as regiões, as dependências e os riscos de uma planilha antes de migrá-la.
            </p>
          </div>
          <button id="btn-close-excel-modal" class="modal-close-btn" title="Fechar">&times;</button>
        </div>
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          ${this.renderBodyWithExtras()}
        </div>
        <div class="modal-footer">
          <span class="footer-info">${ready ? 'Análise concluída. Nada foi gravado no projeto.' : 'Selecione um arquivo .xlsx para analisar.'}</span>
          <div class="footer-actions">
            <button id="btn-cancel-excel" class="tool-btn btn-secondary">Fechar</button>
            <button id="btn-export-excel-json" class="tool-btn btn-secondary" ${ready ? '' : 'disabled'}>Exportar JSON</button>
            <button id="btn-region-graph-excel" class="tool-btn btn-action" ${ready ? '' : 'disabled'}>Gerar grafo de regiões</button>
          </div>
        </div>
      </div>
    `;
    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal) return;
    const $ = <T extends HTMLElement>(sel: string): T | null => this.modal!.querySelector<T>(sel);

    $('#btn-close-excel-modal')?.addEventListener('click', () => this.hide());
    $('#btn-cancel-excel')?.addEventListener('click', () => this.hide());
    $('#btn-export-excel-json')?.addEventListener('click', () => this.downloadJson());
    $('#btn-region-graph-excel')?.addEventListener('click', () => this.generateRegionGraph());

    $<HTMLInputElement>('#excel-file-input')?.addEventListener('change', async ev => {
      const file = (ev.target as HTMLInputElement).files?.[0];
      if (!file) return;
      await this.analyzeBytes(new Uint8Array(await file.arrayBuffer()), file.name);
    });

    $('#btn-review-excel')?.addEventListener('click', () => void this.runReview());
    $<HTMLDetailsElement>('#excel-prompt-details')?.addEventListener('toggle', ev => {
      this.promptOpen = (ev.target as HTMLDetailsElement).open;
    });
    $<HTMLTextAreaElement>('#excel-prompt-text')?.addEventListener('input', ev => {
      this.promptDraft = (ev.target as HTMLTextAreaElement).value;
    });
    $('#btn-save-excel-prompt')?.addEventListener('click', () => {
      this.savePrompt(this.getPrompt());
      this.render();
    });
    $('#btn-reset-excel-prompt')?.addEventListener('click', () => {
      this.resetPrompt();
      this.render();
    });

    this.modal.querySelectorAll<HTMLElement>('[data-excel-tab]').forEach(btn =>
      btn.addEventListener('click', () => {
        this.tab = btn.dataset.excelTab as Tab;
        this.render();
      })
    );
  }
}
