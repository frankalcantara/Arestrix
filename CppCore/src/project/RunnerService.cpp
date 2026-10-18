#include "project/RunnerService.hpp"
#include "project/RunnerSecurity.hpp"
#include "project/RunnerPackaging.hpp"
#include "compiler/TaskflowGenerator.hpp"
#include "build/BuildService.hpp"
#include "platform/Process.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

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

static std::string maskSecret(const std::string& v) {
    if (v.size() <= 6) return "***";
    return v.substr(0, 4) + "... (" + std::to_string(v.size()) + " chars)";
}

static bool isSecretPlaceholder(const std::string& s) {
    if (s.empty()) return true;

    // Referencias a variaveis de ambiente: ${VAR}, %VAR%, $VAR
    if ((s.front() == '$' && s.back() == '}') || (s.front() == '%' && s.back() == '%')) return true;
    if (s.rfind("${", 0) == 0 && s.back() == '}') return true;

    // Nomes de variaveis de ambiente em caixa alta pura: ex.: DEEPSEEK_API_KEY
    bool allCapsEnv = true;
    for (char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
            allCapsEnv = false;
            break;
        }
    }
    if (allCapsEnv && s.size() >= 3) return true;

    // Palavras que indicam explicitamente exemplos, testes ou placeholders
    std::string lower;
    for (char c : s) lower.push_back((char)std::tolower((unsigned char)c));
    if (lower.find("test") != std::string::npos ||
        lower.find("fake") != std::string::npos ||
        lower.find("dummy") != std::string::npos ||
        lower.find("example") != std::string::npos ||
        lower.find("sample") != std::string::npos ||
        lower.find("placeholder") != std::string::npos ||
        lower.find("changeme") != std::string::npos ||
        lower.find("your-") != std::string::npos ||
        lower.find("your_") != std::string::npos ||
        lower.find("xxxx") != std::string::npos ||
        lower.find("***") != std::string::npos ||
        lower.find("redacted") != std::string::npos) {
        return true;
    }
    return false;
}

struct SecretPatternDef {
    const char* name;
    std::string pattern;
};

bool RunnerService::auditTextForSecrets(
    const std::string& text,
    const std::string& sourceName,
    std::string& outError
) {
    if (text.empty()) return true;

    static const std::vector<SecretPatternDef> tokenDefs = {
        {"Chave OpenAI / DeepSeek / Anthropic (sk-...)", R"(\bsk-[A-Za-z0-9][A-Za-z0-9_-]{19,}\b)"},
        {"Chave Google (AIza...)", R"(\bAIza[0-9A-Za-z_-]{30,}\b)"},
        {"Chave Groq (gsk_...)", R"(\bgsk_[A-Za-z0-9]{30,}\b)"},
        {"Token GitHub (ghp_/gho_/github_pat_...)", R"(\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})\b)"},
        {"Token Slack (xox...)", R"(\bxox[baprs]-[A-Za-z0-9-]{10,}\b)"},
        {"Chave AWS (AKIA/ASIA)", R"(\b(?:AKIA|ASIA)[0-9A-Z]{16}\b)"},
        {"Chave Privada RSA/EC/OPENSSH", R"(-----BEGIN [A-Z ]*PRIVATE KEY-----)"},
        {"Token JWT (eyJ...)", R"(\beyJ[A-Za-z0-9_-]{15,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\b)"},
        {"Cabecalho Bearer com token", R"(\bBearer\s+[A-Za-z0-9._~+/=-]{24,}\b)"}
    };

    for (const auto& def : tokenDefs) {
        try {
            std::regex re(def.pattern);
            auto begin = std::sregex_iterator(text.begin(), text.end(), re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                std::string matchStr = it->str();
                if (!isSecretPlaceholder(matchStr)) {
                    outError = "Segredo detectado em [" + sourceName + "]: " + def.name + " (" + maskSecret(matchStr) + ")";
                    return false;
                }
            }
        } catch (...) {
            // Em caso de erro no regex, prossegue para os proximos padroes
        }
    }

    // Atribuicao de segredo: api_key = "...", token: "...", password: "..."
    try {
        std::regex assignRe(R"(["']?\b([A-Za-z0-9_.-]*(?:api[_-]?key|secret|passw(?:or)?d|passwd|token|credential|private[_-]?key)[A-Za-z0-9_.-]*)["']?\s*[:=]\s*["']([^"'\r\n]{14,})["'])", std::regex::icase);
        auto begin = std::sregex_iterator(text.begin(), text.end(), assignRe);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string keyField = (*it)[1].str();
            std::string val = (*it)[2].str();
            if (!isSecretPlaceholder(val)) {
                outError = "Atribuicao de credencial/senha detectada em [" + sourceName + "]: campo '" + keyField + "' (" + maskSecret(val) + ")";
                return false;
            }
        }
    } catch (...) {}

    return true;
}

