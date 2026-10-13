import type { Graph } from '../core/Graph.ts';
import type { EventBus } from '../core/EventBus.ts';
import type { Viewport } from '../view/Viewport.ts';
import { t } from '../i18n/index.ts';

/**
 * Arestrix - Busca no projeto aberto (caixa no canto superior direito).
 *
 * O ranking é feito pelo backend C++ (`search_project`: BM25 + trigramas sobre todos os
 * pipelines, subfluxos e skills do projeto). Aqui ficam apenas a caixa, a lista de resultados,
 * o teclado e a navegação até o vértice. Os comandos rápidos (executar, MCP, memória...) aparecem
 * no mesmo painel.
 */

/** Único pipeline que o Studio sabe abrir no canvas hoje. */
const ACTIVE_PIPELINE_FILE = 'pipelines/main.json';

export interface ISearchCommand {
  id: string;
  label: string;
  keywords: string[];
  run: () => void;
}

export interface ISearchHit {
  kind: 'node' | 'skill' | 'pipeline';
  title: string;
  subtitle: string;
  snippet: string;
  file: string;
  pipelineId: string;
  nodeId: string;
  nodeType: string;
  nodePath: string[];
  score: number;
}

interface IRow {
  group: 'nodes' | 'skills' | 'pipelines' | 'commands';
  title: string;
  subtitle?: string;
  snippet?: string;
  badge?: string;
  run: () => void | Promise<void>;
}

function backend(): any | null {
  const w = typeof window !== 'undefined' ? (window as any) : null;
  return w && w.webui && typeof w.webui.call === 'function' ? w.webui : null;
}

/**
 * Pede ao backend para reindexar o projeto (usa o hash dos arquivos: só refaz o que mudou).
 * Chamado depois de salvar. Não lança exceção.
 */
export async function reindexProjectSearch(): Promise<void> {
  try {
    const b = backend();
    if (b) await b.call('search_reindex');
  } catch {
    // a busca reindexa sozinha na próxima consulta
  }
}

/** Idiomas cujo dicionário de sinônimos já vem embutido no backend. */
const BUILTIN_SYNONYM_LOCALES = ['pt-BR', 'en-US'];

/**
 * Pede ao backend para gerar o dicionário de sinônimos de um idioma, com o modelo padrão do Studio
 * (variáveis de ambiente da máquina do usuário). Não faz nada se o arquivo já existir, se o idioma for
 * embutido ou se não houver modelo configurado. Roda em segundo plano e nunca lança exceção.
 */
export async function ensureSearchSynonyms(locale: string, force = false): Promise<string> {
  if (!force && BUILTIN_SYNONYM_LOCALES.includes(locale)) return 'builtin';
  try {
    const b = backend();
    if (!b) return 'no_backend';
    const res = JSON.parse((await b.call('search_ensure_synonyms', JSON.stringify({ locale, force }))) || '{}');
    if (res.status === 'generated') {
      console.info(`[Busca] Sinônimos de ${locale} gerados com ${res.model} (${res.groups} grupos).`);
    } else if (res.status === 'no_model') {
      console.info(`[Busca] Sinônimos de ${locale} não gerados: ${res.error}`);
    } else if (res.status === 'failed') {
      console.warn(`[Busca] Falha ao gerar os sinônimos de ${locale}: ${res.error}`);
    }
    return res.status || 'failed';
  } catch {
    return 'failed';
  }
}

export class ProjectSearchBox {
  private input: HTMLInputElement;
  private host: HTMLElement;
  private panel: HTMLElement;
  private rows: IRow[] = [];
  private active = -1;
  private debounce: ReturnType<typeof setTimeout> | null = null;
  private seq = 0;
  private status = '';

