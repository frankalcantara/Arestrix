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

#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace native {

constexpr const char* kIdPrefix = "arestrix.";

struct Context {
    std::filesystem::path workspaceRoot;
};

using Handler = std::function<rt::ToolResult(const taskforge::Json& args, const Context& ctx)>;

struct Tool {
    std::string name;
    std::string description;
    taskforge::Json inputSchema;   // JSON Schema (type: object)
    std::string risk;              // "safe" | "moderate" | "high"  (para a interface)
    Handler handler;
};

struct Server {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::vector<Tool> tools;
};

bool isNativeServer(const std::string& id);
const std::vector<Server>& servers();
const Server* findServer(const std::string& id);
const Tool* findTool(const Server& server, const std::string& toolName);

// Executa uma ferramenta nativa: valida os argumentos obrigatorios, captura excecoes e limita a saida.
rt::ToolResult call(const std::string& serverId, const std::string& toolName, const taskforge::Json& args, const Context& ctx);

// Esquema de objeto: schema({{"path", "string", "Caminho relativo"}}, {"path"}).
struct Prop {
    std::string name, type, description;
};
taskforge::Json schema(const std::vector<Prop>& properties, const std::vector<std::string>& required);

// Construtores de cada servidor (um por arquivo).
Server makeFilesServer();
Server makeHttpServer();
Server makeDataServer();
Server makeMemoryServer();
Server makeTimeServer();
Server makeGitServer();
Server makeSqliteServer();

// Exposto para teste: devolve "" se a URL puder ser requisitada neste projeto, ou o motivo da recusa.
namespace http {
std::string checkUrl(const std::filesystem::path& workspaceRoot, const std::string& url);

// Politica de hosts do projeto (.workspace/native/http.json), editada pela tela do Studio.
std::vector<std::string> readPolicy(const std::filesystem::path& workspaceRoot);
bool writePolicy(const std::filesystem::path& workspaceRoot, const std::vector<std::string>& hosts, std::string& error);
}

} // namespace native
} // namespace taskforge
