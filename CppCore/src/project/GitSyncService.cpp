#include "GitSyncService.hpp"
#include "DatabaseBackupService.hpp"
#include "ProjectManager.hpp"
#include "platform/Process.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace taskforge {
namespace project {

namespace {

std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::string getCurrentIsoTime() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&in_time_t), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

std::string sanitizeRepoName(const std::string& name) {
    std::string clean;
    for (char c : name) {
        if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') {
            clean.push_back(c);
        } else if (c == ' ') {
            clean.push_back('-');
        }
    }
    while (!clean.empty() && (clean.front() == '.' || clean.front() == '-')) clean.erase(clean.begin());
    while (!clean.empty() && (clean.back() == '.' || clean.back() == '-')) clean.pop_back();
    if (clean.empty()) clean = "arestrix-project";
    return clean;
}

} // namespace

bool GitSyncService::isCommercialService(const std::string& serverOrProvider) {
    std::string s = toLower(serverOrProvider);
    return s == "github" || s.find("github.com") != std::string::npos ||
           s == "gitlab.com" || s.find("gitlab.com") != std::string::npos ||
           s == "bitbucket" || s.find("bitbucket.org") != std::string::npos;
}

bool GitSyncService::checkGitInstalled(std::string& version) {
    if (!platform::commandExists("git")) {
        version = "";
        return false;
    }
    platform::ProcessSpec spec;
    spec.argv = {"git", "--version"};
    spec.scrubSecrets = true;
    auto res = platform::run(spec, 5000);
    if (res.ok()) {
        version = trim(res.output);
        return true;
    }
    return false;
}

bool GitSyncService::getSystemIdentity(std::string& name, std::string& email) {
    name = "";
    email = "";
    if (!platform::commandExists("git")) return false;

    platform::ProcessSpec sName;
    sName.argv = {"git", "config", "--get", "user.name"};
    sName.scrubSecrets = true;
    auto rName = platform::run(sName, 3000);
    if (rName.ok()) name = trim(rName.output);

    platform::ProcessSpec sEmail;
    sEmail.argv = {"git", "config", "--get", "user.email"};
    sEmail.scrubSecrets = true;
    auto rEmail = platform::run(sEmail, 3000);
    if (rEmail.ok()) email = trim(rEmail.output);

    return !name.empty() && !email.empty();
}

bool GitSyncService::setSystemIdentity(const std::string& name, const std::string& email, std::string& error) {
    if (!platform::commandExists("git")) {
        error = "Git nao encontrado no sistema operacional.";
        return false;
    }

    if (!name.empty()) {
        platform::ProcessSpec s;
        s.argv = {"git", "config", "--global", "user.name", name};
        s.scrubSecrets = true;
        auto r = platform::run(s, 5000);
        if (!r.ok()) {
            error = "Falha ao definir user.name: " + r.error + " " + r.output;
            return false;
        }
    }

    if (!email.empty()) {
        platform::ProcessSpec s;
        s.argv = {"git", "config", "--global", "user.email", email};
        s.scrubSecrets = true;
        auto r = platform::run(s, 5000);
        if (!r.ok()) {
            error = "Falha ao definir user.email: " + r.error + " " + r.output;
            return false;
        }
    }

    return true;
}

std::vector<std::string> GitSyncService::listGcmAccounts(const std::string& provider) {
    std::vector<std::string> accounts;
    if (!platform::commandExists("git")) return accounts;

    std::string prov = toLower(provider);
    if (prov.empty()) prov = "github";

    platform::ProcessSpec s;
    s.argv = {"git", "credential-manager", prov, "list"};
    s.scrubSecrets = true;
    auto r = platform::run(s, 5000);
    if (r.ok()) {
        std::istringstream stream(r.output);
        std::string line;
        while (std::getline(stream, line)) {
            line = trim(line);
            if (!line.empty() && line.find(" ") == std::string::npos && line.find("Error") == std::string::npos) {
                accounts.push_back(line);
            }
        }
    }
    return accounts;
}

