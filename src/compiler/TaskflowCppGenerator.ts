import { Graph } from '../core/Graph.ts';

export class TaskflowCppGenerator {
  /**
   * Gera o código fonte principal C++20 usando a API do Taskflow.
   */
  static generateSource(graph: Graph): string {
    const rootSchema = graph.getRootSchema();
    const meta = rootSchema.metadata;
    const nodes = rootSchema.nodes;
    const edges = rootSchema.edges;
    const hasCritical = nodes.some(n => n.type === 'critical');

    let code = `// ============================================================================\n`;
    code += `// Gerado automaticamente por NodeForge - Visual Taskflow IDE\n`;
    code += `// Pipeline: ${meta.graphName}\n`;
    code += `// Descricao: ${meta.description}\n`;
    code += `// Padrao C++: C++20 | Compilador: ${meta.compilerConfig.compiler.toUpperCase()}\n`;
    code += `// ============================================================================\n\n`;

    code += `#include <taskflow/taskflow.hpp>\n`;
    code += `#include "NodeForgeObserver.hpp"\n`;
    code += `#include <iostream>\n`;
    code += `#include <vector>\n`;
    code += `#include <string>\n`;
    code += `#include <chrono>\n`;
    code += `#include <memory>\n`;
    code += `#include <random>\n`;
    code += `#include <queue>\n`;
    code += `#include <mutex>\n`;
    code += `#include <unordered_map>\n`;
    code += `#include <syncstream> // C++20 synchronized streams\n`;

    // Headers adicionais declarados pelos templates de nós
    const extraHeaders = new Set<string>();
    nodes.forEach(n => {
      n.requiredHeaders?.forEach(h => extraHeaders.add(h));
      if (n.childGraph) {
        n.childGraph.nodes.forEach(cn => cn.requiredHeaders?.forEach(h => extraHeaders.add(h)));
      }
    });
    extraHeaders.forEach(h => {
      if (!code.includes(h)) {
        code += `#include ${h}\n`;
      }
    });
    code += `\n`;

    code += `// Estrutura de Estado Compartilhado de Agentes (C++20)\n`;
    code += `struct AgentContext {\n`;
    code += `  std::string pipeline_name = "${meta.graphName}";\n`;
    code += `  uint64_t tick_count = 0;\n`;
    code += `  bool abort_requested = false;\n\n`;
    code += `  // Buffer concorrente para agentes de fila e mensageria\n`;
    code += `  std::queue<std::string> task_queue;\n`;
    code += `  std::mutex queue_mutex;\n\n`;
    code += `  // Armazenamento relacional/transacional protegido para persistência\n`;
    code += `  std::unordered_map<std::string, std::string> persistent_store;\n`;
    code += `  std::mutex db_mutex;\n\n`;
    code += `  // Metricas e heurísticas de inferência de agentes LLM\n`;
    code += `  double last_confidence_score = 0.0;\n`;
    code += `  int retry_counter = 0;\n`;
    code += `};\n\n`;

    code += `int main(int argc, char* argv[]) {\n`;
    code += `  std::osyncstream(std::cout) << ">>> Iniciando pipeline Taskflow: ${meta.graphName}...\\n";\n\n`;
    code += `  AgentContext ctx;\n`;
    code += `  tf::Executor executor;\n`;
    code += `  // Plugin de Telemetria Desacoplado em Tempo Real (tf::ObserverInterface)\n`;
    code += `  auto live_observer = executor.make_observer<nodeforge::LiveTelemetryObserver>();\n`;
    code += `  tf::Taskflow tf("${meta.graphName}");\n\n`;

    if (hasCritical) {
      code += `  // Secao Critica / Semaforo de Concorrencia (tf::Semaphore com contagem 1)\n`;
      code += `  tf::Semaphore critical_sec(1);\n\n`;
    }

    // 0. Tarefa de Bootstrap / Inicializacao do Pipeline (Garante ativacao sem deadlock em loops)
    code += `  // ------------------------------------------------------------------------\n`;
    code += `  // Bootstrap de Inicializacao e Ativacao do Pipeline\n`;
    code += `  // ------------------------------------------------------------------------\n`;
    code += `  auto task_bootstrap = tf.emplace([&ctx]() {\n`;
    code += `    std::osyncstream(std::cout) << ">>> [Pipeline " << ctx.pipeline_name << "] Ambiente ativado.\\n";\n`;
    code += `  }).name("Pipeline_Bootstrap");\n\n`;

    // 1. Instanciacao dos Nós / Tarefas
    code += `  // ------------------------------------------------------------------------\n`;
    code += `  // Declaracao de Tarefas e Agentes\n`;
    code += `  // ------------------------------------------------------------------------\n`;

    nodes.forEach(node => {
      const sanitizedId = this.sanitizeIdentifier(node.id);
      const label = node.label || node.id;

      switch (node.type) {
        case 'condition': {
          code += `  // Nó de Condicao / Decisao: ${label}\n`;
          code += `  auto task_${sanitizedId} = tf.emplace([&ctx]() -> int {\n`;
          code += this.indent(node.customLambdaBody || `return rand() % 2;`, 4) + `\n`;
          code += `  }).name("${label}");\n\n`;
          break;
        }

        case 'subflow': {
          code += `  // Nó de Subfluxo Dinamico: ${label}\n`;
          code += `  auto task_${sanitizedId} = tf.emplace([&ctx](tf::Subflow& sf) {\n`;
          if (node.childGraph && node.childGraph.nodes.length > 0) {
            code += this.indent(this.generateSubflowInternalCode(node.childGraph), 4) + `\n`;
          } else {
            code += this.indent(node.customLambdaBody || `std::osyncstream(std::cout) << "[Subflow] Executando ${label}\\n";`, 4) + `\n`;
          }
          code += `  }).name("${label}");\n\n`;
          break;
        }

        case 'module': {
          code += `  // Nó de Modulo (Subgrafo Reutilizavel): ${label}\n`;
          code += `  tf::Taskflow module_flow_${sanitizedId}("${label}_Module");\n`;
          if (node.childGraph && node.childGraph.nodes.length > 0) {
            code += this.indent(this.generateModuleInternalCode(node.childGraph, sanitizedId), 2) + `\n`;
          } else {
            code += `  auto mod_task_${sanitizedId} = module_flow_${sanitizedId}.emplace([]() {\n`;
            code += `    std::osyncstream(std::cout) << "Dentro do modulo ${label}\\n";\n`;
            code += `  });\n`;
          }
          code += `  auto task_${sanitizedId} = tf.composed_of(module_flow_${sanitizedId}).name("${label}");\n\n`;
          break;
        }

        case 'critical': {
          code += `  // Nó com Secao Critica: ${label}\n`;
          code += `  auto task_${sanitizedId} = tf.emplace([&ctx]() {\n`;
          code += this.indent(node.customLambdaBody || `std::osyncstream(std::cout) << "[Critico] Acesso exclusivo em ${label}\\n";`, 4) + `\n`;
          code += `  }).name("${label}");\n`;
          code += `  task_${sanitizedId}.acquire(critical_sec);\n`;
          code += `  task_${sanitizedId}.release(critical_sec);\n\n`;
          break;
        }

        case 'static':
        default: {
          code += `  // Nó Estatico Deterministico: ${label}\n`;
          code += `  auto task_${sanitizedId} = tf.emplace([&ctx]() {\n`;
          code += this.indent(node.customLambdaBody || `std::osyncstream(std::cout) << "[Agente] Executando ${label}\\n";`, 4) + `\n`;
          code += `  }).name("${label}");\n\n`;
          break;
        }
      }
    });

    // 2. Precedencias e Dependencias
    code += `  // ------------------------------------------------------------------------\n`;
    code += `  // Grafo de Dependencias e Precedencias\n`;
    code += `  // ------------------------------------------------------------------------\n`;

    // Conecta a tarefa de bootstrap a todos os nós de entrada (sem predecessores não-feedback)
    const entryNodes = nodes.filter(n => {
      const incoming = edges.filter(e => e.targetNodeId === n.id);
      return incoming.length === 0 || incoming.every(e => e.isFeedbackLoop);
    });

    if (entryNodes.length > 0) {
      const targets = entryNodes.map(n => `task_${this.sanitizeIdentifier(n.id)}`);
      code += `  // Ativacao inicial pelo Bootstrap (previne deadlocks em pipelines com loops)\n`;
      code += `  task_bootstrap.precede(${targets.join(', ')});\n\n`;
    }

    // Processar tarefas normais vs condicionais
    nodes.forEach(node => {
      const sanitizedId = this.sanitizeIdentifier(node.id);
      const outgoingEdges = edges.filter(e => e.sourceNodeId === node.id);

      if (outgoingEdges.length === 0) return;

      if (node.type === 'condition') {
        // No Taskflow, para tarefas de condicao, a ordem dos parametros no precede()
        // DEVE corresponder exatamente ao indice de retorno do branch (0, 1, 2...)
        const branchEdges = [...outgoingEdges].sort((a, b) => {
          const portA = node.outputs.find(p => p.id === a.sourcePortId);
          const portB = node.outputs.find(p => p.id === b.sourcePortId);
          const idxA = portA?.conditionBranchIndex ?? 0;
          const idxB = portB?.conditionBranchIndex ?? 0;
          return idxA - idxB;
        });

        const targets = branchEdges.map(e => `task_${this.sanitizeIdentifier(e.targetNodeId)}`);
        code += `  // Branch condicional (ramo 0 -> ${targets[0] || 'none'}, ramo 1 -> ${targets[1] || 'none'})\n`;
        code += `  task_${sanitizedId}.precede(${targets.join(', ')});\n`;
      } else {
        outgoingEdges.forEach(edge => {
          const targetSanitized = this.sanitizeIdentifier(edge.targetNodeId);
          const feedbackComment = edge.isFeedbackLoop ? ' // Feedback loop' : '';
          code += `  task_${sanitizedId}.precede(task_${targetSanitized});${feedbackComment}\n`;
        });
      }
    });

    // 3. Dump DOT opcional e Execucao
    code += `\n  // ------------------------------------------------------------------------\n`;
    code += `  // Execucao e Diagnostico\n`;
    code += `  // ------------------------------------------------------------------------\n`;
    code += `  // Exporta a topologia real do Taskflow para formato DOT (Graphviz)\n`;
    code += `  // tf.dump(std::cout);\n\n`;
    code += `  std::osyncstream(std::cout) << ">>> Disparando execucao no pool de threads...\\n";\n`;
    code += `  auto start = std::chrono::high_resolution_clock::now();\n\n`;
    code += `  executor.run(tf).wait();\n\n`;
    code += `  auto end = std::chrono::high_resolution_clock::now();\n`;
    code += `  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n`;
    code += `  std::osyncstream(std::cout) << ">>> Pipeline concluido com sucesso em " << elapsed_ms << " ms.\\n";\n\n`;
    code += `  return 0;\n`;
    code += `}\n`;

    return code;
  }

