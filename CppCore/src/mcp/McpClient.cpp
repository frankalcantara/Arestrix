#include "McpClient.hpp"

#include <cctype>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

#include "platform/Process.hpp"
#include "rt/Http.hpp"
#include "rt/NetGuard.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace mcp {

using json = taskforge::Json;
using Clock = std::chrono::steady_clock;

namespace {

const char* kProtocolVersion = "2025-03-26";

// ---------------------------------------------------------------------------
// Validacao de entradas nao confiaveis. As configs MCP vivem em arquivos de
// projeto (.workspace/mcps/*.json e arestrix-mcp-servers.json): dado que pode
// ter sido escrito por terceiros e jamais deve ganhar controle do processo.
// ---------------------------------------------------------------------------

// Argumento aceitavel na linha de comando do cmd.exe (npx/npm/uvx sao scripts .cmd no Windows). cmd.exe
// nao tem como escapar '"' dentro de aspas duplas e '%' expande variaveis mesmo dentro de aspas; em vez
// de tentar um escaping sempre incompleto, RECUSA o argumento (falha fechada).
bool safeMcpShellArg(const std::string& a) {
    for (unsigned char c : a) {
        if (c < 0x20 || c == 0x7f) return false;   // controle
        if (c == '"' || c == '&' || c == '|' || c == '<' || c == '>' || c == '^' || c == '%' || c == '!') return false;
    }
    return true;
}

// Politica do transporte HTTP do MCP, espelhando a das ferramentas nativas (native/NativeHttp.cpp):
// somente https:// na porta 443, sem usuario@, sem IPv6 literal, host resolvido e publico (DNS rebinding
// recusado) e sem redirecionamentos. Devolve McpErrorInfo vazio se a URL passar.
McpErrorInfo validateMcpHttpUrl(const std::string& url) {
    McpErrorInfo info;
    auto fail = [&](McpErrorCode code, std::string detail = "") {
        info.set(code, std::move(detail));
        return info;
    };
    if (url.size() > 2048) return fail(McpErrorCode::UrlTooLong);
    for (unsigned char c : url)
        if (c < 0x21 || c == 0x7f) return fail(McpErrorCode::UrlInvalidChars);
    std::string lowerUrl = url;
    for (char& c : lowerUrl) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lowerUrl.rfind("https://", 0) != 0) return fail(McpErrorCode::UrlNotHttps);
    std::string rest = url.substr(8);
    size_t end = rest.find_first_of("/?#");
    std::string authority = end == std::string::npos ? rest : rest.substr(0, end);
    if (authority.find('@') != std::string::npos) return fail(McpErrorCode::UrlUserInfo);
    if (!authority.empty() && authority.front() == '[') return fail(McpErrorCode::UrlIpv6Literal);
    std::string host = authority;
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        std::string port = authority.substr(colon + 1);
        if (port != "443") return fail(McpErrorCode::UrlBadPort, port);
    }
    for (char& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (host.empty() || host.size() > 253) return fail(McpErrorCode::UrlInvalidHost);
    for (char c : host)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.'))
            return fail(McpErrorCode::UrlInvalidHost, host);
    std::string reason = rt::net::checkHostIsPublic(host);
    if (!reason.empty()) return fail(McpErrorCode::UrlNonPublicHost, reason);
    return info;
}

json initializeRequest(int id) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"method", "initialize"},
            {"params", {{"protocolVersion", kProtocolVersion},
                        {"capabilities", json::object()},
                        {"clientInfo", {{"name", "Arestrix"}, {"version", "1.0.0"}}}}}};
}

// Concatena blocos de texto; qualquer outro tipo de bloco vira JSON.
std::string flattenContent(const json& content) {
    if (!content.is_array()) return content.is_null() ? std::string() : content.dump();
    std::string out;
    bool allText = true;
    for (const auto& block : content) {
        if (block.is_object() && block.value("type", "") == "text") {
            if (!out.empty()) out += "\n";
            out += block.value("text", "");
        } else {
            allText = false;
        }
    }
    return allText ? out : content.dump();
}

