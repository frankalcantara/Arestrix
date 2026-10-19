#include "local/LocalRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "LocalRuntimeData.hpp"
#include "platform/Process.hpp"
#include "project/OpenableFiles.hpp"
#include "project/RunnerSecurity.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace taskforge {
namespace local {

namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isValidSha256(const std::string& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool isSafeModelId(const std::string& id) {
    if (id.empty() || id.size() > 128 || id == "." || id == "..") return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
    });
}

bool isSafeFileName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name == "." || name == "..") return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    return true;
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

ModelRecord parseModel(const Json& value) {
    require(value.is_object(), "Entrada de modelo invalida no catalogo.");
    ModelRecord model;
    model.id = value.value("id", std::string());
    model.apiModel = value.value("apiModel", model.id);
    model.name = value.value("name", std::string());
    model.family = value.value("family", std::string());
    model.parameters = value.value("parameters", std::string());
    model.quantization = value.value("quantization", std::string());
    model.description = value.value("description", std::string());
    model.fileName = value.value("fileName", std::string());
    model.downloadUrl = value.value("downloadUrl", std::string());
    model.sourceUrl = value.value("sourceUrl", std::string());
    model.license = value.value("license", std::string());
    model.licenseUrl = value.value("licenseUrl", std::string());
    model.sha256 = lower(value.value("sha256", std::string()));
    model.sizeBytes = value.value("sizeBytes", 0LL);
    model.contextLength = value.value("contextLength", 0);
    model.minRamGB = value.value("minRamGB", 0);
    model.minVramGB = value.value("minVramGB", 0);
    model.supportsTools = value.value("supportsTools", false);
    model.verified = value.value("verified", false);
    model.curated = value.value("curated", false);

    const Json& backends = value["recommendedBackends"];
    if (backends.is_array()) {
        for (const auto& b : backends) if (b.is_string()) model.recommendedBackends.push_back(b.str());
    }
    const Json& languages = value["languages"];
    if (languages.is_array()) {
        for (const auto& l : languages) if (l.is_string()) model.languages.push_back(l.str());
    }

    require(isSafeModelId(model.id), "Id de modelo invalido no catalogo: " + model.id);
    require(isSafeFileName(model.fileName), "Nome de arquivo invalido para o modelo " + model.id);
    require(model.name.size() > 0, "Modelo sem nome: " + model.id);
    require(model.downloadUrl.rfind("https://", 0) == 0, "Modelo " + model.id + " sem URL HTTPS.");
    require(isValidSha256(model.sha256), "Modelo " + model.id + " sem SHA-256 valido.");
    require(model.sizeBytes > 0, "Modelo " + model.id + " sem tamanho valido.");
    return model;
}

std::vector<ModelRecord> parseCatalog(const Json& root, std::string& error) {
    std::vector<ModelRecord> models;
    try {
        require(root.is_object(), "Catalogo de modelos invalido.");
        require(root.value("schemaVersion", 0) == 1, "Versao de catalogo nao suportada.");
        const Json& list = root["models"];
        require(list.is_array(), "Catalogo sem lista de modelos.");
        std::set<std::string> ids;
        for (const auto& item : list) {
            ModelRecord model = parseModel(item);
            require(ids.insert(model.id).second, "Modelo duplicado no catalogo: " + model.id);
            models.push_back(std::move(model));
        }
    } catch (const std::exception& ex) {
        error = ex.what();
        models.clear();
    }
    return models;
}

void validateManifest(const Json& root) {
    require(root.is_object(), "Manifesto de runtime invalido.");
    require(root.value("schemaVersion", 0) == 1, "Versao de manifesto nao suportada.");
    const Json& components = root["components"];
    require(components.is_object(), "Manifesto sem componentes.");
    const Json& llama = components["llama.cpp"];
    require(llama.is_object(), "Manifesto sem o componente llama.cpp.");
    require(!llama.value("version", std::string()).empty(), "llama.cpp sem versao pinada.");
    const Json& binaries = llama["binaries"];
    require(binaries.is_array() && binaries.size() > 0, "llama.cpp sem binarios no manifesto.");
    for (const auto& b : binaries) {
        require(b.is_object(), "Binario invalido no manifesto.");
        require(!b.value("id", std::string()).empty(), "Binario sem id.");
        require(!b.value("platform", std::string()).empty(), "Binario sem plataforma.");
        require(!b.value("backend", std::string()).empty(), "Binario sem backend.");
        require(!b.value("entryPoint", std::string()).empty(), "Binario sem entryPoint.");
        const std::string url = b.value("url", std::string());
        require(url.rfind("https://", 0) == 0, "Binario sem URL HTTPS.");
        require(isValidSha256(lower(b.value("sha256", std::string()))), "Binario sem SHA-256 valido.");
        require(b.value("sizeBytes", 0LL) > 0, "Binario sem tamanho valido.");
    }
}

Json stringArray(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const auto& value : values) out.push_back(value);
    return out;
}

std::string randomHex(std::size_t bytes) {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const unsigned value = static_cast<unsigned>(rng() & 0xff);
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0x0f]);
    }
    return out;
}

int freeLoopbackPort() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) { WSACleanup(); return 0; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        int len = sizeof(addr);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port = ntohs(addr.sin_port);
        }
    }
    closesocket(fd);
    WSACleanup();
    return port;
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    int port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
            port = ntohs(addr.sin_port);
        }
    }
    close(fd);
    return port;
#endif
}

