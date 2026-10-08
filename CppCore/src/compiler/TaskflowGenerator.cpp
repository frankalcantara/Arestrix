#include "TaskflowGenerator.hpp"
#include <algorithm>
#include <sstream>

namespace taskforge {
namespace compiler {

std::string TaskflowGenerator::sanitizeIdentifier(const std::string& id) {
    std::string sanitized = id;
    for (char& c : sanitized) {
        if (!isalnum(c)) {
            c = '_';
        }
    }
    return sanitized;
}

std::string TaskflowGenerator::indent(const std::string& text, int spaces) {
    std::string pad(spaces, ' ');
    std::stringstream ss(text);
    std::string line;
    std::string result;
    bool first = true;
    while (std::getline(ss, line)) {
        if (!first) result += "\n";
        result += pad + line;
        first = false;
    }
    return result;
}

std::string TaskflowGenerator::generateSource(const core::Graph& graph) {
    auto meta = graph.getMetadata();
    const auto& nodes = graph.getNodes();
    const auto& edges = graph.getEdges();

    bool hasCritical = false;
    bool hasCuda = false;
    for (const auto& pair : nodes) {
        if (pair.second.type == core::TaskflowNodeType::Critical) {
            hasCritical = true;
        }
        if (pair.second.type == core::TaskflowNodeType::Cuda) {
            hasCuda = true;
        }
    }

    std::string code = "// ============================================================================\n";
    code += "// Gerado automaticamente por TaskForge (C++ Engine) - Visual Taskflow IDE\n";
    code += "// Pipeline: " + meta.graphName + "\n";
    code += "// Descricao: " + meta.description + "\n";
    code += "// Padrao C++: " + meta.targetStandard + " | Compilador: " + meta.compilerConfig.compiler + "\n";
    code += "// ============================================================================\n\n";

    code += "#include <taskflow/taskflow.hpp>\n";
    if (hasCuda) {
        code += "#include <taskflow/cuda/cudaflow.hpp>\n";
    }
    code += "#include <iostream>\n";
    code += "#include <vector>\n";
    code += "#include <string>\n";
    code += "#include <chrono>\n";
    code += "#include <memory>\n";
    code += "#include <random>\n";
    code += "#include <queue>\n";
    code += "#include <mutex>\n";
    code += "#include <unordered_map>\n";
    code += "#include <syncstream>\n\n";

    code += "struct AgentContext {\n";
    code += "  std::string pipeline_name = \"" + meta.graphName + "\";\n";
    code += "  uint64_t tick_count = 0;\n";
    code += "  bool abort_requested = false;\n\n";
    code += "  std::queue<std::string> task_queue;\n";
    code += "  std::mutex queue_mutex;\n\n";
    code += "  std::unordered_map<std::string, std::string> persistent_store;\n";
    code += "  std::mutex db_mutex;\n\n";
    code += "  double last_confidence_score = 0.0;\n";
    code += "  int retry_counter = 0;\n";
    code += "};\n\n";

    code += "int main(int argc, char* argv[]) {\n";
    code += "  std::osyncstream(std::cout) << \">>> Iniciando pipeline Taskflow: " + meta.graphName + "...\\n\";\n\n";
    code += "  AgentContext ctx;\n";
    code += "  tf::Executor executor;\n";
    code += "  tf::Taskflow tf(\"" + meta.graphName + "\");\n\n";

    if (hasCritical) {
        code += "  tf::Semaphore critical_sec(1);\n\n";
    }

    code += "  auto task_bootstrap = tf.emplace([&ctx]() {\n";
    code += "    std::osyncstream(std::cout) << \">>> [Pipeline \" << ctx.pipeline_name << \"] Ambiente ativado.\\n\";\n";
    code += "  }).name(\"Pipeline_Bootstrap\");\n\n";

    // Create Tasks
    for (const auto& pair : nodes) {
        const auto& node = pair.second;
        std::string sId = sanitizeIdentifier(node.id);
        std::string label = node.label.empty() ? node.id : node.label;
        
        if (node.type == core::TaskflowNodeType::Condition) {
            code += "  auto task_" + sId + " = tf.emplace([&ctx]() -> int {\n";
            std::string body = node.customLambdaBody.empty() ? "return 0;" : node.customLambdaBody;
            code += indent(body, 4) + "\n";
            code += "  }).name(\"" + label + "\");\n\n";
        } else if (node.type == core::TaskflowNodeType::Cuda) {
            code += "  auto task_" + sId + " = tf.emplace([&ctx]() {\n";
            std::string defaultCudaBody = 
                "// Taskflow 4.1+ Native CUDA Graph (tf::cudaGraph / tf::cudaGraphExec)\n"
                "tf::cudaGraph cg;\n"
                "// Construcao do grafo CUDA nativo (kernels, memcpys)\n"
                "tf::cudaGraphExec exec(cg);\n"
                "tf::cudaStream stream;\n"
                "stream.run(exec);\n"
                "stream.synchronize();\n"
                "std::osyncstream(std::cout) << \"[CUDA: " + label + "] Grafo GPU executado com sucesso.\\n\";";
            std::string body = node.customLambdaBody.empty() ? defaultCudaBody : node.customLambdaBody;
            code += indent(body, 4) + "\n";
            code += "  }).name(\"" + label + "\");\n\n";
        } else if (node.type == core::TaskflowNodeType::Subflow || node.type == core::TaskflowNodeType::Module) {
            code += "  auto task_" + sId + " = tf.emplace([&ctx](tf::Subflow& sf) {\n";
            std::string body = node.customLambdaBody.empty() ? "// subflow hierarquico;" : node.customLambdaBody;
            code += indent(body, 4) + "\n";
            code += "  }).name(\"" + label + "\");\n\n";
        } else if (node.type == core::TaskflowNodeType::Agent) {
            code += "  auto task_" + sId + " = tf.emplace([&ctx]() {\n";
            std::string body = node.customLambdaBody.empty() 
                ? "std::cout << \"[Agent: " + label + "] Executando no sandbox isolado (Code Mode)...\\n\";" 
                : node.customLambdaBody;
            code += indent(body, 4) + "\n";
            code += "  }).name(\"" + label + "\");\n\n";
        } else {
            code += "  auto task_" + sId + " = tf.emplace([&ctx]() {\n";
            std::string body = node.customLambdaBody.empty() ? "std::cout << \"Executando " + label + "\\n\";" : node.customLambdaBody;
            code += indent(body, 4) + "\n";
            code += "  }).name(\"" + label + "\");\n\n";
        }
    }

    // Dependencies
    code += "  // Dependencias e Precedencias\n";
    
    // Find entry nodes
    std::vector<std::string> entryNodes;
    for (const auto& pair : nodes) {
        bool hasIncoming = false;
        for (const auto& epair : edges) {
            if (epair.second.targetNodeId == pair.first && !epair.second.isFeedbackLoop) {
                hasIncoming = true;
                break;
            }
        }
        if (!hasIncoming) {
            entryNodes.push_back(pair.first);
        }
    }
    
    if (!entryNodes.empty()) {
        std::string targets;
        for (size_t i = 0; i < entryNodes.size(); ++i) {
            if (i > 0) targets += ", ";
            targets += "task_" + sanitizeIdentifier(entryNodes[i]);
        }
        code += "  task_bootstrap.precede(" + targets + ");\n\n";
    }

    for (const auto& pair : nodes) {
        const auto& node = pair.second;
        std::string sId = sanitizeIdentifier(node.id);
        
        std::vector<core::EdgeData> outEdges;
        for (const auto& epair : edges) {
            if (epair.second.sourceNodeId == node.id) {
                outEdges.push_back(epair.second);
            }
        }
        
        if (outEdges.empty()) continue;

        if (node.type == core::TaskflowNodeType::Condition) {
            // Sort by branch index
            std::sort(outEdges.begin(), outEdges.end(), [&](const core::EdgeData& a, const core::EdgeData& b) {
                int idxA = 0, idxB = 0;
                for(const auto& p : node.outputs) if(p.id == a.sourcePortId) idxA = p.conditionBranchIndex;
                for(const auto& p : node.outputs) if(p.id == b.sourcePortId) idxB = p.conditionBranchIndex;
                return idxA < idxB;
            });
            
            std::string targets;
            for (size_t i = 0; i < outEdges.size(); ++i) {
                if (i > 0) targets += ", ";
                targets += "task_" + sanitizeIdentifier(outEdges[i].targetNodeId);
            }
            code += "  task_" + sId + ".precede(" + targets + ");\n";
        } else {
            for (const auto& edge : outEdges) {
                std::string targetId = sanitizeIdentifier(edge.targetNodeId);
                code += "  task_" + sId + ".precede(task_" + targetId + ");\n";
            }
        }
    }

    code += "\n  std::osyncstream(std::cout) << \">>> Disparando execucao no pool de threads...\\n\";\n";
    code += "  auto start = std::chrono::high_resolution_clock::now();\n\n";
    code += "  executor.run(tf).wait();\n\n";
    code += "  auto end = std::chrono::high_resolution_clock::now();\n";
    code += "  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n";
    code += "  std::cout << \">>> Pipeline concluido com sucesso em \" << elapsed_ms << \" ms.\\n\";\n\n";
    code += "  return 0;\n";
    code += "}\n";

    return code;
}

std::string TaskflowGenerator::generateBatchSource(
    const core::Graph& graph, 
    const std::vector<std::string>& batchItems, 
    const std::vector<std::string>& serializedStages, 
    const std::string& stateFilePath
) {
    auto meta = graph.getMetadata();
    const auto& nodes = graph.getNodes();
    const auto& edges = graph.getEdges();

    std::string code = "// ============================================================================\n";
    code += "// Gerado por TaskForge (C++ Batch DAG & Resume Engine)\n";
    code += "// Pipeline: " + meta.graphName + " (Lote de " + std::to_string(batchItems.size()) + " itens)\n";
    code += "// Arquivo de Checkpoint: " + stateFilePath + "\n";
    code += "// ============================================================================\n\n";

    code += "#include <taskflow/taskflow.hpp>\n";
    code += "#include <iostream>\n";
    code += "#include <vector>\n";
    code += "#include <string>\n";
    code += "#include <chrono>\n";
    code += "#include <mutex>\n";
    code += "#include \"sandbox/PipelineResumeEngine.hpp\"\n";
    code += "#include \"sandbox/AgentTelemetryEmitter.hpp\"\n\n";

    code += "int main() {\n";
    code += "  std::cout << \">>> Inicializando Pipeline em Lote TaskForge...\\n\";\n";
    code += "  tf::Executor executor;\n";
    code += "  tf::Taskflow tf(\"BatchMultiAgentPipeline\");\n\n";
    code += "  auto& resumeEngine = taskforge::sandbox::PipelineResumeEngine::getInstance();\n";
    code += "  resumeEngine.setStateFilePath(\"" + stateFilePath + "\");\n\n";

    // Instancia tarefas por item e por nó
    for (size_t itemIdx = 0; itemIdx < batchItems.size(); ++itemIdx) {
        const std::string& itemId = batchItems[itemIdx];
        std::string sItemId = sanitizeIdentifier(itemId);

        code += "  // --- Tarefas para o Item: " + itemId + " ---\n";
        for (const auto& pair : nodes) {
            const auto& node = pair.second;
            std::string sNodeId = sanitizeIdentifier(node.id);
            std::string taskVar = "task_" + sItemId + "_" + sNodeId;
            std::string taskName = itemId + " :: " + node.label;

            code += "  auto " + taskVar + " = tf.emplace([&resumeEngine, itemId = std::string(\"" + itemId + "\")]() {\n";
            code += "    taskforge::sandbox::ArtifactValidationRule rule;\n";
            code += "    rule.targetPathPattern = \"aulas/{itemId}.md\";\n";
            code += "    rule.minSizeBytes = 200;\n";
            code += "    rule.checkLogExitCode = true;\n\n";
            code += "    if (resumeEngine.isStageCompleted(itemId, \"" + node.id + "\", rule)) {\n";
            code += "      std::cout << \"[CHECKPOINT:RESUME] \" << itemId << \" :: " + node.label + " ja concluido. Reaproveitando artefato.\\n\";\n";
            code += "      taskforge::sandbox::AgentTelemetryEmitter::emitStatus(\"" + node.id + "\", \"settled\", \"Recuperado do Checkpoint\");\n";
            code += "      return;\n";
            code += "    }\n\n";
            code += "    resumeEngine.markStageStarted(itemId, \"" + node.id + "\");\n";
            code += "    taskforge::sandbox::AgentTelemetryEmitter::emitStatus(\"" + node.id + "\", \"running\", \"Executando item \" + itemId);\n";
            code += "    std::cout << \"[EXECUTANDO] \" << itemId << \" :: " + node.label + "\\n\";\n";
            
            // Lambda customizada ou padrão
            if (!node.customLambdaBody.empty()) {
                code += indent(node.customLambdaBody, 4) + "\n";
            } else {
                code += "    // Execucao no Sandbox/MicroVM...\\n";
            }

            code += "    resumeEngine.markStageSuccess(itemId, \"" + node.id + "\", \"aulas/\" + itemId + \".md\");\n";
            code += "    taskforge::sandbox::AgentTelemetryEmitter::emitStatus(\"" + node.id + "\", \"settled\", \"Concluido com sucesso\");\n";
            code += "  }).name(\"" + taskName + "\");\n\n";
        }

        // Dependências internas do item
        code += "  // Dependencias internas do Item: " + itemId + "\n";
        for (const auto& pair : nodes) {
            const auto& node = pair.second;
            std::string sFrom = "task_" + sItemId + "_" + sanitizeIdentifier(node.id);

            for (const auto& epair : edges) {
                if (epair.second.sourceNodeId == node.id && !epair.second.isFeedbackLoop) {
                    std::string sTo = "task_" + sItemId + "_" + sanitizeIdentifier(epair.second.targetNodeId);
                    code += "  " + sFrom + ".precede(" + sTo + ");\n";
                }
            }
        }
        code += "\n";
    }

    // Encadeamento serial de etapas pesadas (serializedStages) entre itens adjacentes
    if (!serializedStages.empty() && batchItems.size() > 1) {
        code += "  // --- Encadeamento Serial Entre Itens (Limite de Concorrencia de Etapas Pesadas) ---\n";
        for (const auto& stageId : serializedStages) {
            std::string sStage = sanitizeIdentifier(stageId);
            for (size_t i = 0; i < batchItems.size() - 1; ++i) {
                std::string sItemCurr = sanitizeIdentifier(batchItems[i]);
                std::string sItemNext = sanitizeIdentifier(batchItems[i + 1]);
                code += "  task_" + sItemCurr + "_" + sStage + ".precede(task_" + sItemNext + "_" + sStage + ");\n";
            }
        }
        code += "\n";
    }

    code += "  auto start = std::chrono::high_resolution_clock::now();\n";
    code += "  executor.run(tf).wait();\n";
    code += "  auto end = std::chrono::high_resolution_clock::now();\n";
    code += "  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();\n";
    code += "  std::cout << \">>> Lote de \" << " + std::to_string(batchItems.size()) + " << \" artigos finalizado com sucesso em \" << elapsed_ms << \" ms.\\n\";\n\n";
    code += "  return 0;\n";
    code += "}\n";

    return code;
}

std::string TaskflowGenerator::generateCMakeLists(const core::Graph& graph) {
    auto meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;
    std::string incPath = meta.compilerConfig.taskflowIncludePath.empty() ? "include" : meta.compilerConfig.taskflowIncludePath;

    return "cmake_minimum_required(VERSION 3.20)\n"
           "project(" + meta.graphName + " CXX)\n\n"
           "set(CMAKE_CXX_STANDARD 20)\n"
           "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
           "set(CMAKE_CXX_EXTENSIONS OFF)\n\n"
           "if(MSVC)\n"
           "  add_compile_options(/std:c++20 /EHsc /W4 /O2 /permissive-)\n"
           "else()\n"
           "  add_compile_options(-std=c++20 -Wall -Wextra -O3 -pthread)\n"
           "endif()\n\n"
           "include_directories(" + incPath + ")\n"
           "add_executable(" + binName + " main.cpp)\n"
           "find_package(Threads REQUIRED)\n"
           "target_link_libraries(" + binName + " PRIVATE Threads::Threads)\n";
}

std::string TaskflowGenerator::generateMakefile(const core::Graph& graph) {
    auto meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;
    std::string incPath = meta.compilerConfig.taskflowIncludePath.empty() ? "include" : meta.compilerConfig.taskflowIncludePath;

    return "# Makefile para " + meta.graphName + " (C++20)\n"
           "CXX = g++\n"
           "CXXFLAGS = -std=c++20 -O3 -Wall -pthread -I" + incPath + "\n"
           "TARGET = " + binName + "\n\n"
           "all: $(TARGET)\n\n"
           "$(TARGET): main.cpp\n"
           "\t$(CXX) $(CXXFLAGS) main.cpp -o $(TARGET)\n\n"
           "clean:\n"
           "\trm -f $(TARGET) $(TARGET).exe\n";
}

TaskflowGenerator::MsvcScripts TaskflowGenerator::generateMsvcBuildScript(const core::Graph& graph) {
    auto meta = graph.getMetadata();
    std::string binName = meta.compilerConfig.outputBinaryName.empty() ? "pipeline_app" : meta.compilerConfig.outputBinaryName;
    std::string incPath = meta.compilerConfig.taskflowIncludePath.empty() ? "include" : meta.compilerConfig.taskflowIncludePath;
    
    std::string extraFlags = "";
    for (const auto& flag : meta.compilerConfig.extraFlags) {
        extraFlags += flag + " ";
    }

    MsvcScripts scripts;
    scripts.ps1 = "# Build Script PowerShell para MSVC (cl.exe)\n"
                  "Write-Host \">>> Compilando " + meta.graphName + " com MSVC (C++20)...\" -ForegroundColor Cyan\n"
                  "cl.exe /std:c++20 /EHsc /O2 /W4 /I\"" + incPath + "\" " + extraFlags + " main.cpp /Fe:\"" + binName + ".exe\"\n"
                  "if ($LASTEXITCODE -eq 0) {\n"
                  "    Write-Host \">>> Compilacao com sucesso\" -ForegroundColor Green\n"
                  "} else {\n"
                  "    Write-Host \">>> Erro na compilacao\" -ForegroundColor Red\n"
                  "}\n";

    scripts.bat = "@echo off\n"
                  "echo >>> Compilando " + meta.graphName + " com MSVC (C++20)...\n"
                  "cl.exe /std:c++20 /EHsc /O2 /W4 /I\"" + incPath + "\" " + extraFlags + " main.cpp /Fe:\"" + binName + ".exe\"\n"
                  "if %errorlevel% equ 0 (\n"
                  "    echo >>> Compilacao com sucesso!\n"
                  ") else (\n"
                  "    echo >>> Erro na compilacao!\n"
                  ")\n";
    return scripts;
}

} // namespace compiler
} // namespace taskforge
