#include "MemoryPredicates.hpp"

#include "MemoryCatalog.hpp"

namespace taskforge {
namespace memory {

PredicateCardinality predicateCardinality(const std::string& predicate) {
    MemoryCatalog& catalog = MemoryCatalog::instance();
    const CatalogPredicate* entry = catalog.find(predicate);
    if (!entry || !entry->claimable) {
        if (!entry && catalog.mode() == "off") return PredicateCardinality::Single;
        return PredicateCardinality::Unknown;
    }
    if (entry->cardinality == "functional") return PredicateCardinality::Single;
    if (entry->cardinality == "multi") return PredicateCardinality::Multiple;
    return PredicateCardinality::Unknown;
}

bool isSingleValuedPredicate(const std::string& predicate) {
    return predicateCardinality(predicate) == PredicateCardinality::Single;
}

bool isMultiValuedPredicate(const std::string& predicate) {
    return predicateCardinality(predicate) == PredicateCardinality::Multiple;
}

bool isKnownPredicate(const std::string& predicate) {
    return MemoryCatalog::instance().isKnown(predicate);
}

bool isStructuralPredicate(const std::string& predicate) {
    return MemoryCatalog::instance().isStructural(predicate);
}

bool isClaimablePredicate(const std::string& predicate) {
    return MemoryCatalog::instance().isClaimable(predicate);
}

std::string canonicalPredicateName(const std::string& predicate) {
    return MemoryCatalog::instance().canonicalName(predicate);
}

} // namespace memory
} // namespace taskforge