bool extractArchive(const fs::path& archive, const fs::path& destination, std::string& error) {
    std::error_code ec;
    fs::create_directories(destination, ec);
    if (ec) {
        error = "create_runtime_dir_failed: " + ec.message();
        return false;
    }
    platform::ProcessSpec spec;
    // tar.exe (Windows 10+) e tar (Linux) extraem zip e tar.gz sem shell.
    spec.argv = {"tar", "-xf", archive.string(), "-C", destination.string()};
    const auto result = platform::run(spec, 180000);
    if (!result.ok()) {
        error = "extract_failed: " + result.output;
        return false;
    }
    return true;
}

} // namespace

Json ModelRecord::toJson() const {
    Json j = Json::object();
    j["id"] = id;
    j["apiModel"] = apiModel.empty() ? id : apiModel;
    j["name"] = name;
    j["family"] = family;
    j["parameters"] = parameters;
    j["quantization"] = quantization;
    j["description"] = description;
    j["fileName"] = fileName;
    j["sizeBytes"] = sizeBytes;
    j["sha256"] = sha256;
    j["downloadUrl"] = downloadUrl;
    j["sourceUrl"] = sourceUrl;
    j["license"] = license;
    j["licenseUrl"] = licenseUrl;
    j["contextLength"] = contextLength;
    j["supportsTools"] = supportsTools;
    j["verified"] = verified;
    j["curated"] = curated;
    j["minRamGB"] = minRamGB;
    j["minVramGB"] = minVramGB;
    j["recommendedBackends"] = stringArray(recommendedBackends);
    j["languages"] = stringArray(languages);
    return j;
}

struct LocalRuntime::Job {
    std::string id;
    std::string kind;      // "model" | "runtime"
    std::string targetId;
    long long sequence = 0;
    fs::path path;
    std::atomic<bool> cancel{false};
    mutable std::mutex mu;
    std::string status = "queued";
    long long received = 0;
    long long total = 0;
    std::string error;

    Json jsonLocked() const {
        Json j = Json::object();
        j["jobId"] = id;
        j["kind"] = kind;
        j["targetId"] = targetId;
        j["status"] = status;
        j["received"] = received;
        j["total"] = total;
        if (!error.empty()) j["error"] = error;
        if (!path.empty()) j["path"] = project::pathToUtf8(path);
        return j;
    }

    Json toJson() const {
        std::lock_guard<std::mutex> lock(mu);
        return jsonLocked();
    }
};

LocalRuntime& LocalRuntime::instance() {
    // Intencionalmente alocado sem delete: downloads em segundo plano podem sobreviver ao
    // encerramento da interface e o processo termina logo em seguida.
    static LocalRuntime* runtime = new LocalRuntime();
    return *runtime;
}

LocalRuntime::LocalRuntime() = default;
LocalRuntime::~LocalRuntime() = default;

fs::path LocalRuntime::dataRoot() const {
    const std::string overrideDir = platform::getEnv("ARESTRIX_DATA_DIR");
    if (!overrideDir.empty()) return project::pathFromUtf8(overrideDir);

#ifdef _WIN32
    std::string base = platform::getEnv("LOCALAPPDATA");
    if (base.empty()) base = platform::getEnv("APPDATA");
    if (!base.empty()) return project::pathFromUtf8(base) / "Arestrix";
#else
    std::string xdg = platform::getEnv("XDG_DATA_HOME");
    if (!xdg.empty()) return project::pathFromUtf8(xdg) / "arestrix";
    std::string home = platform::getEnv("HOME");
    if (!home.empty()) return project::pathFromUtf8(home) / ".local" / "share" / "arestrix";
#endif
    return fs::current_path() / ".arestrix-data";
}

fs::path LocalRuntime::modelsDir() const { return dataRoot() / "models"; }

fs::path LocalRuntime::modelPath(const ModelRecord& model) const {
    return modelsDir() / project::pathFromUtf8(model.id) / project::pathFromUtf8(model.fileName);
}

void LocalRuntime::ensureLoadedLocked() {
    if (loaded_) return;
    loaded_ = true;
    try {
        catalogValue_ = jsonutil::parse(kModelCatalogJson);
        models_ = parseCatalog(catalogValue_, loadError_);
        if (!loadError_.empty()) {
            catalogValue_ = Json::object();
            models_.clear();
        }
        manifestValue_ = jsonutil::parse(kLlamaManifestJson);
        validateManifest(manifestValue_);
    } catch (const std::exception& ex) {
        loadError_ = ex.what();
        catalogValue_ = Json::object();
        manifestValue_ = Json::object();
        models_.clear();
    }
}

Json LocalRuntime::catalog() {
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    return catalogValue_;
}

Json LocalRuntime::manifest() {
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    return manifestValue_;
}

std::shared_ptr<LocalRuntime::Job> LocalRuntime::activeJobForModelLocked(const std::string& modelId) const {
    for (const auto& [id, job] : jobs_) {
        (void)id;
        std::lock_guard<std::mutex> lock(job->mu);
        if (job->kind == "model" && job->targetId == modelId &&
            job->status != "done" && job->status != "failed" && job->status != "cancelled") {
            return job;
        }
    }
    return nullptr;
}

std::shared_ptr<LocalRuntime::Job> LocalRuntime::latestJobForModelLocked(const std::string& modelId) const {
    std::shared_ptr<Job> latest;
    long long sequence = -1;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        if (job->kind != "model" || job->targetId != modelId) continue;
        if (job->sequence > sequence) {
            sequence = job->sequence;
            latest = job;
        }
    }
    return latest;
}

