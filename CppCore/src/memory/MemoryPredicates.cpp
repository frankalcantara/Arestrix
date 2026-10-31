#include "MemoryPredicates.hpp"

#include <algorithm>
#include <cctype>

namespace taskforge {
namespace memory {

namespace {

std::string normalized(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

} // namespace

PredicateCardinality predicateCardinality(const std::string& predicate) {
    // Predicados que podem acumular varios objetos legitimos.
    static const char* multiple[] = {
        "USES", "USES_TOOL", "PRODUCES", "PRODUCES_FILE", "READS", "WRITES",
        "TOUCHES", "CITES", "INCLUDES", "TAGGED_WITH", "HAS", "DEPENDS_ON"};
    const std::string key = normalized(predicate);
    for (const char* candidate : multiple) {
        if (key == candidate) return PredicateCardinality::Multiple;
    }
    // Desconhecido = funcional/single-valued: um novo valor encerra o anterior.
    return PredicateCardinality::Single;
}

bool isSingleValuedPredicate(const std::string& predicate) {
    return predicateCardinality(predicate) == PredicateCardinality::Single;
}

bool isMultiValuedPredicate(const std::string& predicate) {
    return predicateCardinality(predicate) == PredicateCardinality::Multiple;
}

} // namespace memory
} // namespace taskforge