std::vector<McpTool> parseTools(const json& result, const std::string& serverId) {
    std::vector<McpTool> tools;
    if (result.is_object() && result.contains("tools") && result["tools"].is_array()) {
        for (const auto& item : result["tools"]) {
            McpTool t;
            t.name = item.value("name", "");
            t.description = item.value("description", "");
            if (item.contains("inputSchema")) t.inputSchema = item["inputSchema"];
            t.serverId = serverId;
            tools.push_back(std::move(t));
        }
    }
    return tools;
}

McpCallResult parseCallResult(const json& result) {
    McpCallResult r;
    r.success = true;
    r.isError = result.value("isError", false);
    if (result.contains("content")) r.content = flattenContent(result["content"]);
    if (r.isError && r.errorMessage.empty()) r.errorMessage = r.content;
    return r;
}

// ---------------------------------------------------------------------------
// Transporte stdio: JSON-RPC 2.0 delimitado por linhas
// ---------------------------------------------------------------------------
class StdioSession {
public:
    bool start(const McpServerConfig& cfg, std::string& error,
               McpErrorCode* code = nullptr, std::string* detail = nullptr) {
        auto fail = [&](McpErrorCode c, std::string d = "") {
            error = mcpErrorDefaultMessage(c, d);
            if (code) *code = c;
            if (detail) *detail = std::move(d);
            return false;
        };
        if (cfg.command.empty()) return fail(McpErrorCode::EmptyCommand);
        platform::ProcessSpec spec;
        spec.argv.push_back(cfg.command);
        for (const auto& a : cfg.args) spec.argv.push_back(a);
        spec.env = cfg.env;
        spec.captureStdin = true;
        // O servidor e codigo de terceiros: nao herda as chaves de API do ambiente do usuario
        // (so recebe o que o projeto declarou explicitamente em cfg.env).
        spec.scrubSecrets = true;
#ifdef _WIN32
        // npx/uvx/etc. sao scripts .cmd no Windows: precisam do cmd.exe. Os argumentos viram linha de
        // comando e vem de arquivos do projeto: recusa metacaracteres em vez de tentar escapar.
        if (cfg.command == "npx" || cfg.command == "npm" || cfg.command == "uvx") {
            for (const auto& a : cfg.args) {
                if (!safeMcpShellArg(a)) return fail(McpErrorCode::InvalidArg, a);
            }
            spec.shellCommand = cfg.command;
            for (const auto& a : cfg.args) spec.shellCommand += " \"" + a + "\"";
            spec.argv.clear();
        }
#endif
        proc_ = platform::Process::start(spec, error);
        if (!proc_) {
            // `error` traz o motivo do sistema; registra o codigo e usa o texto como detalhe.
            if (code) *code = McpErrorCode::SpawnFailed;
            if (detail) *detail = error;
        }
        return proc_ != nullptr;
    }

    bool send(const json& msg) { return proc_ && proc_->writeStdin(msg.dump() + "\n"); }

    // Aguarda a resposta com `id`; ignora notificacoes e linhas que nao sejam JSON.
    bool request(int id, const json& msg, int timeoutMs, json& response, std::string& error,
                 McpErrorCode* code = nullptr, std::string* detail = nullptr) {
        auto fail = [&](McpErrorCode c) {
            error = mcpErrorDefaultMessage(c, "");
            if (code) *code = c;
            return false;
        };
        if (!send(msg)) return fail(McpErrorCode::WriteFailed);
        auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (Clock::now() < deadline) {
            size_t nl;
            while ((nl = buffer_.find('\n')) != std::string::npos) {
                std::string line = buffer_.substr(0, nl);
                buffer_.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty() || line.front() != '{') continue;
                auto parsed = jsonutil::tryParse(line);
                if (parsed && parsed->contains("id") && (*parsed)["id"] == id && !parsed->contains("method")) {
                    response = std::move(*parsed);
                    return true;
                }
            }
            auto st = proc_->read(buffer_, 100);
            if (st == platform::ReadStatus::Eof && buffer_.find('\n') == std::string::npos) {
                return fail(McpErrorCode::ServerClosed);
            }
        }
        return fail(McpErrorCode::Timeout);
    }

    bool handshake(int timeoutMs, std::string& error,
                   McpErrorCode* code = nullptr, std::string* detail = nullptr) {
        json resp;
        McpErrorCode innerCode = McpErrorCode::None;
        std::string innerDetail;
        if (!request(1, initializeRequest(1), timeoutMs, resp, error, &innerCode, &innerDetail)) {
            if (code) *code = innerCode;
            if (detail) *detail = std::move(innerDetail);
            return false;
        }
        if (resp.contains("error")) {
            std::string d = resp["error"].dump();
            error = mcpErrorDefaultMessage(McpErrorCode::HandshakeRefused, d);
            if (code) *code = McpErrorCode::HandshakeRefused;
            if (detail) *detail = std::move(d);
            return false;
        }
        send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        return true;
    }

    bool alive() const { return proc_ && proc_->running(); }

    ~StdioSession() { if (proc_) { proc_->closeStdin(); proc_->kill(); } }