Json LocalRuntime::modelStateLocked(const ModelRecord& model) const {
    Json item = model.toJson();
    const fs::path finalPath = modelPath(model);
    fs::path partPath = finalPath;
    partPath += ".part";

    std::error_code ec;
    const bool hasFinal = fs::is_regular_file(finalPath, ec);
    long long bytes = 0;
    if (hasFinal) {
        const auto size = fs::file_size(finalPath, ec);
        if (!ec) bytes = static_cast<long long>(size);
    }
    const bool complete = hasFinal && bytes == model.sizeBytes;
    long long partialBytes = 0;
    if (fs::is_regular_file(partPath, ec)) {
        const auto size = fs::file_size(partPath, ec);
        if (!ec) partialBytes = static_cast<long long>(size);
    }

    item["installed"] = complete;
    item["path"] = project::pathToUtf8(finalPath);
    item["bytes"] = bytes;
    item["partialBytes"] = partialBytes;

    if (auto job = latestJobForModelLocked(model.id)) {
        std::lock_guard<std::mutex> lock(job->mu);
        item["download"] = job->jsonLocked();
    }
    return item;
}

Json LocalRuntime::installedLocked() const {
    Json list = Json::array();
    for (const auto& model : models_) list.push_back(modelStateLocked(model));
    return list;
}

Json LocalRuntime::downloadsLocked() const {
    Json list = Json::array();
    for (const auto& [id, job] : jobs_) {
        (void)id;
        list.push_back(job->toJson());
    }
    return list;
}

Json LocalRuntime::state() {
    Json out = Json::object();
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        out["schemaVersion"] = 1;
        out["dataRoot"] = project::pathToUtf8(dataRoot());
        out["modelsDir"] = project::pathToUtf8(modelsDir());
        out["catalog"] = catalogValue_;
        out["manifest"] = manifestValue_;
        out["models"] = installedLocked();
        out["downloads"] = downloadsLocked();
        std::error_code ec;
        fs::path spacePath = modelsDir();
        if (!fs::is_directory(spacePath, ec)) spacePath = dataRoot();
        if (!fs::is_directory(spacePath, ec)) spacePath = fs::current_path();
        const auto space = fs::space(spacePath, ec);
        if (!ec) {
            out["diskFreeBytes"] = static_cast<long long>(space.available);
            out["diskTotalBytes"] = static_cast<long long>(space.capacity);
        }
        if (!loadError_.empty()) out["error"] = loadError_;
    }
    out["runtime"] = runtimeStatus();
    return out;
}

const std::vector<ModelRecord>& LocalRuntime::models() {
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    return models_;
}

const ModelRecord* LocalRuntime::findModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    for (const auto& model : models_) if (model.id == modelId) return &model;
    return nullptr;
}

std::string LocalRuntime::startModelDownload(const std::string& modelId, std::string& error) {
    error.clear();
    std::shared_ptr<Job> job;
    ModelRecord model;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        if (!loadError_.empty()) { error = loadError_; return std::string(); }

        const ModelRecord* found = nullptr;
        for (const auto& candidate : models_) {
            if (candidate.id == modelId) { found = &candidate; break; }
        }
        if (!found) { error = "model_not_found"; return std::string(); }
        model = *found;

        const fs::path finalPath = modelPath(model);
        std::error_code ec;
        if (fs::is_regular_file(finalPath, ec) &&
            static_cast<long long>(fs::file_size(finalPath, ec)) == model.sizeBytes) {
            error = "already_installed";
            return std::string();
        }
        if (auto existing = activeJobForModelLocked(modelId)) return existing->id;

        job = std::make_shared<Job>();
        job->sequence = ++jobCounter_;
        job->id = "dl-" + std::to_string(job->sequence) + "-" +
                  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count());
        job->kind = "model";
        job->targetId = model.id;
        job->path = finalPath;
        job->total = model.sizeBytes;
        jobs_[job->id] = job;
    }

    std::thread([this, job, model]() {
        auto setStatus = [&](const std::string& status) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->status = status;
        };
        auto fail = [&](const std::string& detail) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->status = "failed";
            job->error = detail;
        };

        setStatus("downloading");
        const fs::path finalPath = job->path;
        fs::path partPath = finalPath;
        partPath += ".part";
        std::error_code ec;
        fs::create_directories(finalPath.parent_path(), ec);
        if (ec) { fail("create_directory_failed: " + ec.message()); return; }

        auto progress = [&](long long received, long long total) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->received = received;
            job->total = total > 0 ? total : model.sizeBytes;
        };

        std::string transferError;
        const bool ok = downloadToFile(model.downloadUrl, partPath, job->cancel, progress,
                                       model.sizeBytes, transferError);
        if (job->cancel.load()) { setStatus("cancelled"); return; }
        if (!ok) { fail(transferError.empty() ? "download_failed" : transferError); return; }

        setStatus("verifying");
        std::string hashError;
        const std::string actual = project::RunnerSecurity::sha256File(partPath, hashError);
        if (actual.empty()) { fail(hashError.empty() ? "hash_failed" : hashError); return; }
        if (lower(actual) != lower(model.sha256)) {
            fs::remove(partPath, ec);
            fail("checksum_mismatch");
            return;
        }

        fs::remove(finalPath, ec);
        fs::rename(partPath, finalPath, ec);
        if (ec) { fail("rename_failed: " + ec.message()); return; }

        std::lock_guard<std::mutex> lock(job->mu);
        job->status = "done";
        job->received = model.sizeBytes;
        job->total = model.sizeBytes;
        job->path = finalPath;
    }).detach();

    return job->id;
}

bool LocalRuntime::cancelDownload(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) return false;
    std::lock_guard<std::mutex> jobLock(it->second->mu);
    if (it->second->status == "done" || it->second->status == "failed" ||
        it->second->status == "cancelled") {
        return false;
    }
    it->second->cancel = true;
    it->second->status = "cancelling";
    return true;
}

Json LocalRuntime::downloadStatus(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = jobs_.find(jobId);
    if (it == jobs_.end()) {
        return Json{{"ok", false}, {"error", "job_not_found"}};
    }
    Json out = it->second->toJson();
    out["ok"] = true;
    return out;
}

