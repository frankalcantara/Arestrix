#pragma once

#include <algorithm>
#include <cctype>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>

namespace taskforge {
namespace rt {

// E5: falha tipada do agente. O wrapper gerado consulta errorClass() para
// decidir se a falha vira proposta de licao; o texto da excecao nunca decide.
class AgentFailure : public std::runtime_error {
public:
    AgentFailure(const std::string& message, std::string errorClass)
        : std::runtime_error(message), errorClass_(std::move(errorClass)) {}
    const std::string& errorClass() const noexcept { return errorClass_; }

private:
    std::string errorClass_;
};

// Classificacao estavel de falhas para telemetria e para o gate de conhecimento.
// Nunca use o texto da excecao para decidir se a falha e de infraestrutura.
inline std::string memoryErrorClass(const std::string& errorCode, int httpStatus = 0) {
    if (errorCode == "task_max_turns") return "task_max_turns";
    if (errorCode == "task_max_runs") return "task_max_runs";
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

// Normaliza o texto de uma falha antes da deduplicacao de propostas. Remove
// UUIDs, datas/horas, hex longos e numeros longos (ids, epochs) e compacta
// espacos. Dois erros iguais com partes variaveis geram a mesma proposta.
inline std::string normalizeFailureText(const std::string& text) {
    static const std::regex uuid(
        R"(\b[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\b)");
    static const std::regex timestamp(
        R"(\b\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2}:\d{2}(?:\.\d+)?Z?\b)");
    static const std::regex longHex(R"(\b[0-9a-fA-F]{16,}\b)");
    static const std::regex longNumber(R"(\b\d{6,}\b)");

    std::string normalized = std::regex_replace(text, uuid, "<id>");
    normalized = std::regex_replace(normalized, timestamp, "<time>");
    normalized = std::regex_replace(normalized, longHex, "<id>");
    normalized = std::regex_replace(normalized, longNumber, "<n>");

    std::string collapsed;
    collapsed.reserve(normalized.size());
    bool space = false;
    for (unsigned char c : normalized) {
        if (std::isspace(c)) {
            if (!space) collapsed.push_back(' ');
            space = true;
        } else {
            collapsed.push_back(static_cast<char>(c));
            space = false;
        }
    }
    const size_t first = collapsed.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    const size_t last = collapsed.find_last_not_of(' ');
    return collapsed.substr(first, last - first + 1);
}

} // namespace rt
} // namespace taskforge
