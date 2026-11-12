#pragma once

// Sessoes do Assistente do Studio e o laco da conversa.
//
// Cada conversa e uma sessao gravada no projeto em .arestrix/assistant/sessions/<id>.json: o historico
// completo para o modelo (mensagens neutras) e a transcricao que a interface redesenha ao reabrir.
// O laco roda inteiro no C++: chama o modelo, executa as ferramentas, pede confirmacao quando preciso
// e emite eventos para a interface (texto, atividades, operacoes no canvas, erros, fim).

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "assistant/AssistantTools.hpp"
#include "assistant/GraphDocument.hpp"
#include "memory/ClaimIntelligence.hpp"
#include "rt/ModelClient.hpp"
#include "rt/RuntimeLang.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace assistant {

struct Session {
    std::string id;
    std::string title;
    std::string createdAt;
    std::string updatedAt;
    std::vector<rt::ChatMessage> messages;   // historico para o modelo
    Json transcript = Json::array();          // o que a interface mostra: user, assistant, activity, error

    Json toJson() const;
    static Session fromJson(const Json& j);
};

class SessionStore {
public:
    explicit SessionStore(std::filesystem::path projectRoot);

    std::filesystem::path directory() const { return dir_; }
    // [{id, title, createdAt, updatedAt, turns}] do mais recente para o mais antigo.
    Json list() const;
    std::optional<Session> load(const std::string& id) const;
    bool save(Session& session) const;   // gravacao atomica (temporario + rename)
    bool remove(const std::string& id) const;

    static Session create();
    static bool isValidId(const std::string& id);

private:
    std::filesystem::path pathFor(const std::string& id) const;
    std::filesystem::path dir_;
};

struct ModelChoice {
    std::string provider;
    std::string model;
};

// IA padrao do Studio (ARESTRIX_DEFAULT_PROVIDER/MODEL ou primeira chave configurada) e a tabela de
// provedores com a indicacao de suporte a ferramentas.
//   {"configured", "provider", "model", "supportsTools", "providers": [{"id", "supportsTools"}]}
Json assistantStatus();
std::optional<ModelChoice> defaultModelChoice();

using ModelFn = std::function<rt::ChatReply(const rt::ChatRequest&)>;

// Adaptador único da inteligência do Studio para a memória. A memória não
// conhece provedor, protocolo, URL ou chave. O alvo pode ser um agente lógico
// registrado pelo Studio, um grafo (graph/node) ou, quando o projeto não define
// nada, o modelo padrão do Studio.
memory::ClaimIntelligenceFn studioClaimIntelligence();

// Registro dos agentes de memória do Studio. A chave é o `agent.id` ou, quando
// só há grafo, o caminho em `agent.graph`. O executor do grafo/agente é quem
// decide como produzir o JSON; a memória só consome ClaimIntelligenceFn.
void registerStudioClaimAgent(const std::string& id, memory::ClaimIntelligenceFn fn);
void unregisterStudioClaimAgent(const std::string& id);
void clearStudioClaimAgents();

// Alvos que a UI pode oferecer para a extracao de afirmacoes. Cada agente do
// grafo ativo vira uma entrada; o alvo guarda o id do vertice, o caminho do
// pipeline e o no, para que o projeto possa ser reaberto em outra maquina.
struct StudioClaimAgentInfo {
    std::string id;
    std::string label;
    std::string graph;
    std::string node;
    std::string description;
};

// Reconstroi o registro a partir do grafo do projeto. A chamada e idempotente:
// troca os alvos anteriores pelos agentes do grafo recebido.
void registerStudioClaimAgentsFromGraph(const taskforge::Json& graph,
                                        const std::string& graphPath);
taskforge::Json studioClaimAgentsJson();

struct TurnIO {
    std::function<void(const Json&)> emit;          // evento para a interface
    std::function<bool(const Json&)> confirm;       // bloqueia ate o usuario responder; true = aceitou
    std::function<bool()> cancelled;
};

struct TurnOptions {
    int maxSteps = 16;
    std::string locale = "pt-BR";
};

// Executa uma mensagem do usuario ate o modelo concluir. `store` grava a sessao a cada passo.
// Eventos: {"type": "thinking"|"text"|"activity"|"op"|"error"|"done", "sessionId", ...}.
void runTurn(Session& session, const SessionStore& store, const std::string& userText, GraphDocument& doc,
             AssistantHost& host, const std::optional<ModelChoice>& model, const ModelFn& callModel,
             const TurnIO& io, const TurnOptions& options);

rt::RuntimeLang langForLocale(const std::string& locale);
std::string systemPrompt(rt::RuntimeLang lang, const Json& project, size_t nodeCount);

} // namespace assistant
} // namespace taskforge