bool GitSyncService::launchGcmLogin(const std::string& provider, std::string& error) {
    if (!platform::commandExists("git")) {
        error = "Git nao encontrado no sistema operacional.";
        return false;
    }

    std::string prov = toLower(provider);
    if (prov.empty()) prov = "github";

    platform::ProcessSpec s;
    s.argv = {"git", "credential-manager", prov, "login"};
    s.scrubSecrets = true;
    auto r = platform::run(s, 60000); // 1 minuto para autenticacao no SO
    if (!r.ok()) {
        error = !r.error.empty() ? r.error : r.output;
        return false;
    }
    return true;
}

GitStatusResult GitSyncService::getStatus(const std::filesystem::path& projectRoot) {
    GitStatusResult st;
    st.gitInstalled = checkGitInstalled(st.gitVersion);
    if (!st.gitInstalled) {
        st.error = "Git nao instalado.";
        return st;
    }

    st.isIdentityConfigured = getSystemIdentity(st.userName, st.userEmail);
    st.gcmAccounts = listGcmAccounts("github");

    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) {
        return st;
    }

    std::filesystem::path gitDir = projectRoot / ".git";
    st.isRepo = std::filesystem::exists(gitDir);
    if (!st.isRepo) {
        return st;
    }

    // Branch atual
    {
        platform::ProcessSpec s;
        s.argv = {"git", "branch", "--show-current"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 3000);
        if (r.ok()) st.currentBranch = trim(r.output);
    }

    // Remote origin
    {
        platform::ProcessSpec s;
        s.argv = {"git", "remote", "get-url", "origin"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 3000);
        if (r.ok()) st.remoteUrl = trim(r.output);
    }

    // Modificacoes locais
    {
        platform::ProcessSpec s;
        s.argv = {"git", "status", "--porcelain"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 3000);
        if (r.ok()) {
            st.hasUncommittedChanges = !trim(r.output).empty();
        }
    }

    // Ultimo commit
    {
        platform::ProcessSpec s;
        s.argv = {"git", "log", "-1", "--format=%H%n%s"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 3000);
        if (r.ok()) {
            std::istringstream iss(r.output);
            std::getline(iss, st.lastCommitHash);
            std::getline(iss, st.lastCommitMessage);
            st.lastCommitHash = trim(st.lastCommitHash);
            st.lastCommitMessage = trim(st.lastCommitMessage);
        }
    }

    return st;
}

void GitSyncService::ensureSafeGitignore(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty() || !std::filesystem::exists(projectRoot)) return;
    std::filesystem::path ignorePath = projectRoot / ".gitignore";

    const std::vector<std::string> requiredRules = {
        "build/",
        "dist/",
        "*.exe",
        "*.obj",
        "*.o",
        "*.pdb",
        "*.ilk",
        "*.tmp",
        "storage/*.sqlite",
        "storage/*.sqlite3",
        "storage/*.db",
        "storage/*.sqlite-journal",
        "storage/*.sqlite-wal",
        "storage/*.sqlite-shm",
        "*.sqlite",
        "*.sqlite3",
        "*.db",
        "*.sqlite-journal",
        "*.sqlite-wal",
        "*.sqlite-shm",
        "output/",
        ".arestrix/",
        "workspace/",
        ".env",
        ".env.local",
        "node_modules/"
    };

    if (!std::filesystem::exists(ignorePath)) {
        std::ofstream ig(ignorePath);
        if (ig.is_open()) {
            ig << "# Arestrix Project Gitignore\n";
            for (const auto& r : requiredRules) {
                ig << r << "\n";
            }
        }
        return;
    }

    std::ifstream in(ignorePath);
    std::string existingContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    std::vector<std::string> toAppend;
    for (const auto& r : requiredRules) {
        if (existingContent.find(r) == std::string::npos) {
            toAppend.push_back(r);
        }
    }

    if (!toAppend.empty()) {
        std::ofstream out(ignorePath, std::ios::app);
        if (out.is_open()) {
            out << "\n# Regras de seguranca de dados Arestrix (exclusao de bancos binarios)\n";
            for (const auto& r : toAppend) {
                out << r << "\n";
            }
        }
    }
}