Json LocalRuntime::downloads() {
    std::lock_guard<std::mutex> lock(mu_);
    return downloadsLocked();
}

Json LocalRuntime::deleteModel(const std::string& modelId) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        const ModelRecord* model = nullptr;
        for (const auto& candidate : models_) {
            if (candidate.id == modelId) { model = &candidate; break; }
        }
        if (!model) return Json{{"ok", false}, {"error", "model_not_found"}};
        if (activeJobForModelLocked(modelId)) {
            return Json{{"ok", false}, {"error", "download_in_progress"}};
        }
        std::error_code ec;
        fs::remove_all(modelsDir() / project::pathFromUtf8(model->id), ec);
        if (ec) return Json{{"ok", false}, {"error", ec.message()}};
    }

    // O preset do router referencia arquivos; parar o servidor evita um preset obsoleto.
    {
        std::lock_guard<std::mutex> lock(serverMu_);
        if (server_ && server_->running()) stopServerLocked();
    }
    return Json{{"ok", true}};
}

Json LocalRuntime::verifyModel(const std::string& modelId) {
    ModelRecord model;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        const ModelRecord* found = nullptr;
        for (const auto& candidate : models_) {
            if (candidate.id == modelId) { found = &candidate; break; }
        }
        if (!found) return Json{{"ok", false}, {"error", "model_not_found"}};
        model = *found;
    }

    const fs::path finalPath = modelPath(model);
    std::error_code ec;
    if (!fs::is_regular_file(finalPath, ec)) {
        return Json{{"ok", false}, {"error", "not_installed"}};
    }

    std::string hashError;
    const std::string actual = project::RunnerSecurity::sha256File(finalPath, hashError);
    const bool matches = !actual.empty() && lower(actual) == lower(model.sha256);
    Json out = Json::object();
    out["ok"] = matches;
    out["expected"] = model.sha256;
    out["actual"] = actual;
    out["bytes"] = static_cast<long long>(fs::file_size(finalPath, ec));
    out["path"] = project::pathToUtf8(finalPath);
    if (!hashError.empty()) out["error"] = hashError;
    return out;
}

// ---------------------------------------------------------------------------
// Runtime gerenciado: llama.cpp + modo router nativo
// ---------------------------------------------------------------------------

std::string LocalRuntime::platformTag() const {
#ifdef _WIN32
    return sizeof(void*) == 8 ? "windows-x64" : std::string();
#else
    return sizeof(void*) == 8 ? "linux-x64" : std::string();
#endif
}

std::string LocalRuntime::selectBinaryIdLocked(const std::string& backendOverride) const {
    const Json& llama = manifestValue_["components"]["llama.cpp"];
    const Json& binaries = llama["binaries"];
    const std::string platformName = platformTag();
    if (platformName.empty()) return std::string();

    std::string requested = lower(backendOverride);
    if (requested.empty()) requested = lower(platform::getEnv("ARESTRIX_LOCAL_BACKEND"));
    std::vector<std::string> priority;
    if (!requested.empty()) priority.push_back(requested);
    for (const char* backend : {"vulkan", "cuda", "cpu"}) {
        if (requested != backend) priority.push_back(backend);
    }

    for (const auto& backend : priority) {
        for (const auto& binary : binaries) {
            if (!binary.is_object()) continue;
            if (binary.value("platform", std::string()) != platformName) continue;
            if (lower(binary.value("backend", std::string())) != backend) continue;
            return binary.value("id", std::string());
        }
    }
    return std::string();
}

std::string LocalRuntime::backendOfBinaryLocked(const std::string& binaryId) const {
    const Json& binaries = manifestValue_["components"]["llama.cpp"]["binaries"];
    for (const auto& binary : binaries) {
        if (binary.value("id", std::string()) == binaryId) {
            return binary.value("backend", std::string());
        }
    }
    return std::string();
}

std::string LocalRuntime::installedBinaryIdLocked(const std::string& backendOverride) const {
    const std::string version = manifestValue_["components"]["llama.cpp"].value("version", std::string());
    if (version.empty()) return std::string();

    if (!backendOverride.empty()) {
        const std::string id = selectBinaryIdLocked(backendOverride);
        if (id.empty()) return std::string();
        const fs::path path = runtimeServerPathLocked(version, id);
        std::error_code ec;
        return !path.empty() && fs::is_regular_file(path, ec) ? id : std::string();
    }

    std::vector<std::string> candidates;
    const std::string desired = selectBinaryIdLocked();
    if (!desired.empty()) candidates.push_back(desired);
    for (const char* backend : {"cpu", "vulkan", "cuda"}) {
        const std::string id = selectBinaryIdLocked(backend);
        if (!id.empty() && std::find(candidates.begin(), candidates.end(), id) == candidates.end()) {
            candidates.push_back(id);
        }
    }

    for (const auto& id : candidates) {
        const fs::path path = runtimeServerPathLocked(version, id);
        std::error_code ec;
        if (!path.empty() && fs::is_regular_file(path, ec)) return id;
    }
    return std::string();
}

Json LocalRuntime::detectHardware() {
    {
        std::lock_guard<std::mutex> lock(hardwareMu_);
        if (hardwareDetected_) return hardware_;
    }

    long long ramTotal = 0;
    long long ramFree = 0;
    unsigned cpuThreads = std::thread::hardware_concurrency();
    if (cpuThreads == 0) cpuThreads = 4;

#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        ramTotal = static_cast<long long>(memory.ullTotalPhys);
        ramFree = static_cast<long long>(memory.ullAvailPhys);
    }
