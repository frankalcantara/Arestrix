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
};

} // namespace project
} // namespace taskforge
