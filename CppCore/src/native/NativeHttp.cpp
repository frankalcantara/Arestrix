// arestrix.http - requisicoes HTTPS para hosts que o PROJETO autorizou.
//
// Politica em <projeto>/.workspace/native/http.json:  { "allowedHosts": ["api.exemplo.com", "*.exemplo.org"] }
// Sem esse arquivo (ou com a lista vazia) nenhuma requisicao e feita: um agente nunca decide sozinho para onde os dados
// do projeto saem. Somente HTTPS (porta 443); enderecos locais e privados sao recusados; redirecionamentos NAO sao
// seguidos (cada destino precisaria ser autorizado de novo); credenciais so por variavel de ambiente (bearer_env).
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "native/NativeTools.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace native {

namespace {

using rt::ToolResult;

constexpr size_t kMaxBodyBytes = 256 * 1024;

ToolResult fail(const std::string& m) { return {m, true}; }

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct Url {
    std::string host;
    std::string error;
};

// So aceita https://host[:443]/caminho, sem usuario@, sem IPv6 literal.
Url parseUrl(const std::string& url) {
    Url u;
    if (url.size() > 2048) { u.error = "URL longa demais."; return u; }
    for (unsigned char c : url)
        if (c < 0x21 || c == 0x7f) { u.error = "A URL contem caracteres invalidos (espacos ou controle)."; return u; }
    if (lower(url).rfind("https://", 0) != 0) { u.error = "So URLs https:// sao permitidas."; return u; }
    std::string rest = url.substr(8);
    size_t end = rest.find_first_of("/?#");
    std::string authority = end == std::string::npos ? rest : rest.substr(0, end);
    if (authority.find('@') != std::string::npos) { u.error = "URLs com usuario@ nao sao permitidas."; return u; }
    if (!authority.empty() && authority.front() == '[') { u.error = "Enderecos IPv6 literais nao sao permitidos."; return u; }
    std::string host = authority;
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        if (authority.substr(colon + 1) != "443") { u.error = "So a porta 443 e permitida."; return u; }
    }
    host = lower(host);
    if (host.empty() || host.size() > 253) { u.error = "Host invalido."; return u; }
    for (char c : host)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) { u.error = "Host invalido: " + host; return u; }
    u.host = host;
    return u;
}

bool isPrivateHost(const std::string& host) {
    if (host == "localhost" || host.size() >= 6 && host.compare(host.size() - 6, 6, ".local") == 0 ||
        host.size() >= 9 && host.compare(host.size() - 9, 9, ".internal") == 0 ||
        host.size() >= 10 && host.compare(host.size() - 10, 10, ".localhost") == 0)
        return true;
    // IPv4 literal
    int a[4];
    char tail;
    if (std::sscanf(host.c_str(), "%d.%d.%d.%d%c", &a[0], &a[1], &a[2], &a[3], &tail) == 4) {
        if (a[0] == 10 || a[0] == 127 || a[0] == 0) return true;
        if (a[0] == 169 && a[1] == 254) return true;
        if (a[0] == 172 && a[1] >= 16 && a[1] <= 31) return true;
        if (a[0] == 192 && a[1] == 168) return true;
        if (a[0] == 100 && a[1] >= 64 && a[1] <= 127) return true;   // CGNAT
        return false;
    }
    // so digitos e pontos (ex.: 2130706433, 0x7f.1): formas alternativas de IP, recusadas
    bool digitsOnly = std::all_of(host.begin(), host.end(), [](unsigned char c) { return std::isdigit(c) || c == '.'; });
    return digitsOnly || host.rfind("0x", 0) == 0;
}

bool hostMatches(const std::string& host, const std::string& pattern) {
    std::string p = lower(pattern);
    if (p.rfind("*.", 0) == 0) {
        std::string suffix = p.substr(1);   // ".exemplo.org"
        return host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
    }
    return host == p;
}