private:
    std::unique_ptr<platform::Process> proc_;
    std::string buffer_;
};

// ---------------------------------------------------------------------------
// Transporte HTTP (Streamable HTTP do MCP): POST unico, resposta JSON ou SSE
// ---------------------------------------------------------------------------
class HttpSession {
public:
    explicit HttpSession(std::string url) : url_(std::move(url)) {}

    bool post(const json& msg, int timeoutMs, json* response, std::string& error,
              McpErrorCode* code = nullptr, std::string* detail = nullptr) {
        rt::HttpRequest req;
        req.url = url_;
        req.timeoutSeconds = std::max(5, timeoutMs / 1000);
        req.noRedirects = true;          // nenhum redirecionamento: cada destino precisaria ser validado de novo
        req.requirePublicPeer = true;    // protecao contra DNS rebinding (rt/NetGuard.hpp)
        req.headers["Content-Type"] = "application/json";
        req.headers["Accept"] = "application/json, text/event-stream";
        if (!sessionId_.empty()) req.headers["Mcp-Session-Id"] = sessionId_;
        if (initialized_) req.headers["MCP-Protocol-Version"] = kProtocolVersion;
        req.body = msg.dump();

        auto resp = rt::httpSend(req);
        if (resp.status == 0) {
            error = "Falha de transporte: " + resp.error;
            if (code) *code = McpErrorCode::TransportFailed;
            if (detail) *detail = resp.error;
            return false;
        }
        if (resp.status == 202 || (resp.status == 200 && resp.body.empty())) return !response;
        if (resp.status >= 400) {
            error = "HTTP " + std::to_string(resp.status) + ": " + resp.body.substr(0, 300);
            bool legacy = resp.status == 404 || resp.status == 405;
            if (legacy) {
                error += " (o endpoint nao parece falar MCP Streamable HTTP; servidores SSE legados nao sao suportados)";
            }
            if (code) *code = legacy ? McpErrorCode::LegacySseUnsupported : McpErrorCode::InvalidResponse;
            if (detail) *detail = "HTTP " + std::to_string(resp.status);
            return false;
        }
        auto sid = resp.headers.find("mcp-session-id");
        if (sid != resp.headers.end()) sessionId_ = sid->second;
        if (!response) return true;

        int wantId = msg.value("id", -1);
        auto ct = resp.headers.find("content-type");
        bool sse = ct != resp.headers.end() && ct->second.find("text/event-stream") != std::string::npos;
        if (!sse) {
            auto parsed = jsonutil::tryParse(resp.body);
            if (!parsed) {
                error = mcpErrorDefaultMessage(McpErrorCode::InvalidResponse, "");
                if (code) *code = McpErrorCode::InvalidResponse;
                return false;
            }
            *response = *parsed;
            return true;
        }
        // text/event-stream: cada evento tem linhas "data: {...}"
        std::istringstream in(resp.body);
        std::string line, data;
        auto flush = [&]() {
            if (data.empty()) return false;
            auto parsed = jsonutil::tryParse(data);
            data.clear();
            if (parsed && parsed->contains("id") && (*parsed)["id"] == wantId) { *response = *parsed; return true; }
            return false;
        };
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) == 0) {
                std::string v = line.substr(5);
                if (!v.empty() && v.front() == ' ') v.erase(v.begin());
                data += v;
            } else if (line.empty()) {
                if (flush()) return true;
            }
        }
        if (flush()) return true;
        error = mcpErrorDefaultMessage(McpErrorCode::SseNoResult, "");
        if (code) *code = McpErrorCode::SseNoResult;
        return false;
    }

    bool handshake(int timeoutMs, std::string& error,
                   McpErrorCode* code = nullptr, std::string* detail = nullptr) {
        json resp;
        McpErrorCode innerCode = McpErrorCode::None;
        std::string innerDetail;
        if (!post(initializeRequest(1), timeoutMs, &resp, error, &innerCode, &innerDetail)) {
            if (code) *code = innerCode;
            if (detail) *detail = std::move(innerDetail);
            return false;
        }
        if (resp.contains("error")) {
            std::string d = resp["error"].dump();
            error = mcpErrorDefaultMessage(McpErrorCode::HandshakeRefused, d);
            if (code) *code = McpErrorCode::HandshakeRefused;
            if (detail) *detail = std::move(d);
            return false;
        }
        initialized_ = true;
        std::string ignored;
        post({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}}, timeoutMs, nullptr, ignored);
        return true;
    }

