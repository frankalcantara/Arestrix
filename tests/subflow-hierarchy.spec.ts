import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { TaskflowCppGenerator } from '../src/compiler/TaskflowCppGenerator.ts';

describe('Subflows Hierárquicos e Navegação Aninhada', () => {
  it('deve realizar dive-in em um nó subflow e gerenciar a pilha de navegação', () => {
    const graph = new Graph();
    const parentNode = graph.createNode('subflow_cluster', 'Cluster_Agentes', 'subflow', 100, 100);

    expect(graph.isAtRoot()).toBe(true);
    expect(graph.getBreadcrumbs()).toHaveLength(1);

    // Entra no subgrafo
    const dived = graph.diveIntoSubflow(parentNode.id);
    expect(dived).toBe(true);
    expect(graph.isAtRoot()).toBe(false);

    // Breadcrumbs deve ter 2 níveis
    const breadcrumbs = graph.getBreadcrumbs();
    expect(breadcrumbs).toHaveLength(2);
    expect(breadcrumbs[1].title).toBe('Cluster_Agentes');

    // O grafo ativo agora tem os nós filhos do subfluxo
    const childNodes = graph.getAllNodes();
    expect(childNodes.length).toBeGreaterThan(0);
    expect(childNodes.some(n => n.label.includes('Sub_Worker'))).toBe(true);

    // Retorna ao pai
    const navigatedUp = graph.navigateUp();
    expect(navigatedUp).toBe(true);
    expect(graph.isAtRoot()).toBe(true);
    expect(graph.getBreadcrumbs()).toHaveLength(1);

    // O nó pai no grafo restaurado agora possui o childGraph salvo
    const restoredParent = graph.getNode(parentNode.id);
    expect(restoredParent?.childGraph).toBeDefined();
    expect(restoredParent?.childGraph?.nodes.length).toBeGreaterThan(0);
  });

  it('deve compilar código C++20 com tarefas filhas dentro de sf.emplace(...) para subflows aninhados', () => {
    const graph = new Graph();
    const subflowNode = graph.createNode('subflow_node', 'Processamento_Paralelo', 'subflow', 0, 0);

    // Entra e popula o subfluxo
    graph.diveIntoSubflow(subflowNode.id);
    // Adiciona um nó filho customizado
    graph.createNode('child_special', 'Subagente_Especializado', 'static', 0, 100);

    // Retorna ao pai para consolidar o schema
    graph.navigateUp();

    const cppCode = TaskflowCppGenerator.generateSource(graph);

    // Verifica que o código gerado instancia tarefas filhas no objeto sf
    expect(cppCode).toContain('tf.emplace([&ctx](tf::Subflow& sf) {');
    expect(cppCode).toContain('auto sf_task_');
    expect(cppCode).toContain('.name("Subagente_Especializado")');
  });

  it('deve compilar a árvore completa mesmo se o usuário estiver focado dentro do subflow', () => {
    const graph = new Graph();
    const rootA = graph.createNode('root_a', 'Raiz_A', 'static', -200, 0);
    const subflowNode = graph.createNode('sub_main', 'Subfluxo_Concorrente', 'subflow', 100, 0);
    graph.createEdge(rootA.id, rootA.outputs[0].id, subflowNode.id, subflowNode.inputs[0].id);

    // Usuário entra no subfluxo
    graph.diveIntoSubflow(subflowNode.id);
    expect(graph.isAtRoot()).toBe(false);

    // Gera o código C++ enquanto ainda está dentro do subfluxo
    const cppCode = TaskflowCppGenerator.generateSource(graph);

    // O código deve conter tanto a raiz quanto as tarefas internas do subfluxo
    expect(cppCode).toContain('.name("Raiz_A")');
    expect(cppCode).toContain('.name("Subfluxo_Concorrente")');
    expect(cppCode).toContain('tf.emplace([&ctx](tf::Subflow& sf)');
    expect(cppCode).toContain('sf.emplace(');
  });
});
