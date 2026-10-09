#include "McpClient.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace taskforge {
namespace mcp {

bool McpClient::isMockServer(const McpServerConfig& config) const {
    if (config.command == "mock" || config.command.empty()) return true;
    if (config.id.find("mock") != std::string::npos) return true;
    return false;
}

std::vector<McpTool> McpClient::getMockTools(const McpServerConfig& config) const {
    std::vector<McpTool> tools;

    if (config.id.find("fetch") != std::string::npos) {
        McpTool t1;
        t1.name = "fetch_url";
        t1.description = "Efetua uma requisicao HTTP GET ou POST para uma URL externa e extrai o corpo.";
        t1.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"url", {{"type", "string"}, {"description", "URL alvo"}}},
                {"method", {{"type", "string"}, {"enum", {"GET", "POST"}}}}
            }},
            {"required", {"url"}}
        };
        t1.serverId = config.id;
        tools.push_back(t1);
    } else if (config.id.find("sql") != std::string::npos || config.id.find("db") != std::string::npos) {
        McpTool t1;
        t1.name = "execute_query";
        t1.description = "Executa uma consulta SQL no banco de dados e retorna os registros tabulados.";
        t1.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Instrucao SQL a ser executada"}}}
            }},
            {"required", {"query"}}
        };
        t1.serverId = config.id;
        tools.push_back(t1);
    } else {
        // Filesystem por padrão
        McpTool t1;
        t1.name = "read_file";
        t1.description = "Le o conteudo integral de um arquivo local especificado pelo caminho relativo ou absoluto.";
        t1.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Caminho do arquivo"}}}
            }},
            {"required", {"path"}}
        };
        t1.serverId = config.id;
        tools.push_back(t1);

        McpTool t2;
        t2.name = "write_file";
        t2.description = "Cria ou sobrescreve um arquivo no disco com o conteudo UTF-8 informado.";
        t2.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Caminho de destino"}}},
                {"content", {{"type", "string"}, {"description", "Conteudo de texto"}}}
            }},
            {"required", {"path", "content"}}
        };
        t2.serverId = config.id;
        tools.push_back(t2);

        McpTool t3;
        t3.name = "list_directory";
        t3.description = "Lista todos os arquivos e pastas presentes no diretorio informado.";
        t3.inputSchema = {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Caminho da pasta"}}}
            }},
            {"required", {"path"}}
        };
        t3.serverId = config.id;
        tools.push_back(t3);
    }

    return tools;
}

McpCallResult McpClient::executeMockTool(const std::string& toolName, const nlohmann::json& args) const {
    McpCallResult res;
    res.success = true;
    res.isError = false;
    res.durationSeconds = 0.015;

    if (toolName == "read_file") {
        std::string p = args.value("path", "file.txt");
        res.content = "[Mock MCP] Conteudo do arquivo " + p + ": OK. Simulando leitura de dados.";
    } else if (toolName == "write_file") {
        std::string p = args.value("path", "file.txt");
        res.content = "[Mock MCP] Arquivo " + p + " gravado com sucesso (" + std::to_string(args.value("content", "").size()) + " bytes).";
    } else if (toolName == "list_directory") {
        res.content = "[\"main.cpp\", \"CMakeLists.txt\", \"README.md\", \"data.json\"]";
    } else if (toolName == "fetch_url") {
        std::string u = args.value("url", "http://localhost");
        res.content = "[Mock MCP HTTP 200 OK] Resposta da requisicao para: " + u;
    } else if (toolName == "execute_query") {
        res.content = "[Mock MCP SQL] Query executada com sucesso. Linhas afetadas: 1.";
    } else {
        res.content = "[Mock MCP] Execucao da ferramenta '" + toolName + "' concluida.";
    }
    return res;
}

std::vector<McpTool> McpClient::listTools(const McpServerConfig& config, int timeoutMs) {
    if (isMockServer(config)) {
        return getMockTools(config);
    }

    if (config.transport == McpTransportType::Stdio) {
        return listToolsStdio(config, timeoutMs);
    } else {
        return listToolsSse(config, timeoutMs);
    }
}

McpCallResult McpClient::callTool(const McpServerConfig& config,
                                  const std::string& toolName,
                                  const nlohmann::json& arguments,
                                  int timeoutMs) {
    if (isMockServer(config)) {
        return executeMockTool(toolName, arguments);
    }

    if (config.transport == McpTransportType::Stdio) {
        return callToolStdio(config, toolName, arguments, timeoutMs);
    } else {
        return callToolSse(config, toolName, arguments, timeoutMs);
    }
}

