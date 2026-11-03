#pragma once

// Canonicalizador da Fase 2: resolve aliases, sujeito/objeto e contexto antes
// da promocao. A abstencao devolve um motivo estavel para a proposta
// `unresolved`.

#include <string>

#include "MemoryCatalog.hpp"
#include "MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct CanonicalClaimInput {
    std::string subjectText;
    std::string subjectId;
    std::string predicate;
    std::string objectText;
    std::string objectId;
    std::string valueText;
    std::string unit;
    std::string polarity = "positive";
    taskforge::Json qualifiers = taskforge::Json::object();
};

struct CanonicalClaimResult {
    bool ok = false;
    std::string reason;
    std::string predicate;
    std::string subjectId;
    std::string objectId;
    std::string contextKey;
    std::string claimKey;
    bool unknownPredicate = false;
    bool missingContext = false;
};

class MemoryCanonicalizer {
public:
    static CanonicalClaimResult canonicalize(MemoryStore& store, const std::string& scopeId,
                                             const CanonicalClaimInput& input) {
        CanonicalClaimResult out;
        MemoryCatalog& catalog = MemoryCatalog::instance();
        out.predicate = catalog.canonicalName(input.predicate);
        if (isStructuralPredicate(input.predicate)) {
            out.reason = "structural_predicate";
            return out;
        }
        out.unknownPredicate = !catalog.isKnown(input.predicate);
        if (out.unknownPredicate && catalog.mode() == "strict") {
            out.reason = "unknown_predicate";
            return out;
        }
        out.subjectId = input.subjectId;
        if (out.subjectId.empty()) {
            bool ambiguous = false;
            out.subjectId = store.findEntity(scopeId, input.subjectText, &ambiguous);
            if (out.subjectId.empty() || ambiguous) {
                out.reason = ambiguous ? "ambiguous_subject" : "unknown_subject";
                return out;
            }
        }
        out.objectId = input.objectId;
        if (out.objectId.empty() && !input.objectText.empty()) {
            bool ambiguous = false;
            out.objectId = store.findEntity(scopeId, input.objectText, &ambiguous);
            if (ambiguous) {
                out.reason = "ambiguous_object";
                return out;
            }
        }
        std::string contextError;
        out.missingContext = !catalog.canonicalContext(out.predicate, input.qualifiers,
                                                       out.contextKey, contextError);
        if (out.missingContext) {
            out.reason = contextError.empty() ? "missing_context" : contextError;
            if (catalog.mode() == "strict") return out;
        }
        std::string keyError;
        if (!catalog.canonicalClaimKey(scopeId, out.subjectId, out.predicate, out.objectId,
                                       input.objectText, input.valueText, input.unit,
                                       out.contextKey, input.polarity, out.claimKey, keyError)) {
            out.reason = keyError.empty() ? "claim_key_uncomputable" : keyError;
            return out;
        }
        out.ok = true;
        return out;
    }
};

} // namespace memory
} // namespace taskforge