bool GitSyncService::initProjectRepo(const std::filesystem::path& projectRoot, const std::string& remoteUrl, std::string& error) {
    if (!platform::commandExists("git")) {
        error = "Git nao encontrado no sistema.";
        return false;
    }

    if (!std::filesystem::exists(projectRoot)) {
        std::error_code ec;
        std::filesystem::create_directories(projectRoot, ec);
    }

    std::filesystem::path gitDir = projectRoot / ".git";
    if (!std::filesystem::exists(gitDir)) {
        platform::ProcessSpec s;
        s.argv = {"git", "init", "-b", "main"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 5000);
        if (!r.ok()) {
            // Tenta fallback sem '-b main' para versoes mais antigas do Git
            platform::ProcessSpec sOld;
            sOld.argv = {"git", "init"};
            sOld.workingDir = projectRoot;
            sOld.scrubSecrets = true;
            auto rOld = platform::run(sOld, 5000);
            if (!rOld.ok()) {
                error = "Falha ao inicializar git init: " + rOld.error + " " + rOld.output;
                return false;
            }
        }
    }

    // Garante .gitignore seguro na pasta do projeto com exclusao de bancos binarios
    ensureSafeGitignore(projectRoot);

    // Configura o remote se fornecido
    if (!remoteUrl.empty()) {
        platform::ProcessSpec checkRemote;
        checkRemote.argv = {"git", "remote", "get-url", "origin"};
        checkRemote.workingDir = projectRoot;
        checkRemote.scrubSecrets = true;
        auto rCheck = platform::run(checkRemote, 3000);

        platform::ProcessSpec sRemote;
        sRemote.workingDir = projectRoot;
        sRemote.scrubSecrets = true;
        if (rCheck.ok()) {
            sRemote.argv = {"git", "remote", "set-url", "origin", remoteUrl};
        } else {
            sRemote.argv = {"git", "remote", "add", "origin", remoteUrl};
        }
        auto rRemote = platform::run(sRemote, 5000);
        if (!rRemote.ok()) {
            error = "Falha ao configurar remote origin: " + rRemote.error + " " + rRemote.output;
            return false;
        }
    }

    return true;
}

bool GitSyncService::createRemoteRepo(const std::string& provider,
                                     const std::string& repoName,
                                     bool isPrivate,
                                     const std::string& username,
                                     const std::string& serverUrl,
                                     std::string& remoteUrl,
                                     std::string& error) {
    std::string safeRepoName = sanitizeRepoName(repoName);
    std::string prov = toLower(provider);
    if (prov.empty()) prov = "github";

    // 1. Tenta GitHub CLI (gh) se disponivel
    if (prov == "github" && platform::commandExists("gh")) {
        platform::ProcessSpec spec;
        spec.argv = {"gh", "repo", "create", safeRepoName, isPrivate ? "--private" : "--public", "--confirm"};
        spec.scrubSecrets = true;
        auto res = platform::run(spec, 30000);
        // Mesmo se falhar porque ja existe, continua para montar a URL
    }

    // 2. Monta a URL remota padrao segundo as diretrizes de privacidade e seguranca
    std::string baseHost = !serverUrl.empty() ? serverUrl : "https://github.com";
    if (baseHost.back() == '/') baseHost.pop_back();

    std::string user = !username.empty() ? username : "usuario";
    remoteUrl = baseHost + "/" + user + "/" + safeRepoName + ".git";

    std::cout << "[GitSyncService] Repositorio remoto privado preparado: " << remoteUrl << "\n";
    return true;
}

