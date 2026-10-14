#pragma once

// Cliente HTTP(S) minimo usado pelo runtime dos agentes.
//  - Windows: WinHTTP (nativo, sem dependencias).
//  - Linux:   CLI `curl`, configurado via stdin (-K -) para que chaves de API nunca
//             aparecam na linha de comando / lista de processos.

#include <map>
#include <string>

namespace taskforge {
namespace rt {

struct HttpResponse {
    int status = 0;                       // 0 = falha de transporte
    std::string body;
    std::map<std::string, std::string> headers;   // chaves em minusculas
    std::string error;                    // descricao da falha de transporte
    std::string peer;                     // endereco IP realmente conectado, quando o sistema informa (Windows)
};

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutSeconds = 600;
    bool noRedirects = false;   // true: devolve o 3xx em vez de seguir (as ferramentas nativas validam cada destino)
    // true: o nome do host e resolvido ANTES de conectar e a requisicao e recusada se algum endereco for interno ou reservado
    // (protecao contra DNS rebinding, veja rt/NetGuard.hpp). Linux/macOS fixam a conexao no endereco validado; no Windows o
    // endereco realmente conectado e conferido antes de o corpo ser enviado.
    bool requirePublicPeer = false;
};

HttpResponse httpSend(const HttpRequest& req);

} // namespace rt
} // namespace taskforge
