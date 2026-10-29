#include "rt/compress/SmartCrusher.hpp"

#include <algorithm>

#include "rt/compress/Tokens.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {
namespace compress {
namespace {

std::string marker(size_t omitted) {
    return "[... " + std::to_string(omitted) + " itens omitidos ...]";
}

Json crushValue(const Json& value, const JsonOptions& options, bool& touched) {
    if (value.is_array()) {
        const auto& items = value.arr();
        Json out = Json::array();
        if (items.size() > options.maxArrayItems) {
            // Mantem o inicio (mais relevante em busca/log) e um marcador com o total.
            for (size_t i = 0; i < options.maxArrayItems; ++i) {
                out.push_back(crushValue(items[i], options, touched));
            }
            out.push_back(Json(marker(items.size() - options.maxArrayItems)));
            touched = true;
            return out;
        }
        for (const auto& item : items) out.push_back(crushValue(item, options, touched));
        return out;
    }

    if (value.is_object()) {
        Json out = Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            out[it.key()] = crushValue(it.value(), options, touched);
        }
        return out;
    }

    if (value.is_string()) {
        const std::string& text = value.str();
        if (estimateTokens(text) > options.maxStringTokens) {
            const size_t cutBytes = std::max<size_t>(256, options.maxStringTokens * 4);
            std::string cut = text.substr(0, cutBytes);
            cut += " ...[cortado: " + std::to_string(estimateTokens(text)) + " tokens no original]";
            touched = true;
            return Json(cut);
        }
    }
    return value;
}

} // namespace

CompressResult crushJson(std::string_view input, const JsonOptions& options) {
    CompressResult result;
    result.bytesBefore = input.size();
    result.bytesAfter = input.size();
    result.text.assign(input);

    auto parsed = jsonutil::tryParse(input);
    if (!parsed.has_value()) return result;
    if (!parsed->is_object() && !parsed->is_array()) return result;

    bool touched = false;
    Json crushed = crushValue(*parsed, options, touched);
    if (!touched) return result;

    std::string dumped = crushed.dump();
    // Nao aceita ganho marginal: o marcador e o custo do CCR precisam valer a pena.
    const double before = static_cast<double>(input.size());
    const double after = static_cast<double>(dumped.size());
    if (after >= before * (1.0 - options.minGain)) return result;

    result.changed = true;
    result.text = std::move(dumped);
    result.bytesAfter = result.text.size();
    return result;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
