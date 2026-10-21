#pragma once

// Ferramentas "Code Mode" oferecidas aos agentes: read, write, edit, bash, validate_svg.
// Todas operam confinadas ao diretorio de trabalho (workspace) do pipeline.
// Descricoes e mensagens de erro sao bilíngues: o idioma vem do ToolContext/RuntimeLang
// (detectado da skill e do prompt do agente).

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "util/JsonValue.hpp"

#include "rt/RuntimeLang.hpp"
#include "sandbox/ISandboxDriver.hpp"

namespace taskforge {
namespace rt {

struct ToolDef {
    std::string name;
    std::string description;
    taskforge::Json parameters;   // JSON Schema (type: object)
};

struct ToolResult {
    std::string content;
    bool isError = false;
};

// Resolve `relative` dentro de `root`. Devolve std::nullopt se o caminho escapar do
// workspace (.., caminho absoluto externo, symlink para fora, stream alternativo NTFS).
std::optional<std::filesystem::path> resolveInWorkspace(const std::filesystem::path& root,
                                                        const std::string& relative);

// Caminhos que um agente NUNCA pode gravar: pipelines e configuracao (gravar ali injetaria codigo C++ ou servidores
// MCP que rodariam na proxima execucao), o repositorio Git e as pastas internas do Arestrix. Leitura continua permitida
// (exceto .git). `resolved` deve ser o resultado de resolveInWorkspace.
bool isProtectedPath(const std::filesystem::path& root, const std::filesystem::path& resolved);

// Definicoes (schemas) das ferramentas autorizadas, no idioma do agente.
std::vector<ToolDef> toolDefinitions(const std::vector<std::string>& allowed,
                                     RuntimeLang lang = RuntimeLang::Pt);

// Valida SVG: boa formacao XML, raiz <svg>, viewBox/width+height, ids duplicados,
// referencias de begin/end="id.evento" para ids inexistentes e regra tipografica
// (sem sombra/glow em texto). Devolve a lista de problemas no idioma pedido (vazia = ok).
std::vector<std::string> validateSvg(const std::string& svg, RuntimeLang lang = RuntimeLang::Pt);

struct ToolContext {
    std::filesystem::path workspaceRoot;
    sandbox::SandboxPolicy sandbox;
    std::string agentId;
    std::string projectId;              // projeto da memoria (MemoryService)
    std::string runId;                  // execucao corrente (memory_hit)
    std::string memoryItemId = "pipeline";
    bool memoryWriteEnabled = false;
    std::vector<std::string> memoryDomains;
    std::vector<std::string> allowed;
    RuntimeLang lang = RuntimeLang::Pt;   // idioma do agente (skill + prompt)
};

ToolResult executeTool(const ToolContext& ctx, const std::string& name, const taskforge::Json& args);

} // namespace rt
} // namespace taskforge
