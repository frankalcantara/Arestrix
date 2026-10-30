#include "MemoryStateSchema.hpp"

#include <cmath>
#include <string>

namespace taskforge {
namespace memory {

namespace {

bool matchesType(const taskforge::Json& value, const std::string& type) {
    if (type == "object") return value.is_object();
    if (type == "array") return value.is_array();
    if (type == "string") return value.is_string();
    if (type == "boolean") return value.is_boolean();
    if (type == "null") return value.is_null();
    if (type == "number") return value.is_number();
    if (type == "integer") {
        if (!value.is_number()) return false;
        const double n = value.get<double>();
        return std::floor(n) == n;
    }
    return true;
}

bool validate(const taskforge::Json& schema, const taskforge::Json& value,
              const std::string& path, std::string& error) {
    if (!schema.is_object() || schema.empty()) return true;

    if (schema.contains("enum") && schema["enum"].is_array()) {
        bool found = false;
        for (const auto& candidate : schema["enum"]) {
            if (candidate.dump() == value.dump()) {
                found = true;
                break;
            }
        }
        if (!found) {
            error = path + ": valor fora do enum";
            return false;
        }
    }

    if (schema.contains("type")) {
        bool typeOk = false;
        if (schema["type"].is_string()) {
            typeOk = matchesType(value, schema["type"].str());
        } else if (schema["type"].is_array()) {
            for (const auto& type : schema["type"]) {
                if (type.is_string() && matchesType(value, type.str())) {
                    typeOk = true;
                    break;
                }
            }
        }
        if (!typeOk) {
            error = path + ": tipo invalido";
            return false;
        }
    }

    if (value.is_object()) {
        if (schema.contains("required") && schema["required"].is_array()) {
            for (const auto& required : schema["required"]) {
                if (!required.is_string()) continue;
                const std::string key = required.str();
                if (!value.contains(key)) {
                    error = path + ": campo obrigatorio ausente: " + key;
                    return false;
                }
            }
        }
        const taskforge::Json properties = schema.contains("properties") && schema["properties"].is_object()
            ? schema["properties"] : taskforge::Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (properties.contains(it.key())) {
                if (!validate(properties[it.key()], it.value(), path + "." + it.key(), error)) return false;
            } else if (schema.contains("additionalProperties") &&
                       schema["additionalProperties"].is_boolean() &&
                       !schema["additionalProperties"].get<bool>()) {
                error = path + ": campo nao permitido: " + it.key();
                return false;
            }
        }
    } else if (value.is_array()) {
        if (schema.contains("maxItems") && schema["maxItems"].is_number()) {
            if (static_cast<double>(value.size()) > schema["maxItems"].get<double>()) {
                error = path + ": itens demais";
                return false;
            }
        }
        if (schema.contains("items") && schema["items"].is_object()) {
            for (size_t i = 0; i < value.size(); ++i) {
                if (!validate(schema["items"], value[i], path + "[" + std::to_string(i) + "]", error)) {
                    return false;
                }
            }
        }
    } else if (value.is_string()) {
        const size_t length = value.str().size();
        if (schema.contains("minLength") && schema["minLength"].is_number() &&
            length < static_cast<size_t>(schema["minLength"].get<double>())) {
            error = path + ": texto curto demais";
            return false;
        }
        if (schema.contains("maxLength") && schema["maxLength"].is_number() &&
            length > static_cast<size_t>(schema["maxLength"].get<double>())) {
            error = path + ": texto longo demais";
            return false;
        }
    } else if (value.is_number()) {
        const double number = value.get<double>();
        if (schema.contains("minimum") && schema["minimum"].is_number() &&
            number < schema["minimum"].get<double>()) {
            error = path + ": valor abaixo do minimo";
            return false;
        }
        if (schema.contains("maximum") && schema["maximum"].is_number() &&
            number > schema["maximum"].get<double>()) {
            error = path + ": valor acima do maximo";
            return false;
        }
    }
    return true;
}

} // namespace

bool validateStateValue(const taskforge::Json& schema, const taskforge::Json& value,
                        std::string& error) {
    error.clear();
    return validate(schema, value, "$", error);
}

} // namespace memory
} // namespace taskforge
