// Utilitarios de caminho que aceitam '/' e '\\' (caminhos do Windows ou do backend em formato generico).

function lastSeparator(path: string): number {
  return Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
}

function trimTrailingSeparators(path: string): string {
  const trimmed = path.replace(/[\\/]+$/, '');
  return trimmed || path.slice(0, 1);
}

export function baseName(path: string): string {
  const trimmed = trimTrailingSeparators(path);
  return trimmed.slice(lastSeparator(trimmed) + 1);
}

// "C:/a/b" -> "C:/a", "C:/a" -> "C:/", "/a" -> "/", "a" -> "".
export function dirName(path: string): string {
  const trimmed = trimTrailingSeparators(path);
  const idx = lastSeparator(trimmed);
  if (idx < 0) return '';
  const dir = trimmed.slice(0, idx);
  if (/^[a-zA-Z]:$/.test(dir)) return `${dir}/`;
  return dir || trimmed.slice(0, 1);
}

export function joinPath(dir: string, name: string): string {
  if (!dir) return name;
  return /[\\/]$/.test(dir) ? `${dir}${name}` : `${dir}/${name}`;
}

export function stripExtension(name: string): string {
  const idx = name.lastIndexOf('.');
  return idx > 0 ? name.slice(0, idx) : name;
}

// Segmentos clicaveis de um caminho: "C:/a/b" -> C: (C:/), a (C:/a), b (C:/a/b).
export function pathCrumbs(path: string): Array<{ label: string; path: string }> {
  const norm = path.replace(/\\/g, '/');
  const crumbs: Array<{ label: string; path: string }> = [];
  let acc = '';
  let rest = norm;
  const drive = /^([a-zA-Z]:)\/?/.exec(norm);
  if (drive) {
    acc = drive[1];
    rest = norm.slice(drive[0].length);
    crumbs.push({ label: drive[1], path: `${drive[1]}/` });
  } else if (norm.startsWith('/')) {
    rest = norm.slice(1);
    crumbs.push({ label: '/', path: '/' });
  }
  for (const segment of rest.split('/').filter(Boolean)) {
    acc = (acc || norm.startsWith('/')) ? `${acc}/${segment}` : segment;
    crumbs.push({ label: segment, path: acc });
  }
  return crumbs;
}
