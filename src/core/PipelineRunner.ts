import { Graph } from './Graph.ts';
import { GraphValidator } from './Validator.ts';
import { globalEventBus } from './EventBus.ts';
import { GitSyncManager } from './git/GitSyncManager.ts';

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

    // Auto-commit e push no repositório Git a cada execução
    try {
      await GitSyncManager.getInstance().syncOnRun();
    } catch (e) {
      console.warn('[PipelineRunner] Aviso no Git sync antes de executar:', e);
    }

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
    } finally {
      PipelineRunner.running = false;
      globalEventBus.emit('pipeline:stopped');
      globalEventBus.emit('pipeline:log', { line: '[RUN] Execução interrompida pelo usuário.', isError: true });
    }
  }

  /** Traduz os eventos do backend em eventos do EventBus consumidos pelo Console, Grafo e StatusBar. */
  static handleBackendEvent(ev: IPipelineBackendEvent): void {
    switch (ev.type) {
      case 'phase':
        if (ev.phase === 'running') {
          globalEventBus.emit('pipeline:running', ev.message || 'Executando o pipeline...');
        } else {
          globalEventBus.emit('pipeline:compiling', ev.message || 'Compilando...');
        }
        break;
      case 'log': {
        const line = ev.line || '';
        globalEventBus.emit('pipeline:log', { line, isError: !!ev.isError });
        PipelineRunner.parseLogTelemetry(line);
        break;
      }
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

  /**
   * Extrai eventos de execução de linhas de log (Taskflow Observer, Batch Runner ou JSON inline)
   * e os propaga para o grafo em tempo real.
   */
  static parseLogTelemetry(line: string): void {
    if (!line) return;
    const trimmed = line.trim();

    // 1. JSON de telemetria de agente caso chegue pelo canal de log
    const agentPrefix = '[NF_TELEMETRY:AGENT] ';
    if (trimmed.startsWith(agentPrefix)) {
      try {
        const parsed = JSON.parse(trimmed.slice(agentPrefix.length));
        const ev = parsed && parsed.type === 'agent:telemetry' && parsed.event
          ? parsed.event
          : (parsed && parsed.nodeId && parsed.type ? parsed : null);
        if (ev) {
          globalEventBus.emit('agent:telemetry', ev);
          if (ev.nodeId) {
            globalEventBus.emit(`agent:telemetry:${ev.nodeId}`, ev);
          }
        }
      } catch (_) {}
      return;
    }

    // 2. Observer nativo Taskflow: [NF_TELEMETRY:ENTER] Task=<name> Worker=<id>
    const enterMatch = trimmed.match(/^\[NF_TELEMETRY:ENTER\]\s+Task=(.+?)(?:\s+Worker=(\d+))?$/);
    if (enterMatch) {
      const taskId = enterMatch[1].trim();
      const worker = enterMatch[2] ? parseInt(enterMatch[2], 10) : undefined;
      if (taskId && taskId !== 'Pipeline_Bootstrap') {
        globalEventBus.emit('telemetry:task-enter', { taskId, worker });
      }
      return;
    }

    // 3. Observer nativo Taskflow: [NF_TELEMETRY:EXIT] Task=<name> Worker=<id>
    const exitMatch = trimmed.match(/^\[NF_TELEMETRY:EXIT\]\s+Task=(.+?)(?:\s+Worker=(\d+))?$/);
    if (exitMatch) {
      const taskId = exitMatch[1].trim();
      const worker = exitMatch[2] ? parseInt(exitMatch[2], 10) : undefined;
      if (taskId && taskId !== 'Pipeline_Bootstrap') {
        globalEventBus.emit('telemetry:task-exit', { taskId, worker });
      }
      return;
    }

    // 4. Motor em Lote (Batch): [EXECUTANDO] <itemId> :: <label>
    const batchExecMatch = trimmed.match(/^\[EXECUTANDO\]\s+(.+?)\s+::\s+(.+)$/);
    if (batchExecMatch) {
      globalEventBus.emit('telemetry:task-enter', {
        taskId: batchExecMatch[2].trim(),
        itemId: batchExecMatch[1].trim()
      });
      return;
    }

    // 5. Checkpoint reaproveitado em Lote: [CHECKPOINT:RESUME] <itemId> :: <label>
    const batchResumeMatch = trimmed.match(/^\[CHECKPOINT:RESUME\]\s+(.+?)\s+::\s+(.+?)(?:\s+ja concluido.*)?$/i);
    if (batchResumeMatch) {
      globalEventBus.emit('telemetry:task-exit', {
        taskId: batchResumeMatch[2].trim(),
        itemId: batchResumeMatch[1].trim(),
        cached: true
      });
      return;
    }

    // 6. Runtime de Agente C++: [Agent <label>] iniciando (execucao ...)
    const agentStartMatch = trimmed.match(/^\[Agent\s+(.+?)\]\s+iniciando\b/i);
    if (agentStartMatch) {
      globalEventBus.emit('telemetry:task-enter', {
        taskId: agentStartMatch[1].trim()
      });
      return;
    }

    // 7. Runtime de Agente C++: [Agent <label>] concluido (tokens: ...)
    const agentDoneMatch = trimmed.match(/^\[Agent\s+(.+?)\]\s+concluido\b/i);
    if (agentDoneMatch) {
      globalEventBus.emit('telemetry:task-exit', {
        taskId: agentDoneMatch[1].trim()
      });
      return;
    }

    // 8. Tarefa estática padrão: Executando <label>
    const staticExecMatch = trimmed.match(/^Executando\s+(.+)$/);
    if (staticExecMatch) {
      globalEventBus.emit('telemetry:task-enter', {
        taskId: staticExecMatch[1].trim()
      });
    }
  }
}
