#pragma once

// Indice vetorial exato em SQLite (M2). Para os volumes do Arestrix
// (milhares de objetos), a busca por cosseno em memoria e suficiente e evita
// uma dependencia nativa de ANN. IVectorIndex pode ser extraido depois.

#include <cstdint>
#include <string>
#include <vector>

#include "memory/MemoryStore.hpp"

namespace taskforge {
namespace memory {

struct MemoryVectorHit {
    std::string objectType;
    std::string objectId;
    std::string text;
    std::string domain;
    double score = 0.0;
    double confidence = 0.5;
    std::string createdAt;
};

class MemoryVectorIndex {
public:
    static std::vector<uint8_t> serialize(const std::vector<float>& values);
    static std::vector<float> deserialize(const std::vector<uint8_t>& bytes, int dim);
    static double cosine(const std::vector<float>& a, const std::vector<float>& b);

    static std::vector<MemoryVectorHit> search(
        const MemoryStore& store, const std::string& scopeId, const std::string& model,
        const std::vector<float>& query, size_t limit,
        const std::vector<std::string>& domains = {});
};

} // namespace memory
} // namespace taskforge
