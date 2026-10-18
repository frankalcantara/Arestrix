#pragma once

#include "project/ProjectTypes.hpp"
#include "core/Graph.hpp"
#include <filesystem>
#include <string>

namespace taskforge {
namespace project {

struct RunnerExportResult {
    bool ok = false;
    std::string error;
    std::string outputDir;
    std::string executablePath;
    std::string manifestPath;
};

class RunnerService {
public:
    static RunnerExportResult exportRunner(
        const std::filesystem::path& projectRoot,
        const ProjectManifest& manifest,
        const core::Graph& graph
    );

    static bool launchRunner(
        const std::filesystem::path& projectRoot,
        const std::string& executablePath
    );

    // Auditoria de seguranca fail-closed para garantir que nenhum segredo seja exportado
    static bool auditTextForSecrets(
        const std::string& text,
        const std::string& sourceName,
        std::string& outError
    );

    static bool auditDirectoryForSecrets(
        const std::filesystem::path& dir,
        std::string& outError
    );
};

} // namespace project
} // namespace taskforge
