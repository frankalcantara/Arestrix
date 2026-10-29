#include "rt/compress/ProseRecommendation.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_2.h>
#endif

namespace taskforge {
namespace rt {
namespace compress {
namespace {

constexpr long long kMB = 1024LL * 1024LL;

#ifdef _WIN32
std::string toUtf8(const wchar_t* value) {
    if (!value) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), length, nullptr, nullptr);
    out.resize(static_cast<size_t>(length - 1));
    return out;
}
#endif

void readMemory(long long& totalBytes, long long& freeBytes) {
#ifdef _WIN32
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        totalBytes = static_cast<long long>(memory.ullTotalPhys);
        freeBytes = static_cast<long long>(memory.ullAvailPhys);
    }
#else
    std::ifstream meminfo("/proc/meminfo");
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
    std::string line;
    while (std::getline(meminfo, line)) {
        if (line.rfind("MemTotal:", 0) == 0) totalBytes = parseKiB(line);
        else if (line.rfind("MemAvailable:", 0) == 0) freeBytes = parseKiB(line);
    }
#endif
}

long long freeDiskMB(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path probe = path;
    // fs::space exige um caminho existente: sobe ate o primeiro pai que exista.
    while (!probe.empty() && !std::filesystem::exists(probe, ec)) {
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    if (probe.empty() || !std::filesystem::exists(probe, ec)) return -1;
    const auto space = std::filesystem::space(probe, ec);
    if (ec) return -1;
    return static_cast<long long>(space.available / static_cast<std::uintmax_t>(kMB));
}

bool directMlAvailable() {
#if defined(_WIN32) && defined(ARESTRIX_WITH_ONNX_DML)
    HMODULE module = LoadLibraryW(L"DirectML.dll");
    if (!module) return false;
    FreeLibrary(module);
    return true;
#else
    return false;
#endif
}

} // namespace

long long proseModelSizeMB(const std::string& variant) {
    // "gpu" tambem usa o fp32; o DirectML nao tem um artefato proprio.
    if (variant == "fp32" || variant == "gpu") return 601;
    if (variant == "int8") return 274;
    return 0;
}

std::vector<GpuInfo> detectGpus() {
    std::vector<GpuInfo> gpus;
#ifdef _WIN32
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) ||
        !factory) {
        return gpus;
    }
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        if (!adapter) break;
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            GpuInfo gpu;
            gpu.index = static_cast<int>(i);
            gpu.name = toUtf8(desc.Description);
            gpu.vramMB = static_cast<long long>(desc.DedicatedVideoMemory / static_cast<UINT64>(kMB));
            switch (desc.VendorId) {
                case 0x10DE: gpu.vendor = "nvidia"; break;
                case 0x1002:
                case 0x1022: gpu.vendor = "amd"; break;
                case 0x8086: gpu.vendor = "intel"; break;
                case 0x1414: gpu.vendor = "microsoft"; break;
                default: gpu.vendor = "other"; break;
            }
            gpus.push_back(std::move(gpu));
        }
        adapter->Release();
    }
    factory->Release();
#endif
    return gpus;
}

int bestDmlDevice(const std::vector<GpuInfo>& gpus) {
    int best = -1;
    long long bestVram = 0;
    for (const auto& gpu : gpus) {
        if (gpu.index < 0 || gpu.vramMB < 2048 || gpu.vendor == "microsoft") continue;
        if (gpu.vramMB > bestVram) {
            bestVram = gpu.vramMB;
            best = gpu.index;
        }
    }
    return best;
}

MachineProfile detectMachineProfile(const std::filesystem::path& diskPath) {
    MachineProfile profile;
    profile.cpuThreads = std::max(1u, std::thread::hardware_concurrency());
    long long totalBytes = 0;
    long long freeBytes = 0;
    readMemory(totalBytes, freeBytes);
    profile.ramTotalMB = totalBytes / kMB;
    profile.ramFreeMB = freeBytes / kMB;
    profile.diskFreeMB = freeDiskMB(diskPath);
    profile.gpus = detectGpus();
    profile.dmlDevice = bestDmlDevice(profile.gpus);
    profile.dmlAvailable = profile.dmlDevice >= 0 && directMlAvailable();
    return profile;
}

ProseRecommendation recommendProseVariant(const MachineProfile& profile) {
    ProseRecommendation recommendation;
    if (profile.cpuThreads < 2) {
        recommendation = {"none", "cpu"};
        return recommendation;
    }

    // GPU dedicada + DirectML: ~4x mais rapido nas inferencias de aquecimento (GTX 1650).
    // A primeira inferencia compila os shaders (~2 s), entao o ganho aparece ao longo da execucao.
    if (profile.dmlAvailable) {
        for (const auto& gpu : profile.gpus) {
            if (gpu.index == profile.dmlDevice && gpu.vramMB >= 2048 &&
                profile.ramTotalMB >= 4 * 1024 &&
                (profile.diskFreeMB < 0 || profile.diskFreeMB >= 1200)) {
                recommendation = {"gpu", "gpu"};
                return recommendation;
            }
        }
    }

    // fp32: mais rapido no CPU, mas ocupa ~601 MB e carrega o modelo inteiro na RAM.
    const bool fp32 = profile.ramTotalMB >= 8 * 1024 && profile.ramFreeMB >= 2 * 1024 &&
                      (profile.diskFreeMB < 0 || profile.diskFreeMB >= 1200);
    if (fp32) {
        recommendation = {"fp32", "ok"};
        return recommendation;
    }

    // int8: metade do tamanho e menos memoria; exige ORT >= 1.24 (MatMulNBits).
    const bool int8 = profile.ramTotalMB >= 4 * 1024 && profile.ramFreeMB >= 900 &&
                      (profile.diskFreeMB < 0 || profile.diskFreeMB >= 700);
    if (int8) {
        recommendation = {"int8", "ram"};
        return recommendation;
    }

    recommendation = {"none", profile.diskFreeMB >= 0 && profile.diskFreeMB < 700 ? "disk" : "ram"};
    return recommendation;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