  /**
   * Gera o arquivo CMakeLists.txt moderno para C++20.
   */
  static generateCMakeLists(graph: Graph): string {
    const meta = graph.getMetadata();
    const binName = meta.compilerConfig.outputBinaryName || 'pipeline_app';
    const incPath = meta.compilerConfig.taskflowIncludePath || 'include';

    return `cmake_minimum_required(VERSION 3.20)
project(${meta.graphName} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configuracao para MSVC no Windows
if(MSVC)
  add_compile_options(/std:c++20 /EHsc /W4 /O2 /permissive-)
else()
  add_compile_options(-std=c++20 -Wall -Wextra -O3 -pthread)
endif()

# Include do Taskflow (header-only)
include_directories(${incPath})

# Alvo Executavel
add_executable(${binName} main.cpp)

# Threads
find_package(Threads REQUIRED)
target_link_libraries(${binName} PRIVATE Threads::Threads)
`;
  }

  /**
   * Gera um Makefile compatível com make no Windows (MinGW, Git Bash ou WSL).
   */
  static generateMakefile(graph: Graph): string {
    const meta = graph.getMetadata();
    const binName = meta.compilerConfig.outputBinaryName || 'pipeline_app';
    const incPath = meta.compilerConfig.taskflowIncludePath || 'include';

    return `# Makefile para ${meta.graphName} (C++20)
CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -pthread -I${incPath}
TARGET = ${binName}

all: $(TARGET)

$(TARGET): main.cpp
\t$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)

clean:
\trm -f $(TARGET) $(TARGET).exe
`;
  }

