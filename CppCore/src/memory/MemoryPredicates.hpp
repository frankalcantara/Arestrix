#pragma once

#include <string>

namespace taskforge {
namespace memory {

enum class PredicateCardinality {
    Single,
    Multiple,
};

// C5: predicados de acumulacao (USES, PRODUCES, ...) sao multi-valued;
// predicados funcionais e desconhecidos sao single-valued. Assim um novo
// valor funcional encerra o anterior, mas listas legitimas nao se perdem.
PredicateCardinality predicateCardinality(const std::string& predicate);
bool isSingleValuedPredicate(const std::string& predicate);
bool isMultiValuedPredicate(const std::string& predicate);

} // namespace memory
} // namespace taskforge
