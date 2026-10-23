#include "rt/Interaction.hpp"
#include "rt/NodeInput.hpp"
#include "rt/Runtime.hpp"
#include "platform/Process.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace taskforge {
namespace rt {

namespace fs = std::filesystem;

namespace {

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Arquivo nao encontrado: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string textValue(const Json& value) {
    if (value.is_string()) return value.str();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number() || value.is_null()) return value.dump();
    if (value.is_array()) {
        std::ostringstream out;
        bool first = true;
        for (const auto& item : value) {
            if (!first) out << ", ";
            out << textValue(item);
            first = false;
        }
        return out.str();
    }
    return value.dump();
}

bool optionContains(const Json& field, const std::string& value) {
    if (!field.contains("options") || !field["options"].is_array()) return true;
    for (const auto& option : field["options"]) {
        if (option.is_object() && option.value("value", std::string()) == value) return true;
        if (option.is_string() && option.str() == value) return true;
    }
    return false;
}

void validateField(const Json& field, const Json& value) {
    const std::string type = field.value("type", std::string("text"));
    if (type == "text" || type == "textarea" || type == "date") {
        if (!value.is_string()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser texto.");
        }
        return;
    }
    if (type == "number") {
        if (!value.is_number()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser numero.");
        }
        return;
    }
    if (type == "boolean") {
        if (!value.is_boolean()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser booleano.");
        }
        return;
    }
    if (type == "choice") {
        if (!value.is_string()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser uma escolha.");
        }
        if (!optionContains(field, value.str())) {
            throw std::runtime_error("Opcao invalida para o campo '" + field.value("label", std::string("?")) + "': " + value.str());
        }
        return;
    }
    if (type == "multi_choice") {
        if (!value.is_array()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser uma lista de escolhas.");
        }
        for (const auto& item : value) {
            if (!item.is_string() || !optionContains(field, item.str())) {
                throw std::runtime_error("Opcao invalida no campo '" + field.value("label", std::string("?")) + "'.");
            }
        }
        return;
    }
    throw std::runtime_error("Tipo de campo desconhecido: " + type);
}

} // namespace

std::string runInteraction(const std::string& nodeId, const std::string& label,
                           const std::string& specJson,
                           const std::string& inputContext) {
    if (!safeId(nodeId)) throw std::runtime_error("Vertice de interacao com id invalido: " + nodeId);

    Json spec = jsonutil::parse(specJson);
    if (!spec.is_object()) throw std::runtime_error("Vertice de interacao '" + label + "': configuracao invalida.");
    if (spec.value("schemaSource", std::string("static")) != "static") {
        throw std::runtime_error("Vertice de interacao '" + label +
                                 "': formulario gerado por modelo ainda nao disponivel (Fase 2D).");
    }
    if (!spec.contains("form") || !spec["form"].is_object() ||
        !spec["form"].contains("fields") || !spec["form"]["fields"].is_array() ||
        spec["form"]["fields"].empty()) {
        throw std::runtime_error("Vertice de interacao '" + label + "': formulario sem campos.");
    }

    const Json& form = spec["form"];
    const Json& fields = form["fields"];
    const bool sensitive = spec.value("sensitive", false);
    const std::string channel = spec.value("channel", std::string("any"));

    sandbox::AgentTelemetryEmitter::emitEvent(nodeId, "interaction_request", Json{
        {"nodeId", nodeId},
        {"channel", channel},
        {"fields", fields.size()},
        {"sensitive", sensitive}
    });

    // Canal 2B1: arquivo de respostas. O CLI `--answers` define a variavel.
    const std::string answersPath = platform::getEnv("ARESTRIX_INTERACTION_ANSWERS");
    Json answers = Json::object();
    std::string answeredBy = "answers";
    bool haveAnswers = false;

    if (!answersPath.empty()) {
        fs::path path(answersPath);
        if (path.is_relative()) path = workspaceRoot() / path;
        Json root = jsonutil::parse(readFile(path));
        if (!root.is_object()) throw std::runtime_error("Arquivo de respostas invalido: esperado um objeto JSON.");
        if (root.contains(nodeId)) {
            Json value = root[nodeId];
            if (value.is_array()) {
                static std::mutex countsMutex;
                static std::map<std::string, int> counts;
                int index = 0;
                {
                    std::lock_guard<std::mutex> lock(countsMutex);
                    index = counts[nodeId]++;
                }
                if (value.empty()) throw std::runtime_error("Lista de respostas vazia para '" + nodeId + "'.");
                value = value[std::min<size_t>(static_cast<size_t>(index), value.size() - 1)];
            }
            if (!value.is_object()) {
                throw std::runtime_error("Resposta do vertice '" + nodeId + "' deve ser um objeto JSON.");
            }
            answers = value;
            haveAnswers = true;
        }
    }

    if (!haveAnswers) {
        const std::string onTimeout = spec.value("onTimeout", std::string("fail"));
        if (onTimeout == "default" && spec.contains("defaultAnswers") && spec["defaultAnswers"].is_object()) {
            answers = spec["defaultAnswers"];
            answeredBy = "default";
            haveAnswers = true;
        }
    }

    if (!haveAnswers) {
        throw std::runtime_error("Vertice de interacao '" + label +
                                 "': nenhum canal de respostas. Use --answers <arquivo> (CI) ou o Studio (Fase 2B2).");
    }

    Json normalized = Json::object();
    for (const auto& field : fields) {
        if (!field.is_object()) continue;
        const std::string id = field.value("id", std::string());
        if (id.empty()) throw std::runtime_error("Vertice de interacao '" + label + "': campo sem id.");
        const bool required = field.value("required", false);
        bool hasValue = answers.contains(id) && !answers[id].is_null();
        Json value = hasValue ? answers[id] : Json();
        if (!hasValue && field.contains("defaultValue")) {
            value = field["defaultValue"];
            hasValue = true;
        }
        if (!hasValue) {
            if (required) {
                throw std::runtime_error("Vertice de interacao '" + label + "': campo obrigatorio nao respondido: " +
                                         field.value("label", id));
            }
            continue;
        }
        validateField(field, value);
        normalized[id] = value;
    }

    std::ostringstream text;
    if (!inputContext.empty()) text << "Contexto do chamador:\n" << inputContext << "\n\n";
    for (const auto& field : fields) {
        if (!field.is_object()) continue;
        const std::string id = field.value("id", std::string());
        if (!normalized.contains(id)) continue;
        text << field.value("label", id) << ": " << textValue(normalized[id]) << "\n";
    }

    Json result{
        {"nodeId", nodeId},
        {"status", "ok"},
        {"answers", normalized},
        {"text", text.str()},
        {"answeredBy", answeredBy},
        {"sensitive", sensitive}
    };

    writeNodeOutput(nodeId, text.str());
    std::error_code ec;
    fs::path outDir = workspaceRoot() / ".arestrix" / "out";
    fs::create_directories(outDir, ec);
    std::ofstream(outDir / (nodeId + ".json"), std::ios::binary | std::ios::trunc) << result.dump(2);

    sandbox::AgentTelemetryEmitter::emitEvent(nodeId, "interaction_answer", Json{
        {"nodeId", nodeId},
        {"answeredBy", answeredBy},
        {"fields", normalized.size()},
        {"sensitive", sensitive}
    });
    std::cout << "[Interaction " << label << "] respondido (" << normalized.size()
              << " campo(s), canal: " << answeredBy << ")\n";
    return result.dump();
}

} // namespace rt
} // namespace taskforge
