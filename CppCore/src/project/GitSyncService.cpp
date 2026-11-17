#include "GitSyncService.hpp"
#include "MemoryDatabaseGuard.hpp"
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
#include <regex>

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

/**
 * Verifica se o repositório tem user.name/user.email efetivos (local ou global).
 * Usado para decidir se o commit precisa de uma identidade de fallback pontual.
 */
bool hasGitIdentity(const std::filesystem::path& projectRoot) {
    for (const char* key : {"user.name", "user.email"}) {
        platform::ProcessSpec get;
        get.argv = {"git", "config", "--get", key};
        get.workingDir = projectRoot;
        get.scrubSecrets = true;
        auto current = platform::run(get, 3000);
        if (!current.ok() || trim(current.output).empty()) return false;
    }
    return true;
}

/**
 * Le uma chave do git config global/sistema (sem projeto, sem credencial secreta).
 */
std::string gitConfigGet(const std::string& key) {
    if (!platform::commandExists("git")) return "";
    platform::ProcessSpec s;
    s.argv = {"git", "config", "--get", key};
    s.scrubSecrets = true;
    auto r = platform::run(s, 3000);
    return r.ok() ? trim(r.output) : "";
}

/**
 * Resolve o login da conta no provedor. O nome de exibicao (user.name) nunca e
 * usado: ele costuma conter espacos e nao identifica a conta no servidor.
 */
std::string detectAccountUsernameImpl(const std::string& provider,
                                      bool allowGhCli,
                                      const std::vector<std::string>* gcmAccounts) {
    std::string prov = toLower(provider);
    if (prov.empty()) prov = "github";

    std::vector<std::string> keys;
    if (prov == "github") {
        keys = {"github.user", "credential.https://github.com.username", "credential.username"};
    } else if (prov == "gitlab") {
        keys = {"gitlab.user", "credential.https://gitlab.com.username", "credential.username"};
    } else {
        keys = {"credential.username"};
    }

    for (const auto& key : keys) {
        std::string candidate = GitSyncService::normalizeAccountLogin(gitConfigGet(key));
        if (!candidate.empty()) return candidate;
    }

    const std::vector<std::string> localAccounts =
        gcmAccounts ? *gcmAccounts : GitSyncService::listGcmAccounts(prov);
    for (const auto& account : localAccounts) {
        std::string candidate = GitSyncService::normalizeAccountLogin(account);
        if (!candidate.empty()) return candidate;
    }

    if (allowGhCli && prov == "github" && platform::commandExists("gh")) {
        platform::ProcessSpec s;
        s.argv = {"gh", "api", "user", "--jq", ".login"};
        s.scrubSecrets = true;
        auto r = platform::run(s, 8000);
        if (r.ok()) {
            std::string candidate = GitSyncService::normalizeAccountLogin(r.output);
            if (!candidate.empty()) return candidate;
        }
    }

    return "";
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

std::string GitSyncService::normalizeAccountLogin(const std::string& value) {
    std::string login = trim(value);
    if (login.empty() || login.size() > 100) return "";

    // Um login nunca contem espacos, e-mail ou separadores de URL. Valores assim
    // (tipicamente o nome completo do user.name) sao rejeitados de proposito.
    for (char c : login) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) return "";
        if (c == '/' || c == '\\' || c == '@' || c == ':' || c == '?' || c == '#' ||
            c == '&' || c == '=' || c == '%') {
            return "";
        }
    }
    return login;
}

