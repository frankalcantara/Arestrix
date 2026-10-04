#pragma once

#include "../core/Graph.hpp"
#include <string>

namespace nodeforge {
namespace compiler {

class TaskflowGenerator {
public:
    static std::string generateSource(const core::Graph& graph);
    static std::string generateCMakeLists(const core::Graph& graph);
    static std::string generateMakefile(const core::Graph& graph);
    
    struct MsvcScripts {
        std::string ps1;
        std::string bat;
    };
    static MsvcScripts generateMsvcBuildScript(const core::Graph& graph);

private:
    static std::string sanitizeIdentifier(const std::string& id);
    static std::string indent(const std::string& text, int spaces);
};

} // namespace compiler
} // namespace nodeforge
