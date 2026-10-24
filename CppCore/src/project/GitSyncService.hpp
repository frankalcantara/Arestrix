#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include "util/JsonValue.hpp"
#include "ProjectTypes.hpp"

namespace taskforge {
namespace project {

struct GitStatusResult {
    bool gitInstalled = false;
    std::string gitVersion;
    std::string userName;
    std::string userEmail;
    bool isIdentityConfigured = false;
    std::vector<std::string> gcmAccounts;
    bool isRepo = false;
    std::string currentBranch;
    std::string remoteUrl;
    bool hasUncommittedChanges = false;
    bool hasUnpushedCommits = false;
    int unpushedCommitsCount = 0;
    std::string lastCommitHash;
    std::string lastCommitMessage;
    std::string error;
};

inline void to_json(taskforge::Json& j, const GitStatusResult& r) {
    j = taskforge::Json{
        {"gitInstalled", r.gitInstalled},
        {"gitVersion", r.gitVersion},
        {"userName", r.userName},
        {"userEmail", r.userEmail},
        {"isIdentityConfigured", r.isIdentityConfigured},
        {"gcmAccounts", r.gcmAccounts},
        {"isRepo", r.isRepo},
        {"currentBranch", r.currentBranch},
        {"remoteUrl", r.remoteUrl},
        {"hasUncommittedChanges", r.hasUncommittedChanges},
        {"hasUnpushedCommits", r.hasUnpushedCommits},
        {"unpushedCommitsCount", r.unpushedCommitsCount},
        {"lastCommitHash", r.lastCommitHash},
        {"lastCommitMessage", r.lastCommitMessage},
        {"error", r.error}
    };
}

class GitSyncService {
public:
    // Identifica se um servidor ou URL e um servico comercial (ex: GitHub, GitLab.com, Bitbucket)
    static bool isCommercialService(const std::string& serverOrProvider);

    // Verifica se git esta instalado e operante
    static bool checkGitInstalled(std::string& version);

    // Consulta identidade global configurada no SO (git config --global user.name / user.email)
    static bool getSystemIdentity(std::string& name, std::string& email);

    // Configura a identidade no SO
    static bool setSystemIdentity(const std::string& name, const std::string& email, std::string& error);

    // Lista contas gerenciadas pelo Git Credential Manager no SO
    static std::vector<std::string> listGcmAccounts(const std::string& provider);

    // Dispara o fluxo seguro de login no SO (GCM)
    static bool launchGcmLogin(const std::string& provider, std::string& error);

    // Status completo do Git para a pasta do projeto
    static GitStatusResult getStatus(const std::filesystem::path& projectRoot);

    // Garante que o .gitignore na pasta do projeto contenha regras estritas de exclusao de bancos SQLite e artefatos
    static void ensureSafeGitignore(const std::filesystem::path& projectRoot);

    // Inicializa o repositorio local (git init) e define o remote se necessario
    static bool initProjectRepo(const std::filesystem::path& projectRoot, const std::string& remoteUrl, std::string& error);

    // Cria o repositorio remoto privado (de forma transparente via gh CLI ou convenção segura)
    static bool createRemoteRepo(const std::string& provider,
                                 const std::string& repoName,
                                 bool isPrivate,
                                 const std::string& username,
                                 const std::string& serverUrl,
                                 std::string& remoteUrl,
                                 std::string& error);

    // Executa git add e commit local (sem enviar para o remote, ideal para F5 / syncOnRun)
    static bool commitLocal(const std::filesystem::path& projectRoot,
                            const std::string& commitMessage,
                            std::string& output,
                            std::string& error);

    // Executa git push para o remote (utilizado pelo push temporizado e botao manual)
    static bool pushRemote(const std::filesystem::path& projectRoot,
                           const std::string& branch,
                           std::string& output,
                           std::string& error);

    // Executa git add, commit e push
    static bool commitAndPush(const std::filesystem::path& projectRoot,
                              const std::string& commitMessage,
                              const std::string& branch,
                              std::string& output,
                              std::string& error);

    // Rotina chamada no encerramento da aplicacao (shutdown)
    static void onShutdown(const std::filesystem::path& projectRoot);
};

} // namespace project
} // namespace taskforge
