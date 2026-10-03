import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { TaskflowCppGenerator } from '../src/compiler/TaskflowCppGenerator.ts';

describe('Compilador e Gerador de Código Taskflow C++20', () => {
  it('deve gerar código C++20 com includes, lambdas e precedências', () => {
    const graph = new Graph();
    const a = graph.createNode('agent_a', 'Processador_Dados', 'static', 0, 0);
    const b = graph.createNode('agent_b', 'Treinamento_Modelo', 'static', 200, 0);

    graph.createEdge(a.id, a.outputs[0].id, b.id, b.inputs[0].id);

    const code = TaskflowCppGenerator.generateSource(graph);

    expect(code).toContain('#include <taskflow/taskflow.hpp>');
    expect(code).toContain('#include <syncstream>');
    expect(code).toContain('tf::Taskflow tf("AgentTaskflowGraph");');
    expect(code).toContain('auto task_agent_a = tf.emplace(');
    expect(code).toContain('auto task_agent_b = tf.emplace(');
    expect(code).toContain('task_agent_a.precede(task_agent_b);');
    expect(code).toContain('executor.run(tf).wait();');
  });

  it('deve incluir tf::CriticalSection quando nós críticos existirem', () => {
    const graph = new Graph();
    const crit = graph.createNode('crit_node', 'Acesso_Exclusivo', 'critical', 0, 0);
    const dest = graph.createNode('dest_node', 'Destino', 'static', 200, 0);

    graph.createEdge(crit.id, crit.outputs[0].id, dest.id, dest.inputs[0].id);

    const code = TaskflowCppGenerator.generateSource(graph);

    expect(code).toContain('tf::CriticalSection critical_sec(1);');
    expect(code).toContain('task_crit_node.acquire(critical_sec);');
    expect(code).toContain('task_crit_node.release(critical_sec);');
  });

  it('deve gerar build_msvc.ps1 com padrão C++20 e cl.exe por padrão', () => {
    const graph = new Graph();
    const msvc = TaskflowCppGenerator.generateMsvcBuildScript(graph);

    expect(msvc.ps1).toContain('cl.exe /std:c++20 /EHsc /O2');
    expect(msvc.bat).toContain('cl.exe /std:c++20 /EHsc /O2');
  });

  it('deve gerar CMakeLists.txt moderno para C++20 com suporte a MSVC', () => {
    const graph = new Graph();
    const cmake = TaskflowCppGenerator.generateCMakeLists(graph);

    expect(cmake).toContain('set(CMAKE_CXX_STANDARD 20)');
    expect(cmake).toContain('if(MSVC)');
    expect(cmake).toContain('/std:c++20');
  });

  it('deve gerar Makefile configurado com -std=c++20', () => {
    const graph = new Graph();
    const make = TaskflowCppGenerator.generateMakefile(graph);

    expect(make).toContain('CXXFLAGS = -std=c++20');
    expect(make).toContain('all: $(TARGET)');
  });
});
