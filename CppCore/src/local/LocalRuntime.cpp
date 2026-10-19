#include "local/LocalRuntime.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <set>
#include <stdexcept>
#include <thread>

#include "LocalRuntimeData.hpp"
#include "platform/Process.hpp"
#include "project/OpenableFiles.hpp"
#include "project/RunnerSecurity.hpp"
#include "util/Json.hpp"

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
    std::lock_guard<std::mutex> lock(mu_);
    ensureLoadedLocked();
    Json out = Json::object();
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

} // namespace local
} // namespace taskforge
