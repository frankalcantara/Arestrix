/**
 * Arestrix Studio - Window Management Engine
 * 
 * Gerenciador unificado de janelas e modais no padrão Microsoft Office / Fluent Design:
 * 1. Barra de Títulos Padronizada (Ícone, Título, Subtítulo, Maximizar/Restaurar e Fechar)
 * 2. Arraste Fluido (Draggable) com restrição estrita aos limites da viewport (clamping)
 * 3. Redimensionamento Livre (Resizable) com alça de redimensionamento e limites min/max
 * 4. Alternância Maximizar/Restaurar via botão e duplo-clique na barra de título
 * 5. Foco e Elevação Dinâmica de Z-Index para a janela ativa
 * 6. Total conformidade com as regras globais de design: Zero sombras/glows em fontes (text-shadow: none)
 */

import { t } from '../../i18n/index.ts';

export interface IModalWindowOptions {
  dialog: HTMLElement;
  header?: HTMLElement | null;
  minWidth?: number;
  minHeight?: number;
  maxWidth?: number;
  maxHeight?: number;
  resizable?: boolean;
  draggable?: boolean;
  allowMaximize?: boolean;
  onClose?: () => void;
  onMaximize?: (isMaximized: boolean) => void;
  onResize?: (width: number, height: number) => void;
  onMove?: (left: number, top: number) => void;
}

export interface IModalWindowController {
  dialog: HTMLElement;
  header: HTMLElement | null;
  center(): void;
  maximize(): void;
  restore(): void;
  toggleMaximize(): void;
  isMaximized(): boolean;
  bringToFront(): void;
  destroy(): void;
  setPosition(left: number, top: number): void;
  setSize(width: number, height: number): void;
}

interface ISavedWindowState {
  left: number;
  top: number;
  width: number;
  height: number;
}

export class ModalWindowManager {
  private static activeZIndex: number = 3050;
  private static registeredControllers: IModalWindowController[] = [];

