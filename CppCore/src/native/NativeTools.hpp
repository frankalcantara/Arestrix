#pragma once

// Ferramentas nativas do Arestrix: servidores "MCP" implementados em C++ e executados DENTRO do proprio processo.
//
// Elas aparecem para o vertice MCP e para os agentes exatamente como um servidor MCP externo (servidor + ferramentas
// com esquema JSON), mas uma chamada e so uma chamada de funcao: sem processo filho, sem JSON-RPC, sem Node nem Python,
// sem codigo de terceiros. Sao o jeito mais rapido e mais seguro de fazer o que a maioria dos pipelines precisa.
//
// Servidores (ids reservados com o prefixo "arestrix."):
//   arestrix.arquivos  ler, gravar, listar e buscar arquivos do projeto
//   arestrix.http      requisicoes HTTPS para hosts autorizados pelo projeto
//   arestrix.dados     JSON, CSV, expressoes regulares e base64
//   arestrix.memoria   memoria chave-valor do projeto
//   arestrix.tempo     data e hora, somas e diferencas
//   arestrix.git       consultas somente-leitura ao repositorio Git do projeto
//   arestrix.sqlite    banco de dados SQLite embutido (arquivo dentro do projeto)
//
// Regras de seguranca comuns: tudo fica confinado a pasta do projeto; caminhos que levam a execucao de codigo
// (pipelines, configuracao, .git) nunca sao gravados; resultados limitados a 64 KB; nenhuma ferramenta le segredos.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "rt/RuntimeLang.hpp"
#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace native {

constexpr const char* kIdPrefix = "arestrix.";

struct Context {
    std::filesystem::path workspaceRoot;
    rt::RuntimeLang lang = rt::RuntimeLang::Pt;   // idioma do agente: mensagens de erro devolvidas a ele
};

using Handler = std::function<rt::ToolResult(const taskforge::Json& args, const Context& ctx)>;

struct Tool {
    std::string name;
    std::string description;     // pt (fallback e idioma padrao)
    std::string descriptionEn;   // en: usado quando o agente e em ingles (vazio = usa o pt)
    taskforge::Json inputSchema;   // JSON Schema (type: object)
    std::string risk;              // "safe" | "moderate" | "high"  (para a interface)
    Handler handler;
};

struct Server {
    std::string id;
    std::string name;
    std::string description;
    std::string descriptionEn;
    std::string category;
    std::vector<Tool> tools;
};

bool isNativeServer(const std::string& id);
const std::vector<Server>& servers();                                        // catalogo em portugues (interface/UI)
const std::vector<Server>& serversForLang(rt::RuntimeLang lang);             // catalogo no idioma do agente
const Server* findServer(const std::string& id);
const Tool* findTool(const Server& server, const std::string& toolName);

// Executa uma ferramenta nativa: valida os argumentos obrigatorios, captura excecoes e limita a saida.
// As mensagens de erro devolvidas seguem ctx.lang (idioma do agente).
rt::ToolResult call(const std::string& serverId, const std::string& toolName, const taskforge::Json& args, const Context& ctx);

// Esquema de objeto: schema({{"path", "string", "Caminho relativo", "Path relative to the project"}}, {"path"}).
// A descricao em ingles e opcional (vazia = usa a portuguesa).
struct Prop {
    std::string name, type, description, descriptionEn;
};
taskforge::Json schema(const std::vector<Prop>& properties, const std::vector<std::string>& required,
                       rt::RuntimeLang lang = rt::RuntimeLang::Pt);

// Construtores de cada servidor (um por arquivo), no idioma pedido.
Server makeFilesServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeHttpServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeDataServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeMemoryServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeTimeServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeGitServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);
Server makeSqliteServer(rt::RuntimeLang lang = rt::RuntimeLang::Pt);

// Exposto para teste: devolve "" se a URL puder ser requisitada neste projeto, ou o motivo da recusa.
namespace http {
std::string checkUrl(const std::filesystem::path& workspaceRoot, const std::string& url,
                     rt::RuntimeLang lang = rt::RuntimeLang::Pt);

// Politica de hosts do projeto (.workspace/native/http.json), editada pela tela do Studio.
std::vector<std::string> readPolicy(const std::filesystem::path& workspaceRoot);
bool writePolicy(const std::filesystem::path& workspaceRoot, const std::vector<std::string>& hosts, std::string& error);
}

} // namespace native
} // namespace taskforge
