import { readWorkbook } from './ExcelWorkbookReader.ts';
import { AnalyzeOptions, analyzeRaw } from './WorkbookAnalyzer.ts';
import { WorkbookModel } from './WorkbookModel.ts';

async function sha256Hex(bytes: Uint8Array): Promise<string> {
  const subtle = globalThis.crypto?.subtle;
  if (!subtle) return '';
  const digest = await subtle.digest('SHA-256', bytes as unknown as ArrayBuffer);
  return [...new Uint8Array(digest)].map(b => b.toString(16).padStart(2, '0')).join('');
}

/** Ponto de entrada: bytes de um .xlsx → WorkbookModel. Sem rede, sem LLM. */
export async function analyzeWorkbookFile(bytes: Uint8Array, fileName: string, options: AnalyzeOptions = {}): Promise<WorkbookModel> {
  options.onProgress?.('lendo arquivo', 0);
  const [raw, sha256] = await Promise.all([readWorkbook(bytes), sha256Hex(bytes)]);
  return analyzeRaw(raw, { fileName, sizeBytes: bytes.byteLength, sha256 }, options);
}
