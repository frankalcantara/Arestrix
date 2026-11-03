#pragma once

#include <string>

namespace taskforge {
namespace memory {

enum class PredicateCardinality {
    Single,     // funcional: um valor ativo por (sujeito, predicado, contexto)
    Multiple,   // acumula valores
    Unknown,    // fora do catalogo em warn/strict, ou estrutural
};

// C5/D11: a cardinalidade vem do catalogo. Desconhecido so volta a ser
// funcional no modo `off`, que preserva o comportamento legado.
PredicateCardinality predicateCardinality(const std::string& predicate);
bool isSingleValuedPredicate(const std::string& predicate);
bool isMultiValuedPredicate(const std::string& predicate);
bool isKnownPredicate(const std::string& predicate);
bool isStructuralPredicate(const std::string& predicate);
bool isClaimablePredicate(const std::string& predicate);
std::string canonicalPredicateName(const std::string& predicate);

} // namespace memory
} // namespace taskforge
