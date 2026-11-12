/**
 * Seleção de arquivos de um projeto importado como pipeline/agente.
 *
 * O input de pasta entrega todos os arquivos com `webkitRelativePath`. Este
 * módulo isola a escolha do manifesto e do pipeline principal para que a regra
 * possa ser testada sem DOM.
 */

export interface IProjectFileLike {
  name: string;
  webkitRelativePath?: string;
}

export interface IProjectFileSelection<T extends IProjectFileLike> {
  manifest?: T;
  pipeline?: T;
  rootName: string;
}

function normalizedPath(file: IProjectFileLike): string {
  const raw = (file.webkitRelativePath || file.name).replace(/\\/g, '/');
  return raw.startsWith('/') ? raw : `/${raw}`;
}

export function selectProjectPipelineFiles<T extends IProjectFileLike>(files: T[]): IProjectFileSelection<T> {
  const manifest = files.find(file => /\/arestrix\.project\.json$/i.test(normalizedPath(file)));
  const pipeline = files.find(file => /\/pipelines\/main\.json$/i.test(normalizedPath(file)))
    || files.find(file => /\/pipelines\/[^/]+\.json$/i.test(normalizedPath(file)));
  const firstPath = normalizedPath(files[0] || { name: 'projeto' });
  const rootName = firstPath.split('/').filter(Boolean)[0] || 'projeto';
  return { manifest, pipeline, rootName };
}