std::vector<std::string> allowedHosts(const fs::path& root) {
    std::vector<std::string> out;
    fs::path f = root / ".workspace" / "native" / "http.json";
    std::error_code ec;
    if (!fs::is_regular_file(f, ec)) return out;
    auto parsed = jsonutil::tryParse(jsonutil::readFile(f));
    if (!parsed || !parsed->is_object()) return out;
    const Json& hosts = (*parsed)["allowedHosts"];
    for (size_t i = 0; i < hosts.size(); ++i)
        if (hosts[i].is_string() && hosts[i].str() != "*" && hosts[i].str() != "*.*") out.push_back(hosts[i].str());
    return out;
}

bool validEnvName(const std::string& n) {
    if (n.empty() || n.size() > 128 || !std::isupper(static_cast<unsigned char>(n[0]))) return false;
    return std::all_of(n.begin(), n.end(), [](unsigned char c) { return std::isupper(c) || std::isdigit(c) || c == '_'; });
}

ToolResult send(const Json& a, const Context& ctx, const std::string& method) {
    std::string url = a["url"].is_string() ? a["url"].str() : std::string();
    std::string refusal = http::checkUrl(ctx.workspaceRoot, url);
    if (!refusal.empty()) return fail(refusal);

    rt::HttpRequest req;
    req.method = method;
    req.url = url;
    req.timeoutSeconds = 30;
    req.noRedirects = true;
    req.headers["Accept"] = "application/json, text/plain, text/html;q=0.8, */*;q=0.5";
    req.headers["User-Agent"] = "Arestrix/1.0";

    const Json& headers = a["headers"];
    if (headers.is_object()) {
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            std::string k = it.key();
            std::string kl = lower(k);
            if (kl == "authorization" || kl == "cookie" || kl == "host" || kl.rfind("proxy-", 0) == 0 || kl == "content-length")
                return fail("O cabecalho '" + k + "' nao pode ser definido aqui (use bearer_env para credenciais).");
            if (!std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isalnum(c) || c == '-'; }))
                return fail("Nome de cabecalho invalido: " + k);
            if (!(*it).is_string() || (*it).str().find_first_of("\r\n") != std::string::npos) return fail("Valor de cabecalho invalido: " + k);
            req.headers[k] = (*it).str();
        }
    }
    if (a["bearer_env"].is_string()) {
        const std::string name = a["bearer_env"].str();
        if (!validEnvName(name)) return fail("bearer_env deve ser o NOME de uma variavel de ambiente (ex.: MINHA_API_TOKEN).");
        const char* v = std::getenv(name.c_str());
        if (!v || !*v) return fail("A variavel de ambiente " + name + " nao esta definida nesta maquina.");
        req.headers["Authorization"] = std::string("Bearer ") + v;
    }
    if (method == "POST") {
        const Json& body = a["json"];
        req.body = body.is_string() ? body.str() : body.dump();
        req.headers["Content-Type"] = "application/json";
        if (req.body.size() > kMaxBodyBytes) return fail("Corpo da requisicao grande demais.");
    }

    rt::HttpResponse resp = rt::httpSend(req);
    if (resp.status == 0) return fail("Falha de transporte: " + resp.error);

    std::ostringstream out;
    out << "HTTP " << resp.status << "\n";
    auto ct = resp.headers.find("content-type");
    if (ct != resp.headers.end()) out << "content-type: " << ct->second << "\n";
    if (resp.status >= 300 && resp.status < 400) {
        auto loc = resp.headers.find("location");
        out << "Redirecionamento nao seguido" << (loc != resp.headers.end() ? " (para " + loc->second + ")" : std::string())
            << ". Autorize o novo host em .workspace/native/http.json e chame de novo.\n";
    }
    std::string body = resp.body.size() > kMaxBodyBytes ? resp.body.substr(0, kMaxBodyBytes) : resp.body;
    if (body.find('\0') != std::string::npos) return fail("A resposta parece binaria; so texto e devolvido.");
    out << "\n" << body;
    return {out.str(), resp.status >= 400};
}

} // namespace

