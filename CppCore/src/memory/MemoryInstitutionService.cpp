#include "MemoryInstitutionService.hpp"

#include <algorithm>
#include <cctype>

namespace taskforge {
namespace memory {

namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
} // namespace

MemoryStore* MemoryInstitutionService::store() {
    return store_.get();
}

std::string MemoryInstitutionService::scope() const {
    return "institution:" + (institutionId_.empty() ? "default" : institutionId_);
}

std::string MemoryInstitutionService::roleName(MemoryRole role) const {
    if (role == MemoryRole::Admin) return "admin";
    if (role == MemoryRole::Editor) return "editor";
    return "viewer";
}

bool MemoryInstitutionService::configure(const std::string& institutionId,
                                         const std::string& dbPath,
                                         std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (institutionId.empty() || dbPath.empty()) {
        error = "instituicao ou banco invalido";
        return false;
    }
    auto store = std::make_unique<MemoryStore>(dbPath);
    if (!store->open(error)) return false;
    const std::string institutionScope = "institution:" + institutionId;
    if (!store->ensureScope(institutionScope, "institution", "", institutionId)) {
        error = "falha ao criar escopo institucional";
        return false;
    }
    store_ = std::move(store);
    institutionId_ = institutionId;
    return true;
}

bool MemoryInstitutionService::addToken(const std::string& token,
                                        const std::string& principalId,
                                        MemoryRole role) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (token.empty() || principalId.empty()) return false;
    tokens_[token] = MemoryPrincipal{principalId, role};
    return true;
}

bool MemoryInstitutionService::addToken(const std::string& token,
                                        const std::string& principalId,
                                        const std::string& role) {
    const std::string value = lower(role);
    if (value == "admin") return addToken(token, principalId, MemoryRole::Admin);
    if (value == "editor") return addToken(token, principalId, MemoryRole::Editor);
    if (value == "viewer") return addToken(token, principalId, MemoryRole::Viewer);
    return false;
}

void MemoryInstitutionService::removeToken(const std::string& token) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    tokens_.erase(token);
}

bool MemoryInstitutionService::authenticate(const std::string& token,
                                            MemoryPrincipal& out) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = tokens_.find(token);
    if (it == tokens_.end()) return false;
    out = it->second;
    return true;
}

bool MemoryInstitutionService::requireRole(const std::string& token, MemoryRole required,
                                           MemoryPrincipal& principal,
                                           std::string& error) const {
    if (!authenticate(token, principal)) {
        error = "nao autenticado";
        return false;
    }
    if (static_cast<int>(principal.role) < static_cast<int>(required)) {
        error = "permissao insuficiente: requer " + roleName(required);
        return false;
    }
    return true;
}

std::string MemoryInstitutionService::readContext(
    const std::string& token, const std::string& query, size_t limit,
    const std::vector<std::string>& domains, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPrincipal principal;
    if (!requireRole(token, MemoryRole::Viewer, principal, error)) return "";
    if (!store_) {
        error = "instituicao nao configurada";
        return "";
    }
    return store_->searchContext(scope(), query, limit, domains, nullptr);
}

std::string MemoryInstitutionService::writeLesson(
    const std::string& token, const std::string& domain, const std::string& triggerError,
    const std::string& lessonRule, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPrincipal principal;
    if (!requireRole(token, MemoryRole::Editor, principal, error)) return "";
    if (!store_) {
        error = "instituicao nao configurada";
        return "";
    }
    const std::string id = store_->recordLesson(scope(), domain, triggerError, lessonRule,
                                               principal.id, "institution");
    if (id.empty()) error = "falha ao gravar licao institucional";
    else store_->recordAudit(principal.id, "institution_write_lesson", id, "", domain);
    return id;
}

bool MemoryInstitutionService::setPolicy(const std::string& token, const std::string& key,
                                         const taskforge::Json& value, bool immutable,
                                         std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPrincipal principal;
    if (!requireRole(token, MemoryRole::Admin, principal, error)) return false;
    if (!store_) {
        error = "instituicao nao configurada";
        return false;
    }
    return store_->setPolicy(scope(), key, value, immutable, principal.id, error);
}

taskforge::Json MemoryInstitutionService::status(const std::string& token,
                                                 std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    MemoryPrincipal principal;
    if (!requireRole(token, MemoryRole::Viewer, principal, error)) {
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    if (!store_) {
        error = "instituicao nao configurada";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    return taskforge::Json{{"ok", true},
                           {"institutionId", institutionId_},
                           {"scope", scope()},
                           {"principal", principal.id},
                           {"role", roleName(principal.role)},
                           {"lessons", static_cast<long long>(store_->activeLessonCount(scope()))}};
}

} // namespace memory
} // namespace taskforge
