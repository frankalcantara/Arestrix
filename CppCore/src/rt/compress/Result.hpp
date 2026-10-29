#pragma once

#include <cstddef>
#include <string>

namespace taskforge {
namespace rt {
namespace compress {

// Resultado comum dos compressores de texto. `changed == false` devolve o original intacto.
struct CompressResult {
    bool changed = false;
    std::string text;
    size_t bytesBefore = 0;
    size_t bytesAfter = 0;
};

} // namespace compress
} // namespace rt
} // namespace taskforge
