import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

/**
 * Arestrix Studio - BaseModal (Classe Base para Modais e Diálogos)
 * 
 * Fornece a infraestrutura padronizada para todos os modais atuais e futuros:
 * - Janela flutuante com barra de títulos Office/Fluent
 * - Movimentação por arraste (Draggable com clamping na viewport)
 * - Redimensionamento livre (Resizable com alça visual)
 * - Controles de Maximizar/Restaurar e Fechar
 * - Gestão de ciclo de vida (open, hide, render, destroy)
 * - Estrita aderência à regra: Zero sombras/glows em fontes
 */
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
}

export abstract class BaseModal {
  protected overlay: HTMLElement;
  protected dialog: HTMLElement;
  protected windowController?: IModalWindowController;
  protected config: IBaseModalConfig;
  protected isOpen: boolean = false;

  constructor(config: IBaseModalConfig) {
    this.config = {
      closeOnEscape: true,
      closeOnBackdropClick: false, // Por padrão em janelas desktop, clicar fora não fecha acidentalmente
      resizable: true,
      draggable: true,
      minWidth: 480,
      minHeight: 320,
      ...config
    };

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

    if (this.config.closeOnEscape) {
      window.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && this.isOpen) {
          this.hide();
        }
      });
    }
  }

  /**
   * Renderiza a janela completa (barra de títulos + corpo + rodapé) e anexa os controles de janela
   */
  public render(): void {
    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: this.config.title,
      subtitle: this.config.subtitle,
      icon: this.config.icon,
      closeBtnId: `btn-close-${this.config.id || 'base-modal'}`
    });

    const bodyHtml = this.renderBody();
    const footerHtml = this.renderFooter ? this.renderFooter() : '';

    this.dialog.innerHTML = `
      ${titleBarHtml}
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

    this.bindEvents();
  }

  /**
   * Método abstrato obrigatório para subclasses renderizarem seu conteúdo interno
   */
  protected abstract renderBody(): string;

  /**
   * Método opcional para rodapé da janela
   */
  protected renderFooter?(): string;

  /**
   * Método para vincular manipuladores de eventos específicos do formulário / corpo
   */
  protected bindEvents(): void {
    // Implementado pelas subclasses
  }

  public open(): void {
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