  /**
   * Conecta os recursos de janela flutuante (arraste, resize, maximizar e z-index) a um diálogo
   */
  public static attach(options: IModalWindowOptions): IModalWindowController {
    const dialog = options.dialog;
    if (!dialog) {
      throw new Error('[ModalWindowManager] Elemento de diálogo é obrigatório.');
    }

    const header = options.header || (dialog.querySelector('.modal-header, .window-titlebar') as HTMLElement);
    const minWidth = options.minWidth ?? 480;
    const minHeight = options.minHeight ?? 320;
    const isDraggable = options.draggable !== false;
    const isResizable = options.resizable !== false;
    const allowMaximize = options.allowMaximize !== false;

    let isMaximized = false;
    let savedState: ISavedWindowState | null = null;
    let hasBeenPositioned = false;

    // Garante estilização base no elemento do diálogo
    dialog.classList?.add?.('fluent-window');
    if (isResizable) {
      dialog.classList?.add?.('fluent-window-resizable');
    }

    // 1. Injeta ou aprimora controles de janela na barra de títulos
    let maximizeBtn: HTMLElement | null = null;
    if (header) {
      header.classList?.add?.('fluent-window-header');
      if (isDraggable) {
        header.classList?.add?.('fluent-header-draggable');
      }

      // Procura ou cria contêiner de controles (.window-controls)
      let controlsGroup = header.querySelector?.('.window-controls') as HTMLElement;
      if (!controlsGroup && typeof document !== 'undefined' && document.createElement) {
        // Encontra o botão de fechar existente se houver
        const existingCloseBtn = header.querySelector?.('.modal-close-btn, .btn-close, .window-btn-close') as HTMLElement;
        if (existingCloseBtn && allowMaximize) {
          controlsGroup = document.createElement('div');
          controlsGroup.className = 'window-controls';
          existingCloseBtn.parentNode?.insertBefore(controlsGroup, existingCloseBtn);
          
          // Cria o botão de maximizar/restaurar
          maximizeBtn = document.createElement('button');
          maximizeBtn.setAttribute('type', 'button');
          maximizeBtn.className = 'window-control-btn window-btn-maximize';
          maximizeBtn.title = 'Maximizar (Duplo clique na barra)';
          maximizeBtn.setAttribute('aria-label', maximizeBtn.title);
          maximizeBtn.innerHTML = ModalWindowManager.maximizeIcon();
          controlsGroup.appendChild(maximizeBtn);

          // Move o botão de fechar para o grupo de controles
          controlsGroup.appendChild(existingCloseBtn);
        }
      } else if (controlsGroup) {
        maximizeBtn = controlsGroup.querySelector?.('.window-btn-maximize');
      }
    }

    // 2. Injeta alça visual de redimensionamento no canto inferior direito
    let resizeHandle = dialog.querySelector?.('.window-resize-handle') as HTMLElement;
    if (isResizable && !resizeHandle && typeof document !== 'undefined' && document.createElement) {
      resizeHandle = document.createElement('div');
      resizeHandle.className = 'window-resize-handle';
      resizeHandle.title = t('window.resize_tip');
      resizeHandle.innerHTML = `
        <svg viewBox="0 0 10 10" width="10" height="10" style="display:block; fill:currentColor;">
          <circle cx="8" cy="8" r="1"></circle>
          <circle cx="8" cy="4" r="1"></circle>
          <circle cx="4" cy="8" r="1"></circle>
          <circle cx="8" cy="0" r="1"></circle>
          <circle cx="4" cy="4" r="1"></circle>
          <circle cx="0" cy="8" r="1"></circle>
        </svg>
      `;
      dialog.appendChild?.(resizeHandle);
    }

    // 3. Foco e Elevação de Z-Index
    const bringToFront = () => {
      ModalWindowManager.activeZIndex += 2;
      dialog.style.zIndex = ModalWindowManager.activeZIndex.toString();
      // Se houver um backdrop/overlay pai, também pode acompanhar se necessário
      const parentOverlay = typeof dialog.closest === 'function' ? (dialog.closest('.modal-overlay, .modal-backdrop') as HTMLElement) : null;
      if (parentOverlay) {
        parentOverlay.style.zIndex = (ModalWindowManager.activeZIndex - 1).toString();
      }
    };

    dialog.addEventListener?.('pointerdown', () => bringToFront());

    // 4. Centralização inteligente na tela
    const center = () => {
      if (typeof window === 'undefined') return;
      const viewW = window.innerWidth || 1280;
      const viewH = window.innerHeight || 800;

      const rect = dialog.getBoundingClientRect ? dialog.getBoundingClientRect() : { width: 0, height: 0 };
      const w = rect.width > 0 ? rect.width : (parseInt(dialog.style?.width || '0') || 780);
      const h = rect.height > 0 ? rect.height : (parseInt(dialog.style?.height || '0') || 580);

      const left = Math.max(10, Math.round((viewW - w) / 2));
      const top = Math.max(10, Math.round((viewH - h) / 2));

      if (dialog.style) {
        dialog.style.position = 'absolute';
        dialog.style.margin = '0';
        dialog.style.left = `${left}px`;
        dialog.style.top = `${top}px`;
      }
      hasBeenPositioned = true;
    };

    const clampPosition = (left: number, top: number): { left: number; top: number } => {
      if (typeof window === 'undefined') return { left, top };
      const viewW = window.innerWidth || 1280;
      const viewH = window.innerHeight || 800;

      // Não permite que a barra de títulos saia da viewport
      const clampedLeft = Math.max(0, Math.min(viewW - 80, left));
      const clampedTop = Math.max(0, Math.min(viewH - 45, top));
      return { left: clampedLeft, top: clampedTop };
    };

    // 5. Motor de Arraste (Draggable)
    if (header && isDraggable) {
      let isDragging = false;
      let startX = 0;
      let startY = 0;
      let startLeft = 0;
      let startTop = 0;

      const onPointerDown = (e: PointerEvent) => {
        // Ignora cliques em botões, campos de texto, inputs, seletores e abas
        const target = e.target as HTMLElement;
        if (target?.closest?.('button, input, select, textarea, a, .tab-btn, .office-tab-btn, [data-no-drag]')) {
          return;
        }

        // Se estiver maximizado, não arrasta diretamente
        if (isMaximized) return;

        bringToFront();
        isDragging = true;
        startX = e.clientX;
        startY = e.clientY;

        const currentLeft = dialog.offsetLeft;
        const currentTop = dialog.offsetTop;
        startLeft = currentLeft;
        startTop = currentTop;

        dialog.classList.add('window-is-dragging');
        header.classList.add('header-is-grabbing');

        try {
          header.setPointerCapture(e.pointerId);
        } catch (_) {}

        e.preventDefault();
      };

      const onPointerMove = (e: PointerEvent) => {
        if (!isDragging) return;

        const dx = e.clientX - startX;
        const dy = e.clientY - startY;

        const targetLeft = startLeft + dx;
        const targetTop = startTop + dy;
        const { left, top } = clampPosition(targetLeft, targetTop);

        dialog.style.left = `${left}px`;
        dialog.style.top = `${top}px`;

        if (options.onMove) {
          options.onMove(left, top);
        }
      };

      const onPointerUp = (e: PointerEvent) => {
        if (!isDragging) return;
        isDragging = false;
        dialog.classList.remove('window-is-dragging');
        header.classList.remove('header-is-grabbing');

        try {
          header.releasePointerCapture(e.pointerId);
        } catch (_) {}
      };

      header.addEventListener?.('pointerdown', onPointerDown);
      header.addEventListener?.('pointermove', onPointerMove);
      header.addEventListener?.('pointerup', onPointerUp);
      header.addEventListener?.('pointercancel', onPointerUp);

      // Duplo clique na barra de título para alternar maximizar/restaurar
      if (allowMaximize) {
        header.addEventListener?.('dblclick', (e) => {
          const target = e.target as HTMLElement;
          if (target.closest?.('button, input, select, textarea, a, .tab-btn')) return;
          toggleMaximize();
        });
      }
    }

    // 6. Motor de Redimensionamento (Resizable)
    if (resizeHandle && isResizable) {
      let isResizing = false;
      let startX = 0;
      let startY = 0;
      let startW = 0;
      let startH = 0;

      const onResizePointerDown = (e: PointerEvent) => {
        bringToFront();
        isResizing = true;
        startX = e.clientX;
        startY = e.clientY;
        startW = dialog.offsetWidth;
        startH = dialog.offsetHeight;

        dialog.classList.add('window-is-resizing');

        try {
          resizeHandle.setPointerCapture(e.pointerId);
        } catch (_) {}

        e.preventDefault();
        e.stopPropagation();
      };

      const onResizePointerMove = (e: PointerEvent) => {
        if (!isResizing) return;

        const dx = e.clientX - startX;
        const dy = e.clientY - startY;

        const viewW = typeof window !== 'undefined' ? window.innerWidth : 1920;
        const viewH = typeof window !== 'undefined' ? window.innerHeight : 1080;

        const maxW = options.maxWidth ?? (viewW - dialog.offsetLeft - 10);
        const maxH = options.maxHeight ?? (viewH - dialog.offsetTop - 10);

        let newW = Math.max(minWidth, Math.min(maxW, startW + dx));
        let newH = Math.max(minHeight, Math.min(maxH, startH + dy));

        dialog.style.width = `${newW}px`;
        dialog.style.height = `${newH}px`;

        if (isMaximized) {
          isMaximized = false;
          dialog.classList.remove('is-maximized');
          if (maximizeBtn) {
            maximizeBtn.innerHTML = ModalWindowManager.maximizeIcon();
            maximizeBtn.title = 'Maximizar (Duplo clique na barra)';
            maximizeBtn.setAttribute('aria-label', maximizeBtn.title);
          }
        }

        if (options.onResize) {
          options.onResize(newW, newH);
        }
      };

      const onResizePointerUp = (e: PointerEvent) => {
        if (!isResizing) return;
        isResizing = false;
        dialog.classList.remove('window-is-resizing');

        try {
          resizeHandle.releasePointerCapture(e.pointerId);
        } catch (_) {}
      };

      resizeHandle.addEventListener?.('pointerdown', onResizePointerDown);
      resizeHandle.addEventListener?.('pointermove', onResizePointerMove);
      resizeHandle.addEventListener?.('pointerup', onResizePointerUp);
      resizeHandle.addEventListener?.('pointercancel', onResizePointerUp);
    }

    // 7. Funções de Maximizar e Restaurar
    const maximize = () => {
      if (isMaximized) return;

      const curW = dialog.offsetWidth;
      const curH = dialog.offsetHeight;
      if (curW > 100 && curH > 100) {
        savedState = {
          left: dialog.offsetLeft,
          top: dialog.offsetTop,
          width: curW,
          height: curH
        };
      } else if (!savedState) {
        const viewW = (typeof window !== 'undefined' ? window.innerWidth : 1280) || 1280;
        const viewH = (typeof window !== 'undefined' ? window.innerHeight : 800) || 800;
        const w = Math.min(viewW - 40, Math.max(minWidth, 840));
        const h = Math.min(viewH - 40, Math.max(minHeight, 560));
        savedState = {
          left: Math.max(10, Math.round((viewW - w) / 2)),
          top: Math.max(10, Math.round((viewH - h) / 2)),
          width: w,
          height: h
        };
      }

      isMaximized = true;
      dialog.classList?.add?.('is-maximized');

      if (dialog.style) {
        dialog.style.left = '12px';
        dialog.style.top = '12px';
        dialog.style.width = 'calc(100vw - 24px)';
        dialog.style.height = 'calc(100vh - 24px)';
      }

      if (maximizeBtn) {
        maximizeBtn.innerHTML = ModalWindowManager.restoreIcon();
        maximizeBtn.title = t('window.restore_tip');
        maximizeBtn.setAttribute('aria-label', maximizeBtn.title);
      }

      if (options.onMaximize) {
        options.onMaximize(true);
      }
    };

    const restore = () => {
      if (!isMaximized) return;

      isMaximized = false;
      dialog.classList?.remove?.('is-maximized');

      if (savedState && savedState.width > 100 && savedState.height > 100 && dialog.style) {
        dialog.style.left = `${savedState.left}px`;
        dialog.style.top = `${savedState.top}px`;
        dialog.style.width = `${savedState.width}px`;
        dialog.style.height = `${savedState.height}px`;
      } else {
        center();
      }

      if (maximizeBtn) {
        maximizeBtn.innerHTML = ModalWindowManager.maximizeIcon();
        maximizeBtn.title = t('window.maximize_tip');
        maximizeBtn.setAttribute('aria-label', maximizeBtn.title);
      }

      if (options.onMaximize) {
        options.onMaximize(false);
      }
    };

    const toggleMaximize = () => {
      if (isMaximized) {
        restore();
      } else {
        maximize();
      }
    };

    if (maximizeBtn) {
      maximizeBtn.addEventListener?.('click', (e) => {
        e.preventDefault?.();
        e.stopPropagation?.();
        toggleMaximize();
      });
    }

    const controller: IModalWindowController = {
      dialog,
      header: header || null,
      center,
      maximize,
      restore,
      toggleMaximize,
      isMaximized: () => isMaximized,
      bringToFront,
      destroy: () => {
        // Remove do registro global
        const idx = ModalWindowManager.registeredControllers.indexOf(controller);
        if (idx !== -1) {
          ModalWindowManager.registeredControllers.splice(idx, 1);
        }
      },
      setPosition: (left: number, top: number) => {
        const clamped = clampPosition(left, top);
        dialog.style.left = `${clamped.left}px`;
        dialog.style.top = `${clamped.top}px`;
      },
      setSize: (width: number, height: number) => {
        dialog.style.width = `${Math.max(minWidth, width)}px`;
        dialog.style.height = `${Math.max(minHeight, height)}px`;
      }
    };

    ModalWindowManager.registeredControllers.push(controller);

    // Se ainda não foi posicionado, agenda a centralização inicial
    if (!hasBeenPositioned) {
      // Usamos requestAnimationFrame ou setTimeout para aguardar renderização do layout
      if (typeof window !== 'undefined' && typeof window.requestAnimationFrame === 'function') {
        window.requestAnimationFrame(() => center());
      } else {
        center();
      }
    }

    return controller;
  }

