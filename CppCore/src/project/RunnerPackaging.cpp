#include "project/RunnerPackaging.hpp"
#include "project/RunnerSecurity.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace taskforge {
namespace project {

namespace fs = std::filesystem;

namespace {

std::string sanitizeIdentifier(const std::string& input, const std::string& defaultVal = "ArestrixRunner") {
    std::string out;
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
            out.push_back(c);
        } else if (c == ' ' || c == '-') {
            out.push_back('_');
        }
    }
    if (out.empty()) return defaultVal;
    if (std::isdigit(static_cast<unsigned char>(out.front()))) {
        out = "App_" + out;
    }
    return out;
}

std::string formatVersionQuad(const std::string& ver) {
    int parts[4] = {1, 0, 0, 0};
    std::stringstream ss(ver);
    std::string token;
    int idx = 0;
    while (std::getline(ss, token, '.') && idx < 4) {
        try {
            parts[idx++] = std::stoi(token);
        } catch (...) {
            parts[idx++] = 0;
        }
    }
    std::stringstream out;
    out << parts[0] << "." << parts[1] << "." << parts[2] << "." << parts[3];
    return out.str();
}

std::string formatVersionTriple(const std::string& ver) {
    int parts[3] = {1, 0, 0};
    std::stringstream ss(ver);
    std::string token;
    int idx = 0;
    while (std::getline(ss, token, '.') && idx < 3) {
        try {
            parts[idx++] = std::stoi(token);
        } catch (...) {
            parts[idx++] = 0;
        }
    }
    std::stringstream out;
    out << parts[0] << "." << parts[1] << "." << parts[2];
    return out.str();
}

} // anonymous namespace

std::string RunnerPackaging::formatGuid(const std::string& seed) {
    std::string hash = RunnerSecurity::sha256(seed.empty() ? "arestrix-default-guid-seed" : seed);
    while (hash.size() < 32) hash += "0";

    // Formata como UUID v4 determinístico
    std::string p1 = hash.substr(0, 8);
    std::string p2 = hash.substr(8, 4);
    std::string p3 = hash.substr(12, 4);
    std::string p4 = hash.substr(16, 4);
    std::string p5 = hash.substr(20, 12);

    // Ajusta bits de versão 4
    p3[0] = '4';

    // Ajusta bits de variante RFC 4122 (8, 9, a, b)
    char c = p4[0];
    int val = (c >= '0' && c <= '9') ? (c - '0') : (std::tolower(c) - 'a' + 10);
    val = (val & 0x03) | 0x08;
    p4[0] = (val < 10) ? static_cast<char>('0' + val) : static_cast<char>('a' + val - 10);

    for (char& ch : p1) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (char& ch : p2) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (char& ch : p3) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (char& ch : p4) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (char& ch : p5) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

    return "{" + p1 + "-" + p2 + "-" + p3 + "-" + p4 + "-" + p5 + "}";
}

