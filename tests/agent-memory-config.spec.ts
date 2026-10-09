import { describe, it, expect, beforeEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { INodeData, DEFAULT_NODE_MEMORY_CONFIG, INodeMemoryConfig } from '../src/core/types.ts';
import { UpdateNodeCommand } from '../src/core/history/GraphCommands.ts';

describe('Fase 12: Arquitetura de Memória Heterogênea (Opt-in, Persistência e Escopo por Projeto)', () => {
  let graph: Graph;
  let bus: EventBus;

  beforeEach(() => {
    bus = new EventBus();
    graph = new Graph(bus, { 
      graphName: 'ProjetoAulasMatematica',
      projectId: 'proj_matematica_01'
    });
  });

  describe('1. Memória NÃO é Padrão e NEM Obrigatória (Opt-in)', () => {
    it('deve inicializar padrões globais com memória desativada (enabled: false) e efêmera (persistent: false)', () => {
      const defaults = graph.getGlobalDefaults();
      expect(defaults.memory).toBeDefined();
      expect(defaults.memory?.enabled).toBe(false);
      expect(defaults.memory?.persistent).toBe(false);
      expect(defaults.memory?.readBlackboard).toBe(false);
      expect(defaults.memory?.writeBlackboard).toBe(false);
      expect(defaults.memory?.semanticLessons).toBe(false);
      expect(defaults.memory?.recordLessons).toBe(false);
    });

    it('ao criar um nó do tipo agent, deve herdar memória desativada por padrão', () => {
      const agentNode = graph.buildNode('node_agente_1', 'Redator de Conteúdo', 'agent', 100, 100);
      graph.addNode(agentNode);

      const retrieved = graph.getNode('node_agente_1')!;
      expect(retrieved.agentSettings?.inheritMemory).toBe(true);

      // Verificação efetiva de memória
      const globalDefaults = graph.getGlobalDefaults();
      const effectiveMemory = retrieved.agentSettings?.inheritMemory !== false
        ? globalDefaults.memory
        : retrieved.agentSettings?.memory;

      expect(effectiveMemory?.enabled).toBe(false);
      expect(effectiveMemory?.persistent).toBe(false);
    });
  });

  describe('2. Seleção Granular de Agentes em Cada Projeto', () => {
    it('deve permitir que o usuário ative memória apenas para agentes selecionados no projeto', () => {
      // Agente 1: Redator (sem memória, isolado)
      const redator = graph.buildNode('redator', 'Redator Isolado', 'agent', 0, 0);
      graph.addNode(redator);

      // Agente 2: Revisor (com memória ativada, efêmera, lê Blackboard)
      const revisorMemory: INodeMemoryConfig = {
        enabled: true,
        persistent: false, // Efêmera (apenas nesta sessão)
        projectId: graph.getProjectId(),
        readBlackboard: true,
        writeBlackboard: false,
        semanticLessons: true,
        recordLessons: false,
        allowedKeys: ['resumo_artigo', 'diretrizes'],
        domains: ['markdown', 'revisao']
      };

      const revisor = graph.buildNode('revisor', 'Revisor Consciente', 'agent', 250, 0);
      revisor.agentSettings = {
        ...revisor.agentSettings!,
        inheritMemory: false,
        memory: revisorMemory
      };
      graph.addNode(revisor);

      // Agente 3: Auditor SVG (com memória persistente entre sessões)
      const auditorMemory: INodeMemoryConfig = {
        enabled: true,
        persistent: true, // Persistente entre sessões (disco)
        projectId: graph.getProjectId(),
        readBlackboard: true,
        writeBlackboard: true,
        semanticLessons: true,
        recordLessons: true,
        allowedKeys: ['svg_specs', 'erros_auditados'],
        domains: ['svg_typography', 'validation']
      };

      const auditor = graph.buildNode('auditor', 'Auditor Persistente', 'agent', 500, 0);
      auditor.agentSettings = {
        ...auditor.agentSettings!,
        inheritMemory: false,
        memory: auditorMemory
      };
      graph.addNode(auditor);

      // Verificações
      const n1 = graph.getNode('redator')!;
      const n2 = graph.getNode('revisor')!;
      const n3 = graph.getNode('auditor')!;

      // Nó 1 permanece desativado
      expect(n1.agentSettings?.inheritMemory).toBe(true);
      expect(graph.getGlobalDefaults().memory?.enabled).toBe(false);

      // Nó 2 tem memória efêmera da sessão
      expect(n2.agentSettings?.inheritMemory).toBe(false);
      expect(n2.agentSettings?.memory?.enabled).toBe(true);
      expect(n2.agentSettings?.memory?.persistent).toBe(false);
      expect(n2.agentSettings?.memory?.readBlackboard).toBe(true);
      expect(n2.agentSettings?.memory?.writeBlackboard).toBe(false);

      // Nó 3 tem memória persistente entre sessões
      expect(n3.agentSettings?.inheritMemory).toBe(false);
      expect(n3.agentSettings?.memory?.enabled).toBe(true);
      expect(n3.agentSettings?.memory?.persistent).toBe(true);
      expect(n3.agentSettings?.memory?.recordLessons).toBe(true);
    });
  });

  describe('3. Isolamento Estrito por Projeto (projectId)', () => {
    it('deve associar o identificador de projeto correto e permitir namespaces distintos', () => {
      expect(graph.getProjectId()).toBe('proj_matematica_01');

      // Segundo projeto (ex: Ciências)
      const graphCiencias = new Graph(new EventBus(), {
        graphName: 'ProjetoCiencias',
        projectId: 'proj_ciencias_02'
      });
      expect(graphCiencias.getProjectId()).toBe('proj_ciencias_02');

      const memoryMatematica: INodeMemoryConfig = {
        enabled: true,
        persistent: true,
        projectId: graph.getProjectId(),
        readBlackboard: true,
        writeBlackboard: true,
        semanticLessons: true,
        recordLessons: true
      };

      const memoryCiencias: INodeMemoryConfig = {
        enabled: true,
        persistent: false,
        projectId: graphCiencias.getProjectId(),
        readBlackboard: true,
        writeBlackboard: true,
        semanticLessons: false,
        recordLessons: false
      };

      expect(memoryMatematica.projectId).toBe('proj_matematica_01');
      expect(memoryCiencias.projectId).toBe('proj_ciencias_02');
      expect(memoryMatematica.projectId).not.toBe(memoryCiencias.projectId);
    });
  });

  describe('4. Histórico de Edição (Undo / Redo) nas Configurações de Memória', () => {
    it('deve permitir alterar memória com UpdateNodeCommand e reverter via Undo/Redo', () => {
      const node = graph.buildNode('agente_flexivel', 'Agente Flexível', 'agent', 0, 0);
      graph.addNode(node);

      const oldSettings = { ...node.agentSettings };
      const newSettings = {
        ...node.agentSettings!,
        inheritMemory: false,
        memory: {
          enabled: true,
          persistent: true,
          projectId: graph.getProjectId(),
          readBlackboard: true,
          writeBlackboard: true,
          semanticLessons: true,
          recordLessons: true
        }
      };

      const cmd = new UpdateNodeCommand(
        graph,
        'agente_flexivel',
        { agentSettings: oldSettings },
        { agentSettings: newSettings },
        'Ativar memória persistente'
      );

      // Executa
      graph.commandManager.execute(cmd);
      let current = graph.getNode('agente_flexivel')!;
      expect(current.agentSettings?.memory?.enabled).toBe(true);
      expect(current.agentSettings?.memory?.persistent).toBe(true);

      // Desfaz (Undo)
      expect(graph.canUndo()).toBe(true);
      graph.undo();
      current = graph.getNode('agente_flexivel')!;
      expect(current.agentSettings?.inheritMemory).toBe(true);
      expect(current.agentSettings?.memory).toBeUndefined();

      // Refaz (Redo)
      expect(graph.canRedo()).toBe(true);
      graph.redo();
      current = graph.getNode('agente_flexivel')!;
      expect(current.agentSettings?.memory?.enabled).toBe(true);
      expect(current.agentSettings?.memory?.persistent).toBe(true);
    });
  });
});
