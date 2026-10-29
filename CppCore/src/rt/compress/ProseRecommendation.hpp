#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {

// Adaptador grafico visivel para o DirectML (DXGI). O indice e o mesmo device_id do provider.
struct GpuInfo {
    int index = -1;
    std::string name;
    long long vramMB = 0;
    std::string vendor;   // "nvidia" | "amd" | "intel" | "microsoft" | "other"
};

// Perfil minimo da maquina para recomendar o artefato e o provedor do Kompress.
struct MachineProfile {
    unsigned cpuThreads = 0;
    long long ramTotalMB = 0;
    long long ramFreeMB = 0;
    long long diskFreeMB = -1;   // -1 = desconhecido
    bool dmlAvailable = false;   // build com DirectML e DirectML.dll localizavel
    std::vector<GpuInfo> gpus;
    int dmlDevice = -1;          // indice DXGI escolhido para o DirectML (-1 = nenhum)
};

struct ProseRecommendation {
    // "gpu" (fp32 no DirectML), "fp32" (rapido no CPU), "int8" (compacto) ou "none".
    std::string variant = "none";
    // "gpu" | "ok" | "ram" | "disk" | "cpu": motivo dominante, para a UI traduzir.
    std::string reason = "none";
};

// Le CPU, RAM, GPUs (DXGI) e espaco livre no volume de `diskPath` (sobe para o pai existente).
MachineProfile detectMachineProfile(const std::filesystem::path& diskPath);

// Regra pura (testavel): GPU quando o DirectML esta disponivel e ha uma GPU dedicada;
// senao fp32 com folga de RAM/disco; senao int8; senao indisponivel.
ProseRecommendation recommendProseVariant(const MachineProfile& profile);

// GPUs visiveis, na ordem do DXGI. Vazio fora do Windows ou sem DXGI.
std::vector<GpuInfo> detectGpus();

// Melhor adaptador para o DirectML: maior VRAM dedicada, exigindo >= 2 GB. -1 = nenhum.
int bestDmlDevice(const std::vector<GpuInfo>& gpus);

// Tamanho aproximado do artefato, em MB. "gpu" usa o fp32.
long long proseModelSizeMB(const std::string& variant);

} // namespace compress
} // namespace rt
} // namespace taskforge