bool RunnerPackaging::generateWiXInstaller(
    const fs::path& targetDir,
    const ProjectManifest& manifest,
    const std::string& exeName,
    std::vector<std::string>& outFiles,
    std::string& outError
) {
    const auto& pack = manifest.runner.packaging;
    std::string appTitle = manifest.runner.title.empty() ? manifest.name : manifest.runner.title;
    if (appTitle.empty()) appTitle = "Arestrix Runner";

    std::string safeId = sanitizeIdentifier(pack.appId.empty() ? manifest.name : pack.appId);
    std::string verTriple = formatVersionTriple(manifest.version);
    std::string publisher = pack.publisher.empty() ? "Enterprise IT Corporate" : pack.publisher;

    std::string upgradeCode = pack.upgradeCode;
    if (upgradeCode.empty() || upgradeCode.front() != '{') {
        upgradeCode = formatGuid(pack.appId + "_upgrade_code");
    }

    std::string compGuid1 = formatGuid(safeId + "_comp_exe");
    std::string compGuid2 = formatGuid(safeId + "_comp_manifest");
    std::string compGuid3 = formatGuid(safeId + "_comp_scripts");
    std::string compGuid4 = formatGuid(safeId + "_comp_shortcuts");

    fs::path wxsPath = targetDir / "installer.wxs";
    {
        std::ofstream wxs(wxsPath, std::ios::trunc);
        if (!wxs.is_open()) {
            outError = "Nao foi possivel criar installer.wxs em " + wxsPath.string();
            return false;
        }

        wxs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
        wxs << "<Wix xmlns=\"http://schemas.microsoft.com/wix/2006/wi\">\r\n";
        wxs << "  <Product Id=\"*\"\r\n";
        wxs << "           Name=\"" << appTitle << "\"\r\n";
        wxs << "           Language=\"1033\"\r\n";
        wxs << "           Version=\"" << verTriple << "\"\r\n";
        wxs << "           Manufacturer=\"" << publisher << "\"\r\n";
        wxs << "           UpgradeCode=\"" << upgradeCode << "\">\r\n";
        wxs << "\r\n";
        wxs << "    <Package InstallerVersion=\"500\"\r\n";
        wxs << "             Compressed=\"yes\"\r\n";
        wxs << "             InstallScope=\"perMachine\"\r\n";
        wxs << "             Description=\"Arestrix Standalone Runner Enterprise Package\"\r\n";
        wxs << "             Comments=\"Enterprise Silent Deployment: " << pack.silentInstallArgs << "\" />\r\n";
        wxs << "\r\n";
        wxs << "    <MajorUpgrade DowngradeErrorMessage=\"Uma versao mais recente de " << appTitle << " ja esta instalada.\" AllowSameVersionUpgrades=\"yes\" />\r\n";
        wxs << "    <MediaTemplate EmbedCab=\"yes\" />\r\n";
        wxs << "\r\n";
        wxs << "    <Directory Id=\"TARGETDIR\" Name=\"SourceDir\">\r\n";
        wxs << "      <Directory Id=\"ProgramFiles64Folder\">\r\n";
        wxs << "        <Directory Id=\"ArestrixFolder\" Name=\"Arestrix\">\r\n";
        wxs << "          <Directory Id=\"RunnersFolder\" Name=\"Runners\">\r\n";
        wxs << "            <Directory Id=\"INSTALLFOLDER\" Name=\"" << safeId << "\">\r\n";
        wxs << "              <Component Id=\"MainExecutable\" Guid=\"" << compGuid1 << "\" Win64=\"yes\">\r\n";
        wxs << "                <File Id=\"ExeFile\" Source=\"" << exeName << "\" KeyPath=\"yes\" />\r\n";
        wxs << "              </Component>\r\n";
        wxs << "              <Component Id=\"RunnerManifest\" Guid=\"" << compGuid2 << "\" Win64=\"yes\">\r\n";
        wxs << "                <File Id=\"ManifestFile\" Source=\"runner_manifest.json\" KeyPath=\"yes\" />\r\n";
        wxs << "              </Component>\r\n";
        wxs << "              <Component Id=\"RunnerScripts\" Guid=\"" << compGuid3 << "\" Win64=\"yes\">\r\n";
        wxs << "                <File Id=\"RunBatFile\" Source=\"run.bat\" KeyPath=\"yes\" />\r\n";
        wxs << "              </Component>\r\n";
        wxs << "            </Directory>\r\n";
        wxs << "          </Directory>\r\n";
        wxs << "        </Directory>\r\n";
        wxs << "      </Directory>\r\n";

        if (pack.createStartMenuShortcut) {
            wxs << "      <Directory Id=\"ProgramMenuFolder\">\r\n";
            wxs << "        <Directory Id=\"ApplicationProgramsFolder\" Name=\"Arestrix Runners\" />\r\n";
            wxs << "      </Directory>\r\n";
        }
        if (pack.createDesktopShortcut) {
            wxs << "      <Directory Id=\"DesktopFolder\" Name=\"Desktop\" />\r\n";
        }

        wxs << "    </Directory>\r\n";
        wxs << "\r\n";

        if (pack.createStartMenuShortcut || pack.createDesktopShortcut) {
            wxs << "    <DirectoryRef Id=\"INSTALLFOLDER\">\r\n";
            wxs << "      <Component Id=\"ApplicationShortcuts\" Guid=\"" << compGuid4 << "\" Win64=\"yes\">\r\n";
            if (pack.createStartMenuShortcut) {
                wxs << "        <Shortcut Id=\"AppStartMenuShortcut\"\r\n";
                wxs << "                  Name=\"" << appTitle << "\"\r\n";
                wxs << "                  Description=\"" << (manifest.description.empty() ? appTitle : manifest.description) << "\"\r\n";
                wxs << "                  Target=\"[INSTALLFOLDER]" << exeName << "\"\r\n";
                wxs << "                  WorkingDirectory=\"INSTALLFOLDER\" />\r\n";
                wxs << "        <RemoveFolder Id=\"CleanUpShortCutFolder\" Directory=\"ApplicationProgramsFolder\" On=\"uninstall\" />\r\n";
            }
            if (pack.createDesktopShortcut) {
                wxs << "        <Shortcut Id=\"AppDesktopShortcut\"\r\n";
                wxs << "                  Name=\"" << appTitle << "\"\r\n";
                wxs << "                  Description=\"" << (manifest.description.empty() ? appTitle : manifest.description) << "\"\r\n";
                wxs << "                  Target=\"[INSTALLFOLDER]" << exeName << "\"\r\n";
                wxs << "                  WorkingDirectory=\"INSTALLFOLDER\" />\r\n";
            }
            wxs << "        <RegistryValue Root=\"HKCU\" Key=\"Software\\Arestrix\\" << safeId << "\" Name=\"installed\" Type=\"integer\" Value=\"1\" KeyPath=\"yes\" />\r\n";
            wxs << "      </Component>\r\n";
            wxs << "    </DirectoryRef>\r\n";
            wxs << "\r\n";
        }

        wxs << "    <Feature Id=\"ProductFeature\" Title=\"" << appTitle << "\" Level=\"1\">\r\n";
        wxs << "      <ComponentRef Id=\"MainExecutable\" />\r\n";
        wxs << "      <ComponentRef Id=\"RunnerManifest\" />\r\n";
        wxs << "      <ComponentRef Id=\"RunnerScripts\" />\r\n";
        if (pack.createStartMenuShortcut || pack.createDesktopShortcut) {
            wxs << "      <ComponentRef Id=\"ApplicationShortcuts\" />\r\n";
        }
        wxs << "    </Feature>\r\n";
        wxs << "  </Product>\r\n";
        wxs << "</Wix>\r\n";
    }
    outFiles.push_back(wxsPath.string());

    // Gera build_msi.bat
    fs::path batPath = targetDir / "build_msi.bat";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\r\n";
        bat << "echo [Arestrix] Compilando instalador corporativo MSI para Intune e SCCM...\r\n";
        bat << "cd /d \"%~dp0\"\r\n";
        bat << "where candle.exe >nul 2>nul\r\n";
        bat << "if %ERRORLEVEL% NEQ 0 (\r\n";
        bat << "    echo [Aviso] WiX Toolset (candle.exe / light.exe) nao detectado no PATH.\r\n";
        bat << "    echo Para compilar o pacote MSI, instale o WiX Toolset v3 ou v4.\r\n";
        bat << "    echo Os arquivos installer.wxs e scripts Intune foram gerados com sucesso.\r\n";
        bat << "    exit /b 1\r\n";
        bat << ")\r\n";
        bat << "candle.exe -nologo -arch x64 installer.wxs -out installer.wixobj\r\n";
        bat << "light.exe -nologo -ext WixUIExtension installer.wixobj -out \"" << safeId << ".msi\"\r\n";
        bat << "if exist \"" << safeId << ".msi\" (\r\n";
        bat << "    echo [Sucesso] Instalador MSI gerado: %~dp0" << safeId << ".msi\r\n";
        bat << "    echo Comando de implantacao silenciosa Intune: msiexec.exe /i \"%~dp0" << safeId << ".msi\" " << pack.silentInstallArgs << "\r\n";
        bat << ")\r\n";
    }
    outFiles.push_back(batPath.string());

    // Gera build_msi.ps1
    fs::path ps1Path = targetDir / "build_msi.ps1";
    {
        std::ofstream ps1(ps1Path, std::ios::trunc);
        ps1 << "# Script PowerShell para construcao automatizada do pacote corporativo MSI\r\n";
        ps1 << "$ErrorActionPreference = 'Stop'\r\n";
        ps1 << "$wixInstalled = (Get-Command candle.exe -ErrorAction SilentlyContinue)\r\n";
        ps1 << "if (-not $wixInstalled) {\r\n";
        ps1 << "    Write-Host '[Arestrix] WiX Toolset nao localizado no PATH.' -ForegroundColor Yellow\r\n";
        ps1 << "    Write-Host 'Instale via winget: winget install WiX.Toolset' -ForegroundColor Cyan\r\n";
        ps1 << "    exit 1\r\n";
        ps1 << "}\r\n";
        ps1 << "Set-Location $PSScriptRoot\r\n";
        ps1 << "& candle.exe -nologo -arch x64 installer.wxs -out installer.wixobj\r\n";
        ps1 << "& light.exe -nologo -ext WixUIExtension installer.wixobj -out '" << safeId << ".msi'\r\n";
        ps1 << "if (Test-Path '" << safeId << ".msi') {\r\n";
        ps1 << "    Write-Host '[Sucesso] Instalador MSI pronto para homologacao Intune/SCCM: " << safeId << ".msi' -ForegroundColor Green\r\n";
        ps1 << "}\r\n";
    }
    outFiles.push_back(ps1Path.string());

    return true;
}