  /**
   * Gera o script de compilação direta via MSVC (cl.exe) para Windows PowerShell / CMD.
   */
  static generateMsvcBuildScript(graph: Graph): { ps1: string; bat: string } {
    const meta = graph.getMetadata();
    const binName = meta.compilerConfig.outputBinaryName || 'pipeline_app';
    const incPath = meta.compilerConfig.taskflowIncludePath || 'include';
    const extraFlags = meta.compilerConfig.extraFlags.join(' ');

    const ps1 = `# Build Script PowerShell para MSVC (cl.exe)
Write-Host ">>> Compilando ${meta.graphName} com MSVC (C++20)..." -ForegroundColor Cyan

# Verifica presenca do cl.exe no ambiente
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Warning "cl.exe nao encontrado no PATH! Execute a partir do 'Developer PowerShell for VS'."
}

cl.exe /std:c++20 /EHsc /O2 /W4 /I"${incPath}" ${extraFlags} main.cpp /Fe:"${binName}.exe"

if ($LASTEXITCODE -eq 0) {
    Write-Host ">>> Compilacao com sucesso: ${binName}.exe" -ForegroundColor Green
} else {
    Write-Host ">>> Erro na compilacao com MSVC!" -ForegroundColor Red
}
`;

    const bat = `@echo off
echo >>> Compilando ${meta.graphName} com MSVC (C++20)...
cl.exe /std:c++20 /EHsc /O2 /W4 /I"${incPath}" ${extraFlags} main.cpp /Fe:"${binName}.exe"
if %errorlevel% equ 0 (
    echo >>> Compilacao com sucesso: %~dp0${binName}.exe
) else (
    echo >>> Erro na compilacao com MSVC!
)
`;

    return { ps1, bat };
  }

