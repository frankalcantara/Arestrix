export interface ICommand {
  /** Descricao legivel da acao executada (ex: 'Mover Nó', 'Criar Conexao') */
  readonly description: string;

  /** Executa a acao ou reaplica apos undo */
  execute(): void;

  /** Desfaz a acao revertendo o estado */
  undo(): void;

  /** Refaz a acao (opcional; por padrao chama execute) */
  redo?(): void;
}