bool RunnerPackaging::generateMsixPackage(
    const fs::path& targetDir,
    const ProjectManifest& manifest,
    const std::string& exeName,
    std::vector<std::string>& outFiles,
    std::string& outError
) {
    const auto& pack = manifest.runner.packaging;
    std::string appTitle = manifest.runner.title.empty() ? manifest.name : manifest.runner.title;
    if (appTitle.empty()) appTitle = "Arestrix Runner";

    std::string appId = pack.appId.empty() ? ("Arestrix." + sanitizeIdentifier(manifest.name)) : pack.appId;
    std::string publisher = pack.publisher.empty() ? "CN=Corporate, O=Enterprise, C=BR" : pack.publisher;
    std::string verQuad = formatVersionQuad(manifest.version);

    fs::path manifestPath = targetDir / "AppxManifest.xml";
    {
        std::ofstream xml(manifestPath, std::ios::trunc);
        if (!xml.is_open()) {
            outError = "Nao foi possivel criar AppxManifest.xml em " + manifestPath.string();
            return false;
        }

        xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
        xml << "<Package\r\n";
        xml << "  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\r\n";
        xml << "  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\r\n";
        xml << "  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n";
        xml << "  IgnorableNamespaces=\"uap rescap\">\r\n";
        xml << "\r\n";
        xml << "  <Identity\r\n";
        xml << "    Name=\"" << appId << "\"\r\n";
        xml << "    Publisher=\"" << publisher << "\"\r\n";
        xml << "    Version=\"" << verQuad << "\"\r\n";
        xml << "    ProcessorArchitecture=\"x64\" />\r\n";
        xml << "\r\n";
        xml << "  <Properties>\r\n";
        xml << "    <DisplayName>" << appTitle << "</DisplayName>\r\n";
        xml << "    <PublisherDisplayName>Enterprise Corporate TI</PublisherDisplayName>\r\n";
        xml << "    <Logo>assets\\StoreLogo.png</Logo>\r\n";
        xml << "    <Description>" << (manifest.description.empty() ? appTitle : manifest.description) << "</Description>\r\n";
        xml << "  </Properties>\r\n";
        xml << "\r\n";
        xml << "  <Dependencies>\r\n";
        xml << "    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.17763.0\" MaxVersionTested=\"10.0.22621.0\" />\r\n";
        xml << "  </Dependencies>\r\n";
        xml << "\r\n";
        xml << "  <Capabilities>\r\n";
        xml << "    <rescap:Capability Name=\"runFullTrust\" />\r\n";
        xml << "  </Capabilities>\r\n";
        xml << "\r\n";
        xml << "  <Applications>\r\n";
        xml << "    <Application Id=\"App\"\r\n";
        xml << "      Executable=\"" << exeName << "\"\r\n";
        xml << "      EntryPoint=\"Windows.FullTrustApplication\">\r\n";
        xml << "      <uap:VisualElements\r\n";
        xml << "        DisplayName=\"" << appTitle << "\"\r\n";
        xml << "        Description=\"" << (manifest.description.empty() ? appTitle : manifest.description) << "\"\r\n";
        xml << "        BackgroundColor=\"transparent\"\r\n";
        xml << "        Square150x150Logo=\"assets\\Square150x150Logo.png\"\r\n";
        xml << "        Square44x44Logo=\"assets\\Square44x44Logo.png\">\r\n";
        xml << "      </uap:VisualElements>\r\n";
        xml << "    </Application>\r\n";
        xml << "  </Applications>\r\n";
        xml << "</Package>\r\n";
    }
    outFiles.push_back(manifestPath.string());

    // Gera package_msix.ps1
    fs::path msixScript = targetDir / "package_msix.ps1";
    {
        std::ofstream ps1(msixScript, std::ios::trunc);
        ps1 << "# Script PowerShell de Empacotamento MSIX para Microsoft Intune e SCCM\r\n";
        ps1 << "$ErrorActionPreference = 'Stop'\r\n";
        ps1 << "Set-Location $PSScriptRoot\r\n";
        ps1 << "$OutputPackage = Join-Path $PSScriptRoot '" << sanitizeIdentifier(appId) << ".msix'\r\n";
        ps1 << "$MakeAppx = (Get-Command MakeAppx.exe -ErrorAction SilentlyContinue)\r\n";
        ps1 << "if (-not $MakeAppx) {\r\n";
        ps1 << "    Write-Host '[Arestrix] MakeAppx.exe nao encontrado no PATH (Windows SDK requerido).' -ForegroundColor Yellow\r\n";
        ps1 << "    exit 1\r\n";
        ps1 << "}\r\n";
        ps1 << "& MakeAppx.exe pack /d $PSScriptRoot /p $OutputPackage /nv /o\r\n";
        ps1 << "Write-Host \"[Sucesso] Pacote MSIX corporativo gerado: $OutputPackage\" -ForegroundColor Green\r\n";
        ps1 << "Write-Host 'Para assinar digitalmente: signtool.exe sign /fd SHA256 /a $OutputPackage' -ForegroundColor Cyan\r\n";
    }
    outFiles.push_back(msixScript.string());

    return true;
}