#else
    {
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        auto parseKiB = [](const std::string& value) -> long long {
            long long number = 0;
            bool found = false;
            for (char c : value) {
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    number = number * 10 + (c - '0');
                    found = true;
                } else if (found) {
                    break;
                }
            }
            return found ? number * 1024LL : 0;
        };
        while (std::getline(meminfo, line)) {
            if (line.rfind("MemTotal:", 0) == 0) ramTotal = parseKiB(line);
            else if (line.rfind("MemAvailable:", 0) == 0) ramFree = parseKiB(line);
        }
    }
#endif

    std::string serverPath;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        const std::string version = manifestValue_["components"]["llama.cpp"].value("version", std::string());
        const std::string binaryId = installedBinaryIdLocked();
        if (!version.empty() && !binaryId.empty()) {
            const fs::path path = runtimeServerPathLocked(version, binaryId);
            if (!path.empty()) serverPath = project::pathToUtf8(path);
        }
    }

    Json devices = Json::array();
    long long discreteVramFree = 0;
    if (!serverPath.empty()) {
        platform::ProcessSpec spec;
        spec.argv = {serverPath, "--list-devices"};
        const auto result = platform::run(spec, 15000);
        if (result.ok()) {
            std::istringstream stream(result.output);
            std::string line;
            auto trim = [](std::string value) {
                const size_t first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string();
                const size_t last = value.find_last_not_of(" \t\r\n");
                return value.substr(first, last - first + 1);
            };
            auto firstNumber = [](const std::string& value) -> long long {
                long long number = 0;
                bool found = false;
                for (char c : value) {
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        number = number * 10 + (c - '0');
                        found = true;
                    } else if (found) {
                        break;
                    }
                }
                return found ? number : 0;
            };
            while (std::getline(stream, line)) {
                const size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                const std::string prefix = trim(line.substr(0, colon));
                std::string backend;
                for (char c : prefix) {
                    const unsigned char uc = static_cast<unsigned char>(c);
                    if (std::isalpha(uc)) backend.push_back(static_cast<char>(std::tolower(uc)));
                    else break;
                }
                if (backend.empty()) continue;

                const std::string rest = trim(line.substr(colon + 1));
                const size_t open = rest.rfind('(');
                const size_t close = rest.find(')', open == std::string::npos ? 0 : open);
                if (open == std::string::npos || close == std::string::npos) continue;
                const std::string name = trim(rest.substr(0, open));
                const std::string memoryInfo = rest.substr(open + 1, close - open - 1);
                const size_t comma = memoryInfo.find(',');
                const long long total = firstNumber(comma == std::string::npos ? memoryInfo : memoryInfo.substr(0, comma)) * 1024LL * 1024LL;
                const long long free = comma == std::string::npos ? 0 : firstNumber(memoryInfo.substr(comma + 1)) * 1024LL * 1024LL;
                if (total <= 0) continue;

                Json device = Json::object();
                device["backend"] = backend;
                device["name"] = name;
                device["vramTotalBytes"] = total;
                device["vramFreeBytes"] = free;
                devices.push_back(device);

                const std::string loweredName = lower(name);
                const bool integrated =
                    loweredName.find("uhd graphics") != std::string::npos ||
                    loweredName.find("iris") != std::string::npos ||
                    loweredName.find("radeon graphics") != std::string::npos ||
                    loweredName.find("vega") != std::string::npos ||
                    loweredName.find("llvmpipe") != std::string::npos ||
                    loweredName.find("swiftshader") != std::string::npos ||
                    loweredName.find("microsoft basic") != std::string::npos;
                if (!integrated && free > discreteVramFree) discreteVramFree = free;
            }
        }
    }

    long long modelsMax = 1;
    const std::string envMax = platform::getEnv("ARESTRIX_LOCAL_MODELS_MAX");
    if (!envMax.empty()) {
        try {
            const int value = std::stoi(envMax);
            if (value >= 1 && value <= 4) modelsMax = value;
        } catch (...) {}
    } else if (discreteVramFree >= 10LL * 1024 * 1024 * 1024) {
        modelsMax = 2;
    }

    std::string recommended;
    if (discreteVramFree >= 12LL * 1024 * 1024 * 1024) {
        recommended = "qwen2.5-14b-instruct-q4_k_m";
    } else if (discreteVramFree >= 6LL * 1024 * 1024 * 1024 ||
               ramTotal >= 12LL * 1024 * 1024 * 1024) {
        recommended = "qwen2.5-7b-instruct-q4_k_m";
    } else if (ramTotal >= 6LL * 1024 * 1024 * 1024) {
        recommended = "qwen2.5-1.5b-instruct-q4_k_m";
    } else {
        recommended = "qwen2.5-0.5b-instruct-q4_k_m";
    }

    Json out = Json::object();
    out["ramTotalBytes"] = ramTotal;
    out["ramFreeBytes"] = ramFree;
    out["cpuThreads"] = static_cast<long long>(cpuThreads);
    out["devices"] = devices;
    out["discreteVramFreeBytes"] = discreteVramFree;
    out["modelsMax"] = modelsMax;
    out["recommendedModelId"] = recommended;

    {
        std::lock_guard<std::mutex> lock(hardwareMu_);
        hardware_ = out;
        hardwareDetected_ = true;
    }
    return out;
}

fs::path LocalRuntime::runtimeDirFor(const std::string& version, const std::string& binaryId) const {
    return dataRoot() / "runtime" / "llama.cpp" / project::pathFromUtf8(version) /
           project::pathFromUtf8(binaryId);
}

fs::path LocalRuntime::findServerBinary(const fs::path& dir, const std::string& entryPoint) const {
    const fs::path direct = dir / project::pathFromUtf8(entryPoint);
    std::error_code ec;
    if (fs::is_regular_file(direct, ec)) return direct;
    if (!fs::is_directory(dir, ec)) return fs::path();

    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it.depth() > 4) { it.disable_recursion_pending(); continue; }
        if (it->is_regular_file(ec) && it->path().filename() == project::pathFromUtf8(entryPoint)) {
            return it->path();
        }
    }
    return fs::path();
}