  private static generateSubflowInternalCode(schema: any): string {
    let out = `// Decomposição dinâmica interna do subfluxo: ${schema.metadata.graphName}\n`;
    schema.nodes.forEach((child: any) => {
      const sId = this.sanitizeIdentifier(child.id);
      out += `auto sf_task_${sId} = sf.emplace([&ctx]() {\n`;
      out += this.indent(child.customLambdaBody || `std::osyncstream(std::cout) << "Executando ${child.label}\\n";`, 2) + `\n`;
      out += `}).name("${child.label}");\n`;
    });

    schema.edges.forEach((edge: any) => {
      const srcId = this.sanitizeIdentifier(edge.sourceNodeId);
      const tgtId = this.sanitizeIdentifier(edge.targetNodeId);
      out += `sf_task_${srcId}.precede(sf_task_${tgtId});\n`;
    });

    return out;
  }

  private static generateModuleInternalCode(schema: any, moduleId: string): string {
    let out = `// Tarefas internas encapsuladas do módulo\n`;
    schema.nodes.forEach((child: any) => {
      const sId = this.sanitizeIdentifier(child.id);
      out += `auto mod_task_${sId} = module_flow_${moduleId}.emplace([&ctx]() {\n`;
      out += this.indent(child.customLambdaBody || `std::osyncstream(std::cout) << "Modulo: ${child.label}\\n";`, 2) + `\n`;
      out += `}).name("${child.label}");\n`;
    });

    schema.edges.forEach((edge: any) => {
      const srcId = this.sanitizeIdentifier(edge.sourceNodeId);
      const tgtId = this.sanitizeIdentifier(edge.targetNodeId);
      out += `mod_task_${srcId}.precede(mod_task_${tgtId});\n`;
    });

    return out;
  }

  private static sanitizeIdentifier(id: string): string {
    return id.replace(/[^a-zA-Z0-9_]/g, '_');
  }

  private static indent(text: string, spaces: number): string {
    const pad = ' '.repeat(spaces);
    return text.split('\n').map(line => pad + line).join('\n');
  }
}