  constructor(
    host: HTMLElement,
    input: HTMLInputElement,
    private graph: Graph,
    private viewport: Viewport,
    private bus: EventBus,
    private commands: ISearchCommand[]
  ) {
    this.host = host;
    this.input = input;
    this.input.placeholder = t('search.placeholder', 'Buscar no projeto...') + ' (Ctrl+K)';
    this.input.setAttribute('role', 'combobox');
    this.input.setAttribute('aria-expanded', 'false');
    this.input.autocomplete = 'off';

    this.panel = document.createElement('div');
    this.panel.className = 'project-search-panel';
    this.panel.setAttribute('role', 'listbox');
    this.panel.style.display = 'none';
    this.host.style.position = 'relative';
    this.host.appendChild(this.panel);

    this.input.addEventListener('input', () => this.schedule());
    this.input.addEventListener('focus', () => { if (this.input.value.trim()) this.schedule(); });
    this.input.addEventListener('keydown', (e) => this.onKey(e));
    window.addEventListener('pointerdown', (e) => {
      if (!this.host.contains(e.target as Node)) this.close();
    }, true);
    window.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && !e.shiftKey && !e.altKey && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        this.input.focus();
        this.input.select();
      }
    });
  }

  private schedule(): void {
    if (this.debounce) clearTimeout(this.debounce);
    this.debounce = setTimeout(() => this.run(), 150);
  }

  private matchCommands(q: string): IRow[] {
    const needle = q.toLowerCase();
    return this.commands
      .filter(c => c.label.toLowerCase().includes(needle) || c.keywords.some(k => needle.includes(k) || k.includes(needle)))
      .map(c => ({ group: 'commands' as const, title: c.label, run: () => c.run() }));
  }

  private async run(): Promise<void> {
    const q = this.input.value.trim();
    if (q.length < 2) { this.close(); return; }
    const mySeq = ++this.seq;
    this.status = '';

    let hits: ISearchHit[] = [];
    const b = backend();
    if (!b) {
      this.status = t('search.needs_native', 'A busca no projeto requer o aplicativo nativo.');
    } else {
      try {
        const raw = await b.call('search_project', JSON.stringify({ q, limit: 25 }));
        if (mySeq !== this.seq) return; // chegou uma consulta mais nova
        const data = JSON.parse(raw || '{}');
        if (data.error) this.status = String(data.error);
        hits = data.results || [];
      } catch (err: any) {
        if (mySeq !== this.seq) return;
        this.status = t('search.failed', 'Falha ao buscar no projeto.');
      }
    }

    const rows: IRow[] = hits.map(h => this.rowFor(h));
    rows.push(...this.matchCommands(q));
    this.rows = rows;
    this.active = rows.length > 0 ? 0 : -1;
    this.render();
  }

  private rowFor(h: ISearchHit): IRow {
    const group = h.kind === 'node' ? 'nodes' : h.kind === 'skill' ? 'skills' : 'pipelines';
    const otherPipeline = h.kind !== 'skill' && h.file !== ACTIVE_PIPELINE_FILE;
    return {
      group,
      title: h.title,
      subtitle: h.subtitle,
      snippet: h.snippet,
      badge: h.kind === 'node' ? h.nodeType : undefined,
      run: () => this.openHit(h, otherPipeline)
    };
  }

  private async openHit(h: ISearchHit, otherPipeline: boolean): Promise<void> {
    if (h.kind === 'skill') {
      const b = backend();
      let content = '';
      try {
        const res = JSON.parse(await b.call('read_skill_file', h.file) || '{}');
        if (res.ok) content = res.content;
      } catch { /* abre vazio e somente leitura */ }
      this.bus.emit('node:open-skill-editor', { path: h.file, initialContent: content, readOnly: true });
      return;
    }

    if (otherPipeline) {
      this.showStatus(t('search.other_pipeline', 'Este resultado está em outro pipeline do projeto ({{file}}), que ainda não pode ser aberto no canvas.', { file: h.file }));
      return;
    }

    this.graph.navigateToBreadcrumb(0);
    if (h.kind === 'pipeline') {
      this.viewport.zoomToFit();
      return;
    }
    for (const id of h.nodePath) {
      if (!this.graph.diveIntoSubflow(id)) {
        this.showStatus(t('search.not_found', 'Vértice não encontrado no grafo aberto. Salve o projeto e tente de novo.'));
        return;
      }
    }
    if (!this.graph.getNode(h.nodeId)) {
      this.showStatus(t('search.not_found', 'Vértice não encontrado no grafo aberto. Salve o projeto e tente de novo.'));
      return;
    }
    this.viewport.focusNode(h.nodeId);
  }

  private showStatus(msg: string): void {
    this.status = msg;
    this.panel.style.display = 'block';
    this.render();
  }

  private onKey(e: KeyboardEvent): void {
    if (e.key === 'Escape') { this.close(); this.input.blur(); return; }
    if (this.panel.style.display === 'none') return;
    if (e.key === 'ArrowDown') { e.preventDefault(); this.move(1); }
    else if (e.key === 'ArrowUp') { e.preventDefault(); this.move(-1); }
    else if (e.key === 'Enter') {
      e.preventDefault();
      const row = this.rows[this.active];
      if (row) this.choose(row);
    }
  }

  private move(delta: number): void {
    if (this.rows.length === 0) return;
    this.active = (this.active + delta + this.rows.length) % this.rows.length;
    this.render();
  }

  private choose(row: IRow): void {
    this.status = '';
    Promise.resolve(row.run()).finally(() => {
      // fecha, a menos que a ação só tenha mostrado um aviso
      if (!this.status) this.close();
    });
  }

  private render(): void {
    this.panel.textContent = '';
    this.panel.style.display = 'block';
    this.input.setAttribute('aria-expanded', 'true');

    const groups: Array<[IRow['group'], string]> = [
      ['nodes', t('search.group_nodes', 'Vértices')],
      ['skills', t('search.group_skills', 'Skills')],
      ['pipelines', t('search.group_pipelines', 'Pipelines')],
      ['commands', t('search.group_commands', 'Comandos')]
    ];
    for (const [g, label] of groups) {
      const items = this.rows.map((r, i) => ({ r, i })).filter(x => x.r.group === g);
      if (items.length === 0) continue;
      const head = document.createElement('div');
      head.className = 'project-search-group';
      head.textContent = label;
      this.panel.appendChild(head);
      for (const { r, i } of items) this.panel.appendChild(this.rowEl(r, i));
    }

    if (this.rows.length === 0 && !this.status) {
      const empty = document.createElement('div');
      empty.className = 'project-search-empty';
      empty.textContent = t('search.empty', 'Nenhum resultado no projeto.');
      this.panel.appendChild(empty);
    }
    if (this.status) {
      const st = document.createElement('div');
      st.className = 'project-search-status';
      st.textContent = this.status;
      this.panel.appendChild(st);
    }
  }

  private rowEl(r: IRow, index: number): HTMLElement {
    const el = document.createElement('div');
    el.className = 'project-search-row' + (index === this.active ? ' active' : '');
    el.setAttribute('role', 'option');
    el.setAttribute('aria-selected', index === this.active ? 'true' : 'false');

    const title = document.createElement('div');
    title.className = 'project-search-title';
    title.textContent = r.title;
    if (r.badge) {
      const badge = document.createElement('span');
      badge.className = 'project-search-badge';
      badge.textContent = r.badge;
      title.appendChild(badge);
    }
    el.appendChild(title);

    if (r.subtitle) {
      const sub = document.createElement('div');
      sub.className = 'project-search-sub';
      sub.textContent = r.subtitle;
      el.appendChild(sub);
    }
    if (r.snippet) {
      const sn = document.createElement('div');
      sn.className = 'project-search-snippet';
      sn.textContent = r.snippet;
      el.appendChild(sn);
    }

    el.addEventListener('pointerenter', () => {
      this.active = index;
      this.panel.querySelectorAll('.project-search-row').forEach(n => n.classList.toggle('active', n === el));
    });
    el.addEventListener('click', () => this.choose(r));
    return el;
  }

  close(): void {
    this.seq++;
    this.panel.style.display = 'none';
    this.panel.textContent = '';
    this.rows = [];
    this.active = -1;
    this.status = '';
    this.input.setAttribute('aria-expanded', 'false');
  }
}