bool RunnerPackaging::generateIntuneDetectionScript(
    const fs::path& targetDir,
    const ProjectManifest& manifest,
    const std::string& exeName,
    std::vector<std::string>& outFiles,
    std::string& outError
) {
    const auto& pack = manifest.runner.packaging;
    std::string appTitle = manifest.runner.title.empty() ? manifest.name : manifest.runner.title;
    if (appTitle.empty()) appTitle = "Arestrix Runner";

    std::string safeId = sanitizeIdentifier(pack.appId.empty() ? manifest.name : pack.appId);
    std::string verTriple = formatVersionTriple(manifest.version);
    std::string upgradeCode = pack.upgradeCode;
    if (upgradeCode.empty() || upgradeCode.front() != '{') {
        upgradeCode = formatGuid(pack.appId + "_upgrade_code");
    }

    fs::path detectScript = targetDir / "intune_detection_rule.ps1";
    {
        std::ofstream ps1(detectScript, std::ios::trunc);
        if (!ps1.is_open()) {
            outError = "Nao foi possivel criar intune_detection_rule.ps1 em " + detectScript.string();
            return false;
        }

        ps1 << "# Regra de Deteccao Personalizada para Microsoft Intune / SCCM\r\n";
        ps1 << "# Retorna 0 (STDOUT com texto) se o aplicativo estiver instalado e integro; caso contrario sai com codigo 1.\r\n";
        ps1 << "$AppName = \"" << appTitle << "\"\r\n";
        ps1 << "$ExpectedVersion = [version]\"" << verTriple << "\"\r\n";
        ps1 << "$InstallPath = \"$env:ProgramFiles\\Arestrix\\Runners\\" << safeId << "\\" << exeName << "\"\r\n";
        ps1 << "\r\n";
        ps1 << "# 1. Verificacao pelo caminho fisico do binario\r\n";
        ps1 << "if (Test-Path $InstallPath) {\r\n";
        ps1 << "    try {\r\n";
        ps1 << "        $fileVerStr = (Get-Item $InstallPath).VersionInfo.ProductVersion\r\n";
        ps1 << "        if (-not $fileVerStr) {\r\n";
        ps1 << "            $fileVerStr = (Get-Item $InstallPath).VersionInfo.FileVersion\r\n";
        ps1 << "        }\r\n";
        ps1 << "        if ($fileVerStr) {\r\n";
        ps1 << "            $installedVer = [version]$fileVerStr\r\n";
        ps1 << "            if ($installedVer -ge $ExpectedVersion) {\r\n";
        ps1 << "                Write-Output \"Arestrix Runner instalado e homologado: Versao $installedVer (>= $ExpectedVersion)\"\r\n";
        ps1 << "                exit 0\r\n";
        ps1 << "            }\r\n";
        ps1 << "        } else {\r\n";
        ps1 << "            Write-Output \"Arestrix Runner binario detectado em $InstallPath\"\r\n";
        ps1 << "            exit 0\r\n";
        ps1 << "        }\r\n";
        ps1 << "    } catch {\r\n";
        ps1 << "        Write-Output \"Arestrix Runner binario detectado em $InstallPath\"\r\n";
        ps1 << "        exit 0\r\n";
        ps1 << "    }\r\n";
        ps1 << "}\r\n";
        ps1 << "\r\n";
        ps1 << "# 2. Verificacao no Registro de Desinstalacao do Windows (MSI ProductCode/UpgradeCode)\r\n";
        ps1 << "$uninstallKeys = @(\r\n";
        ps1 << "    \"HKLM:\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\",\r\n";
        ps1 << "    \"HKLM:\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\"\r\n";
        ps1 << ")\r\n";
        ps1 << "\r\n";
        ps1 << "foreach ($key in $uninstallKeys) {\r\n";
        ps1 << "    if (Test-Path $key) {\r\n";
        ps1 << "        $subkeys = Get-ChildItem -Path $key -ErrorAction SilentlyContinue\r\n";
        ps1 << "        foreach ($sub in $subkeys) {\r\n";
        ps1 << "            $displayName = (Get-ItemProperty -Path $sub.PSPath -Name \"DisplayName\" -ErrorAction SilentlyContinue).DisplayName\r\n";
        ps1 << "            $displayVer = (Get-ItemProperty -Path $sub.PSPath -Name \"DisplayVersion\" -ErrorAction SilentlyContinue).DisplayVersion\r\n";
        ps1 << "            if ($displayName -match \"$AppName\" -or $displayName -match \"" << safeId << "\") {\r\n";
        ps1 << "                if ($displayVer -and ([version]$displayVer -ge $ExpectedVersion)) {\r\n";
        ps1 << "                    Write-Output \"Arestrix Runner detectado via Registro de Software: $displayName $displayVer\"\r\n";
        ps1 << "                    exit 0\r\n";
        ps1 << "                }\r\n";
        ps1 << "            }\r\n";
        ps1 << "        }\r\n";
        ps1 << "    }\r\n";
        ps1 << "}\r\n";
        ps1 << "\r\n";
        ps1 << "Write-Error \"Arestrix Runner '$AppName' nao encontrado ou versao inferior a $ExpectedVersion.\"\r\n";
        ps1 << "exit 1\r\n";
    }
    outFiles.push_back(detectScript.string());

    // Gera intune_install_command.txt
    fs::path cmdPath = targetDir / "intune_install_command.txt";
    {
        std::ofstream txt(cmdPath, std::ios::trunc);
        txt << "=================================================================\r\n";
        txt << "Instrucoes de Implantacao Corporativa: Microsoft Intune e SCCM\r\n";
        txt << "=================================================================\r\n\r\n";
        txt << "Aplicativo: " << appTitle << "\r\n";
        txt << "Versao: " << verTriple << "\r\n";
        txt << "UpgradeCode: " << upgradeCode << "\r\n\r\n";
        txt << "1. Comando de Instalacao Silenciosa:\r\n";
        txt << "   msiexec.exe /i \"" << safeId << ".msi\" " << pack.silentInstallArgs << " /l*v \"%TEMP%\\" << safeId << "_install.log\"\r\n\r\n";
        txt << "2. Comando de Desinstalacao Silenciosa:\r\n";
        txt << "   msiexec.exe /x " << upgradeCode << " /qn /l*v \"%TEMP%\\" << safeId << "_uninstall.log\"\r\n\r\n";
        txt << "3. Codigos de Retorno Aceitos pelo Intune:\r\n";
        txt << "   0 (Sucesso), 1707 (Sucesso na instalacao), 3010 (Reinicio do sistema pendente)\r\n\r\n";
        txt << "4. Regra de Deteccao:\r\n";
        txt << "   Utilizar o script 'intune_detection_rule.ps1' como Regra de Deteccao Personalizada (PowerShell Script).\r\n";
    }
    outFiles.push_back(cmdPath.string());

    return true;
}

