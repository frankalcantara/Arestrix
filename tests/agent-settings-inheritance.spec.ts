import { describe, it, expect, beforeEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { INodeData, DEFAULT_PIPELINE_DEFAULTS } from '../src/core/types.ts';

describe('Fase 7: Sistema de Configurações de Agentes, Sandbox & Herança', () => {
  let graph: Graph;
  let bus: EventBus;

  beforeEach(() => {
    bus = new EventBus();
    graph = new Graph(bus, { graphName: 'AgentInheritanceTest' });
  });

  describe('1. Padrões Globais do Pipeline', () => {
    it('deve inicializar com as configurações padrão de fábrica', () => {
      const defaults = graph.getGlobalDefaults();
      expect(defaults).toBeDefined();
      expect(defaults.model.provider).toBe('deepseek');
      expect(defaults.model.model).toBe('deepseek-flash');
      expect(defaults.sandbox.isolationLevel).toBe('container');
      expect(defaults.sandbox.maxMemoryMB).toBe(512);
      expect(defaults.codeMode.allowedTools).toContain('write');
      expect(defaults.codeMode.allowedTools).toContain('edit');
    });

    it('deve permitir atualizar configurações globais e emitir evento', () => {
      let eventPayload: any = null;
      bus.on('graph:defaults-updated', (payload) => {
        eventPayload = payload;
      });

      graph.updateGlobalDefaults({
        model: {
          provider: 'anthropic',
          model: 'claude-opus-5.5'
        },
        sandbox: {
          isolationLevel: 'microvm',
          network: 'offline',
          maxMemoryMB: 1024,
          timeoutSeconds: 600,
          readOnlyMounts: ['plano.md'],
          readWriteMounts: ['aulas/']
        }
      });

      const updated = graph.getGlobalDefaults();
      expect(updated.model.provider).toBe('anthropic');
      expect(updated.model.model).toBe('claude-opus-5.5');
      expect(updated.sandbox.isolationLevel).toBe('microvm');
      expect(updated.sandbox.maxMemoryMB).toBe(1024);

      expect(eventPayload).toBeDefined();
      expect(eventPayload.model.provider).toBe('anthropic');
    });
  });

  describe('2. Herança vs. Sobrescrita de Configurações nos Nós', () => {
    it('deve herdar padrões globais quando inheritSandbox for true', () => {
      const node: INodeData = {
        id: 'node_redigir',
        label: '1. Redigir Artigo',
        type: 'static',
        position: { x: 0, y: 0 },
        inputs: [],
        outputs: [],
        parameters: {},
        agentSettings: {
          inheritSandbox: true,
          inheritModel: true,
          inheritCodeMode: true
        }
      };
      graph.addNode(node);

      const retrieved = graph.getNode('node_redigir')!;
      expect(retrieved.agentSettings?.inheritSandbox).toBe(true);

      const globalDefaults = graph.getGlobalDefaults();
      // O nó herda o padrão
      const effectiveProvider = retrieved.agentSettings?.inheritModel
        ? globalDefaults.model.provider
        : retrieved.agentSettings?.model?.provider;

      expect(effectiveProvider).toBe('deepseek');
    });

    it('deve suportar ambiente heterogêneo: Nó A (Opus 5.5 em MicroVM) e Nó B (Jev) e Nó C (DeepSeek Host)', () => {
      // Nó A: Redação (Claude Opus 5.5 em MicroVM isolada)
      const nodeA: INodeData = {
        id: 'node_redacao',
        label: 'Redação Pesada',
        type: 'static',
        position: { x: 0, y: 0 },
        inputs: [],
        outputs: [],
        parameters: {},
        agentSettings: {
          inheritSandbox: false,
          sandbox: {
            isolationLevel: 'microvm',
            network: 'egress_llm_only',
            maxMemoryMB: 2048,
            timeoutSeconds: 600,
            readOnlyMounts: ['plano.md'],
            readWriteMounts: ['aulas/ART-001.md']
          },
          inheritModel: false,
          model: {
            provider: 'anthropic',
            model: 'claude-opus-5.5',
            skillPath: '.agents/redigir-math/SKILL.md'
          },
          inheritCodeMode: true
        }
      };

      // Nó B: Validação Visual (Jev com engine visual dedicada)
      const nodeB: INodeData = {
        id: 'node_jev',
        label: 'Validação Visual Jev',
        type: 'condition',
        position: { x: 300, y: 0 },
        inputs: [],
        outputs: [],
        parameters: {},
        agentSettings: {
          inheritSandbox: false,
          sandbox: {
            isolationLevel: 'container',
            network: 'offline',
            maxMemoryMB: 1024,
            timeoutSeconds: 120,
            readOnlyMounts: ['../matematica/assets/diagrams/'],
            readWriteMounts: ['aulas/ART-001-validacao-svg.md']
          },
          inheritModel: false,
          model: {
            provider: 'jev',
            model: 'jev-svg-validator'
          },
          inheritCodeMode: false,
          codeMode: {
            allowedTools: ['validate_svg', 'read'],
            customScripts: ['python scripts/audit_exercises.py']
          }
        }
      };

      // Nó C: Frontmatter (Herdando DeepSeek Flash fora da VM)
      const nodeC: INodeData = {
        id: 'node_frontmatter',
        label: 'Inserção de Frontmatter',
        type: 'static',
        position: { x: 600, y: 0 },
        inputs: [],
        outputs: [],
        parameters: {},
        agentSettings: {
          inheritSandbox: false,
          sandbox: {
            isolationLevel: 'host_direct',
            network: 'full',
            maxMemoryMB: 256,
            timeoutSeconds: 60,
            readOnlyMounts: [],
            readWriteMounts: ['aulas/ART-001.md']
          },
          inheritModel: true, // Herda DeepSeek Flash
          inheritCodeMode: true
        }
      };

      graph.addNode(nodeA);
      graph.addNode(nodeB);
      graph.addNode(nodeC);

      const nA = graph.getNode('node_redacao')!;
      const nB = graph.getNode('node_jev')!;
      const nC = graph.getNode('node_frontmatter')!;

      // Verificação do Nó A
      expect(nA.agentSettings?.model?.provider).toBe('anthropic');
      expect(nA.agentSettings?.model?.model).toBe('claude-opus-5.5');
      expect(nA.agentSettings?.sandbox?.isolationLevel).toBe('microvm');

      // Verificação do Nó B
      expect(nB.agentSettings?.model?.provider).toBe('jev');
      expect(nB.agentSettings?.codeMode?.allowedTools).toContain('validate_svg');
      expect(nB.agentSettings?.codeMode?.allowedTools).not.toContain('write');

      // Verificação do Nó C
      expect(nC.agentSettings?.sandbox?.isolationLevel).toBe('host_direct');
      expect(nC.agentSettings?.inheritModel).toBe(true);

      const globalDefaults = graph.getGlobalDefaults();
      const nCProvider = nC.agentSettings?.inheritModel
        ? globalDefaults.model.provider
        : nC.agentSettings?.model?.provider;
      expect(nCProvider).toBe('deepseek');
    });
  });
  describe('3. Nó Especializado de Agente Code Mode (Fase 8)', () => {
    it('deve instanciar nó de tipo agent com portas de flow e data padronizadas e herança ativa', () => {
      const node = graph.buildNode('node_agent_test', 'Agente Autônomo', 'agent', 100, 200);
      expect(node.type).toBe('agent');
      expect(node.inputs.length).toBe(2);
      expect(node.inputs[0].name).toBe('in');
      expect(node.inputs[1].name).toBe('prompt_in');
      expect(node.inputs[1].type).toBe('data');
      
      expect(node.outputs.length).toBe(3);
      expect(node.outputs[0].name).toBe('out');
      expect(node.outputs[1].name).toBe('artifact_out');
      expect(node.outputs[1].type).toBe('data');
      expect(node.outputs[2].name).toBe('call');
      expect(node.outputs[2].type).toBe('call');

      expect(node.agentSettings).toBeDefined();
      expect(node.agentSettings?.inheritSandbox).toBe(true);
      expect(node.agentSettings?.inheritModel).toBe(true);
      expect(node.agentSettings?.inheritCodeMode).toBe(true);
      expect(node.customLambdaBody).toContain('[Agent: Agente Autônomo]');
    });
  });
});
