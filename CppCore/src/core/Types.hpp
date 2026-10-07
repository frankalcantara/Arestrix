#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace taskforge {
namespace core {

enum class TaskflowNodeType {
    Static,
    Condition,
    Subflow,
    Module,
    Critical,
    Cuda,
    Agent
};

enum class PortDirection { Input, Output };
enum class PortType { Flow, Data };

struct Port {
    std::string id;
    std::string name;
    PortDirection direction;
    PortType type;
    std::string dataType;
    int conditionBranchIndex = -1; // -1 means none
};

struct NodeData {
    std::string id;
    std::string label;
    TaskflowNodeType type;
    
    struct {
        float x;
        float y;
        float z;
    } position;
    
    std::vector<Port> inputs;
    std::vector<Port> outputs;
    
    // Using string map for parameters to keep it simple, JSON is dynamic
    std::map<std::string, json> parameters;
    
    std::string customLambdaBody;
    std::string criticalSectionId;
    std::string templateId;
    std::vector<std::string> requiredHeaders;
};

struct EdgeData {
    std::string id;
    std::string sourceNodeId;
    std::string sourcePortId;
    std::string targetNodeId;
    std::string targetPortId;
    bool isFeedbackLoop = false;
};

struct CompilerConfig {
    std::string compiler; // msvc, gcc, clang
    std::string cppStandard; // C++20
    std::string buildSystem; // cmake, makefile, direct_script
    std::string taskflowIncludePath;
    std::string outputBinaryName;
    bool enableOpenMP = false;
    bool enableCuda = false;
    std::vector<std::string> extraFlags;
};

struct GraphMetadata {
    std::string graphName;
    std::string description;
    std::string targetStandard;
    CompilerConfig compilerConfig;
    std::string version;
};

} // namespace core
} // namespace taskforge

// MACROS de serialização automática nlohmann
namespace taskforge {
namespace core {

// Serialização de Enums
NLOHMANN_JSON_SERIALIZE_ENUM(TaskflowNodeType, {
    {TaskflowNodeType::Static, "static"},
    {TaskflowNodeType::Condition, "condition"},
    {TaskflowNodeType::Subflow, "subflow"},
    {TaskflowNodeType::Module, "module"},
    {TaskflowNodeType::Critical, "critical"},
    {TaskflowNodeType::Cuda, "cuda"},
    {TaskflowNodeType::Agent, "agent"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(PortDirection, {
    {PortDirection::Input, "input"},
    {PortDirection::Output, "output"}
})

NLOHMANN_JSON_SERIALIZE_ENUM(PortType, {
    {PortType::Flow, "flow"},
    {PortType::Data, "data"}
})

// Serialização manual de Port para lidar com campos opcionais seguros
inline void to_json(json& j, const Port& p) {
    j = json{
        {"id", p.id},
        {"name", p.name},
        {"direction", p.direction},
        {"type", p.type},
        {"dataType", p.dataType},
        {"conditionBranchIndex", p.conditionBranchIndex}
    };
}
inline void from_json(const json& j, Port& p) {
    j.at("id").get_to(p.id);
    j.at("name").get_to(p.name);
    j.at("direction").get_to(p.direction);
    j.at("type").get_to(p.type);
    p.dataType = j.value("dataType", "");
    p.conditionBranchIndex = j.value("conditionBranchIndex", -1);
}

// Para structs aninhadas (NodeData)
inline void to_json(json& j, const NodeData& n) {
    j = json{
        {"id", n.id},
        {"label", n.label},
        {"type", n.type},
        {"position", {{"x", n.position.x}, {"y", n.position.y}, {"z", n.position.z}}},
        {"inputs", n.inputs},
        {"outputs", n.outputs},
        {"parameters", n.parameters},
        {"customLambdaBody", n.customLambdaBody},
        {"criticalSectionId", n.criticalSectionId},
        {"templateId", n.templateId},
        {"requiredHeaders", n.requiredHeaders}
    };
}
inline void from_json(const json& j, NodeData& n) {
    j.at("id").get_to(n.id);
    j.at("label").get_to(n.label);
    j.at("type").get_to(n.type);
    n.position.x = j.at("position").value("x", 0.0f);
    n.position.y = j.at("position").value("y", 0.0f);
    n.position.z = j.at("position").value("z", 0.0f);
    if(j.contains("inputs")) j.at("inputs").get_to(n.inputs);
    if(j.contains("outputs")) j.at("outputs").get_to(n.outputs);
    if(j.contains("parameters")) j.at("parameters").get_to(n.parameters);
    if(j.contains("customLambdaBody")) j.at("customLambdaBody").get_to(n.customLambdaBody);
    if(j.contains("criticalSectionId")) j.at("criticalSectionId").get_to(n.criticalSectionId);
    if(j.contains("templateId")) j.at("templateId").get_to(n.templateId);
    if(j.contains("requiredHeaders")) j.at("requiredHeaders").get_to(n.requiredHeaders);
}

// Serialização manual de EdgeData para campos opcionais
inline void to_json(json& j, const EdgeData& e) {
    j = json{
        {"id", e.id},
        {"sourceNodeId", e.sourceNodeId},
        {"sourcePortId", e.sourcePortId},
        {"targetNodeId", e.targetNodeId},
        {"targetPortId", e.targetPortId},
        {"isFeedbackLoop", e.isFeedbackLoop}
    };
}
inline void from_json(const json& j, EdgeData& e) {
    j.at("id").get_to(e.id);
    j.at("sourceNodeId").get_to(e.sourceNodeId);
    j.at("sourcePortId").get_to(e.sourcePortId);
    j.at("targetNodeId").get_to(e.targetNodeId);
    j.at("targetPortId").get_to(e.targetPortId);
    e.isFeedbackLoop = j.value("isFeedbackLoop", false);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CompilerConfig, compiler, cppStandard, buildSystem, taskflowIncludePath, outputBinaryName, enableOpenMP, enableCuda, extraFlags)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GraphMetadata, graphName, description, targetStandard, compilerConfig, version)

} // namespace core
} // namespace taskforge
