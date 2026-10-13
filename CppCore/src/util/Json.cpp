#include "util/Json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "simdjson.h"

namespace taskforge {

// ---------------------------------------------------------------------------
// Leitura (simdjson)
// ---------------------------------------------------------------------------
namespace {

// Extrai o valor de um simdjson_result ignorando o codigo de erro (o tipo ja foi verificado).
template <class T>
T take(simdjson::simdjson_result<T>&& r) {
    T v{};
    auto err = std::move(r).get(v);
    if (err != simdjson::SUCCESS) return T{};
    return v;
}

Json convert(simdjson::dom::element el) {
    using simdjson::dom::element_type;
    switch (el.type()) {
        case element_type::OBJECT: {
            Json out = Json::object();
            simdjson::dom::object obj;
            if (el.get_object().get(obj) != simdjson::SUCCESS) return out;
            for (auto field : obj) out[std::string(field.key)] = convert(field.value);
            return out;
        }
        case element_type::ARRAY: {
            Json out = Json::array();
            simdjson::dom::array arr;
            if (el.get_array().get(arr) != simdjson::SUCCESS) return out;
            for (auto child : arr) out.push_back(convert(child));
            return out;
        }
        case element_type::STRING: return Json(std::string(take(el.get_string())));
        case element_type::INT64: return Json(take(el.get_int64()));
        case element_type::UINT64: return Json(take(el.get_uint64()));
        case element_type::DOUBLE: return Json(take(el.get_double()));
        case element_type::BOOL: return Json(take(el.get_bool()));
        case element_type::NULL_VALUE:
        default: return Json();
    }
}

} // namespace

Json Json::parse(std::string_view text) {
    // Um parser por thread: o simdjson reaproveita os buffers internos.
    thread_local simdjson::dom::parser parser;
    simdjson::dom::element root;
    auto err = parser.parse(text.data(), text.size()).get(root);
    if (err != simdjson::SUCCESS) {
        throw JsonError(std::string("JSON invalido: ") + simdjson::error_message(err));
    }
    return convert(root);
}

// ---------------------------------------------------------------------------
// Escrita
// ---------------------------------------------------------------------------
namespace {

void writeString(std::string& out, const std::string& s) {
    static const char* hex = "0123456789abcdef";
    out.push_back('"');
    const size_t n = s.size();
    for (size_t i = 0; i < n;) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (c < 0x20 || c == 0x7f) {
                        out += "\\u00";
                        out.push_back(hex[c >> 4]);
                        out.push_back(hex[c & 0xF]);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
            ++i;
            continue;
        }
        // Sequencia UTF-8 multibyte: valida; bytes invalidos viram U+FFFD (o JSON precisa ser UTF-8 valido).
        int len = (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
        bool ok = len > 0 && i + len <= n;
        uint32_t cp = 0;
        if (ok) {
            cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k < len; ++k) {
                unsigned char cc = static_cast<unsigned char>(s[i + k]);
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
            if (ok && ((len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)))) ok = false;
        }
        if (ok) {
            out.append(s, i, len);
            i += len;
        } else {
            out += "\\ufffd";
            ++i;
        }
    }
    out.push_back('"');
}

void writeDouble(std::string& out, double d) {
    if (!std::isfinite(d)) { out += "null"; return; }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), d);
    std::string text(buf, res.ptr);
    if (text.find_first_of(".eE") == std::string::npos) text += ".0";   // preserva o tipo no round-trip
    out += text;
}

void newline(std::string& out, int indent, int level) {
    out.push_back('\n');
    out.append(static_cast<size_t>(indent) * static_cast<size_t>(level), ' ');
}

void dumpImpl(const Json& j, std::string& out, int indent, int level) {
    switch (j.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += j.get<bool>() ? "true" : "false"; break;
        case Json::Type::Int: out += std::to_string(j.get<int64_t>()); break;
        case Json::Type::UInt: out += std::to_string(j.get<uint64_t>()); break;
        case Json::Type::Double: writeDouble(out, j.get<double>()); break;
        case Json::Type::String: writeString(out, j.str()); break;
        case Json::Type::Array: {
            if (j.arr().empty()) { out += "[]"; break; }
            out.push_back('[');
            bool first = true;
            for (const auto& e : j.arr()) {
                if (!first) out.push_back(',');
                first = false;
                if (indent >= 0) newline(out, indent, level + 1);
                dumpImpl(e, out, indent, level + 1);
            }
            if (indent >= 0) newline(out, indent, level);
            out.push_back(']');
            break;
        }
        case Json::Type::Object: {
            if (j.obj().empty()) { out += "{}"; break; }
            out.push_back('{');
            bool first = true;
            for (const auto& [k, v] : j.obj()) {
                if (!first) out.push_back(',');
                first = false;
                if (indent >= 0) newline(out, indent, level + 1);
                writeString(out, k);
                out.push_back(':');
                if (indent >= 0) out.push_back(' ');
                dumpImpl(v, out, indent, level + 1);
            }
            if (indent >= 0) newline(out, indent, level);
            out.push_back('}');
            break;
        }
    }
}

} // namespace

std::string Json::dump(int indent) const {
    std::string out;
    dumpImpl(*this, out, indent, 0);
    return out;
}

bool Json::operator==(const Json& o) const {
    if (is_number() && o.is_number()) {
        if (type() == Type::Double || o.type() == Type::Double) return get<double>() == o.get<double>();
        if (type() == Type::Int && o.type() == Type::Int) return get<int64_t>() == o.get<int64_t>();
        if (type() == Type::UInt && o.type() == Type::UInt) return get<uint64_t>() == o.get<uint64_t>();
        // Int com UInt: iguais somente se o inteiro com sinal for nao negativo.
        int64_t s = type() == Type::Int ? get<int64_t>() : o.get<int64_t>();
        uint64_t u = type() == Type::UInt ? get<uint64_t>() : o.get<uint64_t>();
        return s >= 0 && static_cast<uint64_t>(s) == u;
    }
    if (type() != o.type()) return false;
    switch (type()) {
        case Type::Null: return true;
        case Type::Bool: return get<bool>() == o.get<bool>();
        case Type::String: return str() == o.str();
        case Type::Array: return arr() == o.arr();
        case Type::Object: return obj() == o.obj();
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// jsonutil
// ---------------------------------------------------------------------------
namespace jsonutil {

Json parse(std::string_view text) { return Json::parse(text); }

std::optional<Json> tryParse(std::string_view text) {
    try {
        return Json::parse(text);
    } catch (const JsonError&) {
        return std::nullopt;
    }
}

Json parseStream(std::istream& in) {
    return Json::parse(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Nao foi possivel abrir o arquivo: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

Json parseFile(const std::filesystem::path& path) { return Json::parse(readFile(path)); }

} // namespace jsonutil
} // namespace taskforge
