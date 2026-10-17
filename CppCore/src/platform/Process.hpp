#pragma once

// Execucao de processos multiplataforma (Windows / POSIX) sem passar por shell,
// com captura de stdout+stderr em streaming, timeout, cancelamento e encerramento
// da arvore inteira de processos (Job Object no Windows, process group no POSIX).

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace taskforge {
namespace platform {

struct ProcessSpec {
    // Programa e argumentos (argv[0] e resolvido via PATH). Nunca interpretado por shell.
    std::vector<std::string> argv;
    // Alternativa: linha de comando entregue ao shell do SO (cmd /c ou sh -c).
    // Use somente para comandos escritos pelo proprio usuario/agente.
    std::string shellCommand;

    std::filesystem::path workingDir;
    std::map<std::string, std::string> env;   // variaveis adicionais/sobrescritas
    int memoryLimitMB = 0;                    // 0 = sem limite
    // Remove do ambiente do filho variaveis que parecem segredos (*_API_KEY, *_TOKEN,
    // *_SECRET, *_PASSWORD), para que codigo executado por agentes nao as leia.
    bool scrubSecrets = false;
    bool captureStdin = false;
};

enum class ReadStatus { Data, Timeout, Eof };

class Process {
public:
    virtual ~Process() = default;

    // Inicia o processo. Em caso de falha devolve nullptr e preenche `error`.
    static std::unique_ptr<Process> start(const ProcessSpec& spec, std::string& error);

    virtual bool writeStdin(const std::string& data) = 0;
    virtual void closeStdin() = 0;
    // Le o que estiver disponivel (aguarda ate timeoutMs). Anexa em `out`.
    virtual ReadStatus read(std::string& out, int timeoutMs) = 0;
    // true se o processo terminou dentro do prazo; exitCode e preenchido.
    virtual bool wait(int timeoutMs, int& exitCode) = 0;
    virtual bool running() = 0;
    // Encerra o processo e todos os descendentes.
    virtual void kill() = 0;
};

struct RunResult {
    int exitCode = -1;
    std::string output;      // stdout + stderr intercalados
    bool timedOut = false;
    bool cancelled = false;
    std::string error;       // falha ao iniciar, etc.
    double seconds = 0.0;
    bool ok() const { return error.empty() && !timedOut && !cancelled && exitCode == 0; }
};

// Executa ate o fim. `onLine` recebe cada linha completa assim que chega.
// timeoutMs <= 0 significa sem limite. `cancel` (opcional) aborta a execucao.
RunResult run(const ProcessSpec& spec,
              int timeoutMs = 0,
              const std::function<void(const std::string&)>& onLine = {},
              const std::atomic<bool>* cancel = nullptr,
              size_t maxOutputBytes = 8u * 1024u * 1024u);

// Caminho do executavel atual.
std::filesystem::path executablePath();

// true se `program` e encontrado no PATH (ou e um caminho executavel valido).
bool commandExists(const std::string& program);

// Variavel de ambiente (vazio se ausente).
std::string getEnv(const std::string& name);

// Define a variavel no processo atual e a persiste para o usuario
// (Registro HKCU\Environment no Windows; ~/.config/arestrix/env no Linux).
// Nao usa shell. Devolve false se o nome/valor forem invalidos.
bool setUserEnv(const std::string& name, const std::string& value);

// Seletor de pasta nativo; vazio se cancelado/indisponivel.
std::string pickFolderDialog(const std::string& title);

// Filtro do seletor de arquivo: nome exibido e padroes separados por ';' (ex: "*.arestrix;*.json").
struct FileFilter {
    std::string name;
    std::string pattern;
};

// Seletor de arquivo nativo (abrir); vazio se cancelado/indisponivel. Caminhos em UTF-8.
std::string pickFileDialog(const std::string& title,
                           const std::vector<FileFilter>& filters,
                           const std::string& initialDir = "");

// Letras de unidade (Windows) ou raizes uteis (POSIX).
std::vector<std::string> listRoots();

// Abre `path` no VS Code (best effort).
bool openInVsCode(const std::filesystem::path& path);

// Abre `path` no gerenciador de arquivos do sistema (Explorer no Windows, xdg-open no Linux).
bool openFolderInExplorer(const std::filesystem::path& path);

} // namespace platform
} // namespace taskforge
