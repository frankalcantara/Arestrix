#pragma once

// Notificacoes assincronas de execucao.
//
// Uma notificacao e um evento fire-and-forget: nao bloqueia a chamada, nao muda o
// resultado sincrono do v1 e nunca carrega input/output do alvo. O runtime grava o
// evento em .arestrix/runs/<runId>/notifications.jsonl e o publica na telemetria
// (type = "notification"), para o Studio mostrar um toast.
//
// ARESTRIX_NOTIFICATIONS=0|false|off desliga o canal.

#include <string>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace rt {

// Best effort: nunca lanca excecao por falha de arquivo/telemetria.
void notify(const std::string& nodeId, const std::string& kind, const std::string& severity,
            const std::string& title, const std::string& message,
            const taskforge::Json& metadata = taskforge::Json::object());

} // namespace rt
} // namespace taskforge
