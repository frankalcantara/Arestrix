#include <unordered_map>
#include <string>
#include "Graph.hpp"
#include <iostream>

namespace taskforge {
namespace core {

Graph::Graph() {
    // Default metadata
    metadata.graphName = "New Taskflow Graph";
    metadata.version = "1.0.0";
    metadata.targetStandard = "C++20";
}

bool Graph::addNode(const NodeData& node) {
    if (nodes.find(node.id) != nodes.end()) {
        std::cerr << "[Graph] Attempted to add duplicate node: " << node.id << std::endl;
        return false;
    }
    nodes[node.id] = node;
    std::cout << "[Graph] Node added: " << node.id << " (" << node.label << ")\n";
    return true;
}

bool Graph::removeNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    if (it != nodes.end()) {
        nodes.erase(it);
        
        // Also remove attached edges
        for(auto edgeIt = edges.begin(); edgeIt != edges.end(); ) {
            if (edgeIt->second.sourceNodeId == nodeId || edgeIt->second.targetNodeId == nodeId) {
                edgeIt = edges.erase(edgeIt);
            } else {
                ++edgeIt;
            }
        }
        
        std::cout << "[Graph] Node removed: " << nodeId << "\n";
        return true;
    }
    return false;
}

NodeData* Graph::getNode(const std::string& nodeId) {
    auto it = nodes.find(nodeId);
    return it != nodes.end() ? &it->second : nullptr;
}

const std::unordered_map<std::string, NodeData>& Graph::getNodes() const {
    return nodes;
}

bool Graph::addEdge(const EdgeData& edge) {
    if (edges.find(edge.id) != edges.end()) {
        std::cerr << "[Graph] Attempted to add duplicate edge: " << edge.id << std::endl;
        return false;
    }
    // Simple validation
    if (nodes.find(edge.sourceNodeId) == nodes.end() || nodes.find(edge.targetNodeId) == nodes.end()) {
        std::cerr << "[Graph] Edge references unknown nodes: " << edge.id << std::endl;
        return false;
    }
    edges[edge.id] = edge;
    std::cout << "[Graph] Edge added: " << edge.sourceNodeId << " -> " << edge.targetNodeId << "\n";
    return true;
}

bool Graph::removeEdge(const std::string& edgeId) {
    auto it = edges.find(edgeId);
    if (it != edges.end()) {
        edges.erase(it);
        std::cout << "[Graph] Edge removed: " << edgeId << "\n";
        return true;
    }
    return false;
}

EdgeData* Graph::getEdge(const std::string& edgeId) {
    auto it = edges.find(edgeId);
    return it != edges.end() ? &it->second : nullptr;
}

const std::unordered_map<std::string, EdgeData>& Graph::getEdges() const {
    return edges;
}

void Graph::setMetadata(const GraphMetadata& meta) {
    metadata = meta;
}

const GraphMetadata& Graph::getMetadata() const {
    return metadata;
}

json Graph::toJson() const {
    json j;
    j["version"] = "1.0";
    j["metadata"] = metadata;
    
    json jNodes = json::array();
    for (const auto& pair : nodes) {
        jNodes.push_back(pair.second);
    }
    j["nodes"] = jNodes;
    
    json jEdges = json::array();
    for (const auto& pair : edges) {
        jEdges.push_back(pair.second);
    }
    j["edges"] = jEdges;
    
    return j;
}

void Graph::fromJson(const json& j) {
    clear();
    
    if (j.contains("metadata")) {
        metadata = j["metadata"].get<GraphMetadata>();
    }
    
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& jNode : j["nodes"]) {
            auto node = jNode.get<NodeData>();
            nodes[node.id] = node;
        }
    }
    
    if (j.contains("edges") && j["edges"].is_array()) {
        for (const auto& jEdge : j["edges"]) {
            auto edge = jEdge.get<EdgeData>();
            edges[edge.id] = edge;
        }
    }
}

void Graph::clear() {
    nodes.clear();
    edges.clear();
}

} // namespace core
} // namespace taskforge
