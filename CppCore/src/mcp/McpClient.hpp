#pragma once

#include "McpErrors.hpp"
#include "McpTypes.hpp"
#include <cstddef>
#include <string>
#include <vector>
#include "util/JsonValue.hpp"

namespace taskforge {
namespace mcp {

class McpClient {
public:
    McpClient() = default;
    ~McpClient() = default;

    // Handshake JSON-RPC 2.0 e listagem de ferramentas. Em falha, devolve vazio e preenche
    // `errorInfo` (quando informado) com o codigo estavel para o i18n e o texto de fallback.
    std::vector<McpTool> listTools(const McpServerConfig& config, int timeoutMs = 7000,
                                   McpErrorInfo* errorInfo = nullptr);

    // Execução remota de ferramenta com argumentos. O resultado carrega errorCode/errorDetail
    // (McpCallResult) para o i18n do frontend.
    McpCallResult callTool(const McpServerConfig& config,
                           const std::string& toolName,
                           const taskforge::Json& arguments,
                           int timeoutMs = 15000);

    // Resolve ${VAR} / ${VAR:-default} em command, args, env, url e headers. Lanca McpError
    // (EnvVarUnset) quando uma variavel obrigatoria nao existe. Nunca grava o valor resolvido.
    static McpServerConfig expandConfig(const McpServerConfig& config);

    // Encerra todas as sessoes persistentes (processos filhos incluidos). Chamado ao sair e nos testes.
    static void shutdownSessions();
    // Quantidade de sessoes abertas e vivas (testes e diagnostico).
    static size_t activeSessions();

private:
    std::vector<McpTool> listToolsStdio(const McpServerConfig& config, int timeoutMs, McpErrorInfo* errorInfo);
    McpCallResult callToolStdio(const McpServerConfig& config,
                                const std::string& toolName,
                                const taskforge::Json& arguments,
                                int timeoutMs);

    std::vector<McpTool> listToolsSse(const McpServerConfig& config, int timeoutMs, McpErrorInfo* errorInfo);
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
