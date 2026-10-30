#include "MemoryIndexer.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#include "memory/MemoryVectorIndex.hpp"

namespace taskforge {
namespace memory {

namespace {

std::string fnvHex(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(out);
}

} // namespace

std::string MemoryIndexer::contentHash(const std::string& objectType, const std::string& objectId,
                                       const std::string& text, const std::string& model,
                                       const std::string& fingerprint) {
    return fnvHex(fingerprint + "|" + model + "|" + objectType + "|" + objectId + "|" + text);
}

EmbeddingIndexStats MemoryIndexer::index(MemoryStore& store, IEmbeddingProvider& provider,
                                         const EmbeddingConfig& config, const std::string& scopeId,
                                         bool force, size_t maxObjects, std::string& error) {
    EmbeddingIndexStats stats;
    error.clear();
    if (!provider.available()) {
        error = "provider de embeddings indisponivel";
        return stats;
    }
    if (config.model.empty()) {
        error = "modelo de embeddings vazio";
        return stats;
    }

    const std::string fingerprint = embeddingFingerprint(config, provider.dim());
    const auto objects = store.embeddingObjects(scopeId);
    stats.candidates = objects.size();
    const auto existingList = store.listEmbeddings(scopeId, config.model);
    std::map<std::string, MemoryEmbeddingRecord> existing;
    for (const auto& record : existingList) {
        // Nunca misture vetores de fingerprints diferentes: o modelo/dimensao
        // atual sobrescreve a linha e o vetor antigo e descartado.
        if (record.fingerprint != fingerprint) {
            if (store.deleteEmbedding(scopeId, record.objectType, record.objectId, config.model)) {
                ++stats.removed;
            }
            continue;
        }
        existing[record.objectType + "|" + record.objectId] = record;
    }

    std::set<std::string> liveKeys;
    struct Pending {
        MemoryEmbeddingObject object;
        std::string hash;
    };
    std::vector<Pending> pending;
    pending.reserve(objects.size());

    for (const auto& object : objects) {
        if (!object.text.empty()) liveKeys.insert(object.objectType + "|" + object.objectId);
    }
    for (const auto& object : objects) {
        if (object.text.empty()) continue;
        const std::string key = object.objectType + "|" + object.objectId;
        const std::string hash = contentHash(object.objectType, object.objectId,
                                             object.text, config.model, fingerprint);
        auto found = existing.find(key);
        if (!force && found != existing.end() && found->second.contentHash == hash) {
            ++stats.skipped;
            continue;
        }
        pending.push_back(Pending{object, hash});
        if (maxObjects > 0 && pending.size() >= maxObjects) break;
    }

    // Remove vetores de objetos que nao existem mais no escopo/modelo.
    for (const auto& record : existingList) {
        if (liveKeys.count(record.objectType + "|" + record.objectId) == 0) {
            if (store.deleteEmbedding(scopeId, record.objectType, record.objectId, config.model)) {
                ++stats.removed;
            }
        }
    }

    const size_t batchSize = static_cast<size_t>(std::max(1, config.batchSize));
    for (size_t start = 0; start < pending.size(); start += batchSize) {
        const size_t end = std::min(pending.size(), start + batchSize);
        std::vector<std::string> texts;
        texts.reserve(end - start);
        for (size_t i = start; i < end; ++i) texts.push_back(pending[i].object.text);

        std::vector<Embedding> embeddings;
        std::string embedError;
        if (!provider.embed(texts, false, embeddings, embedError)) {
            error = embedError.empty() ? "falha ao gerar embeddings" : embedError;
            stats.failed += end - start;
            return stats;
        }
        if (embeddings.size() != texts.size()) {
            error = "provider devolveu quantidade incorreta de embeddings";
            stats.failed += end - start;
            return stats;
        }
        for (size_t i = start; i < end; ++i) {
            const auto& embedding = embeddings[i - start];
            if (embedding.values.empty()) {
                ++stats.failed;
                continue;
            }
            const auto& object = pending[i].object;
            const auto bytes = MemoryVectorIndex::serialize(embedding.values);
            if (!store.upsertEmbedding(scopeId, object.objectType, object.objectId, config.model,
                                       embedding.dim, bytes, pending[i].hash, object.domain,
                                       fingerprint)) {
                ++stats.failed;
                continue;
            }
            ++stats.indexed;
        }
    }
    return stats;
}

} // namespace memory
} // namespace taskforge
