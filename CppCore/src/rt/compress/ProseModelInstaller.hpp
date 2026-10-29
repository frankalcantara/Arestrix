#pragma once

#include <filesystem>
#include <string>

#include "util/Json.hpp"

namespace taskforge {
namespace rt {
namespace compress {

// Manifesto e download do Kompress (fp32/int8) para %LOCALAPPDATA%\Arestrix\models.
// Cada arquivo e baixado para .part, conferido por SHA-256 e promovido atomicamente.
Json proseModelManifest();

// "fp32" | "int8" | "gpu" (gpu usa o fp32). Devolve o id do job ou vazio + error.
std::string startProseModelDownload(const std::string& variant, std::string& error);

bool cancelProseModelDownload(const std::string& jobId);

// Payload do job; {"ok":false,"error":"job_not_found"} quando o id nao existe.
Json proseModelDownloadStatus(const std::string& jobId);

// Jobs ativos e concluidos desta sessao (mais recentes primeiro).
Json proseModelDownloads();

// Modelo da variante + tokenizer + config presentes com o tamanho esperado.
bool proseModelVariantInstalled(const std::string& variant);

} // namespace compress
} // namespace rt
} // namespace taskforge