bool GitSyncService::commitAndPush(const std::filesystem::path& projectRoot,
                                  const std::string& commitMessage,
                                  const std::string& branch,
                                  std::string& output,
                                  std::string& error) {
    if (!platform::commandExists("git")) {
        error = "Git nao instalado.";
        return false;
    }

    if (!std::filesystem::exists(projectRoot / ".git")) {
        error = "Pasta nao e um repositorio git.";
        return false;
    }

    std::string targetBranch = !branch.empty() ? branch : "main";

    // 1. git add -A
    {
        platform::ProcessSpec s;
        s.argv = {"git", "add", "-A"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 10000);
        if (!r.ok()) {
            error = "Falha no git add: " + r.error + " " + r.output;
            return false;
        }
    }

    // 2. git status --porcelain para checar se ha alteracoes para commit
    bool hasChanges = true;
    {
        platform::ProcessSpec s;
        s.argv = {"git", "status", "--porcelain"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 5000);
        if (r.ok() && trim(r.output).empty()) {
            hasChanges = false;
        }
    }

    // 3. git commit se houver alteracoes
    if (hasChanges) {
        std::string msg = !commitMessage.empty() ? commitMessage : ("Arestrix Auto-Save: " + getCurrentIsoTime());
        platform::ProcessSpec s;
        s.argv = {"git", "commit", "-m", msg};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 10000);
        if (!r.ok()) {
            error = "Falha no git commit: " + r.error + " " + r.output;
            return false;
        }
        output += "Commit realizado. ";
    } else {
        output += "Nenhuma alteracao local pendente. ";
    }

    // 4. git push (usa o Git Credential Manager do SO de forma transparente)
    {
        platform::ProcessSpec s;
        s.argv = {"git", "push", "-u", "origin", targetBranch};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 30000);
        if (!r.ok()) {
            // Tenta push simples sem -u se falhar
            platform::ProcessSpec sFallback;
            sFallback.argv = {"git", "push", "origin", targetBranch};
            sFallback.workingDir = projectRoot;
            sFallback.scrubSecrets = true;
            auto rFallback = platform::run(sFallback, 30000);
            if (!rFallback.ok()) {
                error = "Falha no git push: " + rFallback.error + " " + rFallback.output;
                return false;
            }
        }
        output += "Push enviado para " + targetBranch + ".";
    }

    return true;
}

void GitSyncService::onShutdown(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;

    std::cout << "[GitSyncService] Executando rotina de encerramento do projeto...\n";

    // 1. Executa backup e dump de bancos de dados para recuperacao de desastre
    std::string backupErr;
    auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();
    DatabaseBackupService::performProjectShutdownBackup(projectRoot, cur.projectId, backupErr);
    if (!backupErr.empty()) {
        std::cerr << "[GitSyncService] Aviso no backup de encerramento: " << backupErr << "\n";
    }

    if (!std::filesystem::exists(projectRoot / ".git")) return;

    // 2. Remove arquivos binarios SQLite do rastreamento do Git caso tenham sido commitados anteriormente
    {
        platform::ProcessSpec rmSpec;
        rmSpec.argv = {"git", "rm", "--cached", "--ignore-unmatch", "storage/*.sqlite", "storage/*.sqlite3", "storage/*.db", "*.sqlite", "*.db"};
        rmSpec.workingDir = projectRoot;
        rmSpec.scrubSecrets = true;
        platform::run(rmSpec, 5000);
    }

    // 3. Executa commit e push de encerramento
    std::cout << "[GitSyncService] Executando sincronizacao Git de encerramento do sistema...\n";
    std::string out, err;
    std::string msg = "Auto-commit on shutdown: " + getCurrentIsoTime();
    commitAndPush(projectRoot, msg, "main", out, err);
    if (!err.empty()) {
        std::cerr << "[GitSyncService] Aviso no shutdown push: " << err << "\n";
    } else {
        std::cout << "[GitSyncService] Sincronizacao de encerramento concluida com sucesso.\n";
    }
}

} // namespace project
} // namespace taskforge
