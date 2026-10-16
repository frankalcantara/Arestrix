#pragma once

// Codigos estaveis de erro do cliente MCP e do toolbox de servidores do projeto.
//
// O frontend traduz cada codigo pelo i18n (chave `mcp_errors.<codigo>`). Quando o frontend nao conhece
// um codigo, ele mostra o texto de fallback em `error` (humano, em portugues, enviado pelo backend).
// Adicionar codigo novo aqui NAO quebra frontends antigos: eles caem no fallback. Nunca renomeie nem
// remova um codigo ja publicado: o contrato entre o backend e o frontend.

#include <stdexcept>
#include <string>

namespace taskforge {
namespace mcp {

enum class McpErrorCode {
    None = 0,
    EmptyCommand,           // stdio sem comando configurado
    InvalidArg,             // arg com aspas/metacaracteres (npx/npm/uvx no Windows)
    SpawnFailed,            // nao foi possivel iniciar o processo do servidor
    WriteFailed,            // falha ao escrever no stdin do servidor
    ServerClosed,           // servidor encerrou antes de responder
    Timeout,                // tempo esgotado aguardando resposta
    HandshakeRefused,       // initialize recusado pelo servidor
    UrlEmpty,               // transporte HTTP sem URL configurada
    UrlTooLong,             // URL do transporte HTTP longa demais
    UrlInvalidChars,        // URL com espacos/controle
    UrlNotHttps,            // transporte HTTP exige https://
    UrlUserInfo,            // usuario@ na URL
    UrlIpv6Literal,         // endereco IPv6 literal
    UrlBadPort,             // porta diferente de 443
    UrlInvalidHost,         // host invalido
    UrlNonPublicHost,       // host nao resolve somente para enderecos publicos (DNS rebinding)
    TransportFailed,        // falha de transporte HTTP
    InvalidResponse,        // resposta nao e JSON valido / HTTP de erro
    SseNoResult,            // stream SSE sem o resultado da chamada
    LegacySseUnsupported,   // endpoint nao fala MCP Streamable HTTP
    InvalidServerId,        // id fora do padrao [A-Za-z0-9_.-]
    NativeServerReserved,   // prefixo 'arestrix.' reservado para servidores nativos
    ServerNotFound,         // servidor nao encontrado no projeto (.workspace/mcps) ou no registro
    ServerDisabled,         // servidor desabilitado (isEnabled=false)
    EnvVarUnset,            // referencia ${NOME} sem variavel definida na maquina
    NoTools,                // servidor nao respondeu ou nao oferece ferramentas
    Unknown
};

// Nome do codigo como trafega no JSON (ex.: "invalid_arg", "url_not_https").
const char* mcpErrorCodeName(McpErrorCode code);

// Mensagem humana padrao (pt, ASCII) para o codigo; o detalhe dinamico e anexado quando o codigo o admite.
std::string mcpErrorDefaultMessage(McpErrorCode code, const std::string& detail);

// Versao em ingles das mensagens padrao (para agentes em runtime cujo idioma detectado e ingles).
std::string mcpErrorDefaultMessageEn(McpErrorCode code, const std::string& detail);

// Converte o nome que trafega no JSON de volta para o enum (desconhecido = McpErrorCode::Unknown).
McpErrorCode mcpErrorCodeFromName(const std::string& name);

// Erro com codigo estavel. `detail` e a parte dinamica curta (id do servidor, host, nome de variavel,
// argumento recusado...); what() devolve a mensagem humana completa.
class McpError : public std::runtime_error {
public:
    explicit McpError(McpErrorCode code, std::string detail = "")
        : std::runtime_error(mcpErrorDefaultMessage(code, detail)), code_(code), detail_(std::move(detail)) {}
    McpErrorCode code() const { return code_; }
    const std::string& detail() const { return detail_; }

private:
    McpErrorCode code_;
    std::string detail_;
};

// Descricao completa de uma falha, pronta para virar JSON nos handlers:
//   errorCode   -> codigo estavel para o i18n do frontend (campo "errorCode")
//   errorDetail -> parte dinamica (id do servidor, host, variavel...) (campo "errorDetail")
//   message     -> texto humano (pt) para logs e para o fallback do frontend (campo "error")
struct McpErrorInfo {
    McpErrorCode code = McpErrorCode::None;
    std::string detail;
    std::string message;

    bool empty() const { return code == McpErrorCode::None; }
    const char* codeName() const { return mcpErrorCodeName(code); }

    void set(McpErrorCode c, std::string d = "", std::string m = "") {
        code = c;
        detail = std::move(d);
        message = m.empty() ? mcpErrorDefaultMessage(code, detail) : std::move(m);
    }

    static McpErrorInfo fromException(const std::exception& ex) {
        McpErrorInfo info;
        if (const auto* me = dynamic_cast<const McpError*>(&ex)) {
            info.set(me->code(), me->detail(), ex.what());
        } else {
            info.set(McpErrorCode::Unknown, "", ex.what());
        }
        return info;
    }
};

} // namespace mcp
} // namespace taskforge
