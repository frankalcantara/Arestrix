#include "rt/compress/ProseModel.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

#include "rt/compress/ProseOnnxModel.hpp"

namespace taskforge {
namespace rt {
namespace compress {

std::string defaultProseModelDir() {
    std::filesystem::path root;
#ifdef _WIN32
    for (const char* env : {"LOCALAPPDATA", "APPDATA"}) {
        const char* value = std::getenv(env);
        if (value && *value) { root = std::filesystem::path(value); break; }
    }
    if (root.empty()) root = std::filesystem::temp_directory_path();
    return (root / "Arestrix" / "models" / "kompress-v2-base").string();
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        if (*home) return (std::filesystem::path(home) / "Library" / "Application Support" / "Arestrix" /
                           "models" / "kompress-v2-base").string();
    }
    return (std::filesystem::temp_directory_path() / "Arestrix" / "models" / "kompress-v2-base").string();
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (*xdg) return (std::filesystem::path(xdg) / "Arestrix" / "models" / "kompress-v2-base").string();
    }
    if (const char* home = std::getenv("HOME")) {
        if (*home) return (std::filesystem::path(home) / ".local" / "share" / "Arestrix" / "models" /
                           "kompress-v2-base").string();
    }
    return (std::filesystem::temp_directory_path() / "Arestrix" / "models" / "kompress-v2-base").string();
#endif
}

std::shared_ptr<ProseModel> loadProseModel(const std::string& modelDir) {
    std::error_code ec;
    if (modelDir.empty() || !std::filesystem::exists(modelDir, ec)) return nullptr;
#ifdef ARESTRIX_WITH_ONNX
    // O adaptador so existe quando o build linka o ONNX Runtime (ARESTRIX_WITH_ONNX=ON).
    return loadOnnxProseModel(modelDir);
#else
    // Sem a dependencia, `prose` fica indisponivel e a politica a ignora — a execucao continua.
    return nullptr;
#endif
}

} // namespace compress
} // namespace rt
} // namespace taskforge
