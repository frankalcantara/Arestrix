#pragma once

// Catálogo versionado de predicados (Fase 2). Header-only para manter o módulo
// de memória desacoplado do gerador de build e permitir que os testes usem uma
// instância isolada.

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "catalog/PredicatesV1Data.hpp"
#include "project/RunnerSecurity.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace memory {

struct CatalogPredicate {
    std::string name;
    bool claimable = true;
    std::string cardinality = "multi";   // functional | multi | none
    std::vector<std::string> requiredContextKeys;
    std::string valueType = "entity";    // entity | literal | quantity
    bool caseInsensitive = false;
    std::string canonicalUnit;
    std::map<std::string, double> units;
    std::map<std::string, std::map<std::string, std::string>> contextKeys;
    std::vector<std::string> aliases;
    std::vector<std::string> domains;
    std::string description;
};

class MemoryCatalog {
public:
    static MemoryCatalog& instance() {
        static MemoryCatalog catalog;
        return catalog;
    }

    MemoryCatalog() { resetBuiltin(); }

    void resetBuiltin() {
        predicates_.clear();
        aliases_.clear();
        overrides_.clear();
        mode_ = "warn";
        auto parsed = taskforge::jsonutil::tryParse(catalog_data::kPredicatesV1Json);
        if (parsed && parsed->is_object()) mergeJson(*parsed, true, nullptr);
        fingerprintCache_.clear();
    }

    const std::string& mode() const { return mode_; }
    void setMode(std::string mode) {
        if (mode != "off" && mode != "warn" && mode != "strict") mode = "warn";
        mode_ = std::move(mode);
    }

    bool loadProjectFile(const std::string& path, std::string* auditJson = nullptr,
                         std::string* error = nullptr) {
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            if (error) *error = "catalogo de projeto ausente: " + path;
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            if (error) *error = "nao foi possivel ler o catalogo: " + path;
            return false;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = taskforge::jsonutil::tryParse(text);
        if (!parsed || !parsed->is_object()) {
            if (error) *error = "catalogo de projeto nao e JSON objeto";
            return false;
        }
        return mergeJson(*parsed, false, auditJson, error);
    }