bool RunnerService::auditDirectoryForSecrets(
    const fs::path& dir,
    std::string& outError
) {
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return true;
    }

    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!fs::is_regular_file(entry, ec)) continue;

        std::string filename = entry.path().filename().string();
        std::string ext = entry.path().extension().string();
        std::string lowerFn = filename;
        for (char& c : lowerFn) c = (char)std::tolower((unsigned char)c);
        std::string lowerExt = ext;
        for (char& c : lowerExt) c = (char)std::tolower((unsigned char)c);

        // 1. Arquivos estritamente proibidos
        if (lowerFn == ".env" || lowerFn.rfind(".env.", 0) == 0 ||
            lowerFn == "credentials.json" || lowerFn == "credentials" ||
            lowerFn == "secrets.json" || lowerFn == "secrets.yaml" || lowerFn == "secrets.yml" ||
            lowerFn == "id_rsa" || lowerFn == "id_ed25519" ||
            lowerExt == ".pem" || lowerExt == ".key" || lowerExt == ".p12" || lowerExt == ".pfx") {
            outError = "Arquivo sensivel proibido detectado no pacote distribuivel: " + entry.path().filename().string();
            return false;
        }

        // 2. Para executaveis binarios: verificar se nao ha literais de chaves embutidas
        if (lowerExt == ".exe" || lowerExt.empty()) {
            std::ifstream bin(entry.path(), std::ios::binary);
            if (bin.is_open()) {
                // Le os primeiros 4 MB do binario para busca de literais
                std::vector<char> buffer(4 * 1024 * 1024, 0);
                bin.read(buffer.data(), buffer.size());
                std::streamsize bytesRead = bin.gcount();
                std::string binSample(buffer.data(), bytesRead);

                if (binSample.find("sk-") != std::string::npos) {
                    std::string secErr;
                    if (!auditTextForSecrets(binSample, entry.path().filename().string(), secErr)) {
                        outError = "O executavel contem chave de API embutida (sk-...): " + secErr;
                        return false;
                    }
                }
                if (binSample.find("-----BEGIN") != std::string::npos &&
                    binSample.find("PRIVATE KEY-----") != std::string::npos) {
                    outError = "O executavel contem chave privada embutida!";
                    return false;
                }
            }
            continue;
        }

        // 3. Arquivos de texto (manifestos, scripts, skills, json): auditoria de conteudo
        uintmax_t fsize = fs::file_size(entry.path(), ec);
        if (fsize < 5 * 1024 * 1024) { // Menor que 5 MB
            std::ifstream in(entry.path());
            if (in.is_open()) {
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::string secErr;
                if (!auditTextForSecrets(content, entry.path().filename().string(), secErr)) {
                    outError = secErr;
                    return false;
                }
            }
        }
    }

    return true;
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

    // 3. Monta o runner_manifest.json com a topologia do pipeline e configuracoes
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

    std::string manifestDump = runnerJson.dump(2);

    // Auditoria preventiva do manifesto e topologia do grafo antes de gravar no disco
    std::string manifestAuditErr;
    if (!auditTextForSecrets(manifestDump, "runner_manifest.json", manifestAuditErr)) {
        res.ok = false;
        res.error = "Exportacao abortada por seguranca (fail-closed): " + manifestAuditErr;
        return res;
    }

    fs::path manifestPath = targetDir / "runner_manifest.json";
    {
        std::ofstream out(manifestPath, std::ios::trunc);
        if (!out.is_open()) {
            res.ok = false;
            res.error = "Nao foi possivel gravar runner_manifest.json em " + manifestPath.string();
            return res;
        }
        out << manifestDump;
    }
    res.manifestPath = manifestPath.string();

    // 4. Copia skills do workspace caso existam (filtrando arquivos sensiveis)
    fs::path srcSkills = projectRoot / ".workspace" / "skills";
    if (fs::exists(srcSkills, ec) && fs::is_directory(srcSkills, ec)) {
        fs::path destSkills = targetDir / "skills";
        fs::create_directories(destSkills, ec);
        for (const auto& entry : fs::recursive_directory_iterator(srcSkills, ec)) {
            if (fs::is_regular_file(entry, ec)) {
                std::string fname = entry.path().filename().string();
                std::string lfn = fname;
                for (char& c : lfn) c = (char)std::tolower((unsigned char)c);
                if (lfn == ".env" || lfn.rfind(".env.", 0) == 0 ||
                    lfn == "credentials.json" || lfn == "secrets.json" ||
                    entry.path().extension() == ".key" || entry.path().extension() == ".pem") {
                    continue; // Nunca copia segredos
                }

                fs::path rel = fs::relative(entry.path(), srcSkills, ec);
                fs::path targetFile = destSkills / rel;
                fs::create_directories(targetFile.parent_path(), ec);
                fs::copy_file(entry.path(), targetFile, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // 5. Copia ou gera o executável nativo
    fs::path builtExe = projectRoot / ".arestrix" / "build";
#ifdef _WIN32
    builtExe /= "pipeline_app.exe";
#else
    builtExe /= "pipeline_app";
#endif

    if (fs::exists(builtExe, ec)) {
        fs::copy_file(builtExe, finalExePath, fs::copy_options::overwrite_existing, ec);
    }

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
        res.executablePath = finalExePath.string();
    }

    // 6. Gera scripts de inicializacao limpos e sem segredos (.bat)
#ifdef _WIN32
    fs::path batPath = targetDir / "run.bat";
    {
        std::ofstream bat(batPath, std::ios::trunc);
        bat << "@echo off\r\n";
        bat << "title " << (manifest.runner.title.empty() ? manifest.name : manifest.runner.title) << "\r\n";
        bat << "echo ================================================================\r\n";
        bat << "echo Arestrix Standalone Runner - " << (manifest.runner.title.empty() ? manifest.name : manifest.runner.title) << "\r\n";
        bat << "echo ================================================================\r\n";
        bat << "echo [Aviso de Seguranca] Nenhuma chave de API ou senha esta embutida.\r\n";
        bat << "echo [Aviso de Seguranca] As credenciais devem estar no ambiente do SO.\r\n";
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

    // 7. Assinatura Criptografica Corporativa do Pacote
    if (manifest.runner.signature.enabled) {
        std::string signKey = manifest.runner.signature.keyId;
        if (signKey.empty()) {
            signKey = "arestrix-corp-security-default-key";
        }
        auto sigRes = RunnerSecurity::signPackage(
            manifestPath,
            signKey,
            manifest.runner.signature.algorithm.empty() ? "hmac-sha256" : manifest.runner.signature.algorithm,
            manifest.runner.signature.issuer.empty() ? "Arestrix Enterprise Security" : manifest.runner.signature.issuer,
            manifest.runner.signature.keyId
        );
        if (sigRes.ok) {
            res.signaturePath = (manifestPath.parent_path() / "runner_manifest.json.sig").string();
        }
    }

    // 8. Empacotamento Corporativo (WiX MSI / MSIX / Intune Detection Rules & Checksums)
    auto packRes = RunnerPackaging::generatePackagingArtifacts(targetDir, manifest, appName);
    if (packRes.ok) {
        res.packagingArtifacts = packRes.generatedFiles;
    } else if (!packRes.error.empty()) {
        res.ok = false;
        res.error = "Falha ao gerar empacotamento corporativo: " + packRes.error;
        return res;
    }

    // 9. Auditoria profunda final de todo o diretorio de saida (fail-closed)
    std::string dirAuditErr;
    if (!auditDirectoryForSecrets(targetDir, dirAuditErr)) {
        // Remove arquivos gerados para prevenir pacote vulneravel
        fs::remove(manifestPath, ec);
        res.ok = false;
        res.error = "Auditoria de seguranca falhou (fail-closed): " + dirAuditErr;
        return res;
    }

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

bool RunnerService::verifyPackage(
    const fs::path& manifestPath,
    const std::string& publicKeyOrSecret,
    bool requiredForExecution,
    std::string& outError
) {
    return RunnerSecurity::verifyPackage(manifestPath, publicKeyOrSecret, requiredForExecution, outError);
}

} // namespace project
} // namespace taskforge
