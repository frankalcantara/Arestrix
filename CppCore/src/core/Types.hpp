#pragma once

#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>
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
    Switch,
    Mcp,
    Output
};

enum class PortDirection { Input, Output };
enum class PortType { Flow, Data, Call };

// flow: dependencia normal; feedback: retorno de laco; call: chamada/delegacao.
enum class EdgeKind { Flow, Feedback, Call };

struct CallSettings {
    int maxCalls = 1;
    int timeoutSeconds = 0;
    std::string onError = "fail";
    std::string description;
};

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
    json mcpSettings;      // vertice mcp: servidor, ferramenta, argumentos, origem da entrada e arquivo de saida
    json switchSettings;   // vertice switch: modo de decisao (code | rules | model), origem do texto, palavras
    json outputSettings;   // vertice output: arquivos de entrega, origem/destino e formato
};

struct EdgeData {
    std::string id;
    std::string sourceNodeId;
    std::string sourcePortId;
    std::string targetNodeId;
    std::string targetPortId;
    EdgeKind kind = EdgeKind::Flow;
    CallSettings callSettings;
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
    json callLimits = json::object();
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
    {TaskflowNodeType::Switch, "switch"},
    {TaskflowNodeType::Mcp, "mcp"},
    {TaskflowNodeType::Output, "output"}
})

ARX_JSON_ENUM(PortDirection, {
    {PortDirection::Input, "input"},
    {PortDirection::Output, "output"}
})

ARX_JSON_ENUM(PortType, {
    {PortType::Flow, "flow"},
    {PortType::Data, "data"},
    {PortType::Call, "call"}
})

// Serializacao manual de EdgeKind: um valor desconhecido e erro explicito,
// em vez de virar silenciosamente "flow" em um build antigo.
inline void to_json(json& j, const EdgeKind& e) {
    switch (e) {
        case EdgeKind::Flow: j = "flow"; break;
        case EdgeKind::Feedback: j = "feedback"; break;
        case EdgeKind::Call: j = "call"; break;
    }
}
inline void from_json(const json& j, EdgeKind& e) {
    if (!j.is_string()) {
        e = EdgeKind::Flow;
        return;
    }
    const std::string s = j.str();
    if (s == "flow") e = EdgeKind::Flow;
    else if (s == "feedback") e = EdgeKind::Feedback;
    else if (s == "call") e = EdgeKind::Call;
    else throw std::runtime_error("EdgeKind desconhecido: " + s);
}

inline void to_json(json& j, const CallSettings& c) {
    j = json{
        {"maxCalls", c.maxCalls},
        {"timeoutSeconds", c.timeoutSeconds},
        {"onError", c.onError},
        {"description", c.description}
    };
}
inline void from_json(const json& j, CallSettings& c) {
    c.maxCalls = j.value("maxCalls", 1);
    c.timeoutSeconds = j.value("timeoutSeconds", 0);
    c.onError = j.value("onError", std::string("fail"));
    c.description = j.value("description", std::string());
}

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
        {"switchSettings", n.switchSettings},
        {"mcpSettings", n.mcpSettings},
        {"outputSettings", n.outputSettings}
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
    if(j.contains("mcpSettings")) j.at("mcpSettings").get_to(n.mcpSettings);
    if(j.contains("outputSettings")) j.at("outputSettings").get_to(n.outputSettings);
}

// Serialização manual de EdgeData para campos opcionais
inline void to_json(json& j, const EdgeData& e) {
    j = json{
        {"id", e.id},
        {"sourceNodeId", e.sourceNodeId},
        {"sourcePortId", e.sourcePortId},
        {"targetNodeId", e.targetNodeId},
        {"targetPortId", e.targetPortId},
        {"kind", e.kind},
        {"callSettings", e.callSettings},
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
    if (j.contains("kind")) j.at("kind").get_to(e.kind);
    else e.kind = e.isFeedbackLoop ? EdgeKind::Feedback : EdgeKind::Flow;
    if (j.contains("callSettings")) j.at("callSettings").get_to(e.callSettings);
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
        {"globalDefaults", m.globalDefaults},
        {"callLimits", m.callLimits}
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
    if(j.contains("callLimits")) j.at("callLimits").get_to(m.callLimits);
}

} // namespace core
} // namespace taskforge
