#pragma once

// Compactacao do historico de uma interacao longa usando a memoria existente.
//
// O compactor e deterministico e nao escreve conhecimento. Ele substitui um
// prefixo de turnos ja concluidos por um bloco derivado com referencias
// estaveis [MEMO:<id>]; a tarefa atual, o system prompt e as ultimas N
// mensagens nunca sao compactados. Qualquer falha devolve o historico original.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "rt/ModelClient.hpp"

namespace taskforge {
namespace memory {
class MemoryService;
}

namespace rt {

struct CompactionConfig {
    bool enabled = false;
    // Mensagens recentes preservadas byte a byte (inclui tool results do turno
    // em andamento). O primeiro prompt (tarefa atual) nunca entra na conta.
    size_t keepRecentMessages = 6;
    // Limiar para acionar a compactacao. Historico menor fica intacto.
    size_t minHistoryChars = 4000;
    // Orcamento do bloco derivado. O menor dos dois limites vence.
    size_t maxSummaryChars = 2000;
    size_t maxSummaryTokens = 500;
    // Teto de itens duraveis no bloco.
    size_t maxItems = 16;
    // Reservado: um modelo local opcional nunca e usado por padrao. O modo
    // deterministico e o unico implementado; true falha fechado.
    bool useLocalModel = false;
};

struct DurableMemoryItem {
    std::string id;     // id fisico da memoria (lesson, edge, path, facet, passage, trace)
    std::string kind;   // lesson | claim | path | facet | passage | trace | playbook
    std::string text;   // texto ja sanitizado, curto e derivado
};

struct CompactionResult {
    std::vector<ChatMessage> messages;
    // Caracteres do prefixo efetivamente substituido (original) e do bloco
    // derivado que o substitui. Sao comparaveis entre si.
    size_t originalChars = 0;
    size_t compactedChars = 0;
    size_t removedMessages = 0;
    size_t compactedTurns = 0;
    std::vector<std::string> memoryIds;
    bool applied = false;
    std::string fallbackReason;
};

// O Agent injeta um provedor que le o MemoryService/MemoryStore. Testes podem
// injetar um provedor deterministico, vazio ou que lanca excecao.
using DurableItemProvider =
    std::function<std::vector<DurableMemoryItem>(const std::string& query)>;

class ContextCompactor {
public:
    ContextCompactor(CompactionConfig config, DurableItemProvider provider);

    CompactionResult compact(const std::vector<ChatMessage>& messages) const;

    static size_t messageChars(const ChatMessage& message);
    static size_t historyChars(const std::vector<ChatMessage>& messages);
    static size_t estimateTokens(size_t chars);

private:
    CompactionConfig config_;
    DurableItemProvider provider_;
};

// Coleta somente material duravel e ativo do store do projeto. Nunca cria,
// promove ou altera conhecimento; leituras de telemetria sao apenas de hits ja
// registrados.
std::vector<DurableMemoryItem> memoryDurableItems(
    memory::MemoryService& service, const std::string& projectId,
    const std::vector<std::string>& domains, const std::string& taskType,
    const std::string& query);

} // namespace rt
} // namespace taskforge
