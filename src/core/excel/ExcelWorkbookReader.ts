import ExcelJS from 'exceljs';
import { RawCell, RawSheet, RawWorkbook } from './RawWorkbook.ts';

export type ReadErrorCode = 'ENCRYPTED' | 'LEGACY_XLS' | 'NOT_XLSX' | 'CORRUPT';

export class WorkbookReadError extends Error {
  constructor(public code: ReadErrorCode, message: string) {
    super(message);
    this.name = 'WorkbookReadError';
  }
}

const OLE_MAGIC = [0xd0, 0xcf, 0x11, 0xe0];

function startsWith(bytes: Uint8Array, magic: number[]): boolean {
  return magic.every((b, i) => bytes[i] === b);
}

/** Nomes de arquivo dentro de um zip ficam em texto puro nos cabeçalhos; basta procurar a sequência. */
function containsAscii(bytes: Uint8Array, needle: string): boolean {
  const n = Array.from(needle, ch => ch.charCodeAt(0));
  outer: for (let i = 0; i <= bytes.length - n.length; i++) {
    if (bytes[i] !== n[0]) continue;
    for (let j = 1; j < n.length; j++) if (bytes[i + j] !== n[j]) continue outer;
    return true;
  }
  return false;
}

/** Nomes de fluxo em contêineres OLE são UTF-16LE. */
function containsUtf16(bytes: Uint8Array, needle: string): boolean {
  const n = new Uint8Array(needle.length * 2);
  for (let i = 0; i < needle.length; i++) n[i * 2] = needle.charCodeAt(i);
  outer: for (let i = 0; i <= bytes.length - n.length; i++) {
    for (let j = 0; j < n.length; j++) if (bytes[i + j] !== n[j]) continue outer;
    return true;
  }
  return false;
}

function toRawCell(cell: ExcelJS.Cell): RawCell | null {
  if (cell.type === ExcelJS.ValueType.Merge) return null; // escravas de mescla repetem o valor da mestra
  const v = cell.value as unknown;
  if (v === null || v === undefined) return null;

  const base = { row: Number(cell.row), col: Number(cell.col), numFmt: cell.numFmt || undefined };
  const out: RawCell = { ...base, value: null };
  if (!out.numFmt) delete out.numFmt;

  const assign = (x: unknown): void => {
    if (x === null || x === undefined) out.value = null;
    else if (x instanceof Date || typeof x === 'string' || typeof x === 'number' || typeof x === 'boolean') out.value = x;
    else if (typeof x === 'object' && 'error' in x) out.error = String((x as { error: unknown }).error);
    else if (typeof x === 'object' && 'richText' in x) out.value = (x as { richText: { text: string }[] }).richText.map(t => t.text).join('');
    else if (typeof x === 'object' && 'text' in x) out.value = String((x as { text: unknown }).text);
    else out.value = String(x);
  };

  if (typeof v === 'object' && !(v instanceof Date) && ('formula' in v || 'sharedFormula' in v)) {
    const fv = v as { formula?: string; result?: unknown; shareType?: string };
    const formula = cell.formula ?? fv.formula;
    if (formula === undefined) return null;
    out.formula = formula;
    if (fv.shareType === 'array') out.arrayFormula = true;
    assign(fv.result);
  } else {
    assign(v);
  }
  return out;
}

export async function readWorkbook(data: Uint8Array): Promise<RawWorkbook> {
  if (startsWith(data, OLE_MAGIC)) {
    if (containsUtf16(data, 'EncryptedPackage')) {
      throw new WorkbookReadError('ENCRYPTED', 'Arquivo protegido por senha. A importação não tenta abri-lo.');
    }
    throw new WorkbookReadError('LEGACY_XLS', 'Formato .xls antigo ainda não é suportado. Abra no Excel e salve como .xlsx para analisar.');
  }
  if (data[0] !== 0x50 || data[1] !== 0x4b) {
    throw new WorkbookReadError('NOT_XLSX', 'O arquivo não é um .xlsx (esperado um contêiner zip).');
  }

  const wb = new ExcelJS.Workbook();
  try {
    await wb.xlsx.load(data as unknown as ArrayBuffer);
  } catch (e) {
    throw new WorkbookReadError('CORRUPT', `Não foi possível ler o arquivo: ${(e as Error).message}`);
  }

  const sheets: RawSheet[] = [];
  wb.eachSheet(ws => {
    const cells: RawCell[] = [];
    ws.eachRow({ includeEmpty: false }, row => {
      row.eachCell({ includeEmpty: false }, cell => {
        const c = toRawCell(cell);
        if (c) cells.push(c);
      });
    });
    const model = ws.model as unknown as { merges?: string[]; sheetProtection?: unknown };
    sheets.push({
      name: ws.name,
      state: ws.state === 'hidden' || ws.state === 'veryHidden' ? ws.state : 'visible',
      protected: Boolean(model.sheetProtection),
      mergedRanges: [...(model.merges ?? [])].sort(),
      cells
    });
  });

  const definedNames: { name: string; ref: string }[] = [];
  const dn = (wb.definedNames as unknown as { model?: { name: string; ranges: string[] }[] }).model ?? [];
  for (const n of dn) {
    if (!n.name.startsWith('_xlnm')) definedNames.push({ name: n.name, ref: n.ranges.join(',') });
  }

  return { hasMacros: containsAscii(data, 'xl/vbaProject.bin'), sheets, definedNames };
}
