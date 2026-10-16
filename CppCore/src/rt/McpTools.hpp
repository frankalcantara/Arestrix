#pragma once

// Ferramentas MCP (Model Context Protocol) para os agentes.
//
// Um agente declara no spec `"mcp": {"servers": ["id"...], "allowedTools": ["nome"...]}`. Os servidores sao
// lidos de <workspace>/.workspace/mcps/*.json (um objeto ou uma lista de McpServerConfig); as ferramentas de
// cada servidor viram ferramentas do agente com o nome  mcp__<servidor>__<ferramenta>.
//
// Segredos: o arquivo de configuracao NUNCA guarda valores secretos. `command`, `args`, `url` e os valores de
// `env` aceitam referencias ${NOME_DA_VARIAVEL}, resolvidas das variaveis de ambiente do usuario na hora da
// execucao. Variavel ausente = erro explicito (o agente nao roda sem a ferramenta, em vez de seguir as cegas).
//
// Servidores stdio rodam como processos do host (nao ficam dentro do sandbox do agente): so configure
// servidores em que voce confia.
//
// Descricoes das ferramentas NATIVAS e as mensagens de erro devolvidas ao agente seguem o idioma do agente
// (RuntimeLang, detectado da skill e do prompt). Descricoes de servidores MCP externos sao dados externos:
// passam como vieram.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "mcp/McpTypes.hpp"
#include "native/NativeTools.hpp"
#include "rt/RuntimeLang.hpp"
#include "rt/Tools.hpp"

namespace taskforge {
namespace rt {

class McpToolbox {
public:
    // Carrega os servidores e descobre as ferramentas. Lanca std::runtime_error se um servidor nao existir,
    // estiver desabilitado, referenciar variavel ausente ou nao responder.
    McpToolbox(const std::filesystem::path& workspaceRoot, const std::vector<std::string>& serverIds,
               const std::vector<std::string>& allowedTools, RuntimeLang lang = RuntimeLang::Pt);

    const std::vector<ToolDef>& definitions() const { return defs_; }
    bool owns(const std::string& toolName) const { return bindings_.count(toolName) > 0; }
    ToolResult call(const std::string& toolName, const taskforge::Json& args) const;

    // --- expostos para teste ---
    // Troca ${NOME} pelo valor da variavel de ambiente; ${NOME:-padrao} usa o padrao se ela nao existir.
    // Sem padrao e sem variavel, lanca std::runtime_error (o erro diz qual variavel falta).
    static std::string expandEnv(const std::string& text);
    // mcp__<servidor>__<ferramenta>, so [A-Za-z0-9_-], no maximo 64 caracteres.
    static std::string agentToolName(const std::string& server, const std::string& tool);
    // Regra da lista de ferramentas permitidas: vazia = todas; entradas "ferramenta", "servidor/ferramenta" ou "servidor/*".
    static bool isToolAllowed(const std::vector<std::string>& allowed, const std::string& serverId, const std::string& toolName);
    static mcp::McpServerConfig loadServerConfig(const std::filesystem::path& workspaceRoot, const std::string& id);
    // Servidores configurados no projeto (.workspace/mcps): [{id, name, transport, isEnabled}]. So metadados: nunca
    // devolve comando, argumentos nem variaveis de ambiente.
    static taskforge::Json listProjectServers(const std::filesystem::path& workspaceRoot);

private:
    struct Binding {
        mcp::McpServerConfig config;
        std::string tool;
        const native::Server* nativeServer = nullptr;   // ferramenta nativa: chamada em processo, sem JSON-RPC
    };
    std::filesystem::path root_;
    RuntimeLang lang_ = RuntimeLang::Pt;
    std::vector<ToolDef> defs_;
    std::map<std::string, Binding> bindings_;
};

} // namespace rt
} // namespace taskforge
