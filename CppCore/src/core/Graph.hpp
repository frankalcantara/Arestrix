#pragma once

#include "Types.hpp"
#include <unordered_map>
#include <memory>
#include <string>

namespace taskforge {
namespace core {

class Graph {
public:
    Graph();
    ~Graph() = default;

    // Node operations
    bool addNode(const NodeData& node);
    bool removeNode(const std::string& nodeId);
    NodeData* getNode(const std::string& nodeId);
    const std::unordered_map<std::string, NodeData>& getNodes() const;

    // Edge operations
    bool addEdge(const EdgeData& edge);
    bool removeEdge(const std::string& edgeId);
    EdgeData* getEdge(const std::string& edgeId);
    const std::unordered_map<std::string, EdgeData>& getEdges() const;

    // Metadata
    void setMetadata(const GraphMetadata& meta);
    const GraphMetadata& getMetadata() const;

    // Serialization
    json toJson() const;
    void fromJson(const json& j);

    // Clears the graph
    void clear();

private:
    std::unordered_map<std::string, NodeData> nodes;
    std::unordered_map<std::string, EdgeData> edges;
    GraphMetadata metadata;
};

} // namespace core
} // namespace taskforge
