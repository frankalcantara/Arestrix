#pragma once

// Catalogo de ferramentas do Assistente do Studio.
//
// Descricoes, schemas e resultados sao texto para o modelo, no idioma do Studio (pt-BR ou en-US).
// O que a interface mostra ao usuario NAO sai daqui pronto: cada ferramenta devolve uma "atividade"
// (nome + parametros) que a interface traduz pelo i18n.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "assistant/GraphDocument.hpp"
#include "rt/RuntimeLang.hpp"
#include "rt/Tools.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace assistant {

// read: livre. edit: aplicada direto (desfazivel na interface). confirm: pede confirmacao ao usuario antes.
enum class ToolRisk { Read, Edit, Confirm };

struct AssistantToolDef {
    rt::ToolDef def;
    ToolRisk risk = ToolRisk::Read;
};

// Efeitos fora do documento do grafo. Implementado pelo Studio (main.cpp) e por um falso nos testes.
class AssistantHost {
public:
    virtual ~AssistantHost() = default;
    virtual Json currentProject() = 0;   // manifesto do projeto ativo
    virtual Json recentProjects() = 0;   // lista [{name, rootPath, lastOpened}]
    // Cria (ou abre) um projeto e o torna ativo. Devolve o grafo principal dele; std::nullopt em falha.
    virtual std::optional<Json> createProject(const std::string& name, const std::string& directory,
                                              const std::string& description, std::string& error) = 0;
    virtual std::optional<Json> openProject(const std::string& path, std::string& error) = 0;
    virtual bool savePipeline(const Json& schema, std::string& error) = 0;
    // Pasta com os .md da documentacao do Studio no idioma pedido (vazio se nao existir).
    virtual std::filesystem::path docsDirectory(rt::RuntimeLang lang) = 0;
    // Altera nome, versao e/ou descricao do projeto ativo e grava o manifesto. Devolve o manifesto atualizado.
    virtual bool updateProject(const Json& patch, Json& updated, std::string& error) {
        (void)patch;
        (void)updated;
        error = "updateProject nao implementado";
        return false;
    }

    // Memoria do projeto. Implementado pelo Studio; testes podem devolver
    // dados falsos sem abrir o MemoryService.
    virtual Json memoryLevels(const Json& project, std::string& error) {
        (void)project;
        error = "memoryLevels nao implementado";
        return Json::object();
    }
    virtual bool reviewMemoryProposal(const std::string& proposalId, bool accept,
                                      const std::string& reason, std::string& error) {
        (void)proposalId;
        (void)accept;
        (void)reason;
        error = "reviewMemoryProposal nao implementado";
        return false;
    }
    virtual bool declareMemoryStateKey(const Json& spec, std::string& error) {
        (void)spec;
        error = "declareMemoryStateKey nao implementado";
        return false;
    }
    virtual bool unpromoteMemoryPlaybook(const std::string& playbookId,
                                         const std::string& reason, std::string& error) {
        (void)playbookId;
        (void)reason;
        error = "unpromoteMemoryPlaybook nao implementado";
        return false;
    }
};

struct ToolOutcome {
    rt::ToolResult result;    // para o modelo
    std::vector<Json> uiOps;  // operacoes para a interface aplicar no canvas
    Json activity;            // {"tool", "params"}: a interface traduz e mostra
};

std::vector<AssistantToolDef> assistantTools(rt::RuntimeLang lang);
std::optional<ToolRisk> toolRisk(const std::string& name);
const char* riskName(ToolRisk risk);

// Confere campos obrigatorios e tipos basicos contra o JSON Schema. Vazio = valido.
std::string validateArgs(const Json& schema, const Json& args, rt::RuntimeLang lang);

ToolOutcome executeAssistantTool(GraphDocument& doc, AssistantHost& host, rt::RuntimeLang lang,
                                 const std::string& name, const Json& args);

} // namespace assistant
} // namespace taskforge
