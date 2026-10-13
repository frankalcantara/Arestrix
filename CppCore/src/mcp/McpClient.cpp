#include "McpClient.hpp"

#include <chrono>
#include <iostream>
#include <sstream>

#include "platform/Process.hpp"
#include "rt/Http.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace mcp {

using json = taskforge::Json;
using Clock = std::chrono::steady_clock;

namespace {

const char* kProtocolVersion = "2025-03-26";

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
    bool start(const McpServerConfig& cfg, std::string& error) {
        if (cfg.command.empty()) { error = "Comando do servidor MCP nao configurado."; return false; }
        platform::ProcessSpec spec;
        spec.argv.push_back(cfg.command);
        for (const auto& a : cfg.args) spec.argv.push_back(a);
        spec.env = cfg.env;
        spec.captureStdin = true;
#ifdef _WIN32
        // npx/uvx/etc. sao scripts .cmd no Windows: precisam do cmd.exe.
        if (cfg.command == "npx" || cfg.command == "npm" || cfg.command == "uvx") {
            spec.shellCommand = cfg.command;
            for (const auto& a : cfg.args) spec.shellCommand += " \"" + a + "\"";
            spec.argv.clear();
        }
#endif
        proc_ = platform::Process::start(spec, error);
        return proc_ != nullptr;
    }

    bool send(const json& msg) { return proc_ && proc_->writeStdin(msg.dump() + "\n"); }