namespace http {

std::string checkUrl(const fs::path& root, const std::string& url) {
    Url u = parseUrl(url);
    if (!u.error.empty()) return u.error;
    if (isPrivateHost(u.host)) return "Enderecos locais e privados nao sao permitidos: " + u.host;
    auto hosts = allowedHosts(root);
    if (hosts.empty()) {
        return "Nenhum host autorizado neste projeto. Para permitir requisicoes, crie .workspace/native/http.json com "
               "{\"allowedHosts\": [\"" + u.host + "\"]}.";
    }
    for (const auto& pattern : hosts)
        if (hostMatches(u.host, pattern)) return "";
    return "O host '" + u.host + "' nao esta autorizado neste projeto (.workspace/native/http.json).";
}

std::vector<std::string> readPolicy(const fs::path& root) { return allowedHosts(root); }

// Valida e grava a lista de hosts autorizados (usada pela tela do Studio, nunca pelas ferramentas dos agentes).
bool writePolicy(const fs::path& root, const std::vector<std::string>& requested, std::string& error) {
    std::vector<std::string> clean;
    for (const auto& raw : requested) {
        std::string h = lower(raw);
        h.erase(0, h.find_first_not_of(" \t"));
        h.erase(h.find_last_not_of(" \t") + 1);
        if (h.empty()) continue;
        if (h.rfind("https://", 0) == 0 || h.find('/') != std::string::npos || h.find(':') != std::string::npos) {
            error = "Informe so o nome do host (ex.: api.exemplo.com), sem https:// nem caminho: " + raw;
            return false;
        }
        const bool wildcard = h.rfind("*.", 0) == 0;
        const std::string name = wildcard ? h.substr(2) : h;
        if (name.empty() || name.size() > 253 || name.find("..") != std::string::npos || name.front() == '.' || name.back() == '.' ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '.'; })) {
            error = "Host invalido: " + raw;
            return false;
        }
        if (isPrivateHost(name)) { error = "Enderecos locais e privados nao podem ser autorizados: " + raw; return false; }
        if (wildcard && name.find('.') == std::string::npos) {
            error = "Um curinga precisa de um dominio completo (ex.: *.exemplo.org), nao de uma terminacao como *.com: " + raw;
            return false;
        }
        if (std::find(clean.begin(), clean.end(), h) == clean.end()) clean.push_back(h);
    }
    if (clean.size() > 100) { error = "Hosts demais (limite de 100)."; return false; }
    Json doc = Json::object();
    Json arr = Json::array();
    for (const auto& h : clean) arr.push_back(h);
    doc["allowedHosts"] = arr;
    std::error_code ec;
    fs::path file = root / ".workspace" / "native" / "http.json";
    fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << doc.dump(2);
        if (!out) { error = "Nao foi possivel gravar " + file.string(); return false; }
    }
    fs::rename(tmp, file, ec);
    if (ec) { error = "Nao foi possivel gravar " + file.string() + ": " + ec.message(); return false; }
    return true;
}

} // namespace http

Server makeHttpServer() {
    Server s;
    s.id = std::string(kIdPrefix) + "http";
    s.name = "Requisicoes HTTPS";
    s.description = "Consulta APIs e paginas na internet, somente em hosts que o projeto autorizou (.workspace/native/http.json).";
    s.category = "Rede";

    s.tools.push_back({"http_get", "Faz uma requisicao GET HTTPS a um host autorizado e devolve o status e o texto da resposta.",
                       schema({{"url", "string", "URL https:// completa"},
                               {"headers", "object", "Cabecalhos extras (nao use para credenciais)"},
                               {"bearer_env", "string", "NOME da variavel de ambiente com o token (ex.: MINHA_API_TOKEN)"}}, {"url"}),
                       "moderate", [](const Json& a, const Context& ctx) { return send(a, ctx, "GET"); }});

    s.tools.push_back({"http_post_json", "Faz uma requisicao POST HTTPS com corpo JSON a um host autorizado.",
                       schema({{"url", "string", "URL https:// completa"}, {"json", "object", "Corpo JSON (objeto, ou texto JSON)"},
                               {"headers", "object", "Cabecalhos extras (nao use para credenciais)"},
                               {"bearer_env", "string", "NOME da variavel de ambiente com o token"}}, {"url", "json"}),
                       "high", [](const Json& a, const Context& ctx) { return send(a, ctx, "POST"); }});
    return s;
}

} // namespace native
} // namespace taskforge
