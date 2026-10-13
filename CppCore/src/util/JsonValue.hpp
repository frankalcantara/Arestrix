#pragma once

// taskforge::Json - valor JSON proprio do Arestrix.
//
//   * leitura: simdjson (util/Json.cpp); nenhuma outra biblioteca faz parse de JSON;
//   * escrita: serializador proprio (dump) com escape correto e saneamento de UTF-8 invalido;
//   * API deliberadamente parecida com a de nlohmann::json (operator[], value, contains, get<T>,
//     to_json/from_json via ADL), o que manteve a migracao mecanica. Objetos usam std::map
//     (chaves ordenadas), logo a saida e deterministica.

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace taskforge {

class Json;

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template <class T>
concept JsonArithmetic = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

class Json {
public:
    using array_t = std::vector<Json>;
    using object_t = std::map<std::string, Json>;
    enum class Type { Null, Bool, Int, UInt, Double, String, Array, Object };

    // --- construcao -------------------------------------------------------------------------
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : v_(b) {}
    template <JsonArithmetic T>
    Json(T n) {
        if constexpr (std::is_floating_point_v<T>) v_ = static_cast<double>(n);
        else if constexpr (std::is_signed_v<T>) v_ = static_cast<int64_t>(n);
        else v_ = static_cast<uint64_t>(n);
    }
    Json(const char* s) : v_(std::string(s ? s : "")) {}
    Json(const std::string& s) : v_(s) {}
    Json(std::string&& s) : v_(std::move(s)) {}
    Json(std::string_view s) : v_(std::string(s)) {}

    // Tipos com to_json(Json&, const T&) encontrado por ADL (structs, enums, vector, map...).
    template <class T>
        requires(!std::is_same_v<std::decay_t<T>, Json> && !JsonArithmetic<std::decay_t<T>> &&
                 !std::is_convertible_v<T, std::string_view> && !std::is_same_v<std::decay_t<T>, bool> &&
                 !std::is_same_v<std::decay_t<T>, std::nullptr_t> &&
                 !std::is_same_v<std::decay_t<T>, std::initializer_list<Json>>)
    Json(const T& value) {
        to_json(*this, value);
    }

    // Lista de inicializacao: vira objeto se todos os itens forem pares [string, valor]
    // (mesma regra do nlohmann), senao vira array.
    Json(std::initializer_list<Json> init) {
        bool isObject = std::all_of(init.begin(), init.end(), [](const Json& e) {
            return e.is_array() && e.size() == 2 && e.arr()[0].is_string();
        });
        if (isObject) {
            object_t o;
            for (const auto& e : init) o[e.arr()[0].str()] = e.arr()[1];
            v_ = std::move(o);
        } else {
            v_ = array_t(init.begin(), init.end());
        }
    }

    static Json array(std::initializer_list<Json> init = {}) {
        Json j;
        j.v_ = array_t(init.begin(), init.end());
        return j;
    }
    static Json object() {
        Json j;
        j.v_ = object_t{};
        return j;
    }

    // --- leitura / escrita de texto ----------------------------------------------------------
    // Parse feito pelo simdjson. Lanca JsonError em documento invalido.
    static Json parse(std::string_view text);
    // indent < 0: compacto; indent >= 0: identado.
    std::string dump(int indent = -1) const;

    // --- tipos -----------------------------------------------------------------------------
    Type type() const { return static_cast<Type>(v_.index()); }
    bool is_null() const { return type() == Type::Null; }
    bool is_boolean() const { return type() == Type::Bool; }
    bool is_number() const { return type() == Type::Int || type() == Type::UInt || type() == Type::Double; }
    bool is_number_integer() const { return type() == Type::Int || type() == Type::UInt; }
    bool is_string() const { return type() == Type::String; }
    bool is_array() const { return type() == Type::Array; }
    bool is_object() const { return type() == Type::Object; }

    // --- acesso a objetos --------------------------------------------------------------------
    bool contains(std::string_view key) const {
        if (!is_object()) return false;
        return obj().find(std::string(key)) != obj().end();
    }
    Json& operator[](const std::string& key) {
        if (is_null()) v_ = object_t{};
        if (!is_object()) throw JsonError("operator[](chave) exige um objeto JSON");
        return std::get<object_t>(v_)[key];
    }
    Json& operator[](const char* key) { return (*this)[std::string(key)]; }
    const Json& operator[](const std::string& key) const {
        static const Json kNull;
        if (!is_object()) return kNull;
        auto it = obj().find(key);
        return it == obj().end() ? kNull : it->second;
    }
    const Json& operator[](const char* key) const { return (*this)[std::string(key)]; }

    template <std::integral I>
        requires(!std::is_same_v<I, bool>)
    Json& operator[](I idx) {
        size_t i = static_cast<size_t>(idx);
        if (is_null()) v_ = array_t{};
        if (!is_array()) throw JsonError("operator[](indice) exige um array JSON");
        auto& a = std::get<array_t>(v_);
        if (i >= a.size()) a.resize(i + 1);
        return a[i];
    }
    template <std::integral I>
        requires(!std::is_same_v<I, bool>)
    const Json& operator[](I idx) const {
        static const Json kNull;
        size_t i = static_cast<size_t>(idx);
        return (is_array() && i < arr().size()) ? arr()[i] : kNull;
    }

    const Json& at(const std::string& key) const {
        if (!is_object()) throw JsonError("at(\"" + key + "\"): o valor nao e um objeto");
        auto it = obj().find(key);
        if (it == obj().end()) throw JsonError("chave ausente: " + key);
        return it->second;
    }
    Json& at(const std::string& key) {
        return const_cast<Json&>(static_cast<const Json&>(*this).at(key));
    }
    const Json& at(size_t i) const {
        if (!is_array() || i >= arr().size()) throw JsonError("indice fora do intervalo");
        return arr()[i];
    }

    // value(): valor da chave convertido para o tipo do padrao. Ausente/null -> padrao.
    template <class T>
    auto value(std::string_view key, const T& def) const {
        using R = std::conditional_t<std::is_convertible_v<T, const char*> && !std::is_same_v<T, std::string>, std::string, T>;
        if (!is_object()) return R(def);
        auto it = obj().find(std::string(key));
        if (it == obj().end() || it->second.is_null()) return R(def);
        return it->second.template get<R>();
    }

    // --- conversao -------------------------------------------------------------------------
    template <class T>
    T get() const {
        if constexpr (std::is_same_v<T, Json>) {
            return *this;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (!is_boolean()) throw JsonError("esperado booleano");
            return std::get<bool>(v_);
        } else if constexpr (std::is_arithmetic_v<T>) {
            return number_as<T>();
        } else if constexpr (std::is_same_v<T, std::string>) {
            if (!is_string()) throw JsonError("esperado string");
            return std::get<std::string>(v_);
        } else {
            T out{};
            from_json(*this, out);
            return out;
        }
    }
    template <class T>
    void get_to(T& out) const {
        out = get<T>();
    }

    // --- tamanho / conteudo ----------------------------------------------------------------
    size_t size() const {
        if (is_array()) return arr().size();
        if (is_object()) return obj().size();
        return is_null() ? 0 : 1;
    }
    bool empty() const { return is_null() || ((is_array() || is_object()) && size() == 0); }
    void clear() {
        if (is_array()) std::get<array_t>(v_).clear();
        else if (is_object()) std::get<object_t>(v_).clear();
        else v_ = std::monostate{};
    }
    void push_back(Json value) {
        if (is_null()) v_ = array_t{};
        if (!is_array()) throw JsonError("push_back exige um array JSON");
        std::get<array_t>(v_).push_back(std::move(value));
    }
    template <class... A>
    void emplace_back(A&&... a) {
        push_back(Json(std::forward<A>(a)...));
    }
    size_t erase(const std::string& key) {
        if (!is_object()) return 0;
        return std::get<object_t>(v_).erase(key);
    }

    // --- iteracao (arrays: elementos; objetos: valores, com key()) -------------------------
    class iterator {
    public:
        using arr_it = array_t::iterator;
        using obj_it = object_t::iterator;
        iterator() = default;
        explicit iterator(arr_it a) : a_(a), isObj_(false) {}
        explicit iterator(obj_it o) : o_(o), isObj_(true) {}
        Json& operator*() const { return isObj_ ? o_->second : *a_; }
        Json* operator->() const { return &**this; }
        iterator& operator++() { if (isObj_) ++o_; else ++a_; return *this; }
        bool operator==(const iterator& r) const { return isObj_ ? o_ == r.o_ : a_ == r.a_; }
        bool operator!=(const iterator& r) const { return !(*this == r); }
        const std::string& key() const { return o_->first; }
        Json& value() const { return **this; }
    private:
        arr_it a_{};
        obj_it o_{};
        bool isObj_ = false;
    };
    class const_iterator {
    public:
        using arr_it = array_t::const_iterator;
        using obj_it = object_t::const_iterator;
        const_iterator() = default;
        explicit const_iterator(arr_it a) : a_(a), isObj_(false) {}
        explicit const_iterator(obj_it o) : o_(o), isObj_(true) {}
        const Json& operator*() const { return isObj_ ? o_->second : *a_; }
        const Json* operator->() const { return &**this; }
        const_iterator& operator++() { if (isObj_) ++o_; else ++a_; return *this; }
        bool operator==(const const_iterator& r) const { return isObj_ ? o_ == r.o_ : a_ == r.a_; }
        bool operator!=(const const_iterator& r) const { return !(*this == r); }
        const std::string& key() const { return o_->first; }
        const Json& value() const { return **this; }
    private:
        arr_it a_{};
        obj_it o_{};
        bool isObj_ = false;
    };
    iterator begin() {
        if (is_object()) return iterator(std::get<object_t>(v_).begin());
        if (is_array()) return iterator(std::get<array_t>(v_).begin());
        return iterator();
    }
    iterator end() {
        if (is_object()) return iterator(std::get<object_t>(v_).end());
        if (is_array()) return iterator(std::get<array_t>(v_).end());
        return iterator();
    }
    const_iterator begin() const {
        if (is_object()) return const_iterator(obj().begin());
        if (is_array()) return const_iterator(arr().begin());
        return const_iterator();
    }
    const_iterator end() const {
        if (is_object()) return const_iterator(obj().end());
        if (is_array()) return const_iterator(arr().end());
        return const_iterator();
    }

    // --- comparacao ------------------------------------------------------------------------
    bool operator==(const Json& o) const;
    bool operator!=(const Json& o) const { return !(*this == o); }

    // Acesso direto (uso interno e dos serializadores).
    const array_t& arr() const { return std::get<array_t>(v_); }
    const object_t& obj() const { return std::get<object_t>(v_); }
    const std::string& str() const { return std::get<std::string>(v_); }

private:
    template <class T>
    T number_as() const {
        switch (type()) {
            case Type::Int: return static_cast<T>(std::get<int64_t>(v_));
            case Type::UInt: return static_cast<T>(std::get<uint64_t>(v_));
            case Type::Double: {
                double d = std::get<double>(v_);
                if constexpr (std::is_floating_point_v<T>) return static_cast<T>(d);
                if (d != static_cast<double>(static_cast<int64_t>(d))) throw JsonError("esperado inteiro, encontrado decimal");
                return static_cast<T>(static_cast<int64_t>(d));
            }
            default: throw JsonError("esperado numero");
        }
    }

    using Storage = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, array_t, object_t>;
    Storage v_;
};


// ---------------------------------------------------------------------------
// Conversoes genericas (ADL em namespace taskforge)
// ---------------------------------------------------------------------------
template <class T>
void to_json(Json& j, const std::vector<T>& v) {
    j = Json::array();
    for (const auto& e : v) j.push_back(Json(e));
}
template <class T>
void from_json(const Json& j, std::vector<T>& v) {
    if (!j.is_array()) throw JsonError("esperado array");
    v.clear();
    for (const auto& e : j) v.push_back(e.template get<T>());
}
template <class T>
void to_json(Json& j, const std::map<std::string, T>& m) {
    j = Json::object();
    for (const auto& [k, e] : m) j[k] = Json(e);
}
template <class T>
void from_json(const Json& j, std::map<std::string, T>& m) {
    if (!j.is_object()) throw JsonError("esperado objeto");
    m.clear();
    for (auto it = j.begin(); it != j.end(); ++it) m[it.key()] = it.value().template get<T>();
}

} // namespace taskforge

// Mapeia um enum para strings: ARX_JSON_ENUM(Tipo, {{Tipo::A, "a"}, {Tipo::B, "b"}})
// O primeiro item e o valor usado quando a string e desconhecida.
#define ARX_JSON_ENUM(EnumType, ...)                                                       \
    inline void to_json(::taskforge::Json& j, const EnumType& e) {                         \
        static const std::pair<EnumType, const char*> table[] = __VA_ARGS__;               \
        for (const auto& p : table) if (p.first == e) { j = ::taskforge::Json(p.second); return; } \
        j = ::taskforge::Json(table[0].second);                                            \
    }                                                                                      \
    inline void from_json(const ::taskforge::Json& j, EnumType& e) {                       \
        static const std::pair<EnumType, const char*> table[] = __VA_ARGS__;               \
        e = table[0].first;                                                                \
        if (j.is_string()) for (const auto& p : table) if (j.str() == p.second) { e = p.first; return; } \
    }
