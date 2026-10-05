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
    for (const auto& pair : nodes) {
        if (pair.second.type == core::TaskflowNodeType::Critical) {
            hasCritical = true;
            break;
        }
    }

    std::string code = "// ============================================================================\n";
    code += "// Gerado automaticamente por TaskForge (C++ Engine) - Visual Taskflow IDE\n";
    code += "// Pipeline: " + meta.graphName + "\n";
    code += "// Descricao: " + meta.description + "\n";
    code += "// Padrao C++: " + meta.targetStandard + " | Compilador: " + meta.compilerConfig.compiler + "\n";
    code += "// ============================================================================\n\n";

    code += "#include <taskflow/taskflow.hpp>\n";
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
        } else {
            code += "  auto task_" + sId + " = tf.emplace([&ctx]() {\n";
            std::string body = node.customLambdaBody.empty() ? "std::osyncstream(std::cout) << \"Executando " + label + "\\n\";" : node.customLambdaBody;
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
    code += "  std::osyncstream(std::cout) << \">>> Pipeline concluido com sucesso em \" << elapsed_ms << \" ms.\\n\";\n\n";
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
