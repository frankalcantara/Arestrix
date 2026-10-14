// arestrix.dados - transformar dados: consultar JSON, converter CSV, extrair com expressao regular, base64.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <regex>
#include <set>
#include <sstream>

#include "native/NativeTools.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace native {

namespace {

using rt::ToolResult;

constexpr size_t kMaxInput = 256 * 1024;
constexpr size_t kMaxRegexInput = 32 * 1024;
constexpr size_t kMaxRows = 5000;

ToolResult fail(const std::string& m) { return {m, true}; }
ToolResult ok(const std::string& m) { return {m, false}; }
std::string str(const Json& a, const char* key, const std::string& def = "") {
    return a.contains(key) && a[key].is_string() ? a[key].str() : def;
}

// "a.b[0].c" ou "a.b.0.c"  ->  ["a","b","0","c"]
std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() { if (!cur.empty()) out.push_back(cur); cur.clear(); };
    for (char c : path) {
        if (c == '.' || c == '[') flush();
        else if (c == ']') flush();
        else cur += c;
    }
    flush();
    return out;
}

Json parseJsonArg(const Json& v, std::string& error) {
    if (v.is_string()) {
        auto parsed = jsonutil::tryParse(v.str());
        if (!parsed) { error = "O argumento nao e um JSON valido."; return Json(); }
        return *parsed;
    }
    return v;
}

// --- CSV (RFC 4180: aspas, aspas duplicadas, CRLF) -----------------------------------------------------------
std::vector<std::vector<std::string>> parseCsv(const std::string& text, char delim) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false, any = false;
    auto endField = [&]() { row.push_back(field); field.clear(); any = true; };
    auto endRow = [&]() {
        if (any || !field.empty()) { endField(); rows.push_back(row); }
        row.clear();
        any = false;
    };
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
            any = true;
        } else if (c == delim) {
            endField();
        } else if (c == '\n') {
            endRow();
        } else if (c != '\r') {
            field += c;
        }
    }
    endRow();
    return rows;
}

std::string csvCell(const std::string& s, char delim) {
    if (s.find_first_of(std::string("\"\r\n") + delim) == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += (c == '"') ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

// --- regex: recusa os padroes mais perigosos (backtracking catastrofico) ---------------------------------------
bool dangerousPattern(const std::string& p) {
    // grupo seguido de quantificador, contendo outro quantificador: (a+)+  (.*)*  (a|b+)*
    int depth = 0;
    std::vector<bool> hasQuant;
    for (size_t i = 0; i < p.size(); ++i) {
        char c = p[i];
        if (c == '\\') { ++i; continue; }
        if (c == '(') { ++depth; hasQuant.push_back(false); }
        else if (c == ')' && depth > 0) {
            bool inner = hasQuant.back();
            hasQuant.pop_back();
            --depth;
            if (i + 1 < p.size() && (p[i + 1] == '*' || p[i + 1] == '+' || p[i + 1] == '{') && inner) return true;
            if (!hasQuant.empty() && inner) hasQuant.back() = true;
        } else if ((c == '*' || c == '+' || c == '{') && !hasQuant.empty()) {
            hasQuant.back() = true;
        }
    }
    return false;
}

// --- base64 ------------------------------------------------------------------------------------------------------
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64encode(const std::string& in) {
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) | static_cast<unsigned char>(in[i + 2]);
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out += kB64[v >> 18]; out += kB64[(v >> 12) & 63]; out += kB64[(v >> 6) & 63]; out += '=';
    }
    return out;
}

bool b64decode(const std::string& in, std::string& out) {
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        if (std::isspace(c) || c == '=') continue;
        const char* p = std::strchr(kB64, c);
        if (!p || c == 0) return false;
        val = (val << 6) + static_cast<int>(p - kB64);
        bits += 6;
        if (bits >= 0) { out += static_cast<char>((val >> bits) & 0xFF); bits -= 8; }
    }
    return true;
}

} // namespace