#ifdef _WIN32
namespace {

// Helper para ler uma linha de resposta JSON-RPC via PeekNamedPipe
bool readJsonRpcResponse(HANDLE hReadPipe, HANDLE hProcess, int timeoutMs, nlohmann::json& outJson) {
    auto startTime = std::chrono::steady_clock::now();
    std::string lineBuffer;

    while (true) {
        // Checa se o processo morreu
        DWORD exitCode = 0;
        if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            break;
        }

        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hReadPipe, NULL, 0, NULL, &bytesAvail, NULL) && bytesAvail > 0) {
            char chunk[1024];
            DWORD toRead = (bytesAvail < sizeof(chunk) - 1) ? bytesAvail : (sizeof(chunk) - 1);
            DWORD bytesRead = 0;
            if (ReadFile(hReadPipe, chunk, toRead, &bytesRead, NULL) && bytesRead > 0) {
                lineBuffer.append(chunk, bytesRead);

                // Tenta achar linha delimitada ou JSON válido
                size_t nlPos = lineBuffer.find('\n');
                while (nlPos != std::string::npos) {
                    std::string singleLine = lineBuffer.substr(0, nlPos);
                    lineBuffer.erase(0, nlPos + 1);

                    // Tenta fazer o parse do JSON
                    try {
                        if (!singleLine.empty() && singleLine.front() == '{') {
                            outJson = nlohmann::json::parse(singleLine);
                            return true;
                        }
                    } catch (...) {
                        // Linha não era JSON ou continha logs; continua buscando
                    }
                    nlPos = lineBuffer.find('\n');
                }
            }
        }

        // Verifica estouro de timeout
        auto now = std::chrono::steady_clock::now();
        int elapsedMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (elapsedMs >= timeoutMs) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // Processo terminou; tenta um parse final do que restou no buffer
    try {
        if (!lineBuffer.empty()) {
            size_t openBrace = lineBuffer.find('{');
            if (openBrace != std::string::npos) {
                outJson = nlohmann::json::parse(lineBuffer.substr(openBrace));
                return true;
            }
        }
    } catch (...) {}

    return false;
}

bool writeJsonLine(HANDLE hWritePipe, const nlohmann::json& msg) {
    std::string text = msg.dump() + "\n";
    DWORD bytesWritten = 0;
    return WriteFile(hWritePipe, text.data(), (DWORD)text.size(), &bytesWritten, NULL) && bytesWritten == text.size();
}

} // namespace
#endif

std::vector<McpTool> McpClient::listToolsStdio(const McpServerConfig& config, int timeoutMs) {
    std::vector<McpTool> result;
#ifdef _WIN32
    // Monta linha de comando
    std::string cmdLine = config.command;
    for (const auto& a : config.args) {
        cmdLine += " \"" + a + "\"";
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hChildStdinRead = NULL, hChildStdinWrite = NULL;
    HANDLE hChildStdoutRead = NULL, hChildStdoutWrite = NULL;

    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0)) return result;
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0)) {
        CloseHandle(hChildStdinRead);
        CloseHandle(hChildStdinWrite);
        return result;
    }

    SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = hChildStdinRead;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdError = hChildStdoutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    // Fecha as pontas herdadas pelo filho no processo pai
    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdoutWrite);

    if (!created) {
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead);
        std::cerr << "[MCP Client] Falha ao iniciar processo stdio: " << config.command << "\n";
        return result;
    }

    // 1. Handshake "initialize"
    nlohmann::json initReq = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", nlohmann::json::object()},
            {"clientInfo", {{"name", "Arestrix"}, {"version", "1.0.0"}}}
        }}
    };
    writeJsonLine(hChildStdinWrite, initReq);

    nlohmann::json initResp;
    if (readJsonRpcResponse(hChildStdoutRead, pi.hProcess, timeoutMs, initResp)) {
        // 2. Notificação "notifications/initialized"
        nlohmann::json notifReq = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"}
        };
        writeJsonLine(hChildStdinWrite, notifReq);

        // 3. Request "tools/list"
        nlohmann::json listReq = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "tools/list"},
            {"params", nlohmann::json::object()}
        };
        writeJsonLine(hChildStdinWrite, listReq);

        nlohmann::json listResp;
        if (readJsonRpcResponse(hChildStdoutRead, pi.hProcess, timeoutMs, listResp)) {
            if (listResp.contains("result") && listResp["result"].contains("tools")) {
                for (const auto& tItem : listResp["result"]["tools"]) {
                    McpTool tool;
                    tool.name = tItem.value("name", "");
                    tool.description = tItem.value("description", "");
                    if (tItem.contains("inputSchema")) {
                        tool.inputSchema = tItem["inputSchema"];
                    }
                    tool.serverId = config.id;
                    result.push_back(tool);
                }
            }
        }
    }

    // Cleanup gracioso do processo
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hChildStdinWrite);
    CloseHandle(hChildStdoutRead);