    bool mergeJson(const taskforge::Json& root, bool builtin,
                   std::string* auditJson, std::string* error = nullptr) {
        if (!root.is_object()) {
            if (error) *error = "catalogo invalido";
            return false;
        }
        std::vector<std::string> appliedOverrides;
        if (root.contains("overrides") && root["overrides"].is_array()) {
            for (const auto& overrideValue : root["overrides"]) {
                if (!overrideValue.is_object()) continue;
                const std::string name = overrideValue.value("name", std::string());
                const std::string cardinality = overrideValue.value("cardinality", std::string());
                const std::string reason = overrideValue.value("reason", std::string());
                if (name.empty() || (cardinality != "functional" && cardinality != "multi")) {
                    if (error) *error = "override de catalogo invalido";
                    return false;
                }
                auto it = predicates_.find(name);
                if (it == predicates_.end()) {
                    if (error) *error = "override para predicado inexistente: " + name;
                    return false;
                }
                if (reason.empty()) {
                    if (error) *error = "override sem motivo: " + name;
                    return false;
                }
                const std::string previous = it->second.cardinality;
                it->second.cardinality = cardinality;
                overrides_.push_back({name, previous, cardinality, reason});
                appliedOverrides.push_back(name + ":" + previous + "->" + cardinality);
            }
        }
        taskforge::Json predicates = root.contains("predicates") && root["predicates"].is_array()
            ? root["predicates"] : taskforge::Json::array();
        if (builtin && predicates.empty()) {
            if (error) *error = "catalogo embutido sem predicates[]";
            return false;
        }
        for (const auto& value : predicates) {
            if (!value.is_object()) continue;
            CatalogPredicate p;
            p.name = value.value("name", std::string());
            if (p.name.empty()) continue;
            const bool hasClaimable = value.contains("claimable");
            const bool hasCardinality = value.contains("cardinality");
            p.claimable = value.value("claimable", true);
            p.cardinality = value.value("cardinality", p.claimable ? "multi" : "none");
            p.valueType = value.value("valueType", std::string("entity"));
            p.caseInsensitive = value.value("caseInsensitive", false);
            p.canonicalUnit = value.value("canonicalUnit", std::string());
            p.description = value.value("description", std::string());
            if (value.contains("requiredContextKeys") && value["requiredContextKeys"].is_array()) {
                for (const auto& key : value["requiredContextKeys"]) {
                    if (key.is_string()) p.requiredContextKeys.push_back(key.str());
                }
            }
            if (value.contains("aliases") && value["aliases"].is_array()) {
                for (const auto& alias : value["aliases"]) {
                    if (alias.is_string()) p.aliases.push_back(alias.str());
                }
            }
            if (value.contains("domains") && value["domains"].is_array()) {
                for (const auto& domain : value["domains"]) {
                    if (domain.is_string()) p.domains.push_back(domain.str());
                }
            }
            if (value.contains("units") && value["units"].is_object()) {
                for (auto it = value["units"].begin(); it != value["units"].end(); ++it) {
                    if (it.value().is_number()) p.units[it.key()] = it.value().get<double>();
                }
                if (p.canonicalUnit.empty() && !p.units.empty()) {
                    p.canonicalUnit = p.units.begin()->first;
                }
            }
            if (value.contains("contextKeys") && value["contextKeys"].is_object()) {
                for (auto it = value["contextKeys"].begin(); it != value["contextKeys"].end(); ++it) {
                    std::map<std::string, std::string> values;
                    if (it.value().is_object()) {
                        for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
                            if (jt.value().is_string()) values[jt.key()] = jt.value().str();
                        }
                    }
                    p.contextKeys[it.key()] = std::move(values);
                }
            }
            auto existing = predicates_.find(p.name);
            if (existing != predicates_.end()) {
                if (!builtin) {
                    if (!hasClaimable) p.claimable = existing->second.claimable;
                    if (!hasCardinality) p.cardinality = existing->second.cardinality;
                    if (existing->second.claimable != p.claimable) {
                        if (error) *error = "projeto nao pode mudar claimable de " + p.name;
                        return false;
                    }
                    if (existing->second.cardinality != p.cardinality) {
                        if (error) *error = "projeto nao pode mudar cardinalidade de " +
                                            p.name + " sem override";
                        return false;
                    }
                }
                // Projeto pode acrescentar aliases, contextos, unidades e descricao.
                for (const auto& alias : p.aliases) {
                    if (std::find(existing->second.aliases.begin(),
                                  existing->second.aliases.end(), alias) ==
                        existing->second.aliases.end()) {
                        existing->second.aliases.push_back(alias);
                    }
                }
                for (const auto& kv : p.contextKeys) {
                    existing->second.contextKeys[kv.first] = kv.second;
                }
                for (const auto& kv : p.units) {
                    existing->second.units[kv.first] = kv.second;
                }
                if (!p.description.empty()) existing->second.description = p.description;
            } else {
                predicates_[p.name] = p;
            }
        }
        rebuildAliases();
        fingerprintCache_.clear();
        if (auditJson) {
            taskforge::Json audit = taskforge::Json::array();
            for (const auto& item : appliedOverrides) audit.push_back(item);
            *auditJson = audit.dump();
        }
        return true;
    }

    const CatalogPredicate* find(const std::string& predicate) const {
        const std::string key = upper(predicate);
        auto direct = predicates_.find(key);
        if (direct != predicates_.end()) return &direct->second;
        auto alias = aliases_.find(key);
        if (alias != aliases_.end()) {
            auto it = predicates_.find(alias->second);
            if (it != predicates_.end()) return &it->second;
        }
        return nullptr;
    }

    std::string canonicalName(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p ? p->name : upper(predicate);
    }

    bool isKnown(const std::string& predicate) const { return find(predicate) != nullptr; }
    bool isStructural(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p && !p->claimable;
    }
    bool isClaimable(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p && p->claimable;
    }
    bool isMulti(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p && p->claimable && p->cardinality == "multi";
    }
    bool isFunctional(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p && p->claimable && p->cardinality == "functional";
    }
    std::vector<std::string> requiredContextKeys(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p ? p->requiredContextKeys : std::vector<std::string>();
    }
    bool caseInsensitive(const std::string& predicate) const {
        const CatalogPredicate* p = find(predicate);
        return p && p->caseInsensitive;
    }

    std::string fingerprint() const {
        if (!fingerprintCache_.empty()) return fingerprintCache_;
        fingerprintCache_ = project::RunnerSecurity::sha256(fingerprintText());
        return fingerprintCache_;
    }

    std::vector<std::string> claimableNames() const {
        std::vector<std::string> out;
        for (const auto& kv : predicates_) if (kv.second.claimable) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }
    std::vector<std::string> structuralNames() const {
        std::vector<std::string> out;
        for (const auto& kv : predicates_) if (!kv.second.claimable) out.push_back(kv.first);
        std::sort(out.begin(), out.end());
        return out;
    }
    std::map<std::string, std::vector<std::string>> contextKeysFor(
        const std::string& predicate) const {
        std::map<std::string, std::vector<std::string>> out;
        const CatalogPredicate* p = find(predicate);
        if (!p) return out;
        for (const auto& ctx : p->contextKeys) {
            std::vector<std::string> values;
            for (const auto& kv : ctx.second) values.push_back(kv.second);
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
            out[ctx.first] = std::move(values);
        }
        return out;
    }

    std::string promptLines(const std::string& domain = "") const {
        std::vector<std::string> lines;
        for (const auto& kv : predicates_) {
            const auto& p = kv.second;
            if (!p.claimable) continue;
            if (!domain.empty() && !p.domains.empty() &&
                std::find(p.domains.begin(), p.domains.end(), domain) == p.domains.end()) {
                continue;
            }
            std::ostringstream line;
            line << p.name << ", " << p.cardinality;
            if (!p.contextKeys.empty()) {
                line << ", contextos:";
                bool first = true;
                for (const auto& ctx : p.contextKeys) {
                    if (!first) line << "|";
                    first = false;
                    line << ctx.first;
                }
            }
            if (!p.requiredContextKeys.empty()) {
                line << ", obrigatorios:";
                for (size_t i = 0; i < p.requiredContextKeys.size(); ++i) {
                    if (i) line << "|";
                    line << p.requiredContextKeys[i];
                }
            }
            if (!p.units.empty()) {
                line << ", unidades:" << p.canonicalUnit;
            }
            lines.push_back(line.str());
        }
        std::sort(lines.begin(), lines.end());
        std::ostringstream out;
        for (const auto& line : lines) out << line << "\n";
        return out.str();
    }

    // Contexto canonico: somente chaves declaradas, valores resolvidos por alias.
    bool canonicalContext(const std::string& predicate, const taskforge::Json& qualifiers,
                          std::string& contextKey, std::string& error) const {
        contextKey.clear();
        error.clear();
        const CatalogPredicate* p = find(predicate);
        if (!p) {
            error = "unknown_predicate";
            return false;
        }
        std::map<std::string, std::string> values;
        for (const auto& key : p->requiredContextKeys) {
            if (!qualifiers.is_object() || !qualifiers.contains(key) ||
                !qualifiers[key].is_string() || qualifiers[key].str().empty()) {
                error = "missing_context";
                return false;
            }
            values[key] = qualifiers[key].str();
        }
        for (const auto& ctx : p->contextKeys) {
            if (!qualifiers.is_object() || !qualifiers.contains(ctx.first)) continue;
            const auto& value = qualifiers[ctx.first];
            if (!value.is_string()) continue;
            const std::string raw = value.str();
            auto alias = ctx.second.find(lower(raw));
            values[ctx.first] = alias == ctx.second.end() ? raw : alias->second;
        }
        taskforge::Json object = taskforge::Json::object();
        for (const auto& kv : values) object[kv.first] = kv.second;
        contextKey = canonicalJson(object);
        return true;
    }

    bool canonicalClaimKey(const std::string& scopeId, const std::string& subjectId,
                           const std::string& predicate, const std::string& objectId,
                           const std::string& objectText, const std::string& valueText,
                           const std::string& unit, const std::string& contextKey,
                           const std::string& polarity, std::string& claimKey,
                           std::string& error) const {
        claimKey.clear();
        error.clear();
        const CatalogPredicate* p = find(predicate);
        if (!p || !p->claimable) {
            error = "unknown_or_structural_predicate";
            return false;
        }
        const std::string subject = subjectId.empty() ? "" : "e:" + subjectId;
        if (subject.empty()) {
            error = "subject_unresolved";
            return false;
        }
        std::string object;
        if (!objectId.empty()) {
            object = "e:" + objectId;
        } else {
            std::string literal = valueText.empty() ? objectText : valueText;
            literal = collapseSpaces(trim(literal));
            if (literal.empty()) {
                error = "object_unresolved";
                return false;
            }
            if (p->valueType == "quantity" || !unit.empty()) {
                std::string converted;
                std::string unitError;
                if (convertQuantity(literal, unit, *p, converted, unitError)) {
                    literal = converted;
                }
            }
            if (p->caseInsensitive) literal = lower(literal);
            object = "v:" + literal;
        }
        const std::string canonicalPredicate = p->name;
        const std::string context = contextKey.empty() ? "{}" : canonicalJsonText(contextKey);
        const std::string payload = scopeId + "|" + subject + "|" + canonicalPredicate + "|" +
                                    object + "|" + context + "|" + polarity;
        claimKey = project::RunnerSecurity::sha256(payload);
        return true;
    }

    std::string canonicalJsonText(const std::string& value) const {
        auto parsed = taskforge::jsonutil::tryParse(value);
        return parsed ? canonicalJson(*parsed) : value;
    }

    static std::string upper(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    }
    static std::string lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

