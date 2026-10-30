#include "MemoryVectorIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace taskforge {
namespace memory {

std::vector<uint8_t> MemoryVectorIndex::serialize(const std::vector<float>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (float value : values) {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "float precisa ter 32 bits");
        std::memcpy(&bits, &value, sizeof(bits));
        for (int shift = 0; shift < 32; shift += 8) {
            bytes.push_back(static_cast<uint8_t>((bits >> shift) & 0xFFu));
        }
    }
    return bytes;
}

std::vector<float> MemoryVectorIndex::deserialize(const std::vector<uint8_t>& bytes, int dim) {
    std::vector<float> values;
    if (dim <= 0 || bytes.size() != static_cast<size_t>(dim) * sizeof(float)) return values;
    values.reserve(static_cast<size_t>(dim));
    for (size_t offset = 0; offset + 4 <= bytes.size(); offset += 4) {
        uint32_t bits = static_cast<uint32_t>(bytes[offset]) |
                        (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
                        (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
                        (static_cast<uint32_t>(bytes[offset + 3]) << 24);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        values.push_back(value);
    }
    return values;
}

double MemoryVectorIndex::cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.0;
    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * static_cast<double>(b[i]);
        normA += static_cast<double>(a[i]) * static_cast<double>(a[i]);
        normB += static_cast<double>(b[i]) * static_cast<double>(b[i]);
    }
    if (normA <= 0.0 || normB <= 0.0) return 0.0;
    return dot / (std::sqrt(normA) * std::sqrt(normB));
}

std::vector<MemoryVectorHit> MemoryVectorIndex::search(
    const MemoryStore& store, const std::string& scopeId, const std::string& model,
    const std::vector<float>& query, size_t limit, const std::string& fingerprint,
    const std::vector<std::string>& domains) {
    std::vector<MemoryVectorHit> hits;
    if (query.empty() || limit == 0 || model.empty()) return hits;

    std::map<std::string, MemoryEmbeddingObject> objects;
    for (auto& object : store.embeddingObjects(scopeId)) {
        objects[object.objectType + "|" + object.objectId] = std::move(object);
    }

    auto allowed = [&](const std::string& domain) {
        return domains.empty() || std::find(domains.begin(), domains.end(), domain) != domains.end();
    };

    for (const auto& embedding : store.listEmbeddings(scopeId, model, fingerprint)) {
        if (static_cast<int>(query.size()) != embedding.dim) continue;
        const auto key = embedding.objectType + "|" + embedding.objectId;
        auto objectIt = objects.find(key);
        if (objectIt == objects.end()) continue;
        const auto& object = objectIt->second;
        if (!allowed(object.domain)) continue;
        const std::vector<float> values = deserialize(embedding.vector, embedding.dim);
        if (values.empty()) continue;
        const double score = cosine(query, values);
        if (score <= 0.0) continue;
        MemoryVectorHit hit;
        hit.objectType = embedding.objectType;
        hit.objectId = embedding.objectId;
        hit.text = object.text;
        hit.domain = object.domain;
        hit.score = score;
        hit.confidence = object.confidence;
        hit.createdAt = object.createdAt;
        hits.push_back(std::move(hit));
    }

    std::sort(hits.begin(), hits.end(), [](const MemoryVectorHit& a, const MemoryVectorHit& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.objectId < b.objectId;
    });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

} // namespace memory
} // namespace taskforge
