#include "rt/compress/ProseModelInstaller.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "local/LocalRuntime.hpp"
#include "project/RunnerSecurity.hpp"
#include "rt/compress/ProseModel.hpp"

namespace taskforge {
namespace rt {
namespace compress {
namespace fs = std::filesystem;
namespace {

// Revisao fixa do repositorio: o download e reproduzivel e os hashes nao mudam.
constexpr const char* kRevision = "e00cc5e60ff73b01674ef8fa6da6c5acf6d54181";
constexpr const char* kBaseUrl = "https://huggingface.co/chopratejas/kompress-v2-base/resolve/";

struct FileSpec {
    const char* name;      // nome local
    const char* path;      // caminho no repositorio HF
    long long size;
    const char* sha256;
    const char* variant;   // "" = comum a todas as variantes
};

const FileSpec kFiles[] = {
    {"tokenizer.json", "tokenizer.json", 3583228,
     "6c8aaa9a542084f2457eab775d4eeb51f92a70c0fd9de28d5edb0ddec3c08d30", ""},
    {"config.json", "config.json", 432,
     "1449bb02c1375480bc33b8ab0d6bded9c685953d8c6e4213dd2e9ac50116d8b9", ""},
    {"kompress-fp32.onnx", "onnx/kompress-fp32.onnx", 600561247,
     "0f6758d903b5b5b90f784b7f17b599277f040a3c268afb88c193fe5bb9c08649", "fp32"},
    {"kompress-int8-wo.onnx", "onnx/kompress-int8-wo.onnx", 274049435,
     "c47ddd764dec5cf91fd1f10cf69e5538dd2a49d913828da7f6a993931cc7904f", "int8"},
};

std::string normalizeVariant(const std::string& variant) {
    // "gpu" usa o fp32: o DirectML nao tem artefato proprio.
    if (variant == "gpu" || variant == "fp32") return "fp32";
    if (variant == "int8") return "int8";
    return std::string();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const FileSpec* modelSpecFor(const std::string& variant) {
    for (const auto& spec : kFiles) {
        if (spec.variant[0] && variant == spec.variant) return &spec;
    }
    return nullptr;
}

std::vector<const FileSpec*> specsFor(const std::string& variant) {
    std::vector<const FileSpec*> specs;
    for (const auto& spec : kFiles) {
        if (!spec.variant[0] || variant == spec.variant) specs.push_back(&spec);
    }
    return specs;
}

bool fileMatches(const fs::path& path, long long expected) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    return static_cast<long long>(fs::file_size(path, ec)) == expected;
}

struct Job {
    unsigned long long sequence = 0;
    std::string id;
    std::string variant;
    std::string status = "queued";
    std::string error;
    std::string file;
    std::string dir;
    int fileIndex = 0;
    int fileCount = 0;
    long long fileReceived = 0;
    long long fileTotal = 0;
    long long received = 0;
    long long total = 0;
    std::atomic<bool> cancel{false};
};

std::mutex gMu;
std::map<std::string, std::shared_ptr<Job>> gJobs;
unsigned long long gCounter = 0;

Json jobJson(const Job& job) {
    return Json{{"ok", true},
                {"id", job.id},
                {"variant", job.variant},
                {"status", job.status},
                {"error", job.error},
                {"file", job.file},
                {"fileIndex", job.fileIndex},
                {"fileCount", job.fileCount},
                {"fileReceived", job.fileReceived},
                {"fileTotal", job.fileTotal},
                {"received", job.received},
                {"total", job.total},
                {"dir", job.dir}};
}

} // namespace

Json proseModelManifest() {
    Json files = Json::array();
    for (const auto& spec : kFiles) {
        files.push_back(Json{{"name", spec.name},
                             {"url", std::string(kBaseUrl) + kRevision + "/" + spec.path},
                             {"size", spec.size},
                             {"sha256", spec.sha256},
                             {"variant", spec.variant}});
    }
    return Json{{"id", "kompress-v2-base"},
                {"revision", kRevision},
                {"dir", defaultProseModelDir()},
                {"files", std::move(files)}};
}

bool proseModelVariantInstalled(const std::string& variant) {
    const std::string clean = normalizeVariant(variant);
    if (clean.empty()) return false;
    const FileSpec* model = modelSpecFor(clean);
    if (!model) return false;
    const fs::path dir(defaultProseModelDir());
    for (const auto& spec : kFiles) {
        if (spec.variant[0] && std::string(spec.variant) != clean) continue;
        if (!fileMatches(dir / spec.name, spec.size)) return false;
    }
    return true;
}

std::string startProseModelDownload(const std::string& variant, std::string& error) {
    error.clear();
    const std::string clean = normalizeVariant(variant);
    if (clean.empty()) {
        error = "invalid_variant";
        return std::string();
    }
    if (proseModelVariantInstalled(clean)) {
        error = "already_installed";
        return std::string();
    }

    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(gMu);
        for (const auto& entry : gJobs) {
            const auto& existing = entry.second;
            if (existing->status != "downloading" && existing->status != "cancelling" &&
                existing->status != "verifying" && existing->status != "queued") {
                continue;
            }
            if (existing->variant == clean) return existing->id;
            // tokenizer/config sao comuns: dois downloads simultaneos disputariam os .part.
            error = "download_in_progress";
            return std::string();
        }
        job = std::make_shared<Job>();
        job->sequence = ++gCounter;
        job->id = "pd-" + std::to_string(job->sequence) + "-" +
                  std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch()).count());
        job->variant = clean;
        job->dir = defaultProseModelDir();
        const auto specs = specsFor(clean);
        job->fileCount = static_cast<int>(specs.size());
        for (const FileSpec* spec : specs) job->total += spec->size;
        gJobs[job->id] = job;
    }