fs::path LocalRuntime::runtimeServerPathLocked(const std::string& version,
                                               const std::string& binaryId) const {
    const Json& binaries = manifestValue_["components"]["llama.cpp"]["binaries"];
    std::string entryPoint;
    for (const auto& binary : binaries) {
        if (binary.value("id", std::string()) == binaryId) {
            entryPoint = binary.value("entryPoint", std::string());
            break;
        }
    }
    if (entryPoint.empty()) return fs::path();
    return findServerBinary(runtimeDirFor(version, binaryId), entryPoint);
}

std::shared_ptr<LocalRuntime::Job> LocalRuntime::latestJobForKindLocked(
    const std::string& kind, const std::string& targetId) const {
    std::shared_ptr<Job> latest;
    long long sequence = -1;
    for (const auto& [id, job] : jobs_) {
        (void)id;
        if (job->kind != kind) continue;
        if (!targetId.empty() && job->targetId != targetId) continue;
        if (job->sequence > sequence) {
            sequence = job->sequence;
            latest = job;
        }
    }
    return latest;
}

bool LocalRuntime::runtimeInstalled(const std::string& backend) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    return !installedBinaryIdLocked(backend).empty();
}

std::string LocalRuntime::installRuntime(std::string& error, const std::string& backend) {
    error.clear();
    std::shared_ptr<Job> job;
    std::string version;
    std::string binaryId;
    std::string url;
    std::string sha256;
    std::string archive;
    std::string entryPoint;
    long long size = 0;
    fs::path runtimeDir;

    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        if (!loadError_.empty()) { error = loadError_; return std::string(); }

        const Json& llama = manifestValue_["components"]["llama.cpp"];
        version = llama.value("version", std::string());
        binaryId = selectBinaryIdLocked(backend);
        if (version.empty() || binaryId.empty()) {
            error = backend.empty() ? "unsupported_platform" : "backend_not_found";
            return std::string();
        }
        if (!runtimeServerPathLocked(version, binaryId).empty()) {
            error = "already_installed";
            return std::string();
        }

        const Json& binaries = llama["binaries"];
        const Json* chosen = nullptr;
        for (const auto& binary : binaries) {
            if (binary.value("id", std::string()) == binaryId) { chosen = &binary; break; }
        }
        if (!chosen) { error = "binary_not_found"; return std::string(); }

        url = chosen->value("url", std::string());
        sha256 = lower(chosen->value("sha256", std::string()));
        archive = chosen->value("archive", std::string());
        entryPoint = chosen->value("entryPoint", std::string());
        size = chosen->value("sizeBytes", 0LL);
        runtimeDir = runtimeDirFor(version, binaryId);

        if (auto existing = latestJobForKindLocked("runtime", binaryId)) {
            std::lock_guard<std::mutex> jobLock(existing->mu);
            if (existing->status != "done" && existing->status != "failed" &&
                existing->status != "cancelled") {
                return existing->id;
            }
        }

        job = std::make_shared<Job>();
        job->sequence = ++jobCounter_;
        job->id = "rt-" + std::to_string(job->sequence) + "-" +
                  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count());
        job->kind = "runtime";
        job->targetId = binaryId;
        job->path = runtimeDir;
        job->total = size;
        jobs_[job->id] = job;
    }

    std::thread([this, job, version, binaryId, url, sha256, archive, entryPoint, size, runtimeDir]() {
        auto setStatus = [&](const std::string& status) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->status = status;
        };
        auto fail = [&](const std::string& detail) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->status = "failed";
            job->error = detail;
        };

        setStatus("downloading");
        std::error_code ec;
        fs::create_directories(runtimeDir.parent_path(), ec);
        if (ec) { fail("create_runtime_dir_failed: " + ec.message()); return; }

        const std::string extension = archive == "zip" ? ".zip" : ".tar.gz";
        const fs::path archivePath = runtimeDir.parent_path() / (binaryId + "-" + version + extension);
        auto progress = [&](long long received, long long total) {
            std::lock_guard<std::mutex> lock(job->mu);
            job->received = received;
            job->total = total > 0 ? total : size;
        };

        std::string transferError;
        if (!downloadToFile(url, archivePath, job->cancel, progress, size, transferError)) {
            if (job->cancel.load()) { setStatus("cancelled"); return; }
            fail(transferError.empty() ? "download_failed" : transferError);
            return;
        }

        setStatus("verifying");
        std::string hashError;
        const std::string actual = project::RunnerSecurity::sha256File(archivePath, hashError);
        if (actual.empty()) { fail(hashError.empty() ? "hash_failed" : hashError); return; }
        if (lower(actual) != sha256) {
            fs::remove(archivePath, ec);
            fail("checksum_mismatch");
            return;
        }

        setStatus("extracting");
        fs::remove_all(runtimeDir, ec);
        if (!extractArchive(archivePath, runtimeDir, transferError)) {
            fail(transferError);
            return;
        }
        fs::remove(archivePath, ec);

        const fs::path serverPath = findServerBinary(runtimeDir, entryPoint);
        if (serverPath.empty()) { fail("server_binary_not_found"); return; }

        {
            std::lock_guard<std::mutex> hardwareLock(hardwareMu_);
            hardwareDetected_ = false;
            hardware_ = Json::object();
        }

        std::lock_guard<std::mutex> lock(job->mu);
        job->status = "done";
        job->received = size;
        job->total = size;
        job->path = serverPath;
    }).detach();

    return job->id;
}