private:
    struct Override {
        std::string name;
        std::string previous;
        std::string cardinality;
        std::string reason;
    };

    std::map<std::string, CatalogPredicate> predicates_;
    std::map<std::string, std::string> aliases_;
    std::vector<Override> overrides_;
    std::string mode_ = "warn";
    mutable std::string fingerprintCache_;

    void rebuildAliases() {
        aliases_.clear();
        for (const auto& kv : predicates_) {
            for (const auto& alias : kv.second.aliases) aliases_[upper(alias)] = kv.first;
        }
    }

    static std::string trim(std::string value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        return value;
    }
    static std::string collapseSpaces(const std::string& value) {
        std::string out;
        bool space = false;
        for (unsigned char c : value) {
            if (std::isspace(c)) {
                space = true;
                continue;
            }
            if (space && !out.empty()) out.push_back(' ');
            space = false;
            out.push_back(static_cast<char>(c));
        }
        return out;
    }

    static std::string canonicalNumber(double value) {
        if (value == 0) return "0";
        char buffer[64];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec == std::errc()) return std::string(buffer, result.ptr);
        std::ostringstream out;
        out << value;
        return out.str();
    }

    static bool convertQuantity(const std::string& text, const std::string& explicitUnit,
                                const CatalogPredicate& predicate, std::string& out,
                                std::string& error) {
        out.clear();
        error.clear();
        if (predicate.units.empty()) {
            out = text;
            return true;
        }
        const std::string trimmed = trim(text);
        char* end = nullptr;
        errno = 0;
        const double number = std::strtod(trimmed.c_str(), &end);
        if (end == trimmed.c_str() || errno == ERANGE) {
            error = "quantity_unparsable";
            return false;
        }
        std::string unit = trim(std::string(end));
        if (unit.empty()) unit = explicitUnit;
        if (unit.empty()) unit = predicate.canonicalUnit;
        auto given = predicate.units.find(unit);
        auto canonical = predicate.units.find(predicate.canonicalUnit);
        if (given == predicate.units.end() || canonical == predicate.units.end() ||
            canonical->second == 0.0) {
            error = "unit_unparsable";
            return false;
        }
        const double converted = number * given->second / canonical->second;
        out = canonicalNumber(converted) + " " + predicate.canonicalUnit;
        return true;
    }

    static std::string canonicalJson(const taskforge::Json& value) {
        if (value.is_null()) return "null";
        if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
        if (value.is_number()) {
            if (value.is_number_integer()) return std::to_string(value.get<long long>());
            return canonicalNumber(value.get<double>());
        }
        if (value.is_string()) {
            std::string out = "\"";
            for (char c : value.str()) {
                switch (c) {
                    case '\\': out += "\\\\"; break;
                    case '"': out += "\\\""; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default: out.push_back(c); break;
                }
            }
            out += "\"";
            return out;
        }
        if (value.is_array()) {
            std::string out = "[";
            bool first = true;
            for (const auto& item : value) {
                if (!first) out += ",";
                first = false;
                out += canonicalJson(item);
            }
            return out + "]";
        }
        if (value.is_object()) {
            std::vector<std::string> keys;
            for (auto it = value.begin(); it != value.end(); ++it) keys.push_back(it.key());
            std::sort(keys.begin(), keys.end());
            std::string out = "{";
            bool first = true;
            for (const auto& key : keys) {
                if (!first) out += ",";
                first = false;
                taskforge::Json keyJson(key);
                out += canonicalJson(keyJson) + ":" + canonicalJson(value[key]);
            }
            return out + "}";
        }
        return "null";
    }

    std::string fingerprintText() const {
        taskforge::Json root = taskforge::Json::object();
        root["version"] = 1;
        taskforge::Json list = taskforge::Json::array();
        for (const auto& kv : predicates_) {
            const auto& p = kv.second;
            taskforge::Json item = taskforge::Json::object();
            item["name"] = p.name;
            item["claimable"] = p.claimable;
            item["cardinality"] = p.cardinality;
            item["valueType"] = p.valueType;
            item["caseInsensitive"] = p.caseInsensitive;
            item["canonicalUnit"] = p.canonicalUnit;
            item["requiredContextKeys"] = taskforge::Json(p.requiredContextKeys);
            item["aliases"] = taskforge::Json(p.aliases);
            item["domains"] = taskforge::Json(p.domains);
            taskforge::Json units = taskforge::Json::object();
            for (const auto& unit : p.units) units[unit.first] = unit.second;
            item["units"] = units;
            taskforge::Json contexts = taskforge::Json::object();
            for (const auto& ctx : p.contextKeys) {
                taskforge::Json values = taskforge::Json::object();
                for (const auto& value : ctx.second) values[value.first] = value.second;
                contexts[ctx.first] = values;
            }
            item["contextKeys"] = contexts;
            list.push_back(item);
        }
        root["predicates"] = list;
        return canonicalJson(root);
    }
};

} // namespace memory
} // namespace taskforge
