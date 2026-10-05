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