std::string LocalRuntime::writeRouterPresetLocked(std::vector<std::string>& modelNames) {
    modelNames.clear();
    const fs::path presetPath = dataRoot() / "runtime" / "router.ini";
    std::error_code ec;
    fs::create_directories(presetPath.parent_path(), ec);
    std::ofstream out(presetPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return std::string();

    out << "version = 1\n\n[*]\n";
    out << "c = 8192\n";
    out << "jinja = true\n\n";

    for (const auto& model : models_) {
        const fs::path path = modelPath(model);
        if (!fs::is_regular_file(path, ec)) continue;
        const auto size = fs::file_size(path, ec);
        if (ec || static_cast<long long>(size) != model.sizeBytes) continue;

        const std::string name = model.apiModel.empty() ? model.id : model.apiModel;
        modelNames.push_back(name);
        out << "[" << name << "]\n";
        out << "model = " << project::pathToUtf8(path) << "\n";
        out << "load-on-startup = false\n\n";
    }

    out.flush();
    if (!out.good()) return std::string();
    return project::pathToUtf8(presetPath);
}

void LocalRuntime::appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMu_);
    serverLog_.push_back(line);
    while (serverLog_.size() > 300) serverLog_.pop_front();
}

void LocalRuntime::stopServerLocked() {
    logStop_ = true;
    ++serverGeneration_;
    if (server_) {
        server_->kill();
        int exitCode = 0;
        server_->wait(3000, exitCode);
    }
    if (logThread_.joinable()) logThread_.join();
    server_.reset();
    serverReady_ = false;
    serverCrashed_ = false;
    serverBackend_.clear();
    serverBinaryId_.clear();
    serverPath_.clear();
    serverModels_.clear();
    platform::setProcessEnv("LOCAL_LLM_BASE_URL", "");
    platform::setProcessEnv("LOCAL_LLM_API_KEY", "");
    serverBaseUrl_.clear();
    serverApiKey_.clear();
    serverPort_ = 0;
}

Json LocalRuntime::runtimeStatus() {
    Json out = Json::object();
    std::string version;
    std::string binaryId;
    std::string backend;
    std::string installedPath;
    Json download;
    Json installedBackends = Json::array();
    std::vector<std::string> modelNames;
    long long modelsMax = 1;
    std::string recommendedModelId;
    Json hardware;

    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        const Json& llama = manifestValue_["components"]["llama.cpp"];
        version = llama.value("version", std::string());
        const std::string installedBinary = installedBinaryIdLocked();
        const std::string desiredBinary = selectBinaryIdLocked();
        binaryId = installedBinary.empty() ? desiredBinary : installedBinary;
        backend = backendOfBinaryLocked(binaryId);

        const Json& binaries = llama["binaries"];
        for (const auto& binary : binaries) {
            const std::string id = binary.value("id", std::string());
            if (id.empty()) continue;
            const fs::path path = runtimeServerPathLocked(version, id);
            std::error_code ec;
            if (!path.empty() && fs::is_regular_file(path, ec)) {
                installedBackends.push_back(binary.value("backend", std::string()));
            }
        }

        if (!installedBinary.empty()) {
            const fs::path path = runtimeServerPathLocked(version, installedBinary);
            if (!path.empty()) installedPath = project::pathToUtf8(path);
        }
        if (auto job = latestJobForKindLocked("runtime", "")) download = job->toJson();

        for (const auto& model : models_) {
            const fs::path modelFile = modelPath(model);
            std::error_code ec;
            if (fs::is_regular_file(modelFile, ec) &&
                static_cast<long long>(fs::file_size(modelFile, ec)) == model.sizeBytes) {
                modelNames.push_back(model.apiModel.empty() ? model.id : model.apiModel);
            }
        }
    }

    hardware = detectHardware();
    modelsMax = hardware.value("modelsMax", 1LL);
    recommendedModelId = hardware.value("recommendedModelId", std::string());

    out["installed"] = !installedPath.empty();
    out["version"] = version;
    out["binaryId"] = binaryId;
    out["backend"] = backend;
    out["path"] = installedPath;
    out["installedBackends"] = installedBackends;
    out["models"] = stringArray(modelNames);
    if (!download.is_null()) out["download"] = download;
    if (!hardware.is_null()) out["hardware"] = hardware;
    out["modelsMax"] = modelsMax;
    out["recommendedModelId"] = recommendedModelId;

    {
        std::lock_guard<std::mutex> lock(serverMu_);
        const bool running = server_ && server_->running();
        out["running"] = running;
        out["ready"] = running && serverReady_;
        out["baseUrl"] = running ? serverBaseUrl_ : std::string();
        out["port"] = running ? serverPort_ : 0;
        out["crashed"] = !running && serverCrashed_.load();
        out["presetModels"] = running ? stringArray(serverModels_) : Json::array();
        if (running) {
            out["backend"] = serverBackend_;
            out["binaryId"] = serverBinaryId_;
            out["path"] = serverPath_;
        }
    }
    out["log"] = serverLogs();
    return out;
}

