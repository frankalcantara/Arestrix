#pragma once

// Fronteira entre a auditoria de credito da memoria e o executor do Studio.
//
// A memoria decide o que auditar e como reatribuir o credito. O executor recebe
// apenas a entrada do no, o snapshot, os itens entregues/citados e a
// configuracao dos bracos; ele resolve modelo, sandbox e ferramentas e devolve
// os desfechos observados. Nenhum provedor, URL ou chave entra neste modulo.

#include <functional>
#include <string>
#include <vector>

#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

struct CreditAuditExecutionRequest {
    std::string projectId;
    std::string scopeId;
    std::string traceId;
    std::string taskType;
    std::string nodeId;
    std::string validator;
    std::string inputHash;
    std::string containerCommand;
    taskforge::Json snapshot = taskforge::Json::object();
    taskforge::Json trace = taskforge::Json::object();
    taskforge::Json containerPlan = taskforge::Json::object();
    std::vector<std::string> delivered;
    std::vector<std::string> cited;
    std::vector<std::string> suspectItems;
    int repetitions = 2;
    int pairwiseTopM = 3;
};

struct CreditAuditExecutionResult {
    bool ok = false;
    taskforge::Json arms = taskforge::Json::array();
    long long tokens = 0;
    std::string error;
};

using CreditAuditExecutorFn =
    std::function<CreditAuditExecutionResult(const CreditAuditExecutionRequest&)>;

} // namespace memory
} // namespace taskforge
