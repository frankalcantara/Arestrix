import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';

describe('Subflows Hierarquicos e Navegacao Aninhada', () => {
  it('deve realizar dive-in em um no subflow e gerenciar a pilha de navegacao', () => {
    const graph = new Graph();
    const parentNode = graph.createNode('subflow_cluster', 'Cluster_Agentes', 'subflow', 100, 100);

    expect(graph.isAtRoot()).toBe(true);
    expect(graph.getBreadcrumbs()).toHaveLength(1);

    // Entra no subgrafo
    const dived = graph.diveIntoSubflow(parentNode.id);
    expect(dived).toBe(true);
    expect(graph.isAtRoot()).toBe(false);

    // Breadcrumbs deve ter 2 niveis
    const breadcrumbs = graph.getBreadcrumbs();
    expect(breadcrumbs).toHaveLength(2);
    expect(breadcrumbs[1].title).toBe('Cluster_Agentes');

    // O grafo ativo agora tem os nos filhos do subfluxo
    const childNodes = graph.getAllNodes();
    expect(childNodes.length).toBeGreaterThan(0);
    expect(childNodes.some(n => n.label.includes('Sub_Worker'))).toBe(true);

    // Retorna ao pai
    const navigatedUp = graph.navigateUp();
    expect(navigatedUp).toBe(true);
    expect(graph.isAtRoot()).toBe(true);
    expect(graph.getBreadcrumbs()).toHaveLength(1);

    // O no pai no grafo restaurado agora possui o childGraph salvo
    const restoredParent = graph.getNode(parentNode.id);
    expect(restoredParent?.childGraph).toBeDefined();
    expect(restoredParent?.childGraph?.nodes.length).toBeGreaterThan(0);
  });

  it('deve serializar a arvore completa para JSON contendo as tarefas filhas aninhadas', () => {
    const graph = new Graph();
    const subflowNode = graph.createNode('subflow_node', 'Processamento_Paralelo', 'subflow', 0, 0);

    // Entra e popula o subfluxo
    graph.diveIntoSubflow(subflowNode.id);
    // Adiciona um no filho customizado
    graph.createNode('child_special', 'Subagente_Especializado', 'static', 0, 100);

    // Retorna ao pai para consolidar o schema
    graph.navigateUp();

    const jsonSchema = graph.toJSON();

    // Verifica que o schema contem as tarefas filhas dentro do childGraph
    const subflowSchemaNode = jsonSchema.nodes.find(n => n.id === subflowNode.id);
    expect(subflowSchemaNode).toBeDefined();
    expect(subflowSchemaNode?.childGraph).toBeDefined();
    expect(subflowSchemaNode?.childGraph?.nodes.some(n => n.label === 'Subagente_Especializado')).toBe(true);
  });
});

describe('Gravacao do pipeline dentro de um subfluxo', () => {
  it('savePipeline grava o grafo raiz e nao apenas o subgrafo aberto', async () => {
    const { ProjectManager } = await import('../src/core/ProjectManager.ts');
    const { EventBus } = await import('../src/core/EventBus.ts');
    const bus = new EventBus();
    const graph = new Graph(bus);
    graph.createNode('raiz_a', 'Vertice Raiz', 'static', 0, 0);
    const sf = graph.createNode('sf_1', 'Cluster', 'subflow', 200, 0);
    graph.diveIntoSubflow(sf.id);
    expect(graph.isAtRoot()).toBe(false);

    const pm = ProjectManager.getInstance(bus);
    await pm.savePipeline(graph);
    const stored = JSON.parse((pm as any).pipelineStorage.get(pm.getCurrentProject().projectId));
    const ids = stored.nodes.map((n: any) => n.id);
    expect(ids).toContain('raiz_a');
    expect(ids).toContain('sf_1');
    const cluster = stored.nodes.find((n: any) => n.id === 'sf_1');
    expect(cluster.childGraph.nodes.length).toBeGreaterThan(0);
  });
});
