/// <reference lib="webworker" />
import { analyzeWorkbookFile } from './analyzeWorkbookFile.ts';

export type WorkerMessage =
  | { type: 'progress'; stage: string; fraction: number }
  | { type: 'done'; model: import('./WorkbookModel.ts').WorkbookModel }
  | { type: 'error'; code?: string; message: string };

self.onmessage = async (e: MessageEvent<{ bytes: Uint8Array; fileName: string }>) => {
  const post = (m: WorkerMessage): void => (self as unknown as Worker).postMessage(m);
  try {
    const model = await analyzeWorkbookFile(e.data.bytes, e.data.fileName, {
      onProgress: (stage, fraction) => post({ type: 'progress', stage, fraction })
    });
    post({ type: 'done', model });
  } catch (err) {
    post({ type: 'error', code: (err as { code?: string }).code, message: (err as Error).message });
  }
};
