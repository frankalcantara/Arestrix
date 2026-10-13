#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include "util/JsonValue.hpp"

using json = taskforge::Json;

namespace taskforge {
namespace core {

enum class TaskflowNodeType {
    Static,
    Condition,
    Subflow,
    Module,
    Critical,
    Cuda,
    Agent,
    Fork,
    Join,
    Switch
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
    json agentSettings;
    json switchSettings;   // vertice switch: modo de decisao (code | rules | model), origem do texto, palavras
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
    std::string projectId = "default";
    json globalDefaults = json::object();
};

} // namespace core
} // namespace taskforge

// Serialização de enums (ARX_JSON_ENUM, util/JsonValue.hpp)
namespace taskforge {
namespace core {

// Serialização de Enums
ARX_JSON_ENUM(TaskflowNodeType, {
    {TaskflowNodeType::Static, "static"},
    {TaskflowNodeType::Condition, "condition"},
    {TaskflowNodeType::Subflow, "subflow"},
    {TaskflowNodeType::Module, "module"},
    {TaskflowNodeType::Critical, "critical"},
    {TaskflowNodeType::Cuda, "cuda"},
    {TaskflowNodeType::Agent, "agent"},
    {TaskflowNodeType::Fork, "fork"},
    {TaskflowNodeType::Join, "join"},
    {TaskflowNodeType::Switch, "switch"}
})

ARX_JSON_ENUM(PortDirection, {
    {PortDirection::Input, "input"},
    {PortDirection::Output, "output"}
})

ARX_JSON_ENUM(PortType, {
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
        {"requiredHeaders", n.requiredHeaders},
        {"agentSettings", n.agentSettings},
        {"switchSettings", n.switchSettings}
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
    if(j.contains("agentSettings")) j.at("agentSettings").get_to(n.agentSettings);
    if(j.contains("switchSettings")) j.at("switchSettings").get_to(n.switchSettings);
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

inline void to_json(json& j, const CompilerConfig& c) {
    j = json{
        {"compiler", c.compiler},
        {"cppStandard", c.cppStandard},
        {"buildSystem", c.buildSystem},
        {"taskflowIncludePath", c.taskflowIncludePath},
        {"outputBinaryName", c.outputBinaryName},
        {"enableOpenMP", c.enableOpenMP},
        {"enableCuda", c.enableCuda},
        {"extraFlags", c.extraFlags}
    };
}
// Campos ausentes assumem o padrao (o frontend antigo nem sempre os enviava).
inline void from_json(const json& j, CompilerConfig& c) {
    c.compiler = j.value("compiler", "msvc");
    c.cppStandard = j.value("cppStandard", "C++20");
    c.buildSystem = j.value("buildSystem", "cmake");
    c.taskflowIncludePath = j.value("taskflowIncludePath", "third_party/taskflow");
    c.outputBinaryName = j.value("outputBinaryName", "pipeline_app");
    c.enableOpenMP = j.value("enableOpenMP", false);
    c.enableCuda = j.value("enableCuda", false);
    c.extraFlags = j.value("extraFlags", std::vector<std::string>{});
}

inline void to_json(json& j, const GraphMetadata& m) {
    j = json{
        {"graphName", m.graphName},
        {"description", m.description},
        {"targetStandard", m.targetStandard},
        {"compilerConfig", m.compilerConfig},
        {"version", m.version},
        {"projectId", m.projectId},
        {"globalDefaults", m.globalDefaults}
    };
}
inline void from_json(const json& j, GraphMetadata& m) {
    m.graphName = j.value("graphName", "");
    m.description = j.value("description", "");
    m.targetStandard = j.value("targetStandard", "C++20");
    if(j.contains("compilerConfig")) j.at("compilerConfig").get_to(m.compilerConfig);
    m.version = j.value("version", "1.0.0");
    m.projectId = j.value("projectId", "default");
    if(j.contains("globalDefaults")) j.at("globalDefaults").get_to(m.globalDefaults);
}

} // namespace core
} // namespace taskforge
