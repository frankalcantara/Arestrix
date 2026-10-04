import { Graph } from './Graph.ts';
import { GraphValidator } from './Validator.ts';
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

    globalEventBus.emit('pipeline:compiling', `Criando projeto C++ via WebUI backend...`);

    try {
      let success = false;
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        await webui.call("generate_cpp_code", "");
        success = true; // Se não lançou exceção, o C++ terminou.
      } else {
        throw new Error("WebUI não detectado. O backend C++ não está rodando.");
      }

      if (!success) {
        globalEventBus.emit('pipeline:error', {
          stage: 'compile',
          error: 'Falha durante a geração do código C++. Verifique o console.'
        });
        return false;
      }

      globalEventBus.emit('pipeline:success', {
        stdout: "Arquivos gerados com sucesso na pasta Output!",
        elapsedMs: 0
      });
      return true;

    } catch (err: any) {
      globalEventBus.emit('pipeline:error', {
        stage: 'network',
        error: `Falha de comunicação com o backend C++: ${err?.message || err}.`
      });
      return false;
    }
  }
}
