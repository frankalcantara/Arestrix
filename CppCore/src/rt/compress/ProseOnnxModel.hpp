#pragma once

#include <memory>
#include <string>

#include "rt/compress/ProseModel.hpp"

namespace taskforge {
namespace rt {
namespace compress {

// Adaptador ONNX (Kompress-v2-base). So existe quando o build define ARESTRIX_WITH_ONNX e o
// ONNX Runtime esta linkado; caso contrario devolve nullptr.
std::shared_ptr<ProseModel> loadOnnxProseModel(const std::string& modelDir);

} // namespace compress
} // namespace rt
} // namespace taskforge
