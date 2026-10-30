#pragma once

#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

// Valida um valor contra o subconjunto de JSON Schema aceito pelo D1 v2:
// type (object, array, string, number, integer, boolean, null), properties,
// required, additionalProperties=false, items, enum, minLength/maxLength,
// minimum/maximum e maxItems. O validador roda depois da sanitizacao.
bool validateStateValue(const taskforge::Json& schema, const taskforge::Json& value,
                        std::string& error);

} // namespace memory
} // namespace taskforge
