import { ICommand } from './ICommand.ts';
import { EventBus, globalEventBus } from '../EventBus.ts';

export interface IHistoryState {
  canUndo: boolean;
  canRedo: boolean;
  undoDesc: string | null;
  redoDesc: string | null;
}

export class CommandManager {
  private undoStack: ICommand[] = [];
  private redoStack: ICommand[] = [];
  private maxHistorySize: number;
  private bus: EventBus;
  private isExecutingInternal: boolean = false;

  constructor(bus: EventBus = globalEventBus, maxHistorySize: number = 100) {
    this.bus = bus;
    this.maxHistorySize = maxHistorySize;
  }

  /**
   * Executa um novo comando e o registra no histórico de desfazer,
   * limpando a pilha de refazer.
   */
  execute(command: ICommand): void {
    if (this.isExecutingInternal) return;

    this.isExecutingInternal = true;
    try {
      command.execute();
    } finally {
      this.isExecutingInternal = false;
    }

    this.pushUndo(command);
  }

  /**
   * Registra um comando que já foi executado no canvas (ex: arrasto terminado ou edição concluída),
   * adicionando-o ao topo do histórico de desfazer e limpando refazer.
   */
  record(command: ICommand): void {
    this.pushUndo(command);
  }

  private pushUndo(command: ICommand): void {
    this.undoStack.push(command);
    if (this.undoStack.length > this.maxHistorySize) {
      this.undoStack.shift();
    }
    this.redoStack = [];
    this.notifyChange();
  }

  /**
   * Desfaz o último comando da pilha.
   * @returns true se desfez com sucesso, false se não havia comandos para desfazer.
   */
  undo(): boolean {
    if (this.undoStack.length === 0 || this.isExecutingInternal) {
      return false;
    }

    const command = this.undoStack.pop()!;
    this.isExecutingInternal = true;
    try {
      command.undo();
    } finally {
      this.isExecutingInternal = false;
    }

    this.redoStack.push(command);
    this.notifyChange();
    return true;
  }

  /**
   * Refaz o último comando desfeito.
   * @returns true se refez com sucesso, false se não havia comandos para refazer.
   */
  redo(): boolean {
    if (this.redoStack.length === 0 || this.isExecutingInternal) {
      return false;
    }

    const command = this.redoStack.pop()!;
    this.isExecutingInternal = true;
    try {
      if (typeof command.redo === 'function') {
        command.redo();
      } else {
        command.execute();
      }
    } finally {
      this.isExecutingInternal = false;
    }

    this.undoStack.push(command);
    this.notifyChange();
    return true;
  }

  canUndo(): boolean {
    return this.undoStack.length > 0;
  }

  canRedo(): boolean {
    return this.redoStack.length > 0;
  }

  getUndoDescription(): string | null {
    if (this.undoStack.length === 0) return null;
    return this.undoStack[this.undoStack.length - 1].description;
  }

  getRedoDescription(): string | null {
    if (this.redoStack.length === 0) return null;
    return this.redoStack[this.redoStack.length - 1].description;
  }

  clear(): void {
    this.undoStack = [];
    this.redoStack = [];
    this.notifyChange();
  }

  private notifyChange(): void {
    const state: IHistoryState = {
      canUndo: this.canUndo(),
      canRedo: this.canRedo(),
      undoDesc: this.getUndoDescription(),
      redoDesc: this.getRedoDescription()
    };
    this.bus.emit('history:changed', state);
  }
}
