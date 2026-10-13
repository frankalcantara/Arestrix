#pragma once

#include "McpTypes.hpp"
#include <string>
#include <vector>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace mcp {

class McpClient {
public:
    McpClient() = default;
    ~McpClient() = default;

    // Handshake JSON-RPC 2.0 e listagem de ferramentas
    std::vector<McpTool> listTools(const McpServerConfig& config, int timeoutMs = 7000);

    // Execução remota de ferramenta com argumentos
    McpCallResult callTool(const McpServerConfig& config,
                           const std::string& toolName,
                           const taskforge::Json& arguments,
                           int timeoutMs = 15000);

private:
    std::vector<McpTool> listToolsStdio(const McpServerConfig& config, int timeoutMs);
    McpCallResult callToolStdio(const McpServerConfig& config,
                                const std::string& toolName,
                                const taskforge::Json& arguments,
                                int timeoutMs);

    std::vector<McpTool> listToolsSse(const McpServerConfig& config, int timeoutMs);
    McpCallResult callToolSse(const McpServerConfig& config,
                              const std::string& toolName,
                              const taskforge::Json& arguments,
                              int timeoutMs);

    // Utilitário para mock/demo interno
    bool isMockServer(const McpServerConfig& config) const;
    std::vector<McpTool> getMockTools(const McpServerConfig& config) const;
    McpCallResult executeMockTool(const std::string& toolName, const taskforge::Json& args) const;
};

} // namespace mcp
} // namespace taskforge
