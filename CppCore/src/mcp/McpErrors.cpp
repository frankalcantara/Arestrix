#include "mcp/McpErrors.hpp"

namespace taskforge {
namespace mcp {

namespace {

struct Entry {
    McpErrorCode code;
    const char* name;      // como trafega no JSON (contrato com o frontend)
    const char* message;   // mensagem humana padrao (pt, ASCII)
    bool withDetail;       // anexa o detalhe dinamico ao final da mensagem
};

constexpr Entry kTable[] = {
    {McpErrorCode::None, "none", "", false},
    {McpErrorCode::EmptyCommand, "empty_command", "Comando do servidor MCP nao configurado.", false},
    {McpErrorCode::InvalidArg, "invalid_arg",
     "Argumento invalido para o comando do servidor MCP (aspas e metacaracteres como & | < > ^ % ! nao sao permitidos nos args de npx/npm/uvx).",
     true},
    {McpErrorCode::SpawnFailed, "spawn_failed", "Falha ao iniciar o processo do servidor MCP.", true},
    {McpErrorCode::WriteFailed, "write_failed", "Falha ao escrever no stdin do servidor MCP.", false},
    {McpErrorCode::ServerClosed, "server_closed", "O servidor MCP encerrou antes de responder.", false},
    {McpErrorCode::Timeout, "timeout", "Timeout aguardando resposta do servidor MCP.", false},
    {McpErrorCode::HandshakeRefused, "handshake_refused", "O servidor MCP recusou o handshake (initialize).", true},
    {McpErrorCode::UrlEmpty, "url_empty", "A URL do transporte HTTP do servidor MCP nao foi configurada.", false},
    {McpErrorCode::UrlTooLong, "url_too_long", "URL do servidor MCP longa demais.", false},
    {McpErrorCode::UrlInvalidChars, "url_invalid_chars",
     "A URL do servidor MCP contem caracteres invalidos (espacos ou controle).", false},
    {McpErrorCode::UrlNotHttps, "url_not_https",
     "So URLs https:// sao permitidas no transporte HTTP do MCP (servidores locais devem usar o transporte stdio).", false},
    {McpErrorCode::UrlUserInfo, "url_user_info", "URLs com usuario@ nao sao permitidas.", false},
    {McpErrorCode::UrlIpv6Literal, "url_ipv6_literal", "Enderecos IPv6 literais nao sao permitidos.", false},
    {McpErrorCode::UrlBadPort, "url_bad_port", "So a porta 443 e permitida na URL do servidor MCP.", true},
    {McpErrorCode::UrlInvalidHost, "url_invalid_host", "Host invalido na URL do servidor MCP.", true},
    {McpErrorCode::UrlNonPublicHost, "url_non_public_host",
     "O host do servidor MCP nao e publico (recusado por protecao contra DNS rebinding).", true},
    {McpErrorCode::TransportFailed, "transport_failed", "Falha de transporte na conexao com o servidor MCP.", true},
    {McpErrorCode::InvalidResponse, "invalid_response", "Resposta invalida do servidor MCP.", true},
    {McpErrorCode::SseNoResult, "sse_no_result", "A resposta SSE nao trouxe o resultado da chamada.", false},
    {McpErrorCode::LegacySseUnsupported, "legacy_sse",
     "O endpoint nao parece falar MCP Streamable HTTP (servidores SSE legados nao sao suportados).", false},
    {McpErrorCode::InvalidServerId, "invalid_server_id", "Identificador de servidor MCP invalido.", true},
    {McpErrorCode::NativeServerReserved, "native_server_reserved",
     "Este id e reservado para servidores nativos do Arestrix (o prefixo 'arestrix.' nao pode ser configurado em .workspace/mcps).",
     true},
    {McpErrorCode::ServerNotFound, "server_not_found", "Servidor MCP nao encontrado neste projeto (.workspace/mcps).", true},
    {McpErrorCode::ServerDisabled, "server_disabled", "Servidor MCP desabilitado (isEnabled=false).", true},
    {McpErrorCode::EnvVarUnset, "env_var_unset",
     "Variavel de ambiente citada pela configuracao do servidor nao esta definida nesta maquina (defina-a nas variaveis de ambiente do usuario; veja .workspace/mcps).",
     true},
    {McpErrorCode::NoTools, "no_tools",
     "O servidor nao respondeu ou nao oferece ferramentas. Confira o comando em .workspace/mcps.", false},
    {McpErrorCode::Unknown, "unknown", "Erro MCP desconhecido.", true},
};

} // namespace

const char* mcpErrorCodeName(McpErrorCode code) {
    for (const auto& e : kTable)
        if (e.code == code) return e.name;
    return "unknown";
}

std::string mcpErrorDefaultMessage(McpErrorCode code, const std::string& detail) {
    for (const auto& e : kTable) {
        if (e.code != code) continue;
        std::string m = e.message;
        if (e.withDetail && !detail.empty()) {
            m += " ";
            m += detail;
        }
        return m;
    }
    return "Erro MCP desconhecido.";
}

} // namespace mcp
} // namespace taskforge
