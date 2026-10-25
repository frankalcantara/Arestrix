#pragma once

#include <string>

#include "memory/MemoryInstitutionApi.hpp"
#include "memory/MemoryInstitutionService.hpp"

namespace taskforge {
namespace memory {

// Transporte HTTP local do contrato institucional. O binario `arestrix-memory`
// chama run(); os testes exercitam o contrato direto em MemoryInstitutionApi.
class MemoryInstitutionServer {
public:
    explicit MemoryInstitutionServer(MemoryInstitutionService& service);

    bool run(const std::string& host, int port, std::string& error);

private:
    MemoryInstitutionService& service_;
};

} // namespace memory
} // namespace taskforge