Server makeDataServer() {
    Server s;
    s.id = std::string(kIdPrefix) + "dados";
    s.name = "Transformacao de dados";
    s.description = "Consultar JSON, converter CSV, extrair texto com expressoes regulares e codificar em base64.";
    s.category = "Dados";

    s.tools.push_back({"json_query", "Le um campo de um JSON por caminho (ex.: cliente.enderecos[0].cidade) e devolve o valor.",
                       schema({{"json", "object", "O JSON (objeto, lista ou texto JSON)"},
                               {"path", "string", "Caminho do campo; vazio devolve o JSON inteiro"}}, {"json"}),
                       "safe", [](const Json& a, const Context&) -> ToolResult {
                           std::string e;
                           Json cur = parseJsonArg(a["json"], e);
                           if (!e.empty()) return fail(e);
                           for (const auto& key : splitPath(str(a, "path"))) {
                               if (cur.is_object() && cur.contains(key)) {
                                   Json next = cur[key];
                                   cur = next;
                               } else if (cur.is_array() && !key.empty() && std::all_of(key.begin(), key.end(), ::isdigit) &&
                                          static_cast<size_t>(std::stoul(key)) < cur.size()) {
                                   Json next = cur[static_cast<size_t>(std::stoul(key))];
                                   cur = next;
                               } else {
                                   return fail("Campo nao encontrado: " + key);
                               }
                           }
                           return ok(cur.is_string() ? cur.str() : cur.dump());
                       }});

    s.tools.push_back({"csv_to_json", "Converte texto CSV em uma lista JSON (uma linha vira um objeto, usando a primeira linha como nomes das colunas).",
                       schema({{"csv", "string", "Texto CSV"}, {"delimiter", "string", "Separador (padrao: virgula)"},
                               {"header", "boolean", "false se a primeira linha ja e dado (padrao: true)"}}, {"csv"}),
                       "safe", [](const Json& a, const Context&) -> ToolResult {
                           std::string csv = str(a, "csv");
                           if (csv.size() > kMaxInput) return fail("CSV grande demais (limite de 256 KB).");
                           std::string d = str(a, "delimiter", ",");
                           char delim = d == "\\t" || d == "tab" ? '\t' : (d.empty() ? ',' : d[0]);
                           bool header = !(a.contains("header") && a["header"].is_boolean() && !a["header"].get<bool>());
                           auto rows = parseCsv(csv, delim);
                           if (rows.size() > kMaxRows + 1) return fail("CSV com linhas demais (limite de 5000).");
                           Json out = Json::array();
                           if (rows.empty()) return ok("[]");
                           std::vector<std::string> names;
                           size_t start = 0;
                           if (header) {
                               names = rows[0];
                               start = 1;
                           } else {
                               for (size_t i = 0; i < rows[0].size(); ++i) names.push_back("coluna" + std::to_string(i + 1));
                           }
                           for (size_t r = start; r < rows.size(); ++r) {
                               Json obj = Json::object();
                               for (size_t c = 0; c < rows[r].size(); ++c)
                                   obj[c < names.size() && !names[c].empty() ? names[c] : "coluna" + std::to_string(c + 1)] = rows[r][c];
                               out.push_back(obj);
                           }
                           return ok(out.dump());
                       }});

    s.tools.push_back({"json_to_csv", "Converte uma lista de objetos JSON em texto CSV (colunas em ordem alfabetica).",
                       schema({{"json", "object", "Lista de objetos (ou texto JSON)"}, {"delimiter", "string", "Separador (padrao: virgula)"}}, {"json"}),
                       "safe", [](const Json& a, const Context&) -> ToolResult {
                           std::string e;
                           Json data = parseJsonArg(a["json"], e);
                           if (!e.empty()) return fail(e);
                           if (!data.is_array()) return fail("Era esperada uma lista de objetos.");
                           if (data.size() > kMaxRows) return fail("Lista grande demais (limite de 5000 linhas).");
                           std::string d = str(a, "delimiter", ",");
                           char delim = d.empty() ? ',' : d[0];
                           std::vector<std::string> cols;
                           std::set<std::string> seen;
                           for (size_t i = 0; i < data.size(); ++i)
                               if (data[i].is_object())
                                   for (auto it = data[i].begin(); it != data[i].end(); ++it)
                                       if (seen.insert(it.key()).second) cols.push_back(it.key());
                           std::ostringstream out;
                           for (size_t c = 0; c < cols.size(); ++c) out << (c ? std::string(1, delim) : "") << csvCell(cols[c], delim);
                           out << "\n";
                           for (size_t i = 0; i < data.size(); ++i) {
                               for (size_t c = 0; c < cols.size(); ++c) {
                                   std::string cell;
                                   if (data[i].is_object() && data[i].contains(cols[c])) {
                                       const Json& v = data[i][cols[c]];
                                       cell = v.is_string() ? v.str() : (v.is_null() ? std::string() : v.dump());
                                   }
                                   out << (c ? std::string(1, delim) : "") << csvCell(cell, delim);
                               }
                               out << "\n";
                           }
                           return ok(out.str());
                       }});

    s.tools.push_back({"regex_extract", "Extrai trechos de um texto com uma expressao regular (sintaxe ECMAScript). Devolve uma lista JSON.",
                       schema({{"text", "string", "Texto (ate 32 KB)"}, {"pattern", "string", "Expressao regular (ate 200 caracteres)"},
                               {"group", "number", "Grupo a devolver (padrao: 0, o trecho inteiro)"},
                               {"all", "boolean", "true para todas as ocorrencias (padrao: so a primeira)"}}, {"text", "pattern"}),
                       "safe", [](const Json& a, const Context&) -> ToolResult {
                           std::string text = str(a, "text"), pattern = str(a, "pattern");
                           if (text.size() > kMaxRegexInput) return fail("Texto grande demais para a regex (limite de 32 KB).");
                           if (pattern.empty() || pattern.size() > 200) return fail("O padrao deve ter de 1 a 200 caracteres.");
                           if (dangerousPattern(pattern)) return fail("Padrao recusado: quantificadores aninhados podem travar a execucao. Simplifique a expressao.");
                           int group = a.contains("group") && a["group"].is_number() ? a["group"].get<int>() : 0;
                           bool all = a.contains("all") && a["all"].is_boolean() && a["all"].get<bool>();
                           try {
                               std::regex re(pattern, std::regex::ECMAScript);
                               if (group < 0 || static_cast<size_t>(group) > re.mark_count()) return fail("Grupo inexistente neste padrao.");
                               Json out = Json::array();
                               for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end && out.size() < 200; ++it) {
                                   out.push_back((*it)[group].str());
                                   if (!all) break;
                               }
                               return ok(out.dump());
                           } catch (const std::regex_error& ex) {
                               return fail(std::string("Expressao regular invalida: ") + ex.what());
                           }
                       }});

    s.tools.push_back({"base64_encode", "Codifica um texto em base64.", schema({{"text", "string", "Texto"}}, {"text"}), "safe",
                       [](const Json& a, const Context&) -> ToolResult {
                           std::string t = str(a, "text");
                           if (t.size() > kMaxInput) return fail("Texto grande demais.");
                           return ok(b64encode(t));
                       }});

    s.tools.push_back({"base64_decode", "Decodifica base64 para texto.", schema({{"data", "string", "Texto em base64"}}, {"data"}), "safe",
                       [](const Json& a, const Context&) -> ToolResult {
                           std::string out;
                           if (!b64decode(str(a, "data"), out)) return fail("Base64 invalido.");
                           if (out.find('\0') != std::string::npos) return fail("O conteudo decodificado e binario; so texto e devolvido.");
                           return ok(out);
                       }});
    return s;
}

} // namespace native
} // namespace taskforge