    std::thread([job, clean]() {
        auto update = [&](const std::function<void(Job&)>& apply) {
            std::lock_guard<std::mutex> lock(gMu);
            apply(*job);
        };
        auto fail = [&](const std::string& detail) {
            update([&](Job& current) {
                current.status = "failed";
                current.error = detail;
            });
        };

        const auto specs = specsFor(clean);
        std::error_code ec;
        fs::create_directories(job->dir, ec);
        if (ec) {
            fail("create_directory_failed: " + ec.message());
            return;
        }
        update([](Job& current) { current.status = "downloading"; });

        long long base = 0;
        int index = 0;
        for (const FileSpec* spec : specs) {
            const fs::path finalPath = fs::path(job->dir) / spec->name;
            fs::path partPath = finalPath;
            partPath += ".part";
            update([&](Job& current) {
                current.file = spec->name;
                current.fileIndex = index;
                current.fileTotal = spec->size;
                current.fileReceived = 0;
            });

            if (fileMatches(finalPath, spec->size)) {
                base += spec->size;
                update([&](Job& current) { current.received = base; });
                ++index;
                continue;
            }
            if (job->cancel.load()) {
                update([](Job& current) { current.status = "cancelled"; });
                return;
            }

            const std::string url = std::string(kBaseUrl) + kRevision + "/" + spec->path;
            auto progress = [&](long long received, long long total) {
                (void)total;
                update([&](Job& current) {
                    current.fileReceived = received;
                    current.received = base + received;
                });
            };
            std::string transferError;
            const bool ok = local::downloadToFile(url, partPath, job->cancel, progress,
                                                  spec->size, transferError);
            if (job->cancel.load()) {
                update([](Job& current) { current.status = "cancelled"; });
                return;
            }
            if (!ok) {
                fail(transferError.empty() ? "download_failed" : transferError);
                return;
            }

            update([&](Job& current) {
                current.status = "verifying";
                current.fileReceived = spec->size;
                current.received = base + spec->size;
            });
            std::string hashError;
            const std::string actual = project::RunnerSecurity::sha256File(partPath, hashError);
            if (actual.empty()) {
                fail(hashError.empty() ? "hash_failed" : hashError);
                return;
            }
            if (lower(actual) != lower(spec->sha256)) {
                fs::remove(partPath, ec);
                fail("checksum_mismatch");
                return;
            }

            fs::remove(finalPath, ec);
            fs::rename(partPath, finalPath, ec);
            if (ec) {
                fail("rename_failed: " + ec.message());
                return;
            }
            base += spec->size;
            update([&](Job& current) {
                current.status = "downloading";
                current.received = base;
            });
            ++index;
        }

        update([](Job& current) {
            current.status = "done";
            current.received = current.total;
            current.file.clear();
        });
    }).detach();

    return job->id;
}

bool cancelProseModelDownload(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(gMu);
    const auto it = gJobs.find(jobId);
    if (it == gJobs.end()) return false;
    Job& job = *it->second;
    if (job.status == "done" || job.status == "failed" || job.status == "cancelled") return false;
    job.cancel = true;
    job.status = "cancelling";
    return true;
}

Json proseModelDownloadStatus(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(gMu);
    const auto it = gJobs.find(jobId);
    if (it == gJobs.end()) return Json{{"ok", false}, {"error", "job_not_found"}};
    return jobJson(*it->second);
}

Json proseModelDownloads() {
    std::lock_guard<std::mutex> lock(gMu);
    std::vector<const Job*> jobs;
    jobs.reserve(gJobs.size());
    for (const auto& entry : gJobs) jobs.push_back(entry.second.get());
    std::sort(jobs.begin(), jobs.end(),
              [](const Job* a, const Job* b) { return a->sequence > b->sequence; });
    Json out = Json::array();
    for (const Job* job : jobs) out.push_back(jobJson(*job));
    return out;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
