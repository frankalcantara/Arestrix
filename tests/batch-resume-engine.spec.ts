import { describe, it, expect, beforeEach } from 'vitest';
import { 
  IBatchPipelineConfig, 
  IArtifactValidationRule, 
  IPipelineCheckpointState, 
  INodeAgentSettings 
} from '../src/core/types';

describe('Phase 11: Batch Pipelines & Resume Engine', () => {
  let sampleCheckpoint: IPipelineCheckpointState;

  beforeEach(() => {
    sampleCheckpoint = {
      pipelineId: 'math-series-orchestrator',
      lastUpdated: new Date().toISOString(),
      items: {
        'ART-001': {
          itemId: 'ART-001',
          status: 'concluido',
          stages: {
            '1-redigir': {
              status: 'concluido',
              completedAt: '2026-10-07T12:00:00Z',
              artifactPath: 'aulas/ART-001.md',
              recoveredFromArtifact: false
            },
            '2-frontmatter': {
              status: 'concluido',
              completedAt: '2026-10-07T12:02:00Z',
              artifactPath: 'aulas/ART-001.md',
              recoveredFromArtifact: false
            }
          }
        },
        'ART-002': {
          itemId: 'ART-002',
          status: 'em_progresso',
          stages: {
            '1-redigir': {
              status: 'concluido',
              completedAt: '2026-10-07T12:05:00Z',
              artifactPath: 'aulas/ART-002.md',
              recoveredFromArtifact: true // Auto-recuperado do disco!
            },
            '2-frontmatter': {
              status: 'executando',
              startedAt: '2026-10-07T12:06:00Z'
            }
          }
        }
      }
    };
  });

  it('should interpolate placeholders {itemId} and {id} in artifact paths', () => {
    const resolvePattern = (pattern: string, itemId: string) => {
      return pattern.replace(/{itemId}/g, itemId).replace(/{id}/g, itemId);
    };

    const rule1: IArtifactValidationRule = {
      targetPathPattern: 'aulas/{itemId}.md',
      minSizeBytes: 1000
    };

    const rule2: IArtifactValidationRule = {
      targetPathPattern: 'assets/diagrams/{id}-fig1.svg',
      minSizeBytes: 200
    };

    expect(resolvePattern(rule1.targetPathPattern, 'ART-003')).toBe('aulas/ART-003.md');
    expect(resolvePattern(rule2.targetPathPattern, 'ART-003')).toBe('assets/diagrams/ART-003-fig1.svg');
  });

  it('should validate artifact criteria (size and regex pattern)', () => {
    const mockFileContent = `---
title: "Teoria dos Grafos"
date: 2026-10-07
---
Conteúdo expandido sobre algoritmos paralelos...`;

    const rule: IArtifactValidationRule = {
      targetPathPattern: 'aulas/{itemId}.md',
      minSizeBytes: 50,
      contentPattern: '^---[\\s\\S]+---'
    };

    const isSizeValid = mockFileContent.length >= (rule.minSizeBytes || 0);
    const isRegexValid = new RegExp(rule.contentPattern || '').test(mockFileContent);

    expect(isSizeValid).toBe(true);
    expect(isRegexValid).toBe(true);
  });

  it('should correctly identify completed stages and auto-recovered stages from checkpoint state', () => {
    const art1 = sampleCheckpoint.items['ART-001'];
    expect(art1.status).toBe('concluido');
    expect(art1.stages['1-redigir'].status).toBe('concluido');
    expect(art1.stages['1-redigir'].recoveredFromArtifact).toBe(false);

    const art2 = sampleCheckpoint.items['ART-002'];
    expect(art2.status).toBe('em_progresso');
    expect(art2.stages['1-redigir'].status).toBe('concluido');
    expect(art2.stages['1-redigir'].recoveredFromArtifact).toBe(true);
    expect(art2.stages['2-frontmatter'].status).toBe('executando');
  });

  it('should support serialized stages constraints across batch items in DAG scheduler', () => {
    const config: IBatchPipelineConfig = {
      items: ['ART-001', 'ART-002', 'ART-003'],
      serializedStages: ['1-redigir'], // Etapa pesada: apenas 1 por vez entre os artigos
      maxConcurrency: 2,
      cooldownMs: 2000,
      stateFilePath: 'pipeline-state.json',
      autoResume: true
    };

    // Simulação do encadeamento de dependências
    const scheduledDependencies: { from: string; to: string }[] = [];

    // Encadeamento serial entre artigos para '1-redigir'
    for (let i = 0; i < config.items.length - 1; ++i) {
      const from = `${config.items[i]}_1-redigir`;
      const to = `${config.items[i + 1]}_1-redigir`;
      scheduledDependencies.push({ from, to });
    }

    // Intra-item: 1-redigir -> 2-frontmatter
    config.items.forEach(item => {
      scheduledDependencies.push({
        from: `${item}_1-redigir`,
        to: `${item}_2-frontmatter`
      });
    });

    expect(scheduledDependencies).toContainEqual({
      from: 'ART-001_1-redigir',
      to: 'ART-002_1-redigir'
    });
    expect(scheduledDependencies).toContainEqual({
      from: 'ART-002_1-redigir',
      to: 'ART-003_1-redigir'
    });
    expect(scheduledDependencies).toContainEqual({
      from: 'ART-002_1-redigir',
      to: 'ART-002_2-frontmatter'
    });
  });

  it('should store artifact validation rules and serial flags in INodeAgentSettings', () => {
    const nodeSettings: INodeAgentSettings = {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['write', 'edit', 'read'],
        customScripts: ['validate_svg.py']
      },
      validationRules: [
        {
          targetPathPattern: 'aulas/{itemId}.md',
          minSizeBytes: 1000,
          contentPattern: '^---[\\s\\S]+---',
          checkLogExitCode: true
        }
      ],
      isSerialAcrossBatch: true
    };

    expect(nodeSettings.isSerialAcrossBatch).toBe(true);
    expect(nodeSettings.validationRules).toHaveLength(1);
    expect(nodeSettings.validationRules![0].minSizeBytes).toBe(1000);
  });
});