private:
    std::string url_;
    std::string sessionId_;
    bool initialized_ = false;
};

} // namespace

// ---------------------------------------------------------------------------
// Servidores de demonstracao (somente quando solicitados explicitamente)
// ---------------------------------------------------------------------------
bool McpClient::isMockServer(const McpServerConfig& config) const {
    return config.command == "mock" || config.id.rfind("mock", 0) == 0;
}

std::vector<McpTool> McpClient::getMockTools(const McpServerConfig& config) const {
    std::vector<McpTool> tools;
    auto add = [&](const std::string& name, const std::string& desc, const json& schema) {
        McpTool t;
        t.name = name;
        t.description = "[mock] " + desc;
        t.inputSchema = schema;
        t.serverId = config.id;
        tools.push_back(std::move(t));
    };
    add("read_file", "Le um arquivo (simulado)", {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}});
    add("write_file", "Grava um arquivo (simulado)", {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}}});
    add("list_directory", "Lista um diretorio (simulado)", {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}});
    add("fetch_url", "Busca uma URL (simulado)", {{"type", "object"}, {"properties", {{"url", {{"type", "string"}}}}}});
    add("execute_query", "Executa SQL (simulado)", {{"type", "object"}, {"properties", {{"query", {{"type", "string"}}}}}});
    return tools;
}

McpCallResult McpClient::executeMockTool(const std::string& toolName, const json& args) const {
    McpCallResult res;
    res.success = true;
    res.content = "[Mock MCP] Ferramenta '" + toolName + "' simulada. Argumentos: " + args.dump() +
                  ". Nenhum servidor real foi contatado.";
    return res;
}

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------
std::vector<McpTool> McpClient::listTools(const McpServerConfig& config, int timeoutMs, McpErrorInfo* errorInfo) {
    if (isMockServer(config)) return getMockTools(config);
    return config.transport == McpTransportType::Stdio ? listToolsStdio(config, timeoutMs, errorInfo)
                                                       : listToolsSse(config, timeoutMs, errorInfo);
}

McpCallResult McpClient::callTool(const McpServerConfig& config, const std::string& toolName,
                                  const json& arguments, int timeoutMs) {
    if (isMockServer(config)) return executeMockTool(toolName, arguments);
    return config.transport == McpTransportType::Stdio ? callToolStdio(config, toolName, arguments, timeoutMs)
                                                       : callToolSse(config, toolName, arguments, timeoutMs);
}

