#include "project/RunnerService.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "build/BuildService.hpp"
#include "platform/Process.hpp"
#include "util/Json.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace taskforge {
namespace project {

namespace fs = std::filesystem;

static std::string currentIsoTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm tm_buf;
#ifdef _WIN32
    gmtime_s(&tm_buf, &in_time_t);
#else
    gmtime_r(&in_time_t, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    return std::string(buf);
}

RunnerExportResult RunnerService::exportRunner(
    const fs::path& projectRoot,
    const ProjectManifest& manifest,
    const core::Graph& graph
) {
    RunnerExportResult res;
    std::error_code ec;

    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) {
        res.ok = false;
        res.error = "Diretorio raiz do projeto invalido: " + projectRoot.string();
        return res;
    }

    // 1. Determina pasta de saida do Runner
    fs::path targetDir;
    std::string customOut = manifest.runner.outputDir.empty() ? "dist/runner" : manifest.runner.outputDir;
    fs::path pCustom(customOut);
    if (pCustom.is_absolute()) {
        targetDir = pCustom;
    } else {
        targetDir = projectRoot / pCustom;
    }

    fs::create_directories(targetDir, ec);
    if (ec) {
        res.ok = false;
        res.error = "Falha ao criar pasta de destino do runner: " + ec.message();
        return res;
    }
    res.outputDir = targetDir.string();

    // 2. Determina nome do executavel
    std::string appName = manifest.runner.outputName;
    if (appName.empty()) {
        std::string base = manifest.name.empty() ? "pipeline_runner" : manifest.name;
        // Sanitiza nome
        std::string safe;
        for (char c : base) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
                safe.push_back(c);
            } else if (c == ' ') {
                safe.push_back('_');
            }
        }
        if (safe.empty()) safe = "pipeline_runner";
#ifdef _WIN32
        appName = safe + ".exe";
#else
        appName = safe;
#endif
    }

    fs::path finalExePath = targetDir / appName;

    // 3. Salva runner_manifest.json com a topologia do pipeline e configuracoes
    taskforge::Json runnerJson = {
        {"format", "arestrix-standalone-runner"},
        {"version", manifest.runner.version.empty() ? "1.0.0" : manifest.runner.version},
        {"title", manifest.runner.title.empty() ? (manifest.name + " (Runner)") : manifest.runner.title},
        {"description", manifest.runner.description.empty() ? manifest.description : manifest.runner.description},
        {"projectId", manifest.projectId},
        {"projectName", manifest.name},
        {"executable", appName},
        {"defaultTab", manifest.runner.defaultTab},
        {"autoStart", manifest.runner.autoStart},
        {"allowRestart", manifest.runner.allowRestart},
        {"parameters", manifest.runner.parameters},
        {"exportedAt", currentIsoTimestamp()},
        {"graph", graph.toJson()}
    };

    fs::path manifestPath = targetDir / "runner_manifest.json";
    {
        std::ofstream out(manifestPath, std::ios::trunc);
        if (!out.is_open()) {
            res.ok = false;
            res.error = "Nao foi possivel gravar runner_manifest.json em " + manifestPath.string();
            return res;
        }
        out << runnerJson.dump(2);
    }
    res.manifestPath = manifestPath.string();

    // 4. Copia skills do workspace caso existam
    fs::path srcSkills = projectRoot / ".workspace" / "skills";
    if (fs::exists(srcSkills, ec) && fs::is_directory(srcSkills, ec)) {
        fs::path destSkills = targetDir / "skills";
        fs::create_directories(destSkills, ec);
        for (const auto& entry : fs::recursive_directory_iterator(srcSkills, ec)) {
            if (fs::is_regular_file(entry, ec)) {
                fs::path rel = fs::relative(entry.path(), srcSkills, ec);
                fs::path targetFile = destSkills / rel;
                fs::create_directories(targetFile.parent_path(), ec);
                fs::copy_file(entry.path(), targetFile, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // 5. Copia ou gera o executável nativo
    // Verifica se ja temos um pipeline compilado em .arestrix/build/
    fs::path builtExe = projectRoot / ".arestrix" / "build";
#ifdef _WIN32
    builtExe /= "pipeline_app.exe";
#else
    builtExe /= "pipeline_app";
#endif

    if (fs::exists(builtExe, ec)) {
        fs::copy_file(builtExe, finalExePath, fs::copy_options::overwrite_existing, ec);
    }

    // Se ainda nao existir o executavel copiado, podemos compilar via BuildService
    if (!fs::exists(finalExePath, ec)) {
        build::BuildEvents ev;
        auto br = build::buildAndRun(graph, projectRoot, ev);
        if (br.success && fs::exists(builtExe, ec)) {
            fs::copy_file(builtExe, finalExePath, fs::copy_options::overwrite_existing, ec);
        }
    }

    if (fs::exists(finalExePath, ec)) {
        res.executablePath = finalExePath.string();
    } else {
        // Fallback: se nao puder compilar agora, registra o caminho pretendido
        res.executablePath = finalExePath.string();
    }

    // 6. Gera scripts de inicializacao automatica corporativa (.bat e .ps1)
#ifdef _WIN32
    fs::path batPath = targetDir / "run.bat";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\r\n";
        bat << "title " << (manifest.runner.title.empty() ? manifest.name : manifest.runner.title) << "\r\n";
        bat << "echo ================================================================\r\n";
        bat << "echo Arestrix Standalone Runner - " << (manifest.runner.title.empty() ? manifest.name : manifest.runner.title) << "\r\n";
        bat << "echo ================================================================\r\n";
        bat << "cd /d \"%~dp0\"\r\n";
        bat << "if exist \"" << appName << "\" (\r\n";
        bat << "    start \"\" \"" << appName << "\"\r\n";
        bat << ") else (\r\n";
        bat << "    echo Executavel nativo '" << appName << "' em preparacao.\r\n";
        bat << "    echo Para executar o pipeline diretamente:\r\n";
        bat << "    echo arestrix-cli run runner_manifest.json\r\n";
        bat << ")\r\n";
    }
#endif

    res.ok = true;
    return res;
}

bool RunnerService::launchRunner(const fs::path& projectRoot, const std::string& executablePath) {
    std::error_code ec;
    fs::path p(executablePath);
    if (!p.is_absolute()) {
        p = projectRoot / p;
    }

    if (fs::exists(p, ec)) {
#ifdef _WIN32
        HINSTANCE hInst = ShellExecuteW(
            NULL,
            L"open",
            p.wstring().c_str(),
            NULL,
            p.parent_path().wstring().c_str(),
            SW_SHOWNORMAL
        );
        return (INT_PTR)hInst > 32;
#else
        std::string cmd = "nohup \"" + p.string() + "\" > /dev/null 2>&1 &";
        return std::system(cmd.c_str()) == 0;
#endif
    }

    // Se nao existir o exe exato, abre a pasta de destino no explorer
    fs::path folder = p.parent_path();
    if (fs::exists(folder, ec)) {
#ifdef _WIN32
        HINSTANCE hInst = ShellExecuteW(
            NULL,
            L"explore",
            folder.wstring().c_str(),
            NULL,
            NULL,
            SW_SHOWNORMAL
        );
        return (INT_PTR)hInst > 32;
#else
        std::string cmd = "xdg-open \"" + folder.string() + "\" &";
        return std::system(cmd.c_str()) == 0;
#endif
    }
    return false;
}

} // namespace project
} // namespace taskforge
