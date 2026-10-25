#pragma once

// Contrato institucional local do L2/M5. Nao e um servidor HTTP: e a camada
// nativa de autenticacao/papeis/politicas que o endpoint `arestrix-memory`
// pode usar. Tokens vivem apenas em memoria; nenhum token e persistido.

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "memory/MemoryStore.hpp"
#include "util/JsonValue.hpp"

namespace taskforge {
namespace memory {

enum class MemoryRole { Viewer = 0, Editor = 1, Admin = 2 };

struct MemoryPrincipal {
    std::string id;
    MemoryRole role = MemoryRole::Viewer;
};

class MemoryInstitutionService {
public:
    bool configure(const std::string& institutionId, const std::string& dbPath,
                   std::string& error);
    bool addToken(const std::string& token, const std::string& principalId,
                  MemoryRole role);
    bool addToken(const std::string& token, const std::string& principalId,
                  const std::string& role);
    void removeToken(const std::string& token);
    bool authenticate(const std::string& token, MemoryPrincipal& out) const;
    std::string roleName(MemoryRole role) const;

    std::string readContext(const std::string& token, const std::string& query,
                            size_t limit, const std::vector<std::string>& domains,
                            std::string& error);
    std::string writeLesson(const std::string& token, const std::string& domain,
                            const std::string& triggerError, const std::string& lessonRule,
                            std::string& error);
    bool setPolicy(const std::string& token, const std::string& key,
                   const taskforge::Json& value, bool immutable, std::string& error);
    taskforge::Json status(const std::string& token, std::string& error);

private:
    MemoryStore* store();
    bool requireRole(const std::string& token, MemoryRole required,
                     MemoryPrincipal& principal, std::string& error) const;
    std::string scope() const;

    mutable std::recursive_mutex mutex_;
    std::unique_ptr<MemoryStore> store_;
    std::string institutionId_;
    std::map<std::string, MemoryPrincipal> tokens_;
};

} // namespace memory
} // namespace taskforge