  /**
   * Helper para gerar a marcação HTML canônica da barra de títulos para qualquer modal
   */
  public static createTitleBarHTML(params: {
    title: string;
    subtitle?: string;
    icon?: string;
    closeBtnId?: string;
    badgeText?: string;
    badgeClass?: string;
  }): string {
    const iconHtml = `<div class="modal-header-icon office-modal-app-icon">${params.icon && params.icon.trim().startsWith('<svg') ? params.icon : ModalWindowManager.dialogIcon()}</div>`;
    const subtitleHtml = params.subtitle ? `<div class="modal-subtitle">${params.subtitle}</div>` : '';
    const badgeHtml = params.badgeText 
      ? `<span class="modal-title-badge ${params.badgeClass || ''}">${params.badgeText}</span>` 
      : '';
    const closeId = params.closeBtnId || 'btn-close-modal';

    return `
      <div class="modal-header office-modal-titlebar fluent-window-header fluent-header-draggable">
        <div class="modal-title-group office-modal-title-group">
          ${iconHtml}
          <div class="office-modal-title-text">
            <div class="office-modal-title-line">
              <h2 class="modal-title">${params.title}</h2>
              ${badgeHtml}
            </div>
            ${subtitleHtml}
          </div>
        </div>
        <div class="window-controls">
          <button type="button" class="window-control-btn window-btn-maximize" title="${t('window.maximize_tip')}" aria-label="${t('window.maximize_tip')}">${ModalWindowManager.maximizeIcon()}</button>
          <button type="button" class="modal-close-btn window-control-btn window-btn-close" id="${closeId}" title="${t('window.close_tip')}" aria-label="${t('window.close_tip')}">${ModalWindowManager.closeIcon()}</button>
        </div>
      </div>
    `;
  }

