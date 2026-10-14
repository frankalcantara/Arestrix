#pragma once

// Protecao contra "DNS rebinding" e contra nomes que apontam para a rede interna.
//
// Um nome de host autorizado (api.exemplo.com) pode ter o DNS controlado por quem o registrou e, num segundo momento, passar a
// apontar para 127.0.0.1, para um IP da rede local ou para o endereco de metadados de uma nuvem. As ferramentas nativas de HTTP
// resolvem o nome ANTES de conectar e recusam se QUALQUER endereco devolvido nao for publico (devolver varios enderecos, um deles
// interno, e um truque conhecido). Depois, a conexao e fixada no endereco validado (curl --resolve) ou, onde o sistema nao permite
// fixar o endereco (WinHTTP), o endereco realmente conectado e conferido antes de qualquer dado do corpo ser enviado.

#include <functional>
#include <string>
#include <vector>

namespace taskforge {
namespace rt {
namespace net {

// true se `ip` (texto IPv4 ou IPv6) for um endereco publico e roteavel na internet. Recusa loopback, privados, link-local,
// CGNAT, multicast, reservados, documentacao, e as formas que embutem IPv4 (::ffff:a.b.c.d, 64:ff9b::/96, 6to4) quando o IPv4
// embutido nao e publico. Texto que nao e um endereco devolve false.
bool isPublicAddress(const std::string& ip);

struct Resolution {
    std::vector<std::string> addresses;   // IPv4 e IPv6 em texto, sem repeticao
    std::string error;                    // preenchido se o nome nao resolveu
};

// Resolve o nome com o resolvedor do sistema.
Resolution resolveHost(const std::string& host);

// Resolvedor alternativo para testes sem rede (nullptr restaura o do sistema).
using Resolver = std::function<Resolution(const std::string&)>;
void setResolverForTest(Resolver resolver);

// Host de uma URL (sem usuario, sem porta, em minusculas); "" se nao der para extrair.
std::string hostOfUrl(const std::string& url);

// "" se o host resolve SOMENTE para enderecos publicos (e preenche `addresses`); senao, o motivo da recusa.
// Um host que ja e um IP literal e verificado diretamente, sem consulta de DNS.
std::string checkHostIsPublic(const std::string& host, std::vector<std::string>* addresses = nullptr);

} // namespace net
} // namespace rt
} // namespace taskforge
