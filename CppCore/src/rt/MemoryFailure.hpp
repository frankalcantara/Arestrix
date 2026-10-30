#pragma once

#include <string>

namespace taskforge {
namespace rt {

// Classificacao estavel de falhas para telemetria e para o gate de conhecimento.
// Nunca use o texto da excecao para decidir se a falha e de infraestrutura.
inline std::string memoryErrorClass(const std::string& errorCode, int httpStatus = 0) {
    if (errorCode == "task_max_turns") return "task_max_turns";
    if (errorCode == "rate_limited" || httpStatus == 429) return "provider_429";
    if (httpStatus == 401 || httpStatus == 403 ||
        errorCode == "api_key_missing" || errorCode == "gateway_required") {
        return "auth_401_403_or_missing_key";
    }
    if (errorCode == "budget_exceeded" || httpStatus == 402) return "gateway_or_budget_block";
    if (errorCode == "model_not_configured" || errorCode == "base_url_missing" ||
        errorCode == "tools_unsupported") {
        return "config_model_or_skill_missing";
    }
    if (httpStatus >= 500) return "provider_5xx";
    if (errorCode == "bad_response") return "transport_timeout";
    return "agent_exception";
}

inline bool isInfrastructureErrorClass(const std::string& errorClass) {
    return errorClass == "transport_timeout" ||
           errorClass == "provider_429" ||
           errorClass == "provider_5xx" ||
           errorClass == "auth_401_403_or_missing_key" ||
           errorClass == "gateway_or_budget_block" ||
           errorClass == "config_model_or_skill_missing" ||
           errorClass == "sandbox_or_tool_unavailable" ||
           errorClass == "cancelled";
}

} // namespace rt
} // namespace taskforge
