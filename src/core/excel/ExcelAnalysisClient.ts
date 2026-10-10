import type { WorkbookModel } from './WorkbookModel.ts';
import type { WorkerMessage } from './analysis.worker.ts';

export type ProgressFn = (stage: string, fraction: number) => void;

/** Erro com o código vindo do leitor (ENCRYPTED, NOT_XLSX, CORRUPT), preservado através do Worker. */
export class AnalysisError extends Error {
  constructor(message: string, public code?: string) {
    super(message);
  }
}

async function createWorker(): Promise<Worker | null> {
  if (typeof Worker === 'undefined') return null;
  try {
    // inline: o build é um arquivo único (vite-plugin-singlefile), então o worker vai embutido
    const mod = await import('./analysis.worker.ts?worker&inline');
    return new mod.default();
  } catch {
    return null;
  }
}

/**
 * Analisa fora da thread da UI (Web Worker). Se o Worker não existir ou falhar ao subir,
 * cai para a thread principal, só que sem fatiar o trabalho.
 */
export async function analyzeInBackground(bytes: Uint8Array, fileName: string, onProgress?: ProgressFn): Promise<WorkbookModel> {
  const worker = await createWorker();
  if (!worker) {
    try {
      const { analyzeWorkbookFile } = await import('./analyzeWorkbookFile.ts');
      return await analyzeWorkbookFile(bytes, fileName, { onProgress });
    } catch (e) {
      throw new AnalysisError((e as Error).message, (e as { code?: string }).code);
    }
  }

  return new Promise<WorkbookModel>((resolve, reject) => {
    worker.onmessage = (ev: MessageEvent<WorkerMessage>) => {
      const m = ev.data;
      if (m.type === 'progress') onProgress?.(m.stage, m.fraction);
      else {
        worker.terminate();
        if (m.type === 'done') resolve(m.model);
        else reject(new AnalysisError(m.message, m.code));
      }
    };
    worker.onerror = ev => {
      worker.terminate();
      reject(new AnalysisError(ev.message || 'Falha no processo de análise.'));
    };
    worker.postMessage({ bytes, fileName });
  });
}