Json LocalRuntime::startServer(const std::string& backend) {
    std::string serverPath;
    std::string presetPath;
    std::string backendName;
    std::string binaryId;
    std::vector<std::string> modelNames;

    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureLoadedLocked();
        const Json& llama = manifestValue_["components"]["llama.cpp"];
        const std::string version = llama.value("version", std::string());
        binaryId = installedBinaryIdLocked(backend);
        if (version.empty()) {
            return Json{{"ok", false}, {"error", "unsupported_platform"}};
        }
        if (binaryId.empty()) {
            return Json{{"ok", false}, {"error", "runtime_not_installed"}};
        }
        const fs::path runtimePath = runtimeServerPathLocked(version, binaryId);
        if (runtimePath.empty()) {
            return Json{{"ok", false}, {"error", "runtime_not_installed"}};
        }
        backendName = backendOfBinaryLocked(binaryId);
        serverPath = project::pathToUtf8(runtimePath);
        presetPath = writeRouterPresetLocked(modelNames);
        if (presetPath.empty()) return Json{{"ok", false}, {"error", "preset_failed"}};
        if (modelNames.empty()) return Json{{"ok", false}, {"error", "no_models"}};
    }

    const Json hardware = detectHardware();
    const long long modelsMax = hardware.value("modelsMax", 1LL);

    std::shared_ptr<platform::Process> process;
    std::string baseUrl;
    std::string apiKey;
    uint64_t generation = 0;
    int port = 0;

    {
        std::lock_guard<std::mutex> lock(serverMu_);
        if (server_ && server_->running() && modelNames == serverModels_) {
            return Json{{"ok", true}, {"running", true}, {"ready", serverReady_},
                        {"baseUrl", serverBaseUrl_}, {"port", serverPort_},
                        {"backend", backendName}, {"models", stringArray(modelNames)}};
        }
        // Se o conjunto de modelos mudou, reinicia com o preset atualizado.
        stopServerLocked();
        generation = ++serverGeneration_;
        port = freeLoopbackPort();
        if (port <= 0) return Json{{"ok", false}, {"error", "no_free_port"}};
        apiKey = randomHex(32);
        baseUrl = "http://127.0.0.1:" + std::to_string(port) + "/v1";

        platform::ProcessSpec spec;
        spec.argv = {serverPath,
                     "--host", "127.0.0.1",
                     "--port", std::to_string(port),
                     "--api-key", apiKey,
                     "--models-preset", presetPath,
                     "--models-max", std::to_string(modelsMax),
                     "--jinja",
                     "--no-webui"};
        spec.workingDir = fs::path(serverPath).parent_path();
#ifndef _WIN32
        // O pacote Linux traz bibliotecas compartilhadas ao lado do executavel.
        const std::string runtimeLibDir = project::pathToUtf8(spec.workingDir);
        const std::string existingLibPath = platform::getEnv("LD_LIBRARY_PATH");
        spec.env["LD_LIBRARY_PATH"] =
            runtimeLibDir + (existingLibPath.empty() ? std::string() : ":" + existingLibPath);
#endif

        std::string startError;
        auto started = platform::Process::start(spec, startError);
        if (!started) {
            Json failure = Json{{"ok", false}, {"error", "server_start_failed"}, {"detail", startError}};
            if (backendName != "cpu") failure["suggestedBackend"] = "cpu";
            return failure;
        }
        server_ = std::shared_ptr<platform::Process>(std::move(started));
        serverBaseUrl_ = baseUrl;
        serverApiKey_ = apiKey;
        serverPort_ = port;
        serverReady_ = false;
        serverCrashed_ = false;
        serverBackend_ = backendName;
        serverBinaryId_ = binaryId;
        serverPath_ = serverPath;
        serverModels_ = modelNames;
        logStop_ = false;
        {
            std::lock_guard<std::mutex> logLock(logMu_);
            serverLog_.clear();
        }

        process = server_;
        logThread_ = std::thread([this, process]() {
            std::string pending;
            while (!logStop_.load()) {
                std::string chunk;
                const auto status = process->read(chunk, 200);
                if (!chunk.empty()) {
                    pending += chunk;
                    size_t position;
                    while ((position = pending.find('\n')) != std::string::npos) {
                        std::string line = pending.substr(0, position);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        appendLog(line);
                        pending.erase(0, position + 1);
                    }
                    if (pending.size() > 8192) {
                        appendLog(pending);
                        pending.clear();
                    }
                }
                if (status == platform::ReadStatus::Eof) break;
            }
            if (!pending.empty()) appendLog(pending);
            if (!logStop_.load()) serverCrashed_ = true;
        });
    }

    bool ready = false;
    rt::HttpRequest health;
    health.method = "GET";
    health.url = baseUrl + "/models";
    health.timeoutSeconds = 5;
    health.headers["Authorization"] = "Bearer " + apiKey;
    for (int attempt = 0; attempt < 80; ++attempt) {
        if (!process->running()) break;
        const rt::HttpResponse response = rt::httpSend(health);
        if (response.status == 200) { ready = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    {
        std::lock_guard<std::mutex> lock(serverMu_);
        if (generation != serverGeneration_ || server_ != process) {
            return Json{{"ok", false}, {"error", "superseded"}};
        }
        if (!ready) {
            const Json log = serverLogs();
            stopServerLocked();
            Json failure = Json{{"ok", false}, {"error", "server_failed"}, {"log", log}};
            if (backendName != "cpu") failure["suggestedBackend"] = "cpu";
            return failure;
        }
        serverReady_ = true;
        platform::setProcessEnv("LOCAL_LLM_BASE_URL", baseUrl);
        platform::setProcessEnv("LOCAL_LLM_API_KEY", apiKey);
    }

    return Json{{"ok", true}, {"running", true}, {"ready", true}, {"baseUrl", baseUrl},
                {"port", port}, {"backend", backendName}, {"models", stringArray(modelNames)}};
}

Json LocalRuntime::stopServer() {
    std::lock_guard<std::mutex> lock(serverMu_);
    stopServerLocked();
    return Json{{"ok", true}, {"running", false}};
}

Json LocalRuntime::serverLogs() {
    std::lock_guard<std::mutex> lock(logMu_);
    Json lines = Json::array();
    const size_t start = serverLog_.size() > 100 ? serverLog_.size() - 100 : 0;
    for (size_t i = start; i < serverLog_.size(); ++i) lines.push_back(serverLog_[i]);
    return lines;
}

bool LocalRuntime::serverRunning() {
    std::lock_guard<std::mutex> lock(serverMu_);
    return server_ && server_->running();
}

} // namespace local
} // namespace taskforge