// ---------------------------------------------------------------------------
// Sessoes persistentes
//
// Abrir um processo MCP e fazer o handshake a cada chamada custa dezenas a centenas de milissegundos. Um app gerado
// a partir de um grafo faz muitas chamadas, entao cada servidor tem UMA sessao mantida aberta durante a vida do
// processo e reutilizada por todas as threads (chamadas ao mesmo servidor sao serializadas; servidores diferentes
// rodam em paralelo). A sessao e recriada se o servidor morreu, se a configuracao mudou ou depois de qualquer falha
// de transporte. Uma chamada que falhou NUNCA e repetida automaticamente: ela pode ja ter tido efeito.
// ---------------------------------------------------------------------------
namespace {

struct StdioEntry {
    std::mutex mutex;
    std::unique_ptr<StdioSession> session;
    std::string fingerprint;
    int nextId = 2;
};

struct HttpEntry {
    std::mutex mutex;
    std::unique_ptr<HttpSession> session;
    std::string fingerprint;
    int nextId = 2;
};

struct SessionPool {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<StdioEntry>> stdio;
    std::map<std::string, std::unique_ptr<HttpEntry>> http;
};

SessionPool& pool() {
    static SessionPool p;
    return p;
}

std::string fingerprintOf(const McpServerConfig& c) {
    std::string s = c.command + '\x1f' + c.url;
    for (const auto& a : c.args) s += '\x1e' + a;
    for (const auto& [k, v] : c.env) s += '\x1d' + k + '=' + v;
    return s;
}

StdioEntry& stdioEntry(const std::string& key) {
    std::lock_guard<std::mutex> lock(pool().mutex);
    auto& slot = pool().stdio[key];
    if (!slot) slot = std::make_unique<StdioEntry>();
    return *slot;
}

HttpEntry& httpEntry(const std::string& key) {
    std::lock_guard<std::mutex> lock(pool().mutex);
    auto& slot = pool().http[key];
    if (!slot) slot = std::make_unique<HttpEntry>();
    return *slot;
}

bool stdioRequest(const McpServerConfig& cfg, const std::string& method, const json& params, int timeoutMs, json& resp,
                  std::string& error, McpErrorInfo* info = nullptr) {
    StdioEntry& e = stdioEntry(cfg.id);
    std::lock_guard<std::mutex> lock(e.mutex);
    const std::string fp = fingerprintOf(cfg);
    if (e.session && (e.fingerprint != fp || !e.session->alive())) e.session.reset();   // morreu ou mudou: recria
    if (!e.session) {
        auto s = std::make_unique<StdioSession>();
        McpErrorCode code = McpErrorCode::None;
        std::string detail;
        if (!s->start(cfg, error, &code, &detail) || !s->handshake(timeoutMs, error, &code, &detail)) {
            if (info) info->set(code, detail, error);
            return false;
        }
        e.session = std::move(s);
        e.fingerprint = fp;
        e.nextId = 2;
    }
    const int id = e.nextId++;
    json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
    McpErrorCode code = McpErrorCode::None;
    std::string detail;
    if (!e.session->request(id, msg, timeoutMs, resp, error, &code, &detail)) {
        e.session.reset();   // estado desconhecido: a proxima chamada abre outra sessao
        if (info) info->set(code, detail, error);
        return false;
    }
    return true;
}

bool httpRequest(const McpServerConfig& cfg, const std::string& method, const json& params, int timeoutMs, json& resp,
                 std::string& error, McpErrorInfo* info = nullptr) {
    // A URL vem de arquivos do projeto: valida ANTES de qualquer conexao (https apenas, host publico, sem rebinding).
    McpErrorInfo urlInfo = validateMcpHttpUrl(cfg.url);
    if (!urlInfo.empty()) {
        error = urlInfo.message;
        if (info) *info = urlInfo;
        return false;
    }
    HttpEntry& e = httpEntry(cfg.id);
    std::lock_guard<std::mutex> lock(e.mutex);
    const std::string fp = fingerprintOf(cfg);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (e.session && e.fingerprint != fp) e.session.reset();
        McpErrorCode code = McpErrorCode::None;
        std::string detail;
        if (!e.session) {
            auto s = std::make_unique<HttpSession>(cfg.url);
            if (!s->handshake(timeoutMs, error, &code, &detail)) {
                if (info) info->set(code, detail, error);
                return false;
            }
            e.session = std::move(s);
            e.fingerprint = fp;
            e.nextId = 2;
        }
        const int id = e.nextId++;
        json msg = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
        if (e.session->post(msg, timeoutMs, &resp, error, &code, &detail)) return true;
        e.session.reset();
        if (info) info->set(code, detail, error);
        // Sessao expirada no servidor (HTTP 404): o pedido nao foi processado, entao e seguro tentar uma vez com sessao nova.
        if (attempt == 1 || error.find("HTTP 404") == std::string::npos) return false;
    }
    return false;
}

} // namespace

