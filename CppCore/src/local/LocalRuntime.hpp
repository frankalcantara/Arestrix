#pragma once

// Runtime local do Arestrix: catalogo de modelos GGUF e manifesto de binarios pinados.
//
// O Arestrix NAO redistribui pesos de modelos. O catalogo contem apenas metadados e URLs
// pinadas na origem; o download e feito pelo usuario, com verificacao SHA-256.
// O runtime de inferencia (llama.cpp) e um componente separado, baixado sob demanda.

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/Process.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace local {

struct ModelRecord {
    std::string id;
    std::string apiModel;   // nome enviado no campo "model" para o servidor/runtime
    std::string name;
    std::string family;
    std::string parameters;
    std::string quantization;
    std::string description;
    std::string fileName;
    std::string downloadUrl;
    std::string sourceUrl;
    std::string license;
    std::string licenseUrl;
    std::string sha256;
    long long sizeBytes = 0;
    int contextLength = 0;
    int minRamGB = 0;
    int minVramGB = 0;
    bool supportsTools = false;
    bool verified = false;
    std::string verifiedAt;
    std::string verifiedWith;
    bool curated = false;
    std::vector<std::string> recommendedBackends;
    std::vector<std::string> languages;

    Json toJson() const;
};

// Progresso de download: bytes recebidos e total estimado (0 = desconhecido).
using DownloadProgress = std::function<void(long long received, long long total)>;

// Baixa (ou retoma) `url` para `target`, que deve ser um arquivo .part. `cancel` aborta a
// operacao. Em caso de sucesso, o arquivo fica completo em `target`; a verificacao SHA-256 e
// responsabilidade do chamador.
bool downloadToFile(const std::string& url, const std::filesystem::path& target,
                    const std::atomic<bool>& cancel, const DownloadProgress& progress,
                    long long expectedBytes, std::string& error);

class LocalRuntime {
public:
    static LocalRuntime& instance();
    ~LocalRuntime();

    // Estado completo para a interface: catalogo, manifesto, modelos instalados e downloads.
    Json state();

    Json catalog();
    Json manifest();

    // Inicia um download em segundo plano. Devolve o id do job ou vazio com `error`.
    std::string startModelDownload(const std::string& modelId, std::string& error);
    bool cancelDownload(const std::string& jobId);
    Json downloadStatus(const std::string& jobId);
    Json downloads();

    Json deleteModel(const std::string& modelId);
    Json verifyModel(const std::string& modelId);

    // Runtime gerenciado (llama.cpp) e servidor em modo router.
    // `backend` vazio = escolha automatica (Vulkan/CUDA/CPU conforme o hardware).
    bool runtimeInstalled(const std::string& backend = "");
    std::string installRuntime(std::string& error, const std::string& backend = "");
    Json runtimeStatus();
    Json startServer(const std::string& backend = "");
    Json stopServer();
    Json serverLogs();
    bool serverRunning();

    std::filesystem::path dataRoot() const;
    std::filesystem::path modelsDir() const;
    std::filesystem::path modelPath(const ModelRecord& model) const;

    const std::vector<ModelRecord>& models();
    const ModelRecord* findModel(const std::string& modelId);

private:
    LocalRuntime();
    LocalRuntime(const LocalRuntime&) = delete;
    LocalRuntime& operator=(const LocalRuntime&) = delete;

    struct Job;

    void ensureLoadedLocked();
    Json installedLocked() const;
    Json downloadsLocked() const;
    Json modelStateLocked(const ModelRecord& model) const;
    std::shared_ptr<Job> activeJobForModelLocked(const std::string& modelId) const;
    std::shared_ptr<Job> latestJobForModelLocked(const std::string& modelId) const;
    std::shared_ptr<Job> latestJobForKindLocked(const std::string& kind, const std::string& targetId) const;

    std::string platformTag() const;
    std::string selectBinaryIdLocked(const std::string& backend = "") const;
    std::string installedBinaryIdLocked(const std::string& backend = "") const;
    std::string backendOfBinaryLocked(const std::string& binaryId) const;
    std::filesystem::path runtimeDirFor(const std::string& version, const std::string& binaryId) const;
    std::filesystem::path findServerBinary(const std::filesystem::path& dir, const std::string& entryPoint) const;
    std::filesystem::path runtimeServerPathLocked(const std::string& version, const std::string& binaryId) const;
    std::string writeRouterPresetLocked(std::vector<std::string>& modelNames);
    Json detectHardware();
    void cleanupOldRuntimeVersions(const std::string& currentVersion);
    void stopServerLocked();
    void appendLog(const std::string& line);

    mutable std::mutex hardwareMu_;
    bool hardwareDetected_ = false;
    Json hardware_;

    mutable std::mutex serverMu_;
    std::shared_ptr<platform::Process> server_;
    std::thread logThread_;
    std::atomic<bool> logStop_{false};
    std::mutex logMu_;
    std::deque<std::string> serverLog_;
    std::string serverBaseUrl_;
    std::string serverApiKey_;
    int serverPort_ = 0;
    bool serverReady_ = false;
    std::atomic<bool> serverCrashed_{false};
    std::string serverBackend_;
    std::string serverBinaryId_;
    std::string serverPath_;
    std::vector<std::string> serverModels_;
    uint64_t serverGeneration_ = 0;

    mutable std::mutex mu_;
    bool loaded_ = false;
    std::string loadError_;
    Json catalogValue_;
    Json manifestValue_;
    std::vector<ModelRecord> models_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    int jobCounter_ = 0;
};

} // namespace local
} // namespace taskforge