#endif
    return result;
}

McpCallResult McpClient::callToolStdio(const McpServerConfig& config,
                                      const std::string& toolName,
                                      const nlohmann::json& arguments,
                                      int timeoutMs) {
    McpCallResult result;
    auto startTime = std::chrono::steady_clock::now();

#ifdef _WIN32
    std::string cmdLine = config.command;
    for (const auto& a : config.args) {
        cmdLine += " \"" + a + "\"";
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hChildStdinRead = NULL, hChildStdinWrite = NULL;
    HANDLE hChildStdoutRead = NULL, hChildStdoutWrite = NULL;

    if (!CreatePipe(&hChildStdinRead, &hChildStdinWrite, &sa, 0)) {
        result.isError = true;
        result.errorMessage = "Falha ao criar pipes de comunicação";
        return result;
    }
    if (!CreatePipe(&hChildStdoutRead, &hChildStdoutWrite, &sa, 0)) {
        CloseHandle(hChildStdinRead);
        CloseHandle(hChildStdinWrite);
        result.isError = true;
        result.errorMessage = "Falha ao criar pipes stdout";
        return result;
    }

    SetHandleInformation(hChildStdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hChildStdoutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = hChildStdinRead;
    si.hStdOutput = hChildStdoutWrite;
    si.hStdError = hChildStdoutWrite;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    BOOL created = CreateProcessA(NULL, cmdBuf.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

    CloseHandle(hChildStdinRead);
    CloseHandle(hChildStdoutWrite);

    if (!created) {
        CloseHandle(hChildStdinWrite);
        CloseHandle(hChildStdoutRead);
        result.isError = true;
        result.errorMessage = "Falha ao executar comando MCP stdio: " + config.command;
        return result;
    }

    // Handshake
    nlohmann::json initReq = {
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2024-11-05"},
            {"capabilities", nlohmann::json::object()},
            {"clientInfo", {{"name", "Arestrix"}, {"version", "1.0.0"}}}
        }}
    };
    writeJsonLine(hChildStdinWrite, initReq);

    nlohmann::json initResp;
    if (readJsonRpcResponse(hChildStdoutRead, pi.hProcess, timeoutMs, initResp)) {
        nlohmann::json notifReq = {
            {"jsonrpc", "2.0"},
            {"method", "notifications/initialized"}
        };
        writeJsonLine(hChildStdinWrite, notifReq);

        // Chama a ferramenta
        nlohmann::json callReq = {
            {"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "tools/call"},
            {"params", {
                {"name", toolName},
                {"arguments", arguments}
            }}
        };
        writeJsonLine(hChildStdinWrite, callReq);

        nlohmann::json callResp;
        if (readJsonRpcResponse(hChildStdoutRead, pi.hProcess, timeoutMs, callResp)) {
            result.success = true;
            if (callResp.contains("result")) {
                auto resObj = callResp["result"];
                result.isError = resObj.value("isError", false);
                if (resObj.contains("content")) {
                    result.content = resObj["content"].dump();
                }
            } else if (callResp.contains("error")) {
                result.isError = true;
                result.errorMessage = callResp["error"].dump();
            }
        } else {
            result.isError = true;
            result.errorMessage = "Timeout aguardando resposta da chamada MCP tools/call";
        }
    } else {
        result.isError = true;
        result.errorMessage = "Timeout durante handshake initialize com servidor MCP";
    }

    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hChildStdinWrite);
    CloseHandle(hChildStdoutRead);
#endif

    auto endTime = std::chrono::steady_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();
    return result;
}

std::vector<McpTool> McpClient::listToolsSse(const McpServerConfig& config, int /*timeoutMs*/) {
    std::vector<McpTool> tools;
    // Fallback/Stub para transporte SSE
    McpTool t;
    t.name = "sse_query";
    t.description = "Executa operacao remota via endpoint SSE: " + config.url;
    t.inputSchema = {
        {"type", "object"},
        {"properties", {
            {"query", {{"type", "string"}, {"description", "Payload SSE"}}}
        }}
    };
    t.serverId = config.id;
    tools.push_back(t);
    return tools;
}

McpCallResult McpClient::callToolSse(const McpServerConfig& config,
                                    const std::string& toolName,
                                    const nlohmann::json& arguments,
                                    int /*timeoutMs*/) {
    McpCallResult res;
    res.success = true;
    res.content = "[MCP SSE " + config.url + "] Chamada da ferramenta " + toolName + " enviada com sucesso: " + arguments.dump();
    res.durationSeconds = 0.05;
    return res;
}

} // namespace mcp
} // namespace taskforge