bool RunnerPackaging::generateChecksumManifest(
    const fs::path& targetDir,
    std::string& outChecksumPath,
    std::string& outError
) {
    std::error_code ec;
    if (!fs::exists(targetDir, ec) || !fs::is_directory(targetDir, ec)) {
        outError = "Diretorio alvo inexistente: " + targetDir.string();
        return false;
    }

    fs::path sumPath = targetDir / "SHA256SUMS";
    std::stringstream ss;

    for (const auto& entry : fs::recursive_directory_iterator(targetDir, ec)) {
        if (entry.is_regular_file(ec)) {
            if (entry.path().filename() == "SHA256SUMS") continue;

            std::string fileErr;
            std::string hash = RunnerSecurity::sha256File(entry.path(), fileErr);
            if (hash.empty()) continue;

            fs::path rel = fs::relative(entry.path(), targetDir, ec);
            std::string relStr = rel.generic_string();
            ss << hash << "  " << relStr << "\n";
        }
    }

    {
        std::ofstream out(sumPath, std::ios::trunc);
        if (!out.is_open()) {
            outError = "Falha ao gravar SHA256SUMS em " + sumPath.string();
            return false;
        }
        out << ss.str();
    }

    outChecksumPath = sumPath.string();
    return true;
}

PackagingResult RunnerPackaging::generatePackagingArtifacts(
    const fs::path& targetDir,
    const ProjectManifest& manifest,
    const std::string& exeName
) {
    PackagingResult res;
    res.installerType = manifest.runner.packaging.format;
    if (res.installerType.empty()) res.installerType = "standalone-folder";

    std::string err;

    // 1. Sempre gera Intune Detection Script para homologação corporativa
    generateIntuneDetectionScript(targetDir, manifest, exeName, res.generatedFiles, err);

    // 2. Se formato msi-installer, gera WiX Toolset
    if (res.installerType == "msi-installer") {
        if (!generateWiXInstaller(targetDir, manifest, exeName, res.generatedFiles, err)) {
            res.ok = false;
            res.error = err;
            return res;
        }
    }

    // 3. Se formato msix-package, gera AppxManifest e empacotador
    if (res.installerType == "msix-package") {
        if (!generateMsixPackage(targetDir, manifest, exeName, res.generatedFiles, err)) {
            res.ok = false;
            res.error = err;
            return res;
        }
    }

    // 4. Gera manifesto de integridade SHA256SUMS
    std::string sumPath;
    if (generateChecksumManifest(targetDir, sumPath, err)) {
        res.generatedFiles.push_back(sumPath);
    }

    res.ok = true;
    return res;
}

} // namespace project
} // namespace taskforge
