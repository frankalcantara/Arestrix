import { Graph } from './Graph.ts';
import { GraphValidator } from './Validator.ts';
import { globalEventBus } from './EventBus.ts';

/** Evento enviado pelo backend C++ durante a execução (window.onArestrixPipelineEvent). */
export interface IPipelineBackendEvent {
  type: 'phase' | 'log' | 'finished';
  phase?: 'compiling' | 'running';
  message?: string;
  line?: string;
  isError?: boolean;
  success?: boolean;
  exitCode?: number;
  elapsedMs?: number;
  error?: string;
}

/**
 * Orquestra "Compilar & Executar":
 *   validação estática -> backend C++ (gera código Taskflow, compila, executa) -> eventos em streaming.
 * O progresso chega de forma assíncrona via `handleBackendEvent` (registrado em main.ts).
 */
export class PipelineRunner {
  private static running = false;

  static isRunning(): boolean {
    return PipelineRunner.running;
  }

  static async run(graph: Graph): Promise<boolean> {
    if (PipelineRunner.running) {
      globalEventBus.emit('pipeline:log', { line: '[RUN] Já existe uma execução em andamento.', isError: true });
      return false;
    }

    // 1. Validação estática antes da compilação
    const validation = GraphValidator.validate(graph);
    if (!validation.isValid) {
      const err = validation.errors.map(e => `[${e.severity.toUpperCase()}] ${e.message}`).join('\n');
      globalEventBus.emit('pipeline:error', {
        stage: 'validation',
        error: `O grafo contém erros impeditivos de topologia:\n${err}`
      });
      return false;
    }

    // @ts-ignore
    if (typeof webui === 'undefined') {
      globalEventBus.emit('pipeline:error', {
        stage: 'backend',
        error: 'O backend C++ (WebUI) não está em execução. Abra o Arestrix Studio (arestrix.exe) para compilar e executar.'
      });
      return false;
    }

    globalEventBus.emit('telemetry:reset');
    globalEventBus.emit('pipeline:compiling', 'Enviando o grafo ao backend C++...');
    PipelineRunner.running = true;

    try {
      // @ts-ignore
      const raw = await webui.call('run_pipeline', JSON.stringify(graph.toJSON()));
      const res = typeof raw === 'string' && raw ? JSON.parse(raw) : { ok: false, error: 'Resposta vazia do backend' };
      if (!res.ok) {
        PipelineRunner.running = false;
        globalEventBus.emit('pipeline:error', { stage: 'backend', error: res.error || 'Falha ao iniciar a execução.' });
        return false;
      }
      return true;   // o término chega por handleBackendEvent({type:'finished'})
    } catch (err: any) {
      PipelineRunner.running = false;
      globalEventBus.emit('pipeline:error', {
        stage: 'network',
        error: `Falha de comunicação com o backend C++: ${err?.message || err}.`
      });
      return false;
    }
  }

  static async stop(): Promise<void> {
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') await webui.call('stop_pipeline', '');
    } catch (_) {
      /* sem backend */
    }
  }

  /** Traduz os eventos do backend em eventos do EventBus consumidos pelo Console e pela StatusBar. */
  static handleBackendEvent(ev: IPipelineBackendEvent): void {
    switch (ev.type) {
      case 'phase':
        if (ev.phase === 'running') globalEventBus.emit('pipeline:running', ev.message || 'Executando o pipeline...');
        else globalEventBus.emit('pipeline:compiling', ev.message || 'Compilando...');
        break;
      case 'log':
        globalEventBus.emit('pipeline:log', { line: ev.line || '', isError: !!ev.isError });
        break;
      case 'finished':
        PipelineRunner.running = false;
        if (ev.success) {
          globalEventBus.emit('pipeline:success', { stdout: '', elapsedMs: ev.elapsedMs || 0 });
        } else {
          globalEventBus.emit('pipeline:error', { stage: 'run', error: ev.error || 'A execução falhou.' });
        }
        break;
    }
  }
}