  public static dialogIcon(): string {
    return `<svg viewBox="0 0 20 20" width="17" height="17" fill="none" stroke="currentColor" stroke-width="1.4" aria-hidden="true" focusable="false"><path d="M4.5 6.4 10 3.6l5.5 2.8v7.2L10 16.4l-5.5-2.8Z"/><circle cx="4.5" cy="6.4" r="1.45" fill="currentColor" stroke="none"/><circle cx="15.5" cy="6.4" r="1.45" fill="currentColor" stroke="none"/><circle cx="10" cy="13.6" r="1.45" fill="currentColor" stroke="none"/></svg>`;
  }

  private static maximizeIcon(): string {
    return `<svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true" focusable="false"><rect x="2.75" y="2.75" width="10.5" height="10.5" rx="1.25"/></svg>`;
  }

  private static restoreIcon(): string {
    return `<svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.5" aria-hidden="true" focusable="false"><rect x="2.75" y="4.75" width="8.5" height="8.5" rx="1.25"/><path d="M5.25 4.75V3.5A1.25 1.25 0 0 1 6.5 2.25h6A1.25 1.25 0 0 1 13.75 3.5v6a1.25 1.25 0 0 1-1.25 1.25h-1.25"/></svg>`;
  }

  public static closeIcon(): string {
    return `<svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true" focusable="false"><path d="m4 4 8 8M12 4l-8 8"/></svg>`;
  }
}
