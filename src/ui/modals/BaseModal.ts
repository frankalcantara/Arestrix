import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

/**
 * Arestrix Studio - BaseModal (Classe Base para Modais e Diálogos)
 * 
 * Fornece a infraestrutura padronizada para todos os modais atuais e futuros:
 * - Janela flutuante com barra de títulos Office/Fluent
 * - Barra de abas unificada estilo Microsoft Office 365 (Fluent 2)
 * - Movimentação por arraste (Draggable com clamping na viewport)
 * - Redimensionamento livre (Resizable com alça visual)
 * - Controles de Maximizar/Restaurar e Fechar
 * - Gestão de ciclo de vida (open, hide, render, destroy)
 * - Estrita aderência à regra: Zero sombras/glows em fontes
 */
export interface IModalTabDefinition {
  id: string;
  label: string;
  icon?: string;
  badge?: string;
}

export interface IBaseModalConfig {
  id?: string;
  dialogClass?: string;
  title: string;
  subtitle?: string;
  icon?: string;
  minWidth?: number;
  minHeight?: number;
  resizable?: boolean;
  draggable?: boolean;
  closeOnEscape?: boolean;
  closeOnBackdropClick?: boolean;
  tabs?: IModalTabDefinition[];
  defaultTab?: string;
  defaultTabId?: string;
}

export abstract class BaseModal {
  protected overlay: HTMLElement;
  protected dialog: HTMLElement;
  protected windowController?: IModalWindowController;
  protected config: IBaseModalConfig;
  protected isOpen: boolean = false;
  protected tabs: IModalTabDefinition[] = [];
  protected activeTabId: string = '';

  constructor(config: IBaseModalConfig) {
    this.config = {
      closeOnEscape: true,
      closeOnBackdropClick: false, // Por padrão em janelas desktop, clicar fora não fecha acidentalmente
      resizable: true,
      draggable: true,
      minWidth: 520,
      minHeight: 380,
      ...config
    };

    if (this.config.tabs && this.config.tabs.length > 0) {
      this.tabs = [...this.config.tabs];
      this.activeTabId = this.config.defaultTabId || this.config.defaultTab || this.tabs[0].id;
    }

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    if (this.config.id) {
      this.overlay.id = `${this.config.id}-overlay`;
    }

    this.dialog = document.createElement('div');
    this.dialog.className = `modal-dialog ${this.config.dialogClass || ''}`;
    if (this.config.id) {
      this.dialog.id = this.config.id;
    }

    this.overlay.appendChild(this.dialog);
    document.body.appendChild(this.overlay);

    this.setupBaseEvents();
  }

