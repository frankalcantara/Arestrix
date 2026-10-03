import { Graph } from './Graph.ts';
import { GraphValidator } from './Validator.ts';
import { TaskflowCppGenerator } from '../compiler/TaskflowCppGenerator.ts';
import { globalEventBus } from './EventBus.ts';

export class PipelineRunner {
  static async run(graph: Graph): Promise<boolean> {
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

    // 2. Extrai artefatos C++20 e de build
    const cppCode = TaskflowCppGenerator.generateSource(graph);
    const cmakeCode = TaskflowCppGenerator.generateCMakeLists(graph);
    const makeCode = TaskflowCppGenerator.generateMakefile(graph);
    const msvcScripts = TaskflowCppGenerator.generateMsvcBuildScript(graph);
    const meta = graph.getMetadata();

    globalEventBus.emit('pipeline:compiling', `Criando projeto em disco e disparando MSVC cl.exe (/std:c++20)...`);

    try {
      const response = await fetch('/api/run-pipeline', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          graphName: meta.graphName,
          cppCode,
          cmakeCode,
          makeCode,
          msvcBat: msvcScripts.bat,
          msvcPs1: msvcScripts.ps1,
          compiler: meta.compilerConfig.compiler,
          binaryName: meta.compilerConfig.outputBinaryName || 'pipeline_app'
        })
      });

      if (!response.ok) {
        const errText = await response.text();
        globalEventBus.emit('pipeline:error', {
          stage: 'server',
          error: `Erro HTTP do servidor de build: ${response.status} - ${errText}`
        });
        return false;
      }

      const result = await response.json();

      if (!result.success) {
        globalEventBus.emit('pipeline:error', {
          stage: result.stage || 'compile',
          error: result.stderr || result.stdout || 'Erro durante compilação ou execução do executável.'
        });
        return false;
      }

      if (result.telemetryEvents && result.telemetryEvents.length > 0) {
        this.playTelemetryEvents(result.telemetryEvents);
      }

      globalEventBus.emit('pipeline:success', {
        stdout: result.stdout,
        elapsedMs: result.elapsedMs || 0
      });
      return true;

    } catch (err: any) {
      globalEventBus.emit('pipeline:error', {
        stage: 'network',
        error: `Não foi possível conectar ao servidor de compilação: ${err?.message || err}. Certifique-se de estar rodando 'python server_teste.py'.`
      });
      return false;
    }
  }

  private static playTelemetryEvents(events: Array<{ type: string; taskId: string; worker?: number }>): void {
    if (!events || events.length === 0) return;

    let delay = 0;
    const stepDelay = 280; // ms entre passos para animação visual perceptível e limpa

    events.forEach((evt) => {
      setTimeout(() => {
        if (evt.type === 'enter') {
          globalEventBus.emit('telemetry:task-enter', { taskId: evt.taskId, worker: evt.worker });
        } else if (evt.type === 'exit') {
          globalEventBus.emit('telemetry:task-exit', { taskId: evt.taskId, worker: evt.worker });
        }
      }, delay);
      delay += stepDelay;
    });

    // Reset após a conclusão da animação
    setTimeout(() => {
      globalEventBus.emit('telemetry:reset');
    }, delay + 1800);
  }
}