std::string GitSyncService::detectAccountUsername(const std::string& provider, bool allowGhCli) {
    return detectAccountUsernameImpl(provider, allowGhCli, nullptr);
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
    st.accountUsername = detectAccountUsernameImpl("github", false, &st.gcmAccounts);

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

    // Commits pendentes de push (unpushed)
    {
        platform::ProcessSpec s;
        s.argv = {"git", "rev-list", "--count", "@{u}..HEAD"};
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 3000);
        if (r.ok()) {
            try {
                st.unpushedCommitsCount = std::stoi(trim(r.output));
                st.hasUnpushedCommits = (st.unpushedCommitsCount > 0);
            } catch (...) {}
        } else if (!st.remoteUrl.empty()) {
            std::string branch = !st.currentBranch.empty() ? st.currentBranch : "main";
            platform::ProcessSpec s2;
            s2.argv = {"git", "rev-list", "--count", "origin/" + branch + "..HEAD"};
            s2.workingDir = projectRoot;
            s2.scrubSecrets = true;
            auto r2 = platform::run(s2, 3000);
            if (r2.ok()) {
                try {
                    st.unpushedCommitsCount = std::stoi(trim(r2.output));
                    st.hasUnpushedCommits = (st.unpushedCommitsCount > 0);
                } catch (...) {}
            }
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
        "storage/*.sql",
        "storage/*.sqlite-journal",
        "storage/*.sqlite-wal",
        "storage/*.sqlite-shm",
        "*.sqlite",
        "*.sqlite3",
        "*.db",
        "*.sqlite-journal",
        "*.sqlite-wal",
        "*.sqlite-shm",
        "*.sql",
        "*.dump.json",
        "*.db.corrupt-*",
        "output/",
        ".arestrix/",
        ".workspace/",
        "workspace/",
        ".env",
        ".env.local",
        "node_modules/",
        "*.gguf",
        "*.safetensors",
        "*.bin",
        "*.onnx",
        "*.pt",
        "*.pth",
        "models/",
        "*.parquet",
        "*.arrow",
        "*.feather",
        "*.h5",
        "*.hdf5",
        "*.tar",
        "*.gz",
        "*.zip",
        "*.7z",
        "*.rar",
        "*.iso",
        "*.mp4",
        "*.mov",
        "*.avi",
        "*.mkv",
        "*.wav",
        "temp/",
        "cache/",
        "*.cache",
        "*.log"
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
                                     std::string& resolvedUsername,
                                     std::string& error) {
    remoteUrl.clear();
    resolvedUsername.clear();

    std::string safeRepoName = sanitizeRepoName(repoName);
    std::string prov = toLower(provider);
    if (prov.empty()) prov = "github";

    // O username informado pode vir do campo errado (ex.: nome completo do user.name).
    // Nesse caso ele e descartado e o login real da conta e resolvido no SO.
    resolvedUsername = normalizeAccountLogin(username);
    if (resolvedUsername.empty()) {
        resolvedUsername = detectAccountUsername(prov, true);
    }

    std::string baseHost = !serverUrl.empty() ? serverUrl : "https://github.com";
    while (!baseHost.empty() && baseHost.back() == '/') baseHost.pop_back();
    if (baseHost.empty()) baseHost = "https://github.com";

    // 1. Tenta GitHub CLI (gh) apenas no github.com; a URL canonica vem do proprio gh,
    // evitando compor owner/repo com um username incorreto.
    const bool isGithubDotCom = baseHost.find("github.com") != std::string::npos;
    if (prov == "github" && isGithubDotCom && platform::commandExists("gh")) {
        platform::ProcessSpec spec;
        spec.argv = {"gh", "repo", "create", safeRepoName, isPrivate ? "--private" : "--public"};
        spec.scrubSecrets = true;
        auto res = platform::run(spec, 30000);

        // Consulta a URL mesmo quando o repo ja existia (create retorna erro nesse caso).
        platform::ProcessSpec view;
        view.argv = {"gh", "repo", "view", safeRepoName, "--json", "url", "-q", ".url"};
        view.scrubSecrets = true;
        auto viewRes = platform::run(view, 15000);

        std::string ghUrl = viewRes.ok() ? trim(viewRes.output) : "";
        if (ghUrl.empty() && res.ok()) {
            std::smatch match;
            static const std::regex urlPattern(R"(https?://[^\s]+)");
            if (std::regex_search(res.output, match, urlPattern)) ghUrl = match.str(0);
        }

        if (!ghUrl.empty() && ghUrl.rfind("http", 0) == 0) {
            if (ghUrl.size() < 4 || ghUrl.substr(ghUrl.size() - 4) != ".git") ghUrl += ".git";
            remoteUrl = ghUrl;
            if (resolvedUsername.empty()) {
                std::smatch ownerMatch;
                static const std::regex ownerPattern(R"(^https?://[^/]+/([^/]+)/)");
                if (std::regex_search(remoteUrl, ownerMatch, ownerPattern)) {
                    resolvedUsername = normalizeAccountLogin(ownerMatch[1].str());
                }
            }
            std::cout << "[GitSyncService] Repositorio remoto privado preparado: " << remoteUrl << "\n";
            return true;
        }
    }

    // 2. Sem gh, monta a URL remota com o login validado (nunca o nome de exibicao).
    if (resolvedUsername.empty()) {
        error = prov == "github"
            ? "Nao foi possivel identificar o username da conta GitHub no SO. Informe a Conta / Usuario Remoto (ex.: frankalcantara), nao o nome de exibicao."
            : "Nao foi possivel identificar o username da conta remota. Informe a Conta / Usuario Remoto na aba Repositorio.";
        return false;
    }

    remoteUrl = baseHost + "/" + resolvedUsername + "/" + safeRepoName + ".git";

    std::cout << "[GitSyncService] Repositorio remoto privado preparado: " << remoteUrl << "\n";
    return true;
}

bool GitSyncService::commitLocal(const std::filesystem::path& projectRoot,
                                const std::string& commitMessage,
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

    ensureSafeGitignore(projectRoot);

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

    // 3. Verificacao preventiva de segredos (Secret Scanning pre-commit)
    if (hasChanges) {
        platform::ProcessSpec diffSpec;
        diffSpec.argv = {"git", "diff", "--cached", "-U0"};
        diffSpec.workingDir = projectRoot;
        diffSpec.scrubSecrets = false;
        auto diffRes = platform::run(diffSpec, 5000);
        if (diffRes.ok() && !diffRes.output.empty()) {
            static const std::vector<std::regex> secretPatterns = {
                std::regex(R"(\bsk-[A-Za-z0-9_-]{20,})"),
                std::regex(R"(\bAIza[0-9A-Za-z_-]{30,})"),
                std::regex(R"(\b(?:AKIA|ASIA)[0-9A-Z]{16}\b)"),
                std::regex(R"(\b(?:ghp_|github_pat_)[A-Za-z0-9_]{30,})")
            };
            for (const auto& pat : secretPatterns) {
                if (std::regex_search(diffRes.output, pat)) {
                    error = "Secret scan guard: Commit bloqueado: detectada chave de API ou segredo em texto puro no projeto. Utilize referencias ${VAR} para variaveis de ambiente.";
                    std::cerr << "[GitSyncService] " << error << "\n";
                    return false;
                }
            }
        }
    }

    // 4. git commit se houver alteracoes
    if (hasChanges) {
        std::string msg = !commitMessage.empty() ? commitMessage : ("Arestrix Auto-Save: " + getCurrentIsoTime());
        platform::ProcessSpec s;
        if (hasGitIdentity(projectRoot)) {
            s.argv = {"git", "commit", "-m", msg};
        } else {
            // Fallback pontual para maquinas sem user.name/user.email (CI, instalação limpa).
            // Nao grava config local: uma identidade global configurada depois continua valendo.
            s.argv = {"git", "-c", "user.name=Arestrix", "-c", "user.email=arestrix@localhost", "commit", "-m", msg};
        }
        s.workingDir = projectRoot;
        s.scrubSecrets = true;
        auto r = platform::run(s, 10000);
        if (!r.ok()) {
            error = "Falha no git commit: " + r.error + " " + r.output;
            return false;
        }
        output += "Commit realizado com sucesso. ";
    } else {
        output += "Nenhuma alteracao local pendente. ";
    }

    return true;
}

bool GitSyncService::pushRemote(const std::filesystem::path& projectRoot,
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

    platform::ProcessSpec s;
    s.argv = {"git", "push", "-u", "origin", targetBranch};
    s.workingDir = projectRoot;
    s.scrubSecrets = true;
    auto r = platform::run(s, 30000);
    if (!r.ok()) {
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
    return true;
}

bool GitSyncService::commitAndPush(const std::filesystem::path& projectRoot,
                                  const std::string& commitMessage,
                                  const std::string& branch,
                                  std::string& output,
                                  std::string& error) {
    std::string commitOut, commitErr;
    if (!commitLocal(projectRoot, commitMessage, commitOut, commitErr)) {
        error = commitErr;
        return false;
    }
    output += commitOut;

    std::string pushOut, pushErr;
    if (!pushRemote(projectRoot, branch, pushOut, pushErr)) {
        error = pushErr;
        return false;
    }
    output += pushOut;
    return true;
}

void GitSyncService::onShutdown(const std::filesystem::path& projectRoot) {
    if (projectRoot.empty()) return;

    auto cur = taskforge::project::ProjectManager::getInstance().getCurrentProject();

    // Retencao de telemetria e housekeeping local: independe de Git estar habilitado.
    std::string retentionErr;
    MemoryDatabaseGuard::applyRetentionOnShutdown(projectRoot, cur.projectId, retentionErr);
    if (!retentionErr.empty()) {
        std::cerr << "[GitSyncService] Aviso na retencao de memoria: " << retentionErr << "\n";
    }

    if (!cur.git.enabled) {
        std::cout << "[GitSyncService] Sincronizacao Git desativada para este projeto. Operando em modo 100% local.\n";
        return;
    }

    std::cout << "[GitSyncService] Executando rotina de encerramento do projeto...\n";

    if (!std::filesystem::exists(projectRoot / ".git")) return;

    // 1. Remove bancos e dumps do rastreamento do Git caso tenham sido commitados anteriormente.
    // O Arestrix nao gera backup/dump de bancos: a custodia desses arquivos e externa ao produto.
    {
        platform::ProcessSpec rmSpec;
        rmSpec.argv = {"git", "rm", "--cached", "--ignore-unmatch",
                       "storage/*.sqlite", "storage/*.sqlite3", "storage/*.db", "storage/*.sql",
                       "*.sqlite", "*.db", "*.sql", "*.dump.json", "*.db.corrupt-*"};
        rmSpec.workingDir = projectRoot;
        rmSpec.scrubSecrets = true;
        platform::run(rmSpec, 5000);
    }

    // 2. Executa commit e push de encerramento
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
