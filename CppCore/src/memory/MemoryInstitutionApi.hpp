#pragma once

// Contrato HTTP/MCP do servico institucional `arestrix-memory`.
//
// Esta camada e deliberadamente independente de transporte: recebe a rota, o
// token e o corpo, e devolve status + JSON. O executavel liga isso ao
// cpp-httplib (HTTP) ou ao stdin/stdout (MCP stdio), o que permite testar o
// contrato sem abrir sockets.

#include <string>

#include "memory/MemoryInstitutionService.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct InstitutionHttpResponse {
    int status = 200;
    std::string body;
};

struct InstitutionMcpResponse {
    bool hasResponse = false;
    int httpStatus = 200;
    taskforge::Json response;
};

// Carrega tokens de um arquivo (`token principal papel` por linha), de um JSON
// (`[{"token":"...","principal":"...","role":"viewer"}]`) ou de ambos.
// Nunca grava os tokens no banco; eles vivem apenas no MemoryInstitutionService.
bool loadInstitutionTokens(MemoryInstitutionService& service,
                           const std::string& tokensFile,
                           const std::string& tokensJson,
                           std::string& error);

// GET /health, GET /v1/status, POST /v1/context, POST /v1/lessons,
// POST /v1/policies. `path` pode conter query string (ignorada).
InstitutionHttpResponse handleInstitutionRest(MemoryInstitutionService& service,
                                              const std::string& method,
                                              const std::string& path,
                                              const std::string& token,
                                              const std::string& body);

// JSON-RPC 2.0 do MCP: initialize, ping, tools/list e tools/call.
// Notificacoes devolvem hasResponse=false.
InstitutionMcpResponse handleInstitutionMcp(MemoryInstitutionService& service,
                                            const std::string& token,
                                            const taskforge::Json& request);

std::string institutionServiceVersion();

} // namespace memory
} // namespace taskforge
