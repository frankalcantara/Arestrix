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
};

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    int timeoutSeconds = 600;
};

HttpResponse httpSend(const HttpRequest& req);

} // namespace rt
} // namespace taskforge