  protected setupBaseEvents(): void {
    if (this.config.closeOnBackdropClick) {
      this.overlay.addEventListener('click', (e) => {
        if (e.target === this.overlay) {
          this.hide();
        }
      });
    }

    if (this.config.closeOnEscape && typeof window !== 'undefined') {
      window.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && this.isOpen) {
          this.hide();
        }
      });
    }
  }

  /**
   * Renderiza a barra de abas padronizada Office 365
   */
  protected renderTabsStripHtml(): string {
    if (this.tabs.length === 0) return '';
    return `
      <div class="office-modal-tab-strip" role="tablist">
        ${this.tabs.map(tab => {
          const isActive = tab.id === this.activeTabId;
          return `
            <button type="button" role="tab" class="office-modal-tab-btn ${isActive ? 'active' : ''}" data-modal-tab="${tab.id}" aria-selected="${isActive}">
              <span>${tab.label}</span>
              ${tab.badge ? `<span class="office-modal-tab-badge">${tab.badge}</span>` : ''}
            </button>
          `;
        }).join('')}
      </div>
    `;
  }

  /**
   * Renderiza a janela completa (barra de títulos + barra de abas + corpo + rodapé)
   */
  public render(): void {
    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: this.config.title,
      subtitle: this.config.subtitle,
      icon: this.config.icon,
      closeBtnId: `btn-close-${this.config.id || 'base-modal'}`
    });

    const tabsBarHtml = this.renderTabsStripHtml();
    const bodyHtml = this.tabs.length > 0 && this.renderTabContent
      ? this.renderTabContent(this.activeTabId)
      : this.renderBody();
    const footerHtml = this.renderFooter ? this.renderFooter() : '';

    this.dialog.innerHTML = `
      ${titleBarHtml}
      ${tabsBarHtml}
      <div class="modal-body-scrollable">
        ${bodyHtml}
      </div>
      ${footerHtml}
    `;

    // Conecta o gerenciador de janelas flutuantes
    this.windowController = ModalWindowManager.attach({
      dialog: this.dialog,
      minWidth: this.config.minWidth,
      minHeight: this.config.minHeight,
      resizable: this.config.resizable,
      draggable: this.config.draggable,
      onClose: () => this.hide()
    });

    // Conecta evento de fechar no botão padrão da barra
    const closeBtn = this.dialog.querySelector(`#btn-close-${this.config.id || 'base-modal'}`);
    if (closeBtn) {
      closeBtn.addEventListener('click', () => this.hide());
    }

    this.setupTabEvents();
    this.bindEvents();
  }

  /**
   * Conecta ouvintes de eventos para navegação entre abas
   */
  protected setupTabEvents(): void {
    if (this.tabs.length === 0 || typeof this.dialog.querySelectorAll !== 'function') return;
    const tabButtons = this.dialog.querySelectorAll<HTMLButtonElement>('[data-modal-tab]');
    if (!tabButtons || typeof tabButtons.forEach !== 'function') return;
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tabId = btn.getAttribute ? btn.getAttribute('data-modal-tab') : btn.dataset?.modalTab;
        if (tabId && tabId !== this.activeTabId) {
          this.switchTab(tabId);
        }
      });
    });
  }

  /**
   * Alterna a aba ativa e re-renderiza o corpo do modal
   */
  public switchTab(tabId: string): void {
    const exists = this.tabs.some(t => t.id === tabId);
    if (!exists) return;

    this.saveTabState();
    this.activeTabId = tabId;

    if (typeof this.dialog.querySelectorAll === 'function') {
      const tabButtons = this.dialog.querySelectorAll<HTMLButtonElement>('[data-modal-tab]');
      if (tabButtons && typeof tabButtons.forEach === 'function') {
        tabButtons.forEach(btn => {
          const btnTab = btn.getAttribute ? btn.getAttribute('data-modal-tab') : btn.dataset?.modalTab;
          const isCurrent = btnTab === tabId;
          if (btn.classList) {
            if (isCurrent) {
              btn.classList.add('active');
            } else {
              btn.classList.remove('active');
            }
          }
          if (typeof btn.setAttribute === 'function') {
            btn.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
          }
        });
      }
    }

    const bodyContainer = this.dialog.querySelector('.modal-body-scrollable');
    if (bodyContainer) {
      if (this.renderTabContent) {
        bodyContainer.innerHTML = this.renderTabContent(this.activeTabId);
      } else {
        bodyContainer.innerHTML = this.renderBody();
      }
    }

    this.onTabSwitched(tabId);
  }

  /**
   * Gancho chamado antes da troca de aba para persistir inputs no estado do modal
   */
  protected saveTabState(): void {
    // Pode ser implementado por subclasses que desejam capturar inputs antes de alternar abas
  }

  /**
   * Gancho chamado após a troca de aba para vincular manipuladores específicos daquela visualização
   */
  protected onTabSwitched(_tabId: string): void {
    // Pode ser implementado por subclasses
    this.bindEvents();
  }

  /**
   * Renderização do conteúdo de uma aba específica (usado quando o modal possui abas)
   */
  protected renderTabContent?(_tabId: string): string;

  /**
   * Método padrão para subclasses renderizarem seu conteúdo interno
   */
  protected renderBody(): string {
    return '';
  }

  /**
   * Método opcional para rodapé da janela
   */
  protected renderFooter?(): string;

  /**
   * Auxiliar para renderizar rodapé padrão Office 365 com botões Salvar / Cancelar
   */
  protected renderStandardFooter(options?: {
    saveText?: string;
    cancelText?: string;
    primaryLabel?: string;
    secondaryLabel?: string;
    primaryId?: string;
    secondaryId?: string;
    extraHtml?: string;
  }): string {
    const saveLabel = options?.primaryLabel || options?.saveText || 'OK';
    const cancelLabel = options?.secondaryLabel || options?.cancelText || 'Cancelar';
    const saveId = options?.primaryId || 'btn-modal-save';
    const cancelId = options?.secondaryId || 'btn-modal-cancel';
    return `
      <div class="modal-footer office-modal-footer">
        <div class="modal-footer-left">${options?.extraHtml || ''}</div>
        <div class="modal-footer-right" style="display: flex; gap: 8px;">
          <button type="button" class="btn btn-secondary-office" id="${cancelId}">${cancelLabel}</button>
          <button type="button" class="btn btn-primary-office" id="${saveId}">${saveLabel}</button>
        </div>
      </div>
    `;
  }

  /**
   * Utilitário para sanitização de strings para injeção segura em templates HTML
   */
  protected escapeHtml(str: string): string {
    return (str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  /**
   * Método para vincular manipuladores de eventos específicos do formulário / corpo
   */
  protected bindEvents(): void {
    // Implementado pelas subclasses
    const cancelBtn = this.dialog.querySelector('#btn-modal-cancel');
    if (cancelBtn) {
      cancelBtn.addEventListener('click', () => this.hide());
    }
  }

  public open(..._args: any[]): void {
    this.render();
    this.overlay.classList.remove('hidden');
    this.isOpen = true;
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
    this.isOpen = false;
  }

  public getController(): IModalWindowController | undefined {
    return this.windowController;
  }

  public destroy(): void {
    this.windowController?.destroy();
    if (this.overlay && this.overlay.parentNode) {
      this.overlay.parentNode.removeChild(this.overlay);
    }
  }
}