    // Aguarda a resposta com `id`; ignora notificacoes e linhas que nao sejam JSON.
    bool request(int id, const json& msg, int timeoutMs, json& response, std::string& error) {
        if (!send(msg)) { error = "Falha ao escrever no stdin do servidor MCP."; return false; }
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
                error = "O servidor MCP encerrou antes de responder.";
                return false;
            }
        }
        error = "Timeout aguardando resposta do servidor MCP.";
        return false;
    }

    bool handshake(int timeoutMs, std::string& error) {
        json resp;
        if (!request(1, initializeRequest(1), timeoutMs, resp, error)) return false;
        if (resp.contains("error")) { error = "initialize recusado: " + resp["error"].dump(); return false; }
        send({{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        return true;
    }

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

    bool post(const json& msg, int timeoutMs, json* response, std::string& error) {
        rt::HttpRequest req;
        req.url = url_;
        req.timeoutSeconds = std::max(5, timeoutMs / 1000);
        req.headers["Content-Type"] = "application/json";
        req.headers["Accept"] = "application/json, text/event-stream";
        if (!sessionId_.empty()) req.headers["Mcp-Session-Id"] = sessionId_;
        if (initialized_) req.headers["MCP-Protocol-Version"] = kProtocolVersion;
        req.body = msg.dump();

        auto resp = rt::httpSend(req);
        if (resp.status == 0) { error = "Falha de transporte: " + resp.error; return false; }
        if (resp.status == 202 || (resp.status == 200 && resp.body.empty())) return !response;
        if (resp.status >= 400) {
            error = "HTTP " + std::to_string(resp.status) + ": " + resp.body.substr(0, 300);
            if (resp.status == 404 || resp.status == 405) {
                error += " (o endpoint nao parece falar MCP Streamable HTTP; servidores SSE legados nao sao suportados)";
            }
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
            if (!parsed) { error = "Resposta invalida do servidor MCP."; return false; }
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
        error = "A resposta SSE nao trouxe o resultado da chamada.";
        return false;
    }

    bool handshake(int timeoutMs, std::string& error) {
        json resp;
        if (!post(initializeRequest(1), timeoutMs, &resp, error)) return false;
        if (resp.contains("error")) { error = "initialize recusado: " + resp["error"].dump(); return false; }
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
std::vector<McpTool> McpClient::listTools(const McpServerConfig& config, int timeoutMs) {
    if (isMockServer(config)) return getMockTools(config);
    return config.transport == McpTransportType::Stdio ? listToolsStdio(config, timeoutMs)
                                                       : listToolsSse(config, timeoutMs);
}

McpCallResult McpClient::callTool(const McpServerConfig& config, const std::string& toolName,
                                  const json& arguments, int timeoutMs) {
    if (isMockServer(config)) return executeMockTool(toolName, arguments);
    return config.transport == McpTransportType::Stdio ? callToolStdio(config, toolName, arguments, timeoutMs)
                                                       : callToolSse(config, toolName, arguments, timeoutMs);
}

std::vector<McpTool> McpClient::listToolsStdio(const McpServerConfig& config, int timeoutMs) {
    StdioSession s;
    std::string err;
    if (!s.start(config, err) || !s.handshake(timeoutMs, err)) {
        std::cerr << "[MCP] " << config.id << ": " << err << "\n";
        return {};
    }
    json resp;
    if (!s.request(2, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", json::object()}}, timeoutMs, resp, err)) {
        std::cerr << "[MCP] " << config.id << ": " << err << "\n";
        return {};
    }
    return resp.contains("result") ? parseTools(resp["result"], config.id) : std::vector<McpTool>{};
}

McpCallResult McpClient::callToolStdio(const McpServerConfig& config, const std::string& toolName,
                                       const json& arguments, int timeoutMs) {
    McpCallResult out;
    auto t0 = Clock::now();
    auto finish = [&]() {
        out.durationSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
        return out;
    };

    StdioSession s;
    std::string err;
    if (!s.start(config, err) || !s.handshake(timeoutMs, err)) {
        out.isError = true;
        out.errorMessage = err;
        return finish();
    }
    json resp;
    json call = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
                 {"params", {{"name", toolName}, {"arguments", arguments}}}};
    if (!s.request(2, call, timeoutMs, resp, err)) {
        out.isError = true;
        out.errorMessage = err;
        return finish();
    }
    if (resp.contains("error")) {
        out.isError = true;
        out.errorMessage = resp["error"].dump();
    } else if (resp.contains("result")) {
        out = parseCallResult(resp["result"]);
    }
    return finish();
}

std::vector<McpTool> McpClient::listToolsSse(const McpServerConfig& config, int timeoutMs) {
    if (config.url.empty()) return {};
    HttpSession s(config.url);
    std::string err;
    if (!s.handshake(timeoutMs, err)) {
        std::cerr << "[MCP] " << config.id << ": " << err << "\n";
        return {};
    }
    json resp;
    if (!s.post({{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}, {"params", json::object()}}, timeoutMs, &resp, err)) {
        std::cerr << "[MCP] " << config.id << ": " << err << "\n";
        return {};
    }
    return resp.contains("result") ? parseTools(resp["result"], config.id) : std::vector<McpTool>{};
}

McpCallResult McpClient::callToolSse(const McpServerConfig& config, const std::string& toolName,
                                     const json& arguments, int timeoutMs) {
    McpCallResult out;
    auto t0 = Clock::now();
    auto finish = [&]() {
        out.durationSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
        return out;
    };
    if (config.url.empty()) {
        out.isError = true;
        out.errorMessage = "URL do servidor MCP nao configurada.";
        return finish();
    }
    HttpSession s(config.url);
    std::string err;
    if (!s.handshake(timeoutMs, err)) {
        out.isError = true;
        out.errorMessage = err;
        return finish();
    }
    json resp;
    json call = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/call"},
                 {"params", {{"name", toolName}, {"arguments", arguments}}}};
    if (!s.post(call, timeoutMs, &resp, err)) {
        out.isError = true;
        out.errorMessage = err;
        return finish();
    }
    if (resp.contains("error")) {
        out.isError = true;
        out.errorMessage = resp["error"].dump();
    } else if (resp.contains("result")) {
        out = parseCallResult(resp["result"]);
    }
    return finish();
}

} // namespace mcp
} // namespace taskforge
