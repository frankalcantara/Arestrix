#pragma once

#include "memory/CreditAudit.hpp"
#include "sandbox/ISandboxDriver.hpp"

#include <functional>
#include <string>

namespace taskforge {
namespace rt {

// Executa os bracos de uma auditoria em conteineres isolados. O comando e
// fornecido pelo projeto e recebe a descricao do braco em
// ARESTRIX_AUDIT_ARM_JSON; a saida deve ser um JSON com ok/infra/tokens.
using CreditAuditSandboxRunFn =
    std::function<sandbox::ExecutionResult(const sandbox::ExecutionParams&)>;

struct CreditAuditContainerExecutorOptions {
    std::string command;
    int repetitions = 2;
};

memory::CreditAuditExecutionResult runCreditAuditInContainers(
    const memory::CreditAuditExecutionRequest& request,
    const CreditAuditContainerExecutorOptions& options = {},
    CreditAuditSandboxRunFn run = {});

} // namespace rt
} // namespace taskforge
