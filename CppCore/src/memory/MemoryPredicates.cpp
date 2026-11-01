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

bool isKnownPredicate(const std::string& predicate) {
    static const char* catalog[] = {
        "USES", "USES_TOOL", "PRODUCES", "PRODUCES_FILE", "READS", "WRITES",
        "TOUCHES", "CITES", "INCLUDES", "TAGGED_WITH", "HAS", "DEPENDS_ON",
        "HAS_VALUE", "MEASURES", "LOCATED_IN", "AUTHORED_BY", "REQUIRES",
        "SUPPORTS", "CONTRADICTS", "SUPERSEDES", "GENERALIZES", "IS_A",
        "PART_OF", "OWNS", "MANAGES", "PREFERS", "MENTIONS", "DEFINES"
    };
    const std::string key = normalized(predicate);
    for (const char* candidate : catalog) {
        if (key == candidate) return true;
    }
    return false;
}

} // namespace memory
} // namespace taskforge
