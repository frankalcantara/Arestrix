import { describe, expect, it } from 'vitest';
import { selectProjectPipelineFiles } from '../src/core/templates/ProjectPipelineImport.ts';

describe('seleção de pipeline de um projeto importado', () => {
  it('encontra o manifesto e o pipelines/main.json dentro da pasta', () => {
    const files = [
      { name: 'README.md', webkitRelativePath: 'meu-projeto/README.md' },
      { name: 'arestrix.project.json', webkitRelativePath: 'meu-projeto/arestrix.project.json' },
      { name: 'main.json', webkitRelativePath: 'meu-projeto/pipelines/main.json' },
      { name: 'outro.json', webkitRelativePath: 'meu-projeto/pipelines/outro.json' }
    ];
    const selection = selectProjectPipelineFiles(files);
    expect(selection.rootName).toBe('meu-projeto');
    expect(selection.manifest?.name).toBe('arestrix.project.json');
    expect(selection.pipeline?.name).toBe('main.json');
  });

  it('usa o primeiro pipeline quando não existe main.json', () => {
    const files = [
      { name: 'projeto/pipelines/alpha.json' },
      { name: 'projeto/pipelines/beta.json' }
    ];
    const selection = selectProjectPipelineFiles(files);
    expect(selection.pipeline?.name).toBe('projeto/pipelines/alpha.json');
  });

  it('devolve indefinido quando a pasta não contém pipelines', () => {
    const selection = selectProjectPipelineFiles([
      { name: 'README.md', webkitRelativePath: 'vazio/README.md' }
    ]);
    expect(selection.rootName).toBe('vazio');
    expect(selection.manifest).toBeUndefined();
    expect(selection.pipeline).toBeUndefined();
  });
});
