#pragma once

#include <filesystem>
#include <string>

namespace taskforge {
namespace project {

/**
 * Exporta uma pasta de projeto Arestrix como um arquivo ZIP (método "stored",
 * sem compressão) para transporte/backup. O arquivo gerado tem a extensão
 * .arestrix.zip e contém uma pasta de topo com o nome do projeto.
 */
class ProjectArchive {
public:
    static bool exportProjectZip(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& targetDir,
        const std::string& projectName,
        std::filesystem::path& outZipPath,
        std::string& outError
    );
};

} // namespace project
} // namespace taskforge