void McpClient::shutdownSessions() {
    std::lock_guard<std::mutex> lock(pool().mutex);
    pool().stdio.clear();   // destrutores encerram os processos filhos
    pool().http.clear();
}

size_t McpClient::activeSessions() {
    std::lock_guard<std::mutex> lock(pool().mutex);
    size_t n = 0;
    for (auto& [id, e] : pool().stdio) {
        std::lock_guard<std::mutex> l(e->mutex);
        if (e->session && e->session->alive()) ++n;
    }
    for (auto& [id, e] : pool().http) {
        std::lock_guard<std::mutex> l(e->mutex);
        if (e->session) ++n;
    }
    return n;
}

namespace {

std::vector<McpTool> toolsFrom(bool ok, const json& resp, const std::string& error, const McpServerConfig& cfg,
                               const McpErrorInfo* info) {
    if (!ok) {
        std::cerr << "[MCP] " << cfg.id << ": " << (info && !info->message.empty() ? info->message : error) << "\n";
        return {};
    }
    return resp.contains("result") ? parseTools(resp["result"], cfg.id) : std::vector<McpTool>{};
}

McpCallResult callFrom(bool ok, const json& resp, const std::string& error, Clock::time_point t0,
                       const McpErrorInfo* info) {
    McpCallResult out;
    if (!ok) {
        out.isError = true;
        if (info && !info->empty()) {
            out.errorCode = info->codeName();
            out.errorDetail = info->detail;
            out.errorMessage = info->message.empty() ? error : info->message;
        } else {
            out.errorMessage = error;
        }
    } else if (resp.contains("error")) {
        out.isError = true;
        out.errorMessage = resp["error"].dump();
    } else if (resp.contains("result")) {
        out = parseCallResult(resp["result"]);
    }
    out.durationSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return out;
}

} // namespace

std::vector<McpTool> McpClient::listToolsStdio(const McpServerConfig& config, int timeoutMs, McpErrorInfo* errorInfo) {
    json resp;
    std::string err;
    McpErrorInfo info;
    bool ok = stdioRequest(config, "tools/list", json::object(), timeoutMs, resp, err, &info);
    if (errorInfo) *errorInfo = info;
    return toolsFrom(ok, resp, err, config, &info);
}

McpCallResult McpClient::callToolStdio(const McpServerConfig& config, const std::string& toolName,
                                       const json& arguments, int timeoutMs) {
    auto t0 = Clock::now();
    json resp;
    std::string err;
    McpErrorInfo info;
    bool ok = stdioRequest(config, "tools/call", {{"name", toolName}, {"arguments", arguments}}, timeoutMs, resp, err, &info);
    return callFrom(ok, resp, err, t0, &info);
}

std::vector<McpTool> McpClient::listToolsSse(const McpServerConfig& config, int timeoutMs, McpErrorInfo* errorInfo) {
    json resp;
    std::string err;
    McpErrorInfo info;
    bool ok;
    if (config.url.empty()) {
        info.set(McpErrorCode::UrlEmpty);
        err = info.message;
        ok = false;
    } else {
        ok = httpRequest(config, "tools/list", json::object(), timeoutMs, resp, err, &info);
    }
    if (errorInfo) *errorInfo = info;
    return toolsFrom(ok, resp, err, config, &info);
}

McpCallResult McpClient::callToolSse(const McpServerConfig& config, const std::string& toolName,
                                     const json& arguments, int timeoutMs) {
    auto t0 = Clock::now();
    if (config.url.empty()) {
        McpCallResult out;
        out.isError = true;
        out.errorCode = mcpErrorCodeName(McpErrorCode::UrlEmpty);
        out.errorMessage = mcpErrorDefaultMessage(McpErrorCode::UrlEmpty, "");
        return out;
    }
    json resp;
    std::string err;
    McpErrorInfo info;
    bool ok = httpRequest(config, "tools/call", {{"name", toolName}, {"arguments", arguments}}, timeoutMs, resp, err, &info);
    return callFrom(ok, resp, err, t0, &info);
}

} // namespace mcp
} // namespace taskforge
