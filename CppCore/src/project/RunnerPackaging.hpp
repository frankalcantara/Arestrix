#pragma once

#include "project/ProjectTypes.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace taskforge {
namespace project {

struct PackagingResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> generatedFiles;
    std::string installerType;
};

class RunnerPackaging {
public:
    /**
     * Gera todos os artefatos de empacotamento corporativo com base na configuração do Runner.
     * Suporta:
     * - "msi-installer": Gera arquivo WiX Toolset (.wxs), script de build (.bat e .ps1), e script de detecção Intune.
     * - "msix-package": Gera manifesto AppxManifest.xml para Intune/SCCM e script de empacotamento PowerShell com SignTool.
     * - "standalone-folder": Gera scripts padrão de execução e manifesto de integridade SHA256SUMS.
     */
    static PackagingResult generatePackagingArtifacts(
        const std::filesystem::path& targetDir,
        const ProjectManifest& manifest,
        const std::string& exeName
    );

    /**
     * Gera manifesto de arquivo WiX Toolset (installer.wxs) e scripts auxiliares de compilação.
     */
    static bool generateWiXInstaller(
        const std::filesystem::path& targetDir,
        const ProjectManifest& manifest,
        const std::string& exeName,
        std::vector<std::string>& outFiles,
        std::string& outError
    );

    /**
     * Gera manifesto MSIX (AppxManifest.xml) homologável para Intune e script de empacotamento MakeAppx.
     */
    static bool generateMsixPackage(
        const std::filesystem::path& targetDir,
        const ProjectManifest& manifest,
        const std::string& exeName,
        std::vector<std::string>& outFiles,
        std::string& outError
    );

    /**
     * Gera script PowerShell de regra de detecção personalizada para o Microsoft Intune / SCCM.
     */
    static bool generateIntuneDetectionScript(
        const std::filesystem::path& targetDir,
        const ProjectManifest& manifest,
        const std::string& exeName,
        std::vector<std::string>& outFiles,
        std::string& outError
    );

    /**
     * Gera o arquivo de manifesto de checksums SHA256SUMS para todos os arquivos da pasta de distribuição.
     */
    static bool generateChecksumManifest(
        const std::filesystem::path& targetDir,
        std::string& outChecksumPath,
        std::string& outError
    );

    /**
     * Utilitário para derivar GUIDs estáveis e válidos no formato {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}.
     */
    static std::string formatGuid(const std::string& seed);
};

} // namespace project
} // namespace taskforge
