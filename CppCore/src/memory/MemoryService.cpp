#include "MemoryService.hpp"
#include "assistant/AssistantSession.hpp"
#include "MemoryCatalog.hpp"
#include "MemoryPredicates.hpp"
#include "MemoryExtractor.hpp"
#include "MemoryIndexer.hpp"
#include "MemorySanitizer.hpp"
#include "MemoryPlaybookCompiler.hpp"
#include "MemoryRanker.hpp"
#include "MemoryFacets.hpp"
#include "MemorySources.hpp"
#include "MemoryStateSchema.hpp"
#include "MemoryVectorIndex.hpp"
#include "project/RunnerSecurity.hpp"
#include "rt/MemoryFailure.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <sstream>
#include <thread>

#include "util/Json.hpp"

namespace taskforge {
namespace memory {

namespace fs = std::filesystem;

namespace {

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

std::string isoDaysFromNow(int days) {
    const auto now = std::chrono::system_clock::now() +
        std::chrono::hours(24 * std::max(0, days));
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

std::string isoStartOfDay() {
    const auto now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
#ifdef _WIN32
    const std::time_t midnight = _mkgmtime(&tm);
#else
    const std::time_t midnight = timegm(&tm);
#endif
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &midnight);
#else
    gmtime_r(&midnight, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer);
}

double stableUnitHash(const std::string& text) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return static_cast<double>(hash % 1000000ULL) / 1000000.0;
}

std::string isoDaysAgo(int days) {
    const auto now = std::chrono::system_clock::now() -
        std::chrono::hours(24 * std::max(0, days));
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &value);
#else
    gmtime_r(&value, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer);
}

std::string projectKey(const std::string& projectId) {
    return projectId.empty() ? "default" : projectId;
}

std::string stateOwnerFor(const taskforge::Json& info, const std::string& projectId,
                          const std::string& itemId, const std::string& taskType,
                          std::string& error) {
    const std::string kind = info.value("scopeKind", std::string());
    if (kind == "project") return projectKey(projectId);
    if (kind == "task_type") {
        if (taskType.empty()) error = "task_type ausente para o estado";
        return taskType;
    }
    if (kind == "item") {
        if (itemId.empty()) error = "item_id ausente para o estado";
        return itemId;
    }
    error = "escopo de estado desconhecido";
    return "";
}

std::string oneLine(std::string text, size_t limit = 1200) {
    for (char& c : text) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    if (text.size() > limit) {
        text.resize(limit);
        text += "...";
    }
    return text;
}

bool embeddingInCooldown(const std::chrono::steady_clock::time_point& until) {
    return until != std::chrono::steady_clock::time_point{} &&
           std::chrono::steady_clock::now() < until;
}

void embeddingStartCooldown(std::chrono::steady_clock::time_point& until) {
    until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
}

struct TraceCandidateData {
    std::string id;
    std::string kind;
    double lexical = 0.0;
    double vector = 0.0;
    double fused = 0.0;
};

taskforge::Json traceCandidatesToJson(const std::map<std::string, TraceCandidateData>& candidates,
                                      const std::set<std::string>& delivered,
                                      size_t limit = 50) {
    std::vector<std::pair<std::string, TraceCandidateData>> ordered(candidates.begin(), candidates.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
        if (left.second.fused != right.second.fused) return left.second.fused > right.second.fused;
        if (left.second.lexical != right.second.lexical) return left.second.lexical > right.second.lexical;
        if (left.second.vector != right.second.vector) return left.second.vector > right.second.vector;
        return left.first < right.first;
    });
    taskforge::Json out = taskforge::Json::array();
    int position = 0;
    size_t emitted = 0;
    for (const auto& [id, candidate] : ordered) {
        if (emitted >= limit) break;
        out.push_back(taskforge::Json{
            {"id", id},
            {"kind", candidate.kind},
            {"lexical", candidate.lexical},
            {"vector", candidate.vector},
            {"fused", candidate.fused},
            {"position", position++},
            {"delivered", delivered.count(id) != 0}});
        ++emitted;
    }
    return out;
}

void addTraceCandidate(std::map<std::string, TraceCandidateData>& candidates,
                       const std::string& id, const std::string& kind,
                       double lexical, double vector, double fused) {
    if (id.empty()) return;
    TraceCandidateData& candidate = candidates[id];
    candidate.id = id;
    if (!kind.empty()) candidate.kind = kind;
    if (lexical > 0.0) candidate.lexical = lexical;
    if (vector > 0.0) candidate.vector = vector;
    if (fused > 0.0) candidate.fused = fused;
}

std::string formatRanked(const std::vector<RankedMemory>& ranked, size_t limit,
                         std::vector<MemoryHitRef>* hits) {
    std::ostringstream out;
    size_t emitted = 0;
    for (const auto& memory : ranked) {
        if (emitted >= limit) break;
        if (memory.id.empty() || memory.text.empty()) continue;
        // Trechos sao formatados por searchContext com caminho e posicao; nao
        // duplicar no bloco hibrido de fatos.
        if (memory.kind == "passage") continue;
        std::string tag = "[FATO]";
        if (memory.kind == "lesson") tag = "[LICAO]";
        else if (memory.kind == "playbook") tag = "[PLAYBOOK]";
        else if (memory.kind == "entity") tag = "[ENTIDADE]";
        else if (memory.kind == "episode") tag = "[EPISODIO]";
        out << tag << " " << oneLine(memory.text) << "\n";
        if (hits) hits->push_back(MemoryHitRef{memory.id, memory.kind == "lesson" ? "lesson" : "fact"});
        ++emitted;
    }
    return out.str();
}

} // namespace

struct MemoryService::MaintenanceRuntime {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    bool stop = false;
    bool busy = false;
    MemoryMaintenanceConfig config;
    long long episodes = 0;
    long long triggers = 0;
    long long runs = 0;
    long long failures = 0;
    long long actions = 0;
    std::string lastRunAt;
    std::string lastReason;
    std::string lastError;
    std::chrono::steady_clock::time_point idleDeadline{};
    taskforge::Json lastReport = taskforge::Json::object();
};

MemoryMaintenanceConfig maintenanceConfigFromJson(const taskforge::Json& value) {
    MemoryMaintenanceConfig config;
    if (!value.is_object()) return config;

    config.mode = value.value("consolidationMode", config.mode);
    if (config.mode != "idle" && config.mode != "after_episodes" && config.mode != "off") {
        config.mode = "off";
    }
    if (config.mode == "off" && value.value("consolidationEnabled", false)) {
        config.mode = "idle";
    }
    config.enabled = config.mode != "off";
    config.idleSeconds = std::max(5, std::min(86400,
        value.value("consolidationIdleSeconds", config.idleSeconds)));
    config.episodeThreshold = std::max(1, std::min(100000,
        value.value("consolidationEpisodeThreshold", config.episodeThreshold)));
    config.options = consolidationOptionsFromJson(value);
    if (value.contains("consolidationDormantDays")) {
        config.options.dormantAfterDays = std::max(0, std::min(3650,
            value.value("consolidationDormantDays", config.options.dormantAfterDays)));
    }
    const taskforge::Json lensSwitch = value.contains("lenses") ? value["lenses"] : taskforge::Json();
    config.lensesEnabled = lensSwitch.is_boolean()
        ? lensSwitch.get<bool>()
        : lensSwitch.is_object() ? lensSwitch.value("enabled", false) : false;
    const taskforge::Json pathSwitch = value.contains("paths") ? value["paths"] : taskforge::Json();
    config.pathsEnabled = pathSwitch.is_boolean()
        ? pathSwitch.get<bool>()
        : pathSwitch.is_object() ? pathSwitch.value("enabled", false) : false;
    if (pathSwitch.is_object()) {
        config.pathOptions.minDistinctInputs = std::max<long long>(
            1, std::min<long long>(100, pathSwitch.value("minDistinctInputs", 3)));
        config.pathOptions.prior = std::max(0.01, std::min(100.0,
            pathSwitch.value("prior", 1.0)));
        config.pathOptions.mu = std::max(0.0, std::min(10.0,
            pathSwitch.value("mu", 2.674e-7)));
    }
    const taskforge::Json bridgeSwitch = value.contains("bridges")
        ? value["bridges"] : taskforge::Json();
    config.bridgesEnabled = bridgeSwitch.is_boolean()
        ? bridgeSwitch.get<bool>()
        : bridgeSwitch.is_object() ? bridgeSwitch.value("enabled", false) : false;
    if (bridgeSwitch.is_object()) {
        config.bridgeOptions.maxProposals = static_cast<size_t>(std::max<long long>(
            1, std::min<long long>(1000, bridgeSwitch.value("maxProposals", 5))));
        config.bridgeOptions.maxPerEntity = static_cast<size_t>(std::max<long long>(
            1, std::min<long long>(100, bridgeSwitch.value("maxPerEntity", 1))));
        config.bridgeOptions.maxDiscarded = static_cast<size_t>(std::max<long long>(
            1, std::min<long long>(100000, bridgeSwitch.value("maxDiscarded", 1000))));
    }
    config.claimExtractor = MemoryClaimExtractor::configFromJson(value);
    MemoryCatalog::instance().setMode(config.claimExtractor.catalogMode);
    if ((config.claimExtractor.enabled || config.lensesEnabled || config.pathsEnabled ||
         config.bridgesEnabled) && !config.enabled) {
        // Extracao, lentes e caminhos sao tarefas de manutencao; liga-las sem
        // modo de consolidacao ainda precisa de um gatilho ocioso.
        config.enabled = true;
        config.mode = "idle";
    }
    const taskforge::Json claimSwitch = value.contains("claimExtraction")
        ? value["claimExtraction"] : taskforge::Json();
    if (claimSwitch.is_object()) {
        config.claimMaxPassages = static_cast<size_t>(std::max<long long>(
            1, std::min<long long>(100, claimSwitch.value("maxPassages", 10))));
    }
    return config;
}

taskforge::Json maintenanceConfigToJson(const MemoryMaintenanceConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"mode", config.mode},
        {"idleSeconds", config.idleSeconds},
        {"episodeThreshold", config.episodeThreshold},
        {"options", consolidationOptionsToJson(config.options)},
        {"claimExtraction", taskforge::Json{
             {"enabled", config.claimExtractor.enabled},
             {"maxPassages", static_cast<long long>(config.claimMaxPassages)},
             {"agent", taskforge::Json{
                  {"id", config.claimExtractor.agent.id},
                  {"graph", config.claimExtractor.agent.graph},
                  {"node", config.claimExtractor.agent.node}}}}},
        {"lenses", config.lensesEnabled},
        {"paths", taskforge::Json{{"enabled", config.pathsEnabled},
                                  {"minDistinctInputs", config.pathOptions.minDistinctInputs},
                                  {"prior", config.pathOptions.prior},
                                  {"mu", config.pathOptions.mu}}},
        {"bridges", taskforge::Json{{"enabled", config.bridgesEnabled},
                                    {"maxProposals", static_cast<long long>(config.bridgeOptions.maxProposals)},
                                    {"maxPerEntity", static_cast<long long>(config.bridgeOptions.maxPerEntity)}}},
        {"intelligence", "studio-local"}};
}

PathServingConfig pathServingConfigFromJson(const taskforge::Json& value) {
    PathServingConfig config;
    if (!value.is_object()) return config;
    const taskforge::Json switchValue = value.contains("pathServing")
        ? value["pathServing"] : taskforge::Json();
    if (switchValue.is_boolean()) {
        config.enabled = switchValue.get<bool>();
        return config;
    }
    if (!switchValue.is_object()) return config;
    config.enabled = switchValue.value("enabled", false);
    config.explorationRate = std::max(0.0, std::min(1.0,
        switchValue.value("explorationRate", 0.0)));
    config.minUtility = std::max(0.0, std::min(1.0,
        switchValue.value("minUtility", 0.6)));
    config.minUses = std::max<long long>(1, std::min<long long>(100000,
        switchValue.value("minUses", 3)));
    config.maxPaths = static_cast<size_t>(std::max<long long>(1,
        std::min<long long>(8, switchValue.value("maxPaths", 2))));
    return config;
}

taskforge::Json pathServingConfigToJson(const PathServingConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"explorationRate", config.explorationRate},
        {"minUtility", config.minUtility},
        {"minUses", config.minUses},
        {"maxPaths", static_cast<long long>(config.maxPaths)}};
}

TraversalConfig traversalConfigFromJson(const taskforge::Json& value) {
    TraversalConfig config;
    if (!value.is_object()) return config;
    const taskforge::Json switchValue = value.contains("traversal")
        ? value["traversal"] : taskforge::Json();
    if (switchValue.is_boolean()) {
        config.enabled = switchValue.get<bool>();
        return config;
    }
    if (!switchValue.is_object()) return config;
    config.enabled = switchValue.value("enabled", false);
    config.options.candidateLimit = static_cast<size_t>(std::max<long long>(
        1, std::min<long long>(500, switchValue.value("candidateLimit", 40))));
    config.options.beamWidth = static_cast<size_t>(std::max<long long>(
        1, std::min<long long>(32, switchValue.value("beamWidth", 4))));
    config.options.maxDepth = static_cast<size_t>(std::max<long long>(
        1, std::min<long long>(16, switchValue.value("maxDepth", 3))));
    config.options.expandedLimit = static_cast<size_t>(std::max<long long>(
        1, std::min<long long>(5000, switchValue.value("expandedLimit", 200))));
    config.options.maxResults = static_cast<size_t>(std::max<long long>(
        1, std::min<long long>(50, switchValue.value("maxResults", 8))));
    config.options.maxChars = static_cast<size_t>(std::max<long long>(
        256, std::min<long long>(32000, switchValue.value("maxChars", 4000))));
    config.options.timeBudgetMs = static_cast<int>(std::max<long long>(
        1, std::min<long long>(5000, switchValue.value("timeBudgetMs", 50))));
    config.options.minScore = std::max(0.0, std::min(1.0,
        switchValue.value("minScore", 0.02)));
    config.options.exact = switchValue.value("exact", false);
    return config;
}

taskforge::Json traversalConfigToJson(const TraversalConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"candidateLimit", static_cast<long long>(config.options.candidateLimit)},
        {"beamWidth", static_cast<long long>(config.options.beamWidth)},
        {"maxDepth", static_cast<long long>(config.options.maxDepth)},
        {"expandedLimit", static_cast<long long>(config.options.expandedLimit)},
        {"maxResults", static_cast<long long>(config.options.maxResults)},
        {"maxChars", static_cast<long long>(config.options.maxChars)},
        {"timeBudgetMs", config.options.timeBudgetMs},
        {"minScore", config.options.minScore},
        {"exact", config.options.exact}};
}

MemoryUtilityConfig utilityConfigFromJson(const taskforge::Json& value) {
    MemoryUtilityConfig config;
    if (!value.is_object()) return config;
    const taskforge::Json utility = value.contains("utility") ? value["utility"]
                                                              : taskforge::Json();
    if (!utility.is_object()) return config;
    const std::string source = utility.value("source", "edge");
    if (source == "ledger") config.source = "ledger";
    config.priorA = std::max(0.01, std::min(100.0,
        utility.value("prior", taskforge::Json::object()).value("a", 1.0)));
    config.priorB = std::max(0.01, std::min(100.0,
        utility.value("prior", taskforge::Json::object()).value("b", 1.0)));
    config.maxCreditPerOrigin = std::max(0.0, std::min(1000.0,
        utility.value("maxCreditPerOrigin", 1.0)));
    config.originWindowDays = static_cast<int>(std::max<long long>(
        1, std::min<long long>(3650, utility.value("originWindowDays", 7))));
    return config;
}

taskforge::Json utilityConfigToJson(const MemoryUtilityConfig& config) {
    return taskforge::Json{
        {"source", config.source},
        {"prior", taskforge::Json{{"a", config.priorA}, {"b", config.priorB}}},
        {"maxCreditPerOrigin", config.maxCreditPerOrigin},
        {"originWindowDays", config.originWindowDays}};
}

MemoryCreditAuditConfig creditAuditConfigFromJson(const taskforge::Json& value) {
    MemoryCreditAuditConfig config;
    if (!value.is_object()) return config;
    const taskforge::Json audit = value.contains("creditAudit")
        ? value["creditAudit"] : taskforge::Json();
    if (!audit.is_object()) return config;
    config.enabled = audit.value("enabled", false);
    config.sampleRate = std::max(0.0, std::min(1.0, audit.value("sampleRate", 0.1)));
    config.minSuspectRejections = static_cast<int>(std::max<long long>(
        0, std::min<long long>(1000, audit.value("minSuspectRejections", 3))));
    config.repetitions = static_cast<int>(std::max<long long>(
        1, std::min<long long>(10, audit.value("repetitions", 2))));
    config.pairwiseTopM = static_cast<int>(std::max<long long>(
        0, std::min<long long>(10, audit.value("pairwiseTopM", 3))));
    config.maxAuditsPerDay = static_cast<int>(std::max<long long>(
        0, std::min<long long>(1000, audit.value("maxAuditsPerDay", 20))));
    config.maxTokensPerDay = std::max<long long>(
        0, std::min<long long>(100000000, audit.value("maxTokensPerDay", 20000)));
    config.validatorAuditRate = std::max(0.0, std::min(1.0,
        audit.value("validatorAuditRate", 0.05)));
    config.minValidatorAudits = static_cast<int>(std::max<long long>(
        0, std::min<long long>(10000, audit.value("minValidatorAudits", 20))));
    config.maxValidatorDivergence = std::max(0.0, std::min(1.0,
        audit.value("maxValidatorDivergence", 0.10)));
    config.snapshotRetentionDays = static_cast<int>(std::max<long long>(
        1, std::min<long long>(3650, audit.value("snapshotRetentionDays", 14))));
    config.maxSnapshotBytes = std::max<long long>(1024,
        std::min<long long>(10LL * 1024 * 1024 * 1024,
                            audit.value("maxSnapshotBytes", 50LL * 1024 * 1024)));
    config.containerImage = audit.value("containerImage", std::string());
    config.containerMemoryMB = static_cast<int>(std::max<long long>(
        64, std::min<long long>(65536, audit.value("containerMemoryMB", 512))));
    config.containerTimeoutSeconds = static_cast<int>(std::max<long long>(
        1, std::min<long long>(86400, audit.value("containerTimeoutSeconds", 300))));
    config.containerPidsLimit = static_cast<int>(std::max<long long>(
        16, std::min<long long>(4096, audit.value("containerPidsLimit", 256))));
    return config;
}

taskforge::Json creditAuditConfigToJson(const MemoryCreditAuditConfig& config) {
    return taskforge::Json{
        {"enabled", config.enabled},
        {"sampleRate", config.sampleRate},
        {"minSuspectRejections", config.minSuspectRejections},
        {"repetitions", config.repetitions},
        {"pairwiseTopM", config.pairwiseTopM},
        {"maxAuditsPerDay", config.maxAuditsPerDay},
        {"maxTokensPerDay", config.maxTokensPerDay},
        {"validatorAuditRate", config.validatorAuditRate},
        {"minValidatorAudits", config.minValidatorAudits},
        {"maxValidatorDivergence", config.maxValidatorDivergence},
        {"snapshotRetentionDays", config.snapshotRetentionDays},
        {"maxSnapshotBytes", config.maxSnapshotBytes},
        {"containerImage", config.containerImage},
        {"containerMemoryMB", config.containerMemoryMB},
        {"containerTimeoutSeconds", config.containerTimeoutSeconds},
        {"containerPidsLimit", config.containerPidsLimit}};
}

MemoryService::~MemoryService() {
    stopAllMaintenance();
}

MemoryService& MemoryService::getInstance() {
    static MemoryService instance;
    static const bool intelligenceRegistered = []() {
        MemoryClaimExtractor::setIntelligence(assistant::studioClaimIntelligence());
        return true;
    }();
    (void)intelligenceRegistered;
    return instance;
}

std::string MemoryService::safeName(const std::string& projectId) {
    std::string out = projectId.empty() ? "default" : projectId;
    for (char& c : out) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    }
    return out;
}

std::string MemoryService::scopeId(const std::string& projectId) const {
    return "project:" + (projectId.empty() ? "default" : projectId);
}

std::string MemoryService::defaultPath(const std::string&) const {
    if (const char* env = std::getenv("ARESTRIX_MEMORY_DB"); env && *env) return env;
    return ".arestrix/memory/memory.db";
}

std::string MemoryService::databasePath(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = paths_.find(projectKey(projectId));
    return it == paths_.end() ? std::string() : it->second;
}

std::string MemoryService::defaultTelemetryPath(const std::string& projectId) const {
    if (const char* env = std::getenv("ARESTRIX_TELEMETRY_DB"); env && *env) return env;
    auto it = paths_.find(projectKey(projectId));
    if (it != paths_.end() && !it->second.empty() && it->second != ":memory:") {
        std::filesystem::path db(it->second);
        if (db.filename() == "memory.db" && db.parent_path().filename() == "memory" &&
            db.parent_path().parent_path().filename() == ".arestrix") {
            return (db.parent_path().parent_path() / "telemetry" / "telemetry.db").string();
        }
    }
    return ":memory:";
}

bool MemoryService::configureTelemetry(const std::string& projectId, const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectKey(projectId);
    const std::string path = dbPath.empty() ? defaultTelemetryPath(id) : dbPath;
    auto existing = telemetry_.find(id);
    if (existing != telemetry_.end() && existing->second && existing->second->isOpen() &&
        telemetryPaths_.find(id) != telemetryPaths_.end() && telemetryPaths_.at(id) == path) {
        return true;
    }
    auto store = std::make_unique<MemoryStore>(path, MemoryStore::kMaxActiveLessons, false, false);
    std::string error;
    if (!store->open(error)) return false;
    if (!store->ensureScope(scopeId(id), "project", "", id)) return false;
    telemetry_[id] = std::move(store);
    telemetryPaths_[id] = path;
    return true;
}

std::string MemoryService::telemetryDatabasePath(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = telemetryPaths_.find(projectKey(projectId));
    return it == telemetryPaths_.end() ? std::string() : it->second;
}

MemoryStore* MemoryService::telemetryStore(const std::string& projectId) {
    return ensureTelemetry(projectId);
}

MemoryStore* MemoryService::existingStore(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = stores_.find(projectKey(projectId));
    if (it == stores_.end() || !it->second || !it->second->isOpen()) return nullptr;
    return it->second.get();
}

MemoryStore* MemoryService::ensureTelemetry(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectKey(projectId);
    auto it = telemetry_.find(id);
    if (it != telemetry_.end() && it->second && it->second->isOpen()) return it->second.get();
    if (!configureTelemetry(id, "")) return nullptr;
    it = telemetry_.find(id);
    return it == telemetry_.end() ? nullptr : it->second.get();
}

bool MemoryService::configureCatalog(const std::string& projectId, const std::string& mode,
                                     const std::string& catalogPath, std::string* error) {
    (void)projectId;
    MemoryCatalog& catalog = MemoryCatalog::instance();
    catalog.resetBuiltin();
    catalog.setMode(mode);
    if (!catalogPath.empty()) {
        std::string localError;
        std::string audit;
        if (!catalog.loadProjectFile(catalogPath, &audit, &localError)) {
            if (error) *error = localError;
            return false;
        }
        if (auto* store = existingStore(projectId)) {
            if (!audit.empty() && audit != "[]") {
                store->recordAudit("catalog", "catalog_override", projectId, "", audit);
            }
        }
    }
    if (!catalogPath.empty()) {
        if (auto* store = existingStore(projectId)) {
            store->setMeta("catalog_fingerprint", catalog.fingerprint());
            store->setMeta("catalog_mode", catalog.mode());
        }
    }
    return true;
}

std::string MemoryService::catalogFingerprint() const {
    return MemoryCatalog::instance().fingerprint();
}

bool MemoryService::configure(const std::string& projectId, bool persistent,
                              const std::string& dbPath, bool readOnly) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = projectId.empty() ? "default" : projectId;
    // A0: modo nao persistente e SEMPRE :memory:. Um caminho de arquivo
    // so pode ser aberto com persistent=true; caso contrario, clearSession
    // poderia purgar o banco real do projeto.
    const std::string path = persistent
        ? (dbPath.empty() ? defaultPath(id) : dbPath)
        : ":memory:";

    auto existing = stores_.find(id);
    if (existing != stores_.end() && persistent_.find(id) != persistent_.end() &&
        persistent_.at(id) == persistent && paths_.find(id) != paths_.end() &&
        paths_.at(id) == path && readOnly_.find(id) != readOnly_.end() &&
        readOnly_.at(id) == readOnly) {
        return existing->second && existing->second->isOpen();
    }

    auto store = std::make_unique<MemoryStore>(path, MemoryStore::kMaxActiveLessons, readOnly);
    std::string error;
    if (!store->open(error)) {
        return false;
    }
    // O banco congelado do medico A/B ja nasce com escopo; abrir somente
    // leitura nao pode tentar criar nem migrar nada.
    if (!readOnly && !store->ensureScope(scopeId(id), "project", "", id)) {
        return false;
    }
    stores_[id] = std::move(store);
    persistent_[id] = persistent;
    paths_[id] = path;
    readOnly_[id] = readOnly;

    if (persistent && !readOnly && path != ":memory:") {
        configureWorkspaceForProjectLocked(id, path);
        const fs::path catalogPath = fs::path(path).parent_path() / "catalog.json";
        std::string catalogMode = MemoryCatalog::instance().mode();
        if (const char* env = std::getenv("ARESTRIX_MEMORY_CATALOG_MODE"); env && *env) {
            catalogMode = env;
        }
        std::string catalogError;
        configureCatalog(id, catalogMode,
                         fs::is_regular_file(catalogPath) ? catalogPath.string() : "",
                         &catalogError);
    }
    return true;
}

std::string MemoryService::workspaceScope(const std::string& workspaceId) const {
    return "workspace:" + (workspaceId.empty() ? "default" : workspaceId);
}

bool MemoryService::configureWorkspaceLocked(const std::string& workspaceId,
                                             const std::string& dbPath,
                                             const std::string& parentId,
                                             const std::string& name) {
    const std::string id = workspaceId.empty() ? "default" : workspaceId;
    if (dbPath.empty()) return false;
    auto existing = workspaceStores_.find(id);
    if (existing != workspaceStores_.end() && existing->second &&
        existing->second->isOpen() && workspacePaths_[id] == dbPath) {
        return true;
    }
    auto store = std::make_unique<MemoryStore>(dbPath);
    std::string error;
    if (!store->open(error)) return false;
    if (!parentId.empty()) {
        store->ensureScope(parentId, "institution", "", parentId);
    }
    if (!store->ensureScope(workspaceScope(id), "workspace", parentId, name.empty() ? id : name)) {
        return false;
    }
    workspaceStores_[id] = std::move(store);
    workspacePaths_[id] = dbPath;
    return true;
}

void MemoryService::configureWorkspaceForProjectLocked(const std::string& projectId,
                                                       const std::string& dbPath) {
    const std::filesystem::path projectDb(dbPath);
    if (projectDb.filename() != "memory.db") return;
    const std::filesystem::path memoryDir = projectDb.parent_path();
    const std::filesystem::path arestrixDir = memoryDir.parent_path();
    if (memoryDir.filename() != "memory" || arestrixDir.filename() != ".arestrix") return;
    const std::filesystem::path root = arestrixDir.parent_path();
    const std::filesystem::path workspaceDb = root / ".workspace" / "memory" / "memory.db";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(workspaceDb, ec)) return;
    std::string workspaceId;
    if (const char* env = std::getenv("ARESTRIX_WORKSPACE_ID"); env && *env) {
        workspaceId = env;
    } else {
        workspaceId = root.filename().string();
        if (workspaceId.empty()) workspaceId = "default";
    }
    if (!configureWorkspaceLocked(workspaceId, workspaceDb.string(), "", workspaceId)) return;
    projectWorkspace_[projectId] = workspaceId;
}

bool MemoryService::configureWorkspace(const std::string& workspaceId,
                                       const std::string& dbPath,
                                       const std::string& parentId,
                                       const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return configureWorkspaceLocked(workspaceId, dbPath, parentId, name);
}

bool MemoryService::configureProjectWorkspace(const std::string& projectId,
                                              const std::string& workspaceId,
                                              const std::string& dbPath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!configureWorkspaceLocked(workspaceId, dbPath, "", workspaceId)) return false;
    projectWorkspace_[projectId.empty() ? "default" : projectId] = workspaceId;
    return true;
}

MemoryStore* MemoryService::workspaceStore(const std::string& workspaceId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = workspaceStores_.find(workspaceId.empty() ? "default" : workspaceId);
    return it == workspaceStores_.end() ? nullptr : it->second.get();
}

void MemoryService::closeWorkspace(const std::string& workspaceId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string id = workspaceId.empty() ? "default" : workspaceId;
    workspaceStores_.erase(id);
    workspacePaths_.erase(id);
}

std::string MemoryService::workspaceIdForProject(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = projectWorkspace_.find(projectId.empty() ? "default" : projectId);
    return it == projectWorkspace_.end() ? std::string() : it->second;
}

taskforge::Json MemoryService::workspaceStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const std::string workspaceId = workspaceIdForProject(projectId);
    if (workspaceId.empty()) {
        return taskforge::Json{{"configured", false}, {"projectId", projectId}};
    }
    auto* store = workspaceStore(workspaceId);
    if (!store) return taskforge::Json{{"configured", false}, {"projectId", projectId}};
    const std::string scope = workspaceScope(workspaceId);
    auto* projectStore = ensureStore(projectId);
    (void)projectStore;
    auto summary = store->consolidationSummary(scope);
    return taskforge::Json{
        {"configured", true},
        {"projectId", projectId},
        {"workspaceId", workspaceId},
        {"scope", scope},
        {"database", workspacePaths_[workspaceId]},
        {"lessons", static_cast<long long>(store->activeLessonCount(scope))},
        {"playbooks", static_cast<long long>(store->listPlaybooks(scope, "", "active", 1000).size())},
        {"activeEdges", summary.value("active", 0LL)},
        {"aliases", summary.value("aliases", 0LL)}};
}

bool MemoryService::promoteToWorkspace(const std::string& projectId, const std::string& kind,
                                       const std::string& memoryId,
                                       const taskforge::Json& evidence,
                                       const std::string& actor, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!evidence.is_array() || evidence.empty()) {
        error = "promocao sem evidencia";
        return false;
    }
    auto* source = ensureStore(projectId);
    const std::string workspaceId = workspaceIdForProject(projectId);
    auto* target = workspaceStore(workspaceId);
    if (!source || !target) {
        error = "workspace nao configurado";
        return false;
    }
    const std::string sourceScope = scopeId(projectId);
    const std::string targetScope = workspaceScope(workspaceId);
    const std::string reviewer = actor.empty() ? "human" : actor;
    taskforge::Json promotionEvidence = evidence;
    sanitizeJsonInPlace(promotionEvidence);
    std::string promotedId;
    std::string promotedKind = kind;

    if (kind == "lesson") {
        MemoryLesson lesson;
        bool found = false;
        for (const auto& item : source->getLessons(sourceScope)) {
            if (item.id == memoryId) {
                lesson = item;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "licao nao encontrada";
            return false;
        }
        promotedId = target->recordLesson(targetScope, lesson.domain, lesson.triggerError,
                                          lesson.lessonRule, projectId, "workspace_promotion");
        if (promotedId.empty()) {
            error = "falha ao promover licao";
            return false;
        }
    } else if (kind == "fact") {
        MemoryEdgeRecord edge;
        if (!source->getEdge(sourceScope, memoryId, edge) || edge.status != "active" ||
            isStructuralPredicate(edge.relation)) {
            error = "fato nao encontrado ou nao promovivel";
            return false;
        }
        const auto names = source->nodeNames(sourceScope);
        auto srcName = names.find(edge.srcId);
        auto dstName = names.find(edge.dstId);
        const std::string srcNode = srcName == names.end() ? "" :
            target->resolveNode(targetScope, srcName->second, "entity");
        const std::string dstNode = dstName == names.end() ? "" :
            target->resolveNode(targetScope, dstName->second, "entity");
        if (srcNode.empty() || (!edge.dstId.empty() && dstNode.empty())) {
            error = "falha ao resolver entidades do fato";
            return false;
        }
        auto valueOpt = taskforge::jsonutil::tryParse(edge.valueJson);
        taskforge::Json value = valueOpt ? *valueOpt : taskforge::Json(edge.valueJson);
        auto metadataOpt = taskforge::jsonutil::tryParse(edge.metadata);
        taskforge::Json metadata = metadataOpt && metadataOpt->is_object()
            ? *metadataOpt : taskforge::Json::object();
        metadata["promotionEvidence"] = promotionEvidence;
        metadata["promotedFromProject"] = projectId;
        metadata["promotedBy"] = reviewer;
        metadata["promotedAt"] = isoNow();
        promotedId = target->recordEdge(targetScope, srcNode, dstNode, edge.relation, value,
                                        edge.validFrom, edge.validTo, edge.confidence, "active",
                                        "workspace_promotion", projectId, metadata);
        if (promotedId.empty()) {
            error = "falha ao promover fato";
            return false;
        }
    } else if (kind == "playbook") {
        MemoryPlaybookRecord playbook;
        if (!source->getPlaybook(sourceScope, memoryId, playbook)) {
            error = "playbook nao encontrado";
            return false;
        }
        auto steps = taskforge::jsonutil::tryParse(playbook.stepsJson);
        auto preconditions = taskforge::jsonutil::tryParse(playbook.preconditionsJson);
        const std::string stepsJson = steps && steps->is_array() ? steps->dump() : "[]";
        const std::string existing = target->playbookIdBySignature(targetScope, playbook.taskType, stepsJson);
        if (!existing.empty()) {
            MemoryPlaybookRecord current;
            target->getPlaybook(targetScope, existing, current);
            if (!target->updatePlaybook(targetScope, existing, 0, 0,
                                        std::max(current.confidence, playbook.confidence),
                                        "active", projectId, promotionEvidence, error)) {
                return false;
            }
            promotedId = existing;
        } else {
            promotedId = target->recordPlaybook(
                targetScope, playbook.taskType, playbook.title,
                steps && steps->is_array() ? *steps : taskforge::Json::array(),
                preconditions && preconditions->is_object() ? *preconditions : taskforge::Json::object(),
                playbook.confidence, "active", projectId, promotionEvidence,
                playbook.successCount, playbook.failureCount);
            if (promotedId.empty()) {
                error = "falha ao promover playbook";
                return false;
            }
        }
    } else {
        error = "tipo de memoria nao promovivel: " + kind;
        return false;
    }

    {
        MemoryEvidenceRecord snapshot;
        snapshot.scopeId = targetScope;
        snapshot.targetKind = kind == "fact" ? "fact" : kind;
        snapshot.targetId = promotedId;
        snapshot.action = "promote_workspace";
        snapshot.ok = true;
        snapshot.summary = "Promovido do projeto " + projectId + " por " + reviewer;
        target->recordEvidence(snapshot);
    }
    source->recordAudit(reviewer, "promote_to_workspace", memoryId, "", targetScope + "|" + promotedId);
    target->recordAudit(reviewer, "receive_workspace_promotion", promotedId, projectId, kind);
    invalidateBriefCache();
    return true;
}

bool MemoryService::isPersistent(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = persistent_.find(projectKey(projectId));
    return it != persistent_.end() && it->second;
}

MemoryStore* MemoryService::ensureStore(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = stores_.find(projectId);
    if (it != stores_.end() && it->second && it->second->isOpen()) return it->second.get();
    if (!configure(projectId, false)) return nullptr;
    it = stores_.find(projectId);
    return it == stores_.end() ? nullptr : it->second.get();
}

MemoryStore* MemoryService::store(const std::string& projectId) {
    return ensureStore(projectId);
}

bool MemoryService::setWorking(const std::string& projectId, const std::string& itemId,
                               const std::string& key, const taskforge::Json& value) {
    auto* s = ensureStore(projectId);
    return s && s->setWorking(scopeId(projectId), itemId, key, value);
}

taskforge::Json MemoryService::getWorking(const std::string& projectId, const std::string& itemId,
                                          const std::string& key) {
    auto* s = ensureStore(projectId);
    return s ? s->getWorking(scopeId(projectId), itemId, key) : taskforge::Json();
}

taskforge::Json MemoryService::workingSnapshot(const std::string& projectId, const std::string& itemId,
                                               const std::vector<std::string>& allowedKeys) {
    auto* s = ensureStore(projectId);
    return s ? s->workingSnapshot(scopeId(projectId), itemId, allowedKeys) : taskforge::Json::object();
}

bool MemoryService::ingestWorkingDelta(const std::string& projectId, const std::string& itemId,
                                       const taskforge::Json& delta) {
    auto* s = ensureStore(projectId);
    return s && s->ingestWorkingDelta(scopeId(projectId), itemId, delta);
}

void MemoryService::clearWorkingItem(const std::string& projectId, const std::string& itemId) {
    if (auto* s = ensureStore(projectId)) s->clearWorkingItem(scopeId(projectId), itemId);
}

bool MemoryService::declareStateKey(const std::string& projectId, const std::string& key,
                                    const std::string& scopeKind, const taskforge::Json& schema,
                                    long long maxBytes, long long ttlSeconds,
                                    const std::string& valueMode, long long versionedKeep,
                                    std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    return s->declareStateKey(scopeId(projectId), key, scopeKind, schema, maxBytes,
                              ttlSeconds, valueMode, versionedKeep, error);
}

taskforge::Json MemoryService::stateKeyInfo(const std::string& projectId,
                                            const std::string& key) {
    auto* s = existingStore(projectId);
    return s ? s->stateKeyInfo(scopeId(projectId), key) : taskforge::Json::object();
}

taskforge::Json MemoryService::stateKeys(const std::string& projectId) {
    auto* s = existingStore(projectId);
    return s ? s->stateKeys(scopeId(projectId)) : taskforge::Json::array();
}

taskforge::Json MemoryService::getState(const std::string& projectId, const std::string& key,
                                        const std::string& itemId, const std::string& taskType,
                                        std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->getState(scopeId(projectId), key, owner, error);
}

taskforge::Json MemoryService::setState(const std::string& projectId, const std::string& key,
                                        const std::string& itemId, const std::string& taskType,
                                        const taskforge::Json& value, bool hasExpected,
                                        long long expectedVersion, std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) {
        error = "chave de estado nao declarada: " + key;
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const auto schema = info.contains("schema") ? info["schema"] : taskforge::Json::object();
    taskforge::Json cleanValue = value;
    sanitizeJsonInPlace(cleanValue);
    if (!validateStateValue(schema, cleanValue, error)) {
        return taskforge::Json{{"ok", false}, {"error", error}, {"schemaError", true}};
    }
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->setState(scopeId(projectId), key, owner, cleanValue, hasExpected,
                       expectedVersion, error);
}

taskforge::Json MemoryService::listState(const std::string& projectId, const std::string& itemId,
                                         const std::string& taskType) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "banco de conhecimento indisponivel"}};
    taskforge::Json out{{"ok", true}, {"keys", taskforge::Json::array()}};
    for (const auto& info : s->stateKeys(scopeId(projectId))) {
        std::string error;
        const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
        if (owner.empty()) continue;
        taskforge::Json item = s->listState(scopeId(projectId), info.value("key", std::string()), owner);
        item["scopeKind"] = info.value("scopeKind", std::string());
        item["scopeOwner"] = owner;
        item["valueMode"] = info.value("valueMode", std::string());
        item["ttlSeconds"] = info.value("ttlSeconds", 0LL);
        out["keys"].push_back(item);
    }
    return out;
}

taskforge::Json MemoryService::stateHistory(const std::string& projectId, const std::string& key,
                                            const std::string& itemId, const std::string& taskType,
                                            size_t limit) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "banco de conhecimento indisponivel"}};
    const auto info = s->stateKeyInfo(scopeId(projectId), key);
    if (info.empty()) return taskforge::Json{{"ok", false}, {"error", "chave de estado nao declarada"}};
    std::string error;
    const std::string owner = stateOwnerFor(info, projectId, itemId, taskType, error);
    if (owner.empty()) return taskforge::Json{{"ok", false}, {"error", error}};
    return s->stateHistory(scopeId(projectId), key, owner, limit);
}

void MemoryService::clearProject(const std::string& projectId) {
    purgeProject(projectId, false, true);
}

bool MemoryService::purgeProject(const std::string& projectId, bool keepAudit, bool vacuum,
                                 bool dropTombstones) {
    MemoryPurgeOptions options;
    options.keepAudit = keepAudit;
    options.dropTombstones = dropTombstones;
    options.vacuum = vacuum;
    bool ok = true;
    bool any = false;
    if (auto* s = existingStore(projectId)) {
        any = true;
        ok = s->purge(scopeId(projectId), options) && ok;
    }
    if (auto* t = ensureTelemetry(projectId)) {
        any = true;
        ok = t->purge(scopeId(projectId), options) && ok;
    }
    return any && ok;
}

MemoryPruneResult MemoryService::prune(const std::string& projectId, int olderThanDays) {
    if (olderThanDays <= 0) return MemoryPruneResult{};
    MemoryPruneResult total;
    if (auto* t = ensureTelemetry(projectId)) {
        const auto pruned = t->prune(scopeId(projectId), olderThanDays);
        total.episodes += pruned.episodes;
        total.hits += pruned.hits;
        total.usage += pruned.usage;
    }
    if (auto* s = existingStore(projectId)) {
        const auto pruned = s->prune(scopeId(projectId), olderThanDays);
        total.episodes += pruned.episodes;
        total.hits += pruned.hits;
        total.usage += pruned.usage;
    }
    return total;
}

bool MemoryService::freezeKnowledge(const std::string& projectId, const std::string& targetPath,
                                    std::string& fingerprint, std::string& error) {
    fingerprint.clear();
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    if (!s->backupTo(targetPath, error)) return false;
    // O medico abre a copia com SQLITE_OPEN_READONLY. Consolidar o WAL agora
    // garante que a leitura nao crie -wal/-shm e que o hash permaneca estavel.
    if (!MemoryStore::sealForReadOnly(targetPath, error)) return false;
    fingerprint = project::RunnerSecurity::sha256File(targetPath, error);
    if (fingerprint.empty()) return false;
    return true;
}

std::string MemoryService::recordLesson(const std::string& projectId, const std::string& domain,
                                        const std::string& triggerError, const std::string& lessonRule,
                                        const std::string& sourceItem, const std::string& sourceKind) {
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    invalidateBriefCache();
    return s->recordLesson(scopeId(projectId), domain, triggerError, lessonRule, sourceItem, sourceKind);
}

std::string MemoryService::proposeLesson(const std::string& projectId, const std::string& domain,
                                         const std::string& triggerError, const std::string& lessonRule,
                                         const std::string& sourceItem,
                                         const std::string& errorClass,
                                         const std::string& operationKey) {
    auto* s = ensureStore(projectId);
    if (!s) return "";
    const std::string scope = scopeId(projectId);
    std::string rule = lessonRule;
    if (!errorClass.empty()) {
        // E5: o texto da falha carrega ids/horas/numeros variaveis. Normaliza
        // antes de gravar para que a deduplicacao por payload funcione.
        const std::string normalized = rt::normalizeFailureText(rule);
        if (!normalized.empty()) rule = normalized;
    }
    taskforge::Json payload{{"domain", domain.empty() ? "geral" : domain},
                            {"triggerError", triggerError},
                            {"lessonRule", rule}};
    if (!errorClass.empty()) payload["errorClass"] = errorClass;
    taskforge::Json evidence = taskforge::Json::array(
        {taskforge::Json{{"kind", "agent"}, {"node", sourceItem}}});
    if (!errorClass.empty()) evidence.push_back(taskforge::Json{{"kind", "errorClass"}, {"value", errorClass}});
    return s->recordProposal(scope, "lesson", payload, "", evidence, 0.6, operationKey);
}

std::vector<MemoryLesson> MemoryService::getLessons(const std::string& projectId,
                                                    const std::string& domain) {
    auto* s = ensureStore(projectId);
    return s ? s->getLessons(scopeId(projectId), domain) : std::vector<MemoryLesson>();
}

size_t MemoryService::activeLessonCount(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->activeLessonCount(scopeId(projectId)) : 0;
}

std::string MemoryService::formatLessons(const std::string& projectId,
                                         const std::vector<std::string>& domains) {
    auto* s = ensureStore(projectId);
    return s ? s->formatLessons(scopeId(projectId), domains) : std::string();
}

taskforge::Json MemoryService::servePaths(const std::string& projectId,
                                          const std::string& taskType,
                                          const std::string& intent,
                                          const std::vector<std::string>& access) {
    const auto config = pathServingConfig(projectId);
    if (!config.enabled) return taskforge::Json{{"ok", true}, {"served", false},
                                                {"enabled", false},
                                                {"paths", taskforge::Json::array()}};
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"served", false},
                                   {"paths", taskforge::Json::array()}};
    auto served = MemoryPaths::serve(*s, scopeId(projectId), taskType, intent, access,
                                     8, config.explorationRate);
    taskforge::Json filtered = taskforge::Json::array();
    for (const auto& path : served["paths"]) {
        if (!path.is_object()) continue;
        if (path.value("utility", 0.0) < config.minUtility) continue;
        if (path.value("uses", 0LL) < config.minUses) continue;
        if (filtered.size() >= config.maxPaths) break;
        filtered.push_back(path);
    }
    served["paths"] = filtered;
    served["served"] = !filtered.empty();
    served["enabled"] = true;
    return served;
}

std::string MemoryService::buildBrief(const std::string& projectId, const std::string& itemId,
                                      const std::vector<std::string>& domains,
                                      const std::vector<std::string>& allowedKeys,
                                      size_t maxTokens, bool includeLessons,
                                      bool includeBlackboard, MemoryBriefInfo* info,
                                      const std::string& taskType,
                                      const std::string& intent,
                                      const std::string& runId,
                                      const std::string& nodeId) {
    if (info) *info = MemoryBriefInfo{};
    const std::string intentHash = intent.empty()
        ? std::string() : project::RunnerSecurity::sha256(intent);
    auto recordStep = [&](const std::string& kind, const std::string& query,
                          const taskforge::Json& candidates) {
        if (runId.empty() || nodeId.empty()) return;
        recordTraceStep(projectId, runId, nodeId, itemId, taskType, intentHash, "",
                        taskforge::Json{{"kind", kind},
                                        {"queryHash", query.empty() ? std::string()
                                                                    : project::RunnerSecurity::sha256(query)},
                                        {"candidates", candidates}});
    };
    auto candidatesFromInfo = [&](const MemoryBriefInfo& briefInfo) {
        std::map<std::string, TraceCandidateData> candidates;
        std::set<std::string> delivered;
        for (const auto& id : briefInfo.lessonIds) {
            addTraceCandidate(candidates, id, "lesson", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.factIds) {
            addTraceCandidate(candidates, id, "fact", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.playbookIds) {
            addTraceCandidate(candidates, id, "playbook", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.pathIds) {
            addTraceCandidate(candidates, id, "path", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.pathClaimIds) {
            addTraceCandidate(candidates, id, "fact", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.traversalIds) {
            addTraceCandidate(candidates, id, "fact", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        for (const auto& id : briefInfo.facetIds) {
            addTraceCandidate(candidates, id, "facet", 0.0, 0.0, 0.0);
            delivered.insert(id);
        }
        if (!briefInfo.workingKeys.empty()) {
            const std::string stateId = "state:" + itemId;
            addTraceCandidate(candidates, stateId, "state", 0.0, 0.0, 0.0);
            delivered.insert(stateId);
        }
        return traceCandidatesToJson(candidates, delivered);
    };
    if (!includeLessons && !includeBlackboard) return std::string();
    // 0 = nao injetar memoria. Nunca interpretar 0 como "sem orcamento" (infinito).
    if (maxTokens == 0) {
        recordStep("brief", "", taskforge::Json::array());
        return std::string();
    }
    auto appendKey = [](std::string& key, const std::string& value) {
        key += std::to_string(value.size());
        key += ':';
        key += value;
        key += '\x1f';
    };
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    std::string cacheKey;
    appendKey(cacheKey, projectId);
    appendKey(cacheKey, std::to_string(s->knowledgeVersion()));
    std::string cachePrefixKey = taskType.empty() ? "default" : taskType;
    std::vector<std::string> cacheDomains = domains;
    std::sort(cacheDomains.begin(), cacheDomains.end());
    for (const auto& domain : cacheDomains) cachePrefixKey += "|" + domain;
    appendKey(cacheKey, std::to_string(
        s->latestPrefixEpoch(scopeId(projectId), cachePrefixKey)));
    appendKey(cacheKey, itemId);
    appendKey(cacheKey, taskType);
    appendKey(cacheKey, intent);
    appendKey(cacheKey, std::to_string(maxTokens));
    appendKey(cacheKey, includeLessons ? "1" : "0");
    appendKey(cacheKey, includeBlackboard ? "1" : "0");
    for (const auto& domain : domains) appendKey(cacheKey, domain);
    for (const auto& allowed : allowedKeys) appendKey(cacheKey, allowed);
    if (auto cached = briefCache_.find(cacheKey); cached != briefCache_.end()) {
        if (info) *info = cached->second.info;
        if (!runId.empty() && !nodeId.empty()) {
            recordStep("brief", "", candidatesFromInfo(cached->second.info));
        }
        return cached->second.value;
    }

    std::map<std::string, TraceCandidateData> traceCandidates;
    std::vector<MemoryLesson> selectedLessons;
    if (includeLessons) {
        // C2: filtra os temas no SQL, sem varrer todas as licoes do projeto.
        selectedLessons = domains.empty()
            ? s->getLessons(scopeId(projectId))
            : s->getLessonsForDomains(scopeId(projectId), domains);
        for (const auto& lesson : selectedLessons) {
            addTraceCandidate(traceCandidates, lesson.id, "lesson", 0.0, 0.0, 0.0);
        }
    }

    std::string workingText;
    std::vector<std::string> workingKeys;
    if (includeBlackboard) {
        auto snapshot = s->workingSnapshot(scopeId(projectId), itemId, allowedKeys);
        if (snapshot.is_object() && snapshot.size() > 0) {
            workingText = "\n[ARESTRIX MEMORY - ESTADO DA TAREFA]\n" + snapshot.dump(2) + "\n";
            for (auto it = snapshot.begin(); it != snapshot.end(); ++it) workingKeys.push_back(it.key());
        }
    }

    const size_t budget = maxTokens == 0 ? std::string::npos : maxTokens * 4;   // ~4 caracteres/token
    std::string brief;
    bool truncated = false;

    // E16: caminhos ativos elegiveis sao servidos antes da travessia/grafo.
    bool pathServed = false;
    if (includeLessons) {
        const auto servedPaths = servePaths(projectId, taskType, intent, {});
        std::string pathText;
        std::vector<std::string> pathIds;
        std::vector<std::string> pathClaimIds;
        if (servedPaths.value("served", false)) {
            for (const auto& path : servedPaths["paths"]) {
                if (!path.is_object()) continue;
                const std::string text = path.value("text", std::string());
                if (text.empty()) continue;
                pathText += text;
                pathIds.push_back(path.value("id", std::string()));
                for (const auto& claim : path["claims"]) {
                    if (claim.is_string()) pathClaimIds.push_back(claim.str());
                }
            }
        }
        if (!pathText.empty()) {
            const std::string header =
                "\n--- [ARESTRIX MEMORY: CAMINHOS CANONICOS] ---\n"
                "Caminhos reutilizaveis; cada afirmacao tambem pode ser citada.\n";
            if (budget == std::string::npos || header.size() + pathText.size() <= budget) {
                brief += header + pathText;
                if (info) {
                    info->pathIds = pathIds;
                    info->pathClaimIds = pathClaimIds;
                }
                for (const auto& id : pathIds) {
                    addTraceCandidate(traceCandidates, id, "path", 0.0, 0.0, 0.0);
                }
                for (const auto& id : pathClaimIds) {
                    addTraceCandidate(traceCandidates, id, "fact", 0.0, 0.0, 0.0);
                }
                taskforge::Json pathCandidates = taskforge::Json::array();
                for (const auto& id : pathIds) {
                    pathCandidates.push_back(taskforge::Json{
                        {"id", id}, {"kind", "path"}, {"delivered", true}});
                }
                for (const auto& id : pathClaimIds) {
                    pathCandidates.push_back(taskforge::Json{
                        {"id", id}, {"kind", "fact"}, {"delivered", true}});
                }
                pathServed = true;
                recordStep("path", "", pathCandidates);
                if (servedPaths.value("exploration", false) && !runId.empty() && !nodeId.empty()) {
                    recordTraceStep(projectId, runId, nodeId, itemId, taskType, intentHash, "",
                                    taskforge::Json{{"kind", "path_exploration"},
                                                    {"exploration", true},
                                                    {"candidates", pathCandidates}});
                }
            } else {
                truncated = true;
            }
        }
    }

    // Facetas ativas entram como conhecimento volatil quando as lentes estao
    // ligadas; o id permite remover a linha inteira durante a execucao.
    if (includeLessons && maintenanceConfig(projectId).lensesEnabled) {
        std::string facetText;
        std::vector<std::string> facetIds;
        for (const auto& facet : s->listFacets(scopeId(projectId))) {
            if (facet.status != "active" || facet.description.empty()) continue;
            if (facetIds.size() >= 4) break;
            const std::string line = "[FACET] [MEMO:" + facet.id + "] " +
                oneLine(facet.description, 400) + "\n";
            if (budget != std::string::npos &&
                brief.size() + facetText.size() + line.size() + 80 > budget) {
                truncated = true;
                break;
            }
            facetText += line;
            facetIds.push_back(facet.id);
            addTraceCandidate(traceCandidates, facet.id, "facet", 0.0, 0.0, 0.0);
        }
        if (!facetText.empty()) {
            const std::string header = "\n--- [ARESTRIX MEMORY: FACETAS] ---\n";
            brief += header + facetText;
            if (info) info->facetIds = facetIds;
        }
    }

    // E17: quando nenhum caminho cobre a consulta, a travessia em feixe
    // fornece um pacote com justificativa e cobertura.
    if (includeLessons && !pathServed) {
        const auto traversal = traversalConfig(projectId);
        if (traversal.enabled) {
            std::string query = taskType;
            if (!intent.empty()) {
                if (!query.empty()) query += " ";
                query += oneLine(intent, 600);
            }
            for (const auto& domain : domains) {
                if (!query.empty()) query += " ";
                query += domain;
            }
            auto found = MemoryTraversal::run(*s, scopeId(projectId), query, taskType,
                                               traversal.options);
            const std::string traversalText = found.value("text", std::string());
            if (!traversalText.empty()) {
                const std::string header =
                    "\n--- [ARESTRIX MEMORY: TRAVESSIA EM FEIXE] ---\n";
                if (budget == std::string::npos ||
                    brief.size() + header.size() + traversalText.size() <= budget) {
                    brief += header + traversalText;
                    std::vector<std::string> ids;
                    for (const auto& node : found["nodes"]) {
                        if (!node.is_object()) continue;
                        const std::string id = node.value("id", std::string());
                        if (id.empty()) continue;
                        ids.push_back(id);
                        addTraceCandidate(traceCandidates, id, "fact", 0.0, 0.0, 0.0);
                    }
                    if (info) info->traversalIds = ids;
                    recordStep("traversal", query, found["nodes"]);
                    if (!runId.empty() && !nodeId.empty()) {
                        recordTraceStep(projectId, runId, nodeId, itemId, taskType, intentHash, "",
                                        taskforge::Json{{"kind", "traversal_coverage"},
                                                        {"coverage", found["coverage"]},
                                                        {"candidates", found["nodes"]}});
                    }
                } else {
                    truncated = true;
                }
            }
        }
    }

    if (!selectedLessons.empty()) {
        const std::string header =
            "\n--- [ARESTRIX MEMORIA SEMANTICA: LICOES APRENDIDAS - PROJETO: " + scopeId(projectId) + "] ---\n"
            "Atencao: os itens anteriores identificaram as seguintes diretrizes para prevencao de erros.\n"
            "Se usar um item, cite o ID [MEMO:<id>] na resposta final.\n";
        if (header.size() <= budget) {
            brief += header;
            int lessonNumber = 1;
            for (const auto& lesson : selectedLessons) {
                std::ostringstream line;
                line << "[" << lessonNumber << "] [MEMO:" << lesson.id << "] (Dominio: "
                     << lesson.domain << ", confianca " << lesson.confidence << ") "
                     << lesson.lessonRule << "\n";
                if (budget != std::string::npos && brief.size() + line.str().size() > budget) {
                    truncated = true;
                    break;
                }
                brief += line.str();
                ++lessonNumber;
                if (info) info->lessonIds.push_back(lesson.id);
            }
        } else {
            truncated = true;
        }
    }

    // Licoes promovidas para o workspace (ancestral) entram como contexto extra.
    if (includeLessons) {
        const std::string workspaceId = workspaceIdForProject(projectId);
        if (!workspaceId.empty()) {
            if (auto* workspace = workspaceStore(workspaceId)) {
                std::string workspaceLessons;
                std::set<std::string> projectRules;
                for (const auto& lesson : selectedLessons) {
                    projectRules.insert(normalizeMemoryKey(lesson.lessonRule));
                }
                for (const auto& lesson : workspace->getLessons(workspaceScope(workspaceId))) {
                    if (!domains.empty() &&
                        std::find(domains.begin(), domains.end(), lesson.domain) == domains.end()) {
                        continue;
                    }
                    if (projectRules.count(normalizeMemoryKey(lesson.lessonRule))) continue;
                    std::string line = "[WORKSPACE] (Dominio: " + lesson.domain + ") " +
                                       lesson.lessonRule + "\n";
                    if (budget != std::string::npos &&
                        brief.size() + workspaceLessons.size() + line.size() > budget) {
                        truncated = true;
                        break;
                    }
                    workspaceLessons += line;
                }
                if (!workspaceLessons.empty()) {
                    const std::string header = "\n--- [ARESTRIX WORKSPACE MEMORY: LICOES] ---\n";
                    if (budget == std::string::npos ||
                        brief.size() + header.size() + workspaceLessons.size() <= budget) {
                        brief += header + workspaceLessons;
                    } else {
                        truncated = true;
                    }
                }
            }
        }
    }

    // Fatos/playbooks ranqueados (hibrido) entram depois das licoes e antes do
    // blackboard. Sem embeddings habilitados, o bloco nem e consultado.
    std::string traceQuery;
    if (includeLessons) {
        std::string query;
        auto appendQuery = [&](const std::string& text) {
            if (text.empty()) return;
            if (!query.empty()) query += " ";
            query += text;
        };
        for (const auto& domain : domains) {
            std::string normalized = domain;
            std::replace(normalized.begin(), normalized.end(), '_', ' ');
            appendQuery(normalized);
        }
        // C1: a intencao do vertice e um template do projeto, nunca dado do
        // usuario; entra na consulta ao lado do task_type e do tema.
        appendQuery(taskType);
        if (!intent.empty()) appendQuery(oneLine(intent, 600));
        if (query.empty() && itemId != "pipeline") query = itemId;
        traceQuery = query;
        std::vector<RankedMemory> ranked;
        taskforge::Json hybridCandidates = taskforge::Json::array();
        if (!query.empty() && hybridRecall(projectId, query, 4, domains, ranked,
                                          &hybridCandidates, {}, taskType)) {
            for (const auto& candidate : hybridCandidates) {
                if (!candidate.is_object()) continue;
                addTraceCandidate(traceCandidates,
                                  candidate.value("id", std::string()),
                                  candidate.value("kind", std::string()),
                                  candidate.value("lexical", 0.0),
                                  candidate.value("vector", 0.0),
                                  candidate.value("fused", 0.0));
            }
            std::string facts;
            std::vector<std::string> factIds;
            for (const auto& memory : ranked) {
                if (memory.kind != "fact" && memory.kind != "playbook") continue;
                std::string line = std::string(memory.kind == "playbook" ? "[PLAYBOOK] " : "[FATO] ") +
                                   "[MEMO:" + memory.id + "] " + oneLine(memory.text, 500) + "\n";
                if (budget != std::string::npos && brief.size() + facts.size() + line.size() > budget) {
                    truncated = true;
                    break;
                }
                facts += line;
                factIds.push_back(memory.id);
            }
            if (!facts.empty()) {
                const std::string header =
                    "\n--- [ARESTRIX MEMORIA: FATOS RELEVANTES] ---\n"
                    "Se usar um item, cite o ID [MEMO:<id>] na resposta final.\n";
                if (budget == std::string::npos || brief.size() + header.size() + facts.size() <= budget) {
                    brief += header + facts;
                    if (info) info->factIds = factIds;
                } else {
                    truncated = true;
                }
            }
        }
    }

    // Playbooks por task_type/dominio. Apenas ativos entram no brief.
    if (includeLessons && (!domains.empty() || !taskType.empty())) {
        std::string playbookText;
        std::vector<std::string> playbookIds;
        std::set<std::string> seenTaskTypes;
        std::vector<std::string> taskTypes;
        if (!taskType.empty()) taskTypes.push_back(taskType);
        for (const auto& domain : domains) {
            if (std::find(taskTypes.begin(), taskTypes.end(), domain) == taskTypes.end()) {
                taskTypes.push_back(domain);
            }
        }
        for (const auto& type : taskTypes) {
            if (!seenTaskTypes.insert(type).second) continue;
            for (const auto& playbook : s->listPlaybooks(scopeId(projectId), type, "active", 1)) {
                auto stepsOpt = taskforge::jsonutil::tryParse(playbook.stepsJson);
                std::string stepsText;
                if (stepsOpt && stepsOpt->is_array()) {
                    for (const auto& step : *stepsOpt) {
                        if (!step.is_object()) continue;
                        const std::string action = step.value("tool",
                            step.value("action", std::string()));
                        if (action.empty()) continue;
                        if (!stepsText.empty()) stepsText += " -> ";
                        stepsText += action;
                    }
                }
                if (stepsText.empty()) continue;
                addTraceCandidate(traceCandidates, playbook.id, "playbook", 0.0, 0.0, 0.0);
                std::string line = "[PLAYBOOK] [MEMO:" + playbook.id + "] " + playbook.taskType +
                                   ": " + playbook.title + " | " + oneLine(stepsText, 400) + "\n";
                if (budget != std::string::npos &&
                    brief.size() + playbookText.size() + line.size() > budget) {
                    truncated = true;
                    break;
                }
                playbookText += line;
                playbookIds.push_back(playbook.id);
            }
        }
        if (!playbookText.empty()) {
            const std::string header = "\n--- [ARESTRIX MEMORIA: PLAYBOOKS] ---\n";
            if (budget == std::string::npos ||
                brief.size() + header.size() + playbookText.size() <= budget) {
                brief += header + playbookText;
                if (info) info->playbookIds = playbookIds;
            } else {
                truncated = true;
            }
        }
    }

    if (!workingText.empty()) {
        addTraceCandidate(traceCandidates, "state:" + itemId, "state", 0.0, 0.0, 0.0);
        if (budget == std::string::npos || brief.size() + workingText.size() <= budget) {
            brief += workingText;
            if (info) info->workingKeys = workingKeys;
        } else {
            truncated = true;
        }
    }

    if (truncated) brief += "\n[... truncado pelo orcamento de memoria]\n";
    if (info) {
        MemoryBriefInfo stable;
        buildStablePrefix(projectId, domains, taskType, maxTokens, &stable);
        info->stableBlock = stable.stableBlock;
        info->stableIds = stable.stableIds;
        info->prefixKey = stable.prefixKey;
        info->prefixIdentity = stable.prefixIdentity;
        info->prefixEpoch = stable.prefixEpoch;
        info->removalSeqStart = stable.removalSeqStart;
        info->chars = brief.size();
        info->estimatedTokens = (brief.size() + 3) / 4;
        info->truncated = truncated;
    }
    const MemoryBriefInfo resultInfo = info ? *info : MemoryBriefInfo{};
    briefCache_[cacheKey] = BriefCacheEntry{brief, resultInfo};
    if (!runId.empty() && !nodeId.empty()) {
        std::set<std::string> delivered;
        for (const auto& id : resultInfo.lessonIds) delivered.insert(id);
        for (const auto& id : resultInfo.factIds) delivered.insert(id);
        for (const auto& id : resultInfo.playbookIds) delivered.insert(id);
        for (const auto& id : resultInfo.pathIds) delivered.insert(id);
        for (const auto& id : resultInfo.pathClaimIds) delivered.insert(id);
        for (const auto& id : resultInfo.traversalIds) delivered.insert(id);
        if (!resultInfo.workingKeys.empty()) delivered.insert("state:" + itemId);
        recordStep("brief", traceQuery,
                   traceCandidatesToJson(traceCandidates, delivered));
    }
    return brief;
}

std::string MemoryService::buildStablePrefix(const std::string& projectId,
                                             const std::vector<std::string>& domains,
                                             const std::string& taskType, size_t maxTokens,
                                             MemoryBriefInfo* info, bool lockedToEpoch) {
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    std::string prefixKey = taskType.empty() ? "default" : taskType;
    std::vector<std::string> sortedDomains = domains;
    std::sort(sortedDomains.begin(), sortedDomains.end());
    for (const auto& domain : sortedDomains) prefixKey += "|" + domain;
    // Fase 4: dentro de uma epoca o prefixo e o conjunto publicado, nao as
    // licoes vivas. Publicar a proxima epoca usa lockedToEpoch=false para
    // capturar as adicoes que esperavam no bloco volatil.
    std::set<std::string> epochIds;
    bool hasEpoch = false;
    if (lockedToEpoch && prefixEpochConfig(projectId).enabled) {
        const auto epochs = s->listPrefixEpochs(scopeId(projectId), prefixKey, 1);
        if (!epochs.empty()) {
            hasEpoch = true;
            const auto& items = epochs[0]["itemIds"];
            if (items.is_array()) {
                for (const auto& item : items) {
                    if (item.is_string()) epochIds.insert(item.str());
                }
            }
        }
    }
    auto lessons = domains.empty() ? s->getLessons(scopeId(projectId))
                                   : s->getLessonsForDomains(scopeId(projectId), domains);
    if (hasEpoch) {
        lessons.erase(std::remove_if(lessons.begin(), lessons.end(),
            [&](const MemoryLesson& lesson) { return epochIds.count(lesson.id) == 0; }),
            lessons.end());
    }
    std::sort(lessons.begin(), lessons.end(), [](const MemoryLesson& a, const MemoryLesson& b) {
        return a.id < b.id;
    });
    std::vector<MemoryPlaybookRecord> playbooks;
    std::vector<std::string> types;
    if (!taskType.empty()) types.push_back(taskType);
    for (const auto& domain : sortedDomains) {
        if (std::find(types.begin(), types.end(), domain) == types.end()) types.push_back(domain);
    }
    std::set<std::string> seenPlaybooks;
    for (const auto& type : types) {
        for (const auto& playbook : s->listPlaybooks(scopeId(projectId), type, "active", 20)) {
            if (seenPlaybooks.insert(playbook.id).second) playbooks.push_back(playbook);
        }
    }
    if (hasEpoch) {
        playbooks.erase(std::remove_if(playbooks.begin(), playbooks.end(),
            [&](const MemoryPlaybookRecord& playbook) { return epochIds.count(playbook.id) == 0; }),
            playbooks.end());
    }
    std::sort(playbooks.begin(), playbooks.end(), [](const MemoryPlaybookRecord& a,
                                                     const MemoryPlaybookRecord& b) {
        return a.id < b.id;
    });
    const size_t budget = maxTokens == 0 ? std::string::npos : maxTokens * 4;
    std::string out;
    std::vector<std::string> stableIds;
    const std::string header = "\n--- [ARESTRIX STABLE MEMORY] ---\n";
    if (header.size() <= budget) out += header;
    for (const auto& lesson : lessons) {
        std::string line = "[LESSON] [MEMO:" + lesson.id + "] (" + lesson.domain + ") " +
                           lesson.lessonRule + "\n";
        if (budget != std::string::npos && out.size() + line.size() > budget) break;
        out += line;
        stableIds.push_back(lesson.id);
    }
    for (const auto& playbook : playbooks) {
        std::string line = "[PLAYBOOK] [MEMO:" + playbook.id + "] " + playbook.taskType +
                           ": " + playbook.title + "\n";
        if (budget != std::string::npos && out.size() + line.size() > budget) break;
        out += line;
        stableIds.push_back(playbook.id);
    }
    if (info) {
        info->stableBlock = out;
        info->stableIds = stableIds;
        info->prefixKey = prefixKey;
        info->prefixIdentity = project::RunnerSecurity::sha256(
            "prefix-v1|" + prefixKey + "|" + project::RunnerSecurity::sha256(out));
        info->prefixEpoch = s->latestPrefixEpoch(scopeId(projectId), prefixKey);
        info->removalSeqStart = s->currentRemovalSeq(scopeId(projectId));
    }
    return out;
}

taskforge::Json MemoryService::removedSince(
    const std::string& projectId, long long removalSeqStart,
    const std::vector<std::pair<std::string, std::string>>& delivered) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"maxSeq", removalSeqStart}, {"removed", taskforge::Json::array()}};
    return s->removedSince(scopeId(projectId), removalSeqStart, delivered);
}

taskforge::Json MemoryService::prefixEpochs(const std::string& projectId,
                                            const std::string& prefixKey, size_t limit) {
    auto* s = ensureStore(projectId);
    return s ? s->listPrefixEpochs(scopeId(projectId), prefixKey, limit)
             : taskforge::Json::array();
}

taskforge::Json MemoryService::publishPrefixEpoch(const std::string& projectId,
                                                  const std::string& prefixKey,
                                                  const std::string& skillHash,
                                                  const std::string& promptTemplateHash) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    MemoryBriefInfo info;
    buildStablePrefix(projectId, {}, "", 800, &info, false);
    const std::string contentHash = project::RunnerSecurity::sha256(info.stableBlock);
    const std::string identity = project::RunnerSecurity::sha256(
        "prefix-v1|" + skillHash + "|" + promptTemplateHash + "|" + contentHash);
    const auto latest = s->listPrefixEpochs(scopeId(projectId), prefixKey, 1);
    if (!latest.empty() && latest[0].value("contentHash", std::string()) == contentHash) {
        // Mudanca de skill ou de molde muda a identidade dos bytes, mas nao o
        // conjunto publicado: a epoca continua a mesma e nenhum cache e pago.
        return taskforge::Json{{"ok", true},
                               {"created", false},
                               {"epoch", latest[0].value("epoch", 0LL)},
                               {"prefixKey", prefixKey},
                               {"prefixIdentity", identity},
                               {"contentHash", contentHash}};
    }
    long long epoch = 0;
    taskforge::Json itemIds = taskforge::Json::array();
    for (const auto& id : info.stableIds) itemIds.push_back(id);
    if (!s->publishPrefixEpoch(scopeId(projectId), prefixKey, contentHash, itemIds.dump(), &epoch)) {
        return taskforge::Json{{"ok", false}, {"error", "falha ao publicar epoca"}};
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        prefixEpochLastPublish_[projectKey(projectId)] = std::chrono::steady_clock::now();
    }
    invalidateBriefCache();
    return taskforge::Json{{"ok", true}, {"created", true}, {"epoch", epoch}, {"prefixKey", prefixKey},
                           {"prefixIdentity", identity}, {"contentHash", contentHash}};
}

taskforge::Json MemoryService::configurePrefixEpochs(const std::string& projectId,
                                                     const taskforge::Json& raw) {
    PrefixEpochConfig config;
    if (raw.is_boolean()) {
        config.enabled = raw.get<bool>();
    } else if (raw.is_object()) {
        config.enabled = raw.value("enabled", false);
        config.cacheTtlSeconds = static_cast<int>(std::max<long long>(
            0, std::min<long long>(86400, raw.value("cacheTtlSeconds", 300))));
        config.maxPending = static_cast<int>(std::max<long long>(
            1, std::min<long long>(1000, raw.value("maxPending", 5))));
        config.minIntervalSeconds = static_cast<int>(std::max<long long>(
            0, std::min<long long>(86400, raw.value("minIntervalSeconds", 60))));
    }
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        prefixEpochs_[projectKey(projectId)] = config;
    }
    return taskforge::Json{{"enabled", config.enabled},
                           {"cacheTtlSeconds", config.cacheTtlSeconds},
                           {"maxPending", config.maxPending},
                           {"minIntervalSeconds", config.minIntervalSeconds}};
}

PrefixEpochConfig MemoryService::prefixEpochConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = prefixEpochs_.find(projectKey(projectId));
    return it == prefixEpochs_.end() ? PrefixEpochConfig{} : it->second;
}

bool MemoryService::maybePublishPrefixEpoch(const std::string& projectId,
                                            const std::string& prefixKey,
                                            const std::string& skillHash,
                                            const std::string& promptTemplateHash,
                                            int pendingAdditions, bool removal) {
    if (removal) {
        return publishPrefixEpoch(projectId, prefixKey, skillHash, promptTemplateHash)
            .value("created", false);
    }
    const PrefixEpochConfig config = prefixEpochConfig(projectId);
    if (!config.enabled) return false;
    // Primeira epoca: sem ela nao existe prefixo estavel publicado para
    // cachear. A publicacao seguinte continua respeitando TTL/limiar.
    if (prefixEpochs(projectId, prefixKey, 1).empty()) {
        return publishPrefixEpoch(projectId, prefixKey, skillHash, promptTemplateHash)
            .value("created", false);
    }
    const auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last{};
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = prefixEpochLastPublish_.find(projectKey(projectId));
        if (it != prefixEpochLastPublish_.end()) last = it->second;
    }
    const long long elapsed = last.time_since_epoch().count() == 0
        ? 0 : std::chrono::duration_cast<std::chrono::seconds>(now - last).count();
    const bool idleDue = last.time_since_epoch().count() != 0 &&
        elapsed >= config.cacheTtlSeconds;
    const bool pendingDue = pendingAdditions >= config.maxPending &&
        (last.time_since_epoch().count() == 0 || elapsed >= config.minIntervalSeconds);
    if (!idleDue && !pendingDue) return false;
    return publishPrefixEpoch(projectId, prefixKey, skillHash, promptTemplateHash)
        .value("created", false);
}

void MemoryService::invalidateBriefCache() {
    briefCache_.clear();
}

size_t MemoryService::ingestSources(const std::string& projectId, const std::string& workspaceRoot,
                                    const std::vector<std::string>& uris, size_t maxFileBytes,
                                    size_t maxFiles, bool allowForgotten) {
    auto* s = existingStore(projectId);
    if (!s || workspaceRoot.empty() || uris.empty()) return 0;
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(workspaceRoot, ec), ec);
    if (ec || root.empty()) return 0;

    std::set<std::string> uniqueUris;
    std::vector<std::string> candidates;
    for (const auto& raw : uris) {
        if (raw.empty()) continue;
        const fs::path requested = root / raw;
        const fs::path resolved = fs::weakly_canonical(requested, ec);
        if (ec) continue;
        const fs::path relative = fs::relative(resolved, root, ec);
        if (ec || relative.empty()) continue;
        const std::string relativeUri = MemorySources::normalizeUri(relative.generic_string());
        if (relativeUri.rfind("..", 0) == 0 || MemorySources::isExcluded(relativeUri)) continue;
        if (fs::is_directory(resolved, ec)) {
            for (fs::recursive_directory_iterator it(resolved, fs::directory_options::skip_permission_denied, ec), end;
                 it != end; it.increment(ec)) {
                if (ec) break;
                if (it->is_directory(ec)) {
                    const std::string dirUri = MemorySources::normalizeUri(
                        fs::relative(it->path(), root, ec).generic_string());
                    if (MemorySources::isExcluded(dirUri)) {
                        it.disable_recursion_pending();
                    }
                    continue;
                }
                if (!it->is_regular_file(ec)) continue;
                const std::string fileUri = MemorySources::normalizeUri(
                    fs::relative(it->path(), root, ec).generic_string());
                if (MemorySources::isExcluded(fileUri)) continue;
                if (uniqueUris.insert(fileUri).second) candidates.push_back(fileUri);
                if (maxFiles > 0 && candidates.size() >= maxFiles) break;
            }
        } else if (fs::is_regular_file(resolved, ec)) {
            if (uniqueUris.insert(relativeUri).second) candidates.push_back(relativeUri);
        }
        if (maxFiles > 0 && candidates.size() >= maxFiles) break;
    }

    size_t changed = 0;
    for (const auto& uri : candidates) {
        const fs::path path = root / uri;
        if (!fs::is_regular_file(path, ec)) continue;
        const auto rawSize = fs::file_size(path, ec);
        if (ec || rawSize == 0) continue;
        if (!MemorySources::isSupported(uri, static_cast<size_t>(rawSize), maxFileBytes)) continue;
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.empty() || content.find('\0') != std::string::npos) continue;
        SanitizeResult clean = sanitizeText(content, 0);
        if (clean.text.empty()) continue;
        const std::string kind = MemorySources::kindForUri(uri);
        if (kind.empty()) continue;
        const std::string contentHash = project::RunnerSecurity::sha256(clean.text);
        const auto drafts = MemorySources::segment(clean.text, kind);
        if (drafts.empty()) continue;

        MemorySourceRecord source;
        source.scopeId = scopeId(projectId);
        source.uri = uri;
        source.contentHash = contentHash;
        source.kind = kind;
        source.size = static_cast<long long>(clean.text.size());
        source.id = MemorySources::sourceId(source.scopeId, uri, contentHash);
        std::vector<MemoryPassageRecord> passages;
        passages.reserve(drafts.size());
        for (const auto& draft : drafts) {
            MemoryPassageRecord passage;
            passage.id = MemorySources::passageId(contentHash, draft.ordinal);
            passage.sourceId = source.id;
            passage.scopeId = source.scopeId;
            passage.ordinal = draft.ordinal;
            passage.start = draft.start;
            passage.end = draft.end;
            passage.text = draft.text;
            passage.textHash = project::RunnerSecurity::sha256(draft.text);
            passage.tokens = static_cast<long long>((draft.text.size() + 3) / 4);
            passages.push_back(std::move(passage));
        }
        std::string ingestError;
        size_t inserted = 0;
        const std::string sourceId = s->ingestSource(source, passages, &inserted, ingestError,
                                                     allowForgotten);
        if (!sourceId.empty() && inserted > 0) ++changed;
    }
    return changed;
}

taskforge::Json MemoryService::sourceSummary(const std::string& projectId) {
    auto* s = existingStore(projectId);
    return s ? s->sourceSummary(scopeId(projectId)) : taskforge::Json::object();
}

taskforge::Json MemoryService::forgetSource(const std::string& projectId,
                                            const MemorySourceSelector& selector,
                                            const std::string& reason,
                                            const std::string& requestedBy,
                                            bool async, bool vacuum) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    const MemoryEraseResult erased =
        s->eraseSource(scopeId(projectId), selector, reason, requestedBy);
    taskforge::Json out{{"ok", erased.ok},
                        {"sourceVersionId", erased.sourceVersionId},
                        {"uri", erased.uri},
                        {"sources", static_cast<long long>(erased.sources)},
                        {"passages", static_cast<long long>(erased.passages)},
                        {"claimsRetracted", static_cast<long long>(erased.claimsRetracted)},
                        {"proposals", static_cast<long long>(erased.proposals)},
                        {"evidenceDeleted", static_cast<long long>(erased.evidenceDeleted)},
                        {"derivativesTombstoned",
                         static_cast<long long>(erased.derivativesTombstoned)},
                        {"jobs", static_cast<long long>(erased.jobs)},
                        {"passageIds", taskforge::Json::array()},
                        {"retractedEdgeIds", taskforge::Json::array()},
                        {"tombstoneIds", taskforge::Json::array()},
                        {"jobIds", taskforge::Json::array()}};
    for (const auto& id : erased.passageIds) out["passageIds"].push_back(id);
    for (const auto& id : erased.retractedEdgeIds) out["retractedEdgeIds"].push_back(id);
    for (const auto& id : erased.tombstoneIds) out["tombstoneIds"].push_back(id);
    for (const auto& id : erased.jobIds) out["jobIds"].push_back(id);
    if (!erased.error.empty()) out["error"] = erased.error;
    if (!erased.ok) return out;
    out["paths"] = MemoryPaths::reconcile(*s, scopeId(projectId), "source_forgotten");
    out["facets"] = MemoryFacets::rebuild(*s, scopeId(projectId),
                                          embeddingConfig(projectId), true);
    out["prefixEpoch"] = publishPrefixEpoch(projectId, "default", "", "");
    invalidateBriefCache();
    if (!async) {
        out["drain"] = drainJobs(projectId, 100);
        const auto summary = s->jobSummary(scopeId(projectId));
        out["pendingJobs"] = summary.value("pending", 0LL) + summary.value("running", 0LL);
        out["deadJobs"] = summary.value("dead", 0LL);
        if (out.value("pendingJobs", 0LL) > 0 || out.value("deadJobs", 0LL) > 0) {
            out["ok"] = false;
            out["telemetryPending"] = out.value("pendingJobs", 0LL) > 0;
        }
    } else {
        out["async"] = true;
    }
    if (vacuum && !async) {
        std::string vacuumError;
        if (!s->vacuum(vacuumError)) {
            out["ok"] = false;
            out["vacuumError"] = vacuumError;
        } else {
            out["vacuumed"] = true;
        }
    }
    return out;
}

taskforge::Json MemoryService::drainJobs(const std::string& projectId, size_t maxJobs) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    const std::string scope = scopeId(projectId);
    auto jobs = s->claimJobs(scope, maxJobs, "memory-service", 120);
    long long done = 0;
    long long failed = 0;
    long long deferred = 0;
    for (const auto& job : jobs) {
        std::string error;
        if (job.kind == "purge_telemetry") {
            auto* t = ensureTelemetry(projectId);
            if (!t) {
                ++deferred;
                continue;
            }
            if (t->purgeTelemetry(scope, job.payloadJson, error)) {
                s->completeJob(job.id);
                ++done;
            } else {
                s->failJob(job.id, error);
                ++failed;
            }
        } else if (job.kind == "rebuild_facet") {
            const auto report = MemoryFacets::rebuild(*s, scope, embeddingConfig(projectId), true);
            if (report.value("ok", false)) {
                s->completeJob(job.id);
                ++done;
            } else {
                s->failJob(job.id, "rebuild_facet falhou");
                ++failed;
            }
        } else if (job.kind == "recompile_playbook") {
            const auto payload = taskforge::jsonutil::tryParse(job.payloadJson);
            const std::string taskType = payload && payload->is_object()
                ? payload->value("taskType", std::string()) : std::string();
            const auto report = s->recompilePlaybook(scope, taskType);
            if (report.value("ok", false)) {
                s->completeJob(job.id);
                ++done;
            } else {
                s->failJob(job.id, report.value("reason", std::string("recompile falhou")));
                ++failed;
            }
        } else if (job.kind == "reconsolidate_lesson") {
            taskforge::memory::ConsolidationOptions options;
            options.generalize = true;
            std::string consolidateError;
            const auto report = MemoryConsolidator::run(*s, scope, options, consolidateError);
            if (report.ok()) {
                s->completeJob(job.id);
                ++done;
            } else {
                s->failJob(job.id, consolidateError.empty()
                    ? "reconsolidate_lesson falhou" : consolidateError);
                ++failed;
            }
        } else {
            s->failJob(job.id, "executor de trabalho indisponivel: " + job.kind);
            ++failed;
        }
    }
    const auto summary = s->jobSummary(scope);
    return taskforge::Json{{"ok", failed == 0},
                           {"claimed", static_cast<long long>(jobs.size())},
                           {"done", done},
                           {"failed", failed},
                           {"deferred", deferred},
                           {"summary", summary}};
}

taskforge::Json MemoryService::lineageBackfill(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    taskforge::Json report;
    const size_t linked = s->backfillLineage(scopeId(projectId), &report);
    report["ok"] = true;
    report["linked"] = static_cast<long long>(linked);
    return report;
}

long long MemoryService::knowledgeVersion(const std::string& projectId) {
    auto* s = existingStore(projectId);
    return s ? s->knowledgeVersion() : 0;
}

bool MemoryService::recordTraceStep(const std::string& projectId, const std::string& runId,
                                    const std::string& nodeId, const std::string& itemId,
                                    const std::string& taskType,
                                    const std::string& intentTemplateHash,
                                    const std::string& inputHash,
                                    const taskforge::Json& step) {
    if (runId.empty() || nodeId.empty()) return false;
    auto* t = ensureTelemetry(projectId);
    auto* s = existingStore(projectId);
    if (!t) return false;
    MemoryTraceRecord trace;
    trace.scopeId = scopeId(projectId);
    trace.runId = runId;
    trace.nodeId = nodeId;
    trace.itemId = itemId;
    trace.taskType = taskType;
    trace.intentTemplateHash = intentTemplateHash;
    trace.inputHash = inputHash;
    trace.knowledgeVersion = s ? s->knowledgeVersion() : 0;
    return t->appendTrace(trace, step);
}

bool MemoryService::closeTrace(const std::string& projectId, const std::string& runId,
                               const std::string& nodeId, const std::string& outcome,
                               const std::string& model, const std::string& tier,
                               bool memoryEnabled) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return false;
    return t->closeTrace(scopeId(projectId), runId, nodeId, outcome, model, tier, memoryEnabled);
}

taskforge::Json MemoryService::getTrace(const std::string& projectId, const std::string& runId,
                                        const std::string& nodeId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->getTrace(scopeId(projectId), runId, nodeId) : taskforge::Json::object();
}

bool MemoryService::recordBriefHits(const std::string& projectId, const std::string& runId,
                                    const std::string& nodeId, const MemoryBriefInfo& info) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return false;
    const std::string scope = scopeId(projectId);
    bool ok = true;
    for (const auto& id : info.lessonIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "lesson", true, false) && ok;
    }
    for (const auto& id : info.factIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "fact", true, false) && ok;
    }
    for (const auto& id : info.playbookIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "playbook", true, false) && ok;
    }
    for (const auto& id : info.pathIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "path", true, false) && ok;
    }
    for (const auto& id : info.pathClaimIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "fact", true, false) && ok;
    }
    for (const auto& id : info.traversalIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "fact", true, false) && ok;
    }
    for (const auto& id : info.facetIds) {
        ok = t->recordHit(scope, runId, nodeId, id, "facet", true, false) && ok;
    }
    for (const auto& key : info.workingKeys) {
        ok = t->recordHit(scope, runId, nodeId, "working:" + key, "working", true, false) && ok;
    }
    return ok;
}

int MemoryService::markHitsUsed(const std::string& projectId, const std::string& runId,
                                const std::string& nodeId, const std::vector<std::string>& memoryIds,
                                int* invalid) {
    if (invalid) *invalid = 0;
    auto* t = ensureTelemetry(projectId);
    if (!t) return 0;
    // E16: citar um caminho conta como uso de suas afirmacoes; citar uma
    // afirmacao tambem marca o caminho que a contem.
    std::vector<std::string> expanded = memoryIds;
    std::set<std::string> seen(expanded.begin(), expanded.end());
    auto add = [&](const std::string& id) {
        if (!id.empty() && seen.insert(id).second) expanded.push_back(id);
    };
    if (auto* s = existingStore(projectId)) {
        const std::string scope = scopeId(projectId);
        for (const auto& id : memoryIds) {
            if (id.rfind("path_", 0) == 0) {
                const auto path = s->getPath(scope, id);
                if (!path.empty() && path["claims"].is_array()) {
                    for (const auto& claim : path["claims"]) {
                        if (claim.is_string()) add(claim.str());
                    }
                }
            }
            for (const auto& pathId : s->pathsForClaim(id)) add(pathId);
        }
    }
    return t->markHitsUsed(scopeId(projectId), runId, nodeId, expanded, invalid);
}

bool MemoryService::finishHits(const std::string& projectId, const std::string& runId,
                               const std::string& nodeId, const std::string& outcome,
                               bool reinforce) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return false;
    const std::string scope = scopeId(projectId);
    const bool ok = t->finishHits(scope, runId, nodeId, outcome, reinforce);
    // O conhecimento so e reforcado se o store do projeto ja existe; nao abrir
    // o banco de conhecimento apenas para registrar telemetria. Se os dois
    // bancos apontarem para o mesmo arquivo, o proprio finishHits ja reforcou.
    if (auto* k = existingStore(projectId)) {
        if (databasePath(projectId) != telemetryDatabasePath(projectId)) {
            std::vector<std::string> ids;
            for (const auto& ref : t->usedHitRefs(scope, runId, nodeId)) ids.push_back(ref.id);
            if (!ids.empty()) {
                if (reinforce) {
                    k->reinforceFromHits(scope, ids, outcome);
                } else {
                    // E1: o resultado do agente nao altera utility; o uso so
                    // atualiza last_used ate o veredito independente chegar.
                    k->touchHitsLastUsed(scope, ids);
                }
            }
        }
    }
    return ok;
}

taskforge::Json MemoryService::hitSummary(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->hitSummary(scopeId(projectId)) : taskforge::Json::object();
}

void MemoryService::clearLessons(const std::string& projectId) {
    invalidateBriefCache();
    if (auto* s = ensureStore(projectId)) s->clearLessons(scopeId(projectId));
}

std::string MemoryService::recordEpisode(const std::string& projectId, const std::string& runId,
                                         const std::string& nodeId, const std::string& itemId,
                                         const std::string& kind, const std::string& content,
                                         const taskforge::Json& metadata) {
    auto* t = ensureTelemetry(projectId);
    const std::string id = t
        ? t->recordEpisode(scopeId(projectId), runId, nodeId, itemId, kind, content, metadata)
        : std::string();
    if (!id.empty()) noteMaintenanceEpisode(projectId);
    return id;
}

std::string MemoryService::recordEpisode(const std::string& projectId, const MemoryEpisodeRecord& episode) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return std::string();
    MemoryEpisodeRecord copy = episode;
    copy.scopeId = scopeId(projectId);
    const std::string id = t->recordEpisode(copy);
    if (!id.empty()) noteMaintenanceEpisode(projectId);
    return id;
}

taskforge::Json MemoryService::getEpisode(const std::string& projectId, const std::string& episodeId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->getEpisode(scopeId(projectId), episodeId) : taskforge::Json::object();
}

taskforge::Json MemoryService::extractClaims(const std::string& projectId,
                                             const ClaimExtractorConfig& config,
                                             size_t maxPassages) {
    taskforge::Json out{{"ok", true}, {"candidates", 0LL}, {"extracted", 0LL},
                        {"proposed", 0LL}, {"abstained", 0LL}, {"failed", 0LL},
                        {"extractorFingerprint", config.fingerprint()},
                        {"agent", taskforge::Json{
                             {"id", config.agent.id},
                             {"graph", config.agent.graph},
                             {"node", config.agent.node}}},
                        {"diagnostics", taskforge::Json::array()}};
    if (!config.enabled) {
        out["ok"] = false;
        out["skipped"] = "claim_extraction_off";
        return out;
    }
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) {
        out["ok"] = false;
        out["error"] = "memoria indisponivel";
        return out;
    }
    const std::string scope = scopeId(projectId);
    const auto used = t->usedPassageIds(scope, std::max<size_t>(1, maxPassages) * 4);
    const auto passages = s->passagesForIds(scope, used, std::max<size_t>(1, maxPassages) * 4);
    out["candidates"] = static_cast<long long>(passages.size());
    size_t extracted = 0;
    for (const auto& passage : passages) {
        if (extracted >= maxPassages) break;
        if (s->claimExtractionSeen(scope, passage.passageHash, config.fingerprint())) continue;
        ++extracted;
        std::string error;
        taskforge::Json diagnostics = taskforge::Json::object();
        const auto claims = MemoryClaimExtractor::extract(
            passage.text, config, error, &diagnostics);
        out["diagnostics"].push_back(taskforge::Json{
            {"passageId", passage.passageId}, {"passageHash", passage.passageHash},
            {"accepted", diagnostics.value("accepted", 0LL)},
            {"rejected", diagnostics.value("rejected", 0LL)},
            {"reasons", diagnostics.contains("reasons") ? diagnostics["reasons"]
                                                         : taskforge::Json::array()},
            {"error", error}});
        if (!error.empty()) {
            out["failed"] = out.value("failed", 0LL) + 1;
            s->markClaimExtraction(scope, passage.passageId, passage.passageHash,
                                   config.fingerprint(), "failed", error);
            continue;
        }
        size_t proposed = 0;
        size_t abstained = 0;
        for (const auto& claim : claims) {
            bool ambiguous = false;
            const std::string subjectId = s->findEntity(scope, claim.subject, &ambiguous);
            if (subjectId.empty() || ambiguous) {
                ++abstained;
                continue;
            }
            std::string objectId;
            if (!claim.object.empty()) {
                objectId = s->findEntity(scope, claim.object, &ambiguous);
                if (objectId.empty() || ambiguous) {
                    ++abstained;
                    continue;
                }
            }
            taskforge::Json payload{
                {"subject", claim.subject}, {"subjectId", subjectId},
                {"subjectType", claim.subjectType}, {"predicate", claim.predicate},
                {"object", claim.object}, {"objectId", objectId}, {"value", claim.value},
                {"unit", claim.unit}, {"polarity", claim.polarity},
                {"qualifiers", taskforge::jsonutil::tryParse(claim.qualifiersJson)
                    .value_or(taskforge::Json::object())},
                {"validFrom", claim.validFrom}, {"validTo", claim.validTo},
                {"lens", claim.lens}, {"quote", claim.quote},
                {"knownPredicate", claim.knownPredicate},
                {"passageId", passage.passageId}, {"passageHash", passage.passageHash},
                {"sourceUri", passage.uri}, {"start", passage.start}, {"end", passage.end},
                {"extractorFingerprint", config.fingerprint()},
                {"intelligence", "studio-local"},
                {"promptVersion", config.promptVersion}, {"schemaVersion", config.schemaVersion}};
            taskforge::Json evidence = taskforge::Json::array({
                taskforge::Json{{"kind", "passage"},
                                {"passageId", passage.passageId},
                                {"passageHash", passage.passageHash},
                                {"sourceUri", passage.uri},
                                {"start", passage.start},
                                {"end", passage.end},
                                {"quote", claim.quote},
                                {"extractorFingerprint", config.fingerprint()},
                                {"intelligence", "studio-local"}}});
            if (!s->recordProposal(scope, "claim", payload, "", evidence, 0.6).empty()) {
                ++proposed;
            }
        }
        s->markClaimExtraction(scope, passage.passageId, passage.passageHash,
                               config.fingerprint(),
                               claims.empty() ? "empty" : proposed > 0 ? "proposed" : "abstained");
        out["proposed"] = out.value("proposed", 0LL) + static_cast<long long>(proposed);
        out["abstained"] = out.value("abstained", 0LL) + static_cast<long long>(abstained);
    }
    out["extracted"] = static_cast<long long>(extracted);
    return out;
}

taskforge::Json MemoryService::rebuildFacets(const std::string& projectId, bool facetsEnabled) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    const auto embedding = embeddingConfig(projectId);
    return MemoryFacets::rebuild(*s, scopeId(projectId), embedding, facetsEnabled);
}

taskforge::Json MemoryService::rebuildPaths(const std::string& projectId, bool pathsEnabled,
                                            const PathOptions& options) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    if (!pathsEnabled) {
        return taskforge::Json{{"ok", true}, {"disabled", true},
                               {"paths", s->pathSummary(scopeId(projectId))}};
    }
    const std::string scope = scopeId(projectId);
    const auto approved = t->approvedTraceIds(scope, 10000);
    const std::set<std::string> approvedSet(approved.begin(), approved.end());
    std::vector<MemoryTraceRecord> traces;
    for (const auto& trace : t->listTraces(scope, 10000)) {
        if (approvedSet.count(trace.traceId)) traces.push_back(trace);
    }
    auto report = MemoryPaths::consolidate(*s, scope, traces, options);
    report["reconcile"] = MemoryPaths::reconcile(*s, scope);
    report["shortcuts"] = MemoryPaths::materializeShortcuts(*s, scope);
    report["summary"] = s->pathSummary(scope);
    return report;
}

taskforge::Json MemoryService::rebuildBridges(const std::string& projectId,
                                               bool bridgesEnabled,
                                               const BridgeOptions& options) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    if (!bridgesEnabled) return taskforge::Json{{"ok", true}, {"disabled", true}};
    const std::string scope = scopeId(projectId);
    const auto approved = t->approvedTraceIds(scope, 10000);
    const std::set<std::string> approvedSet(approved.begin(), approved.end());
    std::vector<MemoryTraceRecord> traces;
    for (const auto& trace : t->listTraces(scope, 10000)) {
        if (approvedSet.count(trace.traceId)) traces.push_back(trace);
    }
    return MemoryBridges::detect(*s, scope, traces, options);
}

taskforge::Json MemoryService::listPaths(const std::string& projectId,
                                         const std::string& state) {
    auto* s = ensureStore(projectId);
    taskforge::Json out = taskforge::Json::array();
    if (!s) return out;
    for (const auto& path : s->listPaths(scopeId(projectId), state)) {
        out.push_back(taskforge::Json{
            {"id", path.id}, {"taskType", path.taskType}, {"state", path.state},
            {"revision", path.revision}, {"success", path.success},
            {"failure", path.failure}, {"uses", path.uses},
            {"distinctInputs", path.distinctInputs}, {"utility", path.utility}});
    }
    return out;
}

taskforge::Json MemoryService::getPath(const std::string& projectId,
                                       const std::string& pathId) {
    auto* s = ensureStore(projectId);
    return s ? s->getPath(scopeId(projectId), pathId) : taskforge::Json::object();
}

bool MemoryService::setPathState(const std::string& projectId, const std::string& pathId,
                                 const std::string& state, std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "memoria indisponivel";
        return false;
    }
    return s->setPathState(scopeId(projectId), pathId, state, "cli");
}

size_t MemoryService::extractProposals(const std::string& projectId, const std::string& episodeId) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return 0;
    const std::string scope = scopeId(projectId);
    const taskforge::Json episode = t->getEpisode(scope, episodeId);
    if (!episode.is_object() || episode.empty()) return 0;
    size_t proposals = 0;
    for (const auto& draft : MemoryExtractor::extract(episode)) {
        if (!s->recordProposal(scope, draft.kind, draft.payload, episodeId, draft.evidence,
                               draft.confidence).empty()) {
            ++proposals;
        }
    }
    MemoryPlaybookCompiler::observe(*s, scope, episode);
    return proposals;
}

taskforge::Json MemoryService::listProposals(const std::string& projectId, const std::string& status,
                                             size_t limit) {
    auto* s = ensureStore(projectId);
    taskforge::Json out = taskforge::Json::array();
    if (!s) return out;
    for (const auto& proposal : s->listProposals(scopeId(projectId), status, limit)) {
        auto payloadOpt = taskforge::jsonutil::tryParse(proposal.payloadJson);
        auto evidenceOpt = taskforge::jsonutil::tryParse(proposal.evidenceJson);
        taskforge::Json payload = payloadOpt ? *payloadOpt : taskforge::Json::object();
        taskforge::Json evidence = evidenceOpt ? *evidenceOpt : taskforge::Json::array();
        out.push_back(taskforge::Json{{"id", proposal.id},
                                      {"scopeId", proposal.scopeId},
                                      {"kind", proposal.kind},
                                      {"payload", payload},
                                      {"payloadJson", proposal.payloadJson},
                                      {"sourceEpisodeId", proposal.sourceEpisodeId},
                                      {"evidence", evidence},
                                      {"evidenceJson", proposal.evidenceJson},
                                      {"confidence", proposal.confidence},
                                      {"status", proposal.status},
                                      {"createdAt", proposal.createdAt},
                                      {"reviewedAt", proposal.reviewedAt},
                                      {"reviewedBy", proposal.reviewedBy}});
    }
    return out;
}

bool MemoryService::promoteProposal(const std::string& projectId, const std::string& proposalId,
                                    const std::string& reviewer, std::string& error,
                                    std::string* promotedId) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    const bool promoted = s->promoteProposal(scopeId(projectId), proposalId, reviewer, error, promotedId);
    if (promoted) {
        invalidateBriefCache();
        MemoryPaths::reconcile(*s, scopeId(projectId), "claim_state_changed");
        publishPrefixEpoch(projectId, "default", "", "");
    }
    return promoted;
}

bool MemoryService::rejectProposal(const std::string& projectId, const std::string& proposalId,
                                   const std::string& reviewer, const std::string& reason,
                                   std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    return s->rejectProposal(scopeId(projectId), proposalId, reviewer, reason, error);
}

std::string MemoryService::recordEdge(const std::string& projectId, const std::string& srcId,
                                      const std::string& dstId, const std::string& relation,
                                      const taskforge::Json& value, const std::string& validFrom,
                                      const std::string& validTo, double confidence,
                                      const std::string& status, const std::string& sourceKind,
                                      const std::string& sourceRef, const taskforge::Json& metadata) {
    auto* s = ensureStore(projectId);
    return s ? s->recordEdge(scopeId(projectId), srcId, dstId, relation, value, validFrom, validTo,
                             confidence, status, sourceKind, sourceRef, metadata)
             : std::string();
}

std::vector<MemoryEdgeRecord> MemoryService::neighbors(const std::string& projectId,
                                                       const std::string& nodeId, int depth,
                                                       const std::string& asOf) {
    auto* s = ensureStore(projectId);
    return s ? s->neighbors(scopeId(projectId), nodeId, depth, asOf) : std::vector<MemoryEdgeRecord>();
}

std::vector<MemoryEdgeRecord> MemoryService::searchEdges(const std::string& projectId,
                                                         const std::string& text, size_t limit) {
    auto* s = ensureStore(projectId);
    return s ? s->searchEdges(scopeId(projectId), text, limit) : std::vector<MemoryEdgeRecord>();
}

std::string MemoryService::searchContext(const std::string& projectId, const std::string& text,
                                         size_t limit, const std::vector<std::string>& domains,
                                         std::vector<MemoryHitRef>* hits,
                                         const std::string& runId, const std::string& nodeId,
                                         const std::vector<std::string>& lenses,
                                         const std::string& taskType,
                                         const std::string& intent) {
    auto* s = ensureStore(projectId);
    if (!s) return std::string();
    std::string result;
    std::string pathResult;
    std::string hybrid;
    taskforge::Json stepCandidates = taskforge::Json::array();
    auto appendPathCandidate = [&](const std::string& id, const std::string& kind) {
        stepCandidates.push_back(taskforge::Json{
            {"id", id}, {"kind", kind}, {"lexical", 0.0}, {"vector", 0.0},
            {"fused", 0.0}, {"position", static_cast<long long>(stepCandidates.size())},
            {"delivered", true}});
    };

    // E16: caminho elegivel vem antes da busca no grafo e nao esconde o
    // fallback lexical/vetorial quando o caminho nao cobre a consulta.
    const auto servedPaths = servePaths(projectId, taskType, intent, {});
    if (servedPaths.value("served", false)) {
        std::string pathText;
        for (const auto& path : servedPaths["paths"]) {
            if (!path.is_object()) continue;
            const std::string textBlock = path.value("text", std::string());
            if (textBlock.empty()) continue;
            pathText += textBlock;
            const std::string pathId = path.value("id", std::string());
            if (!pathId.empty()) {
                if (hits) hits->push_back(MemoryHitRef{pathId, "path"});
                appendPathCandidate(pathId, "path");
            }
            for (const auto& claim : path["claims"]) {
                if (!claim.is_string()) continue;
                if (hits) hits->push_back(MemoryHitRef{claim.str(), "fact"});
                appendPathCandidate(claim.str(), "fact");
            }
        }
        if (!pathText.empty()) {
            pathResult = "\n--- [ARESTRIX MEMORY: CAMINHOS CANONICOS] ---\n" + pathText;
        }
    }

    auto* utilityTelemetry = utilityConfig(projectId).source == "ledger"
        ? ensureTelemetry(projectId) : nullptr;
    if (hybridSearch(projectId, text, limit, domains, hits, hybrid, &stepCandidates,
                     lenses, taskType)) {
        result = std::move(hybrid);
    } else if (utilityTelemetry) {
        // Ranqueamento lexical puro com a utilidade do livro. O caminho `edge`
        // continua usando o formatador historico.
        auto lexical = s->lexicalCandidates(scopeId(projectId), text,
                                            std::max<size_t>(limit * 4, 20), domains);
        if (!lenses.empty()) {
            const auto allowedEdges = s->edgeIdsForLenses(scopeId(projectId), lenses);
            const std::set<std::string> allowed(allowedEdges.begin(), allowedEdges.end());
            lexical.erase(std::remove_if(lexical.begin(), lexical.end(),
                [&](const MemorySearchCandidate& candidate) {
                    return candidate.kind == "fact" && allowed.count(candidate.id) == 0;
                }), lexical.end());
        }
        struct WeightedCandidate {
            MemorySearchCandidate candidate;
            double score = 0.0;
        };
        std::vector<WeightedCandidate> weighted;
        std::set<std::string> seen;
        const std::string lookupTask = taskType.empty() ? "*" : taskType;
        for (size_t i = 0; i < lexical.size(); ++i) {
            const std::string key = lexical[i].kind + "|" + lexical[i].id;
            if (!seen.insert(key).second) continue;
            const std::string itemKey = itemKeyFor(
                scopeId(projectId), lexical[i].kind, lexical[i].id, s);
            const double utility = utilityTelemetry->utilityFor(
                scopeId(projectId), itemKey, lookupTask).value("utility", 0.5);
            const double base = 1.0 / (60.0 + static_cast<double>(i + 1));
            weighted.push_back(WeightedCandidate{lexical[i], base * (0.5 + utility)});
        }
        std::sort(weighted.begin(), weighted.end(),
            [](const WeightedCandidate& a, const WeightedCandidate& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.candidate.confidence > b.candidate.confidence;
            });
        std::map<std::string, TraceCandidateData> candidates;
        std::string ledgerResult;
        size_t emitted = 0;
        for (const auto& entry : weighted) {
            if (emitted >= limit) break;
            const auto& candidate = entry.candidate;
            const std::string label = candidate.kind == "lesson" ? "LICAO"
                : candidate.kind == "playbook" ? "PLAYBOOK" : "FATO";
            ledgerResult += "[" + label + "] [MEMO:" + candidate.id + "] " +
                candidate.text + "\n";
            if (hits) hits->push_back(MemoryHitRef{candidate.id, candidate.kind});
            addTraceCandidate(candidates, candidate.id, candidate.kind,
                              0.0, 0.0, entry.score);
            ++emitted;
        }
        if (!ledgerResult.empty()) {
            result = "\n--- [ARESTRIX MEMORIA: BUSCA LEXICAL] ---\n" + ledgerResult;
        }
        stepCandidates = traceCandidatesToJson(candidates, {});
    } else {
        result = s->searchContext(scopeId(projectId), text, limit, domains, hits, lenses);
        std::map<std::string, TraceCandidateData> candidates;
        const auto lexical = s->lexicalCandidates(scopeId(projectId), text, 50, domains);
        for (size_t i = 0; i < lexical.size(); ++i) {
            const double score = 1.0 / (60.0 + static_cast<double>(i + 1));
            addTraceCandidate(candidates, lexical[i].id, lexical[i].kind, score, 0.0, score);
        }
        stepCandidates = traceCandidatesToJson(candidates, {});
    }
    if (!pathResult.empty()) {
        if (!result.empty()) result = pathResult + "\n" + result;
        else result = std::move(pathResult);
    }

    std::string passageText;
    const auto passages = s->searchPassages(scopeId(projectId), text, limit);
    for (size_t i = 0; i < passages.size(); ++i) {
        const auto& passage = passages[i];
        if (passageText.empty()) passageText = "\n[ARESTRIX PASSAGENS DE FONTE]\n";
        passageText += "[MEMO:psg:" + passage.id + "] " + passage.uri + ":" +
                       std::to_string(passage.start) + "-" + std::to_string(passage.end) + "\n" +
                       oneLine(passage.text, 1200) + "\n";
        if (hits) hits->push_back(MemoryHitRef{passage.id, "passage"});
        const double score = 1.0 / (60.0 + static_cast<double>(i + 1));
        stepCandidates.push_back(taskforge::Json{
            {"id", passage.id}, {"kind", "passage"}, {"lexical", score},
            {"vector", 0.0}, {"fused", score},
            {"position", static_cast<long long>(stepCandidates.size())},
            {"delivered", true}});
    }
    if (!passageText.empty()) {
        if (!result.empty()) result += "\n";
        result += passageText;
    }

    if (!runId.empty() && !nodeId.empty()) {
        std::set<std::string> delivered;
        if (hits) {
            for (const auto& hit : *hits) delivered.insert(hit.id);
        }
        for (size_t i = 0; i < stepCandidates.size(); ++i) {
            if (!stepCandidates[i].is_object()) continue;
            if (delivered.count(stepCandidates[i].value("id", std::string()))) {
                stepCandidates[i]["delivered"] = true;
            }
        }
        recordTraceStep(projectId, runId, nodeId, "", taskType, intent.empty()
                            ? std::string() : project::RunnerSecurity::sha256(intent), "",
                        taskforge::Json{{"kind", "search"},
                                        {"queryHash", project::RunnerSecurity::sha256(text)},
                                        {"exploration", servedPaths.value("exploration", false)},
                                        {"candidates", stepCandidates}});
    }

    const std::string workspaceId = workspaceIdForProject(projectId);
    if (!workspaceId.empty()) {
        if (auto* workspace = workspaceStore(workspaceId)) {
            std::string fromWorkspace = workspace->searchContext(
                workspaceScope(workspaceId), text, limit, domains, nullptr);
            if (!fromWorkspace.empty()) {
                if (!result.empty()) result += "\n";
                result += "[ARESTRIX WORKSPACE MEMORY]\n" + fromWorkspace;
            }
        }
    }
    return result;
}

MemoryService::EmbeddingRuntime& MemoryService::embeddingRuntimeLocked(
    const std::string& projectId) {
    const std::string id = projectKey(projectId);
    auto it = embeddings_.find(id);
    if (it != embeddings_.end()) return it->second;

    EmbeddingRuntime runtime;
    runtime.config = embeddingConfigFromJson(embeddingConfigToJson(defaultEmbeddingConfig()));
    runtime.provider = makeEmbeddingProvider(runtime.config);
    auto inserted = embeddings_.emplace(id, std::move(runtime));
    return inserted.first->second;
}

bool MemoryService::configureEmbeddings(const std::string& projectId,
                                        const EmbeddingConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    EmbeddingConfig normalized = embeddingConfigFromJson(embeddingConfigToJson(config));
    EmbeddingRuntime runtime;
    runtime.config = normalized;
    runtime.provider = makeEmbeddingProvider(normalized);
    embeddings_[projectKey(projectId)] = std::move(runtime);
    return true;
}

void MemoryService::setEmbeddingProvider(const std::string& projectId,
                                         std::shared_ptr<IEmbeddingProvider> provider) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    runtime.provider = provider ? std::move(provider) : std::make_shared<NullEmbeddingProvider>();
    runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
}

EmbeddingConfig MemoryService::embeddingConfig(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return embeddingRuntimeLocked(projectId).config;
}

taskforge::Json MemoryService::embeddingStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    taskforge::Json out = embeddingConfigToJson(runtime.config);
    const bool available = runtime.provider && runtime.provider->available();
    const int effectiveDim = runtime.provider && runtime.provider->dim() > 0
        ? runtime.provider->dim() : runtime.config.dim;
    const std::string fingerprint = embeddingFingerprint(
        runtime.config, effectiveDim,
        runtime.provider ? runtime.provider->revision() : std::string());
    out["available"] = available;
    out["active"] = runtime.config.enabled && available;
    out["providerModel"] = runtime.provider ? runtime.provider->modelId() : std::string("none");
    out["providerDim"] = runtime.provider ? runtime.provider->dim() : 0;
    out["cooldown"] = embeddingInCooldown(runtime.cooldownUntil);
    out["fingerprint"] = fingerprint;
    out["fingerprintShort"] = fingerprint.substr(0, 12);
    long long indexed = 0;
    long long candidates = 0;
    long long allVersions = 0;
    if (auto* s = ensureStore(projectId)) {
        const std::string scope = scopeId(projectId);
        indexed = static_cast<long long>(s->embeddingCount(scope, runtime.config.model, fingerprint));
        allVersions = static_cast<long long>(s->embeddingCount(scope, runtime.config.model));
        for (const auto& object : s->embeddingObjects(scope)) {
            if (!object.text.empty()) ++candidates;
        }
    }
    out["indexed"] = indexed;
    out["candidates"] = candidates;
    out["pending"] = std::max<long long>(0, candidates - indexed);
    out["stale"] = indexed < candidates || allVersions > indexed;
    if (runtime.reindexing) {
        out["state"] = "pending";
    } else if (!runtime.lastIndexError.empty()) {
        out["state"] = "failed";
        out["error"] = runtime.lastIndexError;
    } else if (!runtime.config.enabled || !available) {
        out["state"] = "stale";
    } else if (indexed == 0 && candidates > 0) {
        out["state"] = "stale";
    } else if (indexed < candidates) {
        out["state"] = "partial";
    } else {
        out["state"] = "ready";
    }
    return out;
}

taskforge::Json MemoryService::testEmbeddings(const std::string& projectId,
                                              const std::string& sample) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& runtime = embeddingRuntimeLocked(projectId);
    const auto started = std::chrono::steady_clock::now();
    taskforge::Json out = taskforge::Json::object();
    out["enabled"] = runtime.config.enabled;
    out["model"] = runtime.config.model;
    out["available"] = runtime.provider && runtime.provider->available();
    if (!runtime.provider || !runtime.provider->available()) {
        out["ok"] = false;
        out["error"] = "provider de embeddings indisponivel (configure embeddingBaseUrl/model)";
        return out;
    }
    std::vector<Embedding> embeddings;
    std::string error;
    const std::string text = sample.empty() ? "teste de memoria local" : sample;
    const bool ok = runtime.provider->embed({text}, true, embeddings, error);
    if (ok) runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
    else embeddingStartCooldown(runtime.cooldownUntil);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count();
    out["ok"] = ok;
    out["latencyMs"] = static_cast<long long>(elapsed);
    out["error"] = error;
    if (ok && !embeddings.empty()) {
        out["dim"] = embeddings[0].dim;
        out["values"] = static_cast<long long>(embeddings[0].values.size());
        out["sample"] = text;
    }
    return out;
}

taskforge::Json MemoryService::indexPending(const std::string& projectId, int timeBudgetMs,
                                            size_t maxObjects) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    auto& runtime = embeddingRuntimeLocked(projectId);
    if (!s) {
        return taskforge::Json{{"ok", false}, {"state", "failed"},
                               {"error", "banco de memoria indisponivel"}};
    }
    if (!runtime.config.enabled) {
        taskforge::Json out = embeddingStatus(projectId);
        out["ok"] = true;
        out["skipped"] = "embeddings_disabled";
        return out;
    }
    if (!runtime.provider || !runtime.provider->available()) {
        taskforge::Json out = embeddingStatus(projectId);
        out["ok"] = false;
        out["error"] = "provider de embeddings indisponivel";
        return out;
    }
    if (embeddingInCooldown(runtime.cooldownUntil)) {
        taskforge::Json out = embeddingStatus(projectId);
        out["ok"] = false;
        out["error"] = "provider em cooldown";
        return out;
    }

    runtime.reindexing = true;
    EmbeddingIndexStats stats;
    std::string error;
    try {
        stats = MemoryIndexer::index(*s, *runtime.provider, runtime.config,
                                     scopeId(projectId), false, maxObjects, error,
                                     timeBudgetMs);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    runtime.reindexing = false;
    if (error.empty() && stats.failed > 0) {
        error = std::to_string(stats.failed) + " embedding(s) falharam";
    }
    if (error.empty()) {
        runtime.lastIndexError.clear();
        runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
    } else {
        runtime.lastIndexError = error;
        embeddingStartCooldown(runtime.cooldownUntil);
    }

    taskforge::Json out = embeddingStatus(projectId);
    out["ok"] = error.empty();
    out["indexedThisRun"] = static_cast<long long>(stats.indexed);
    out["failedThisRun"] = static_cast<long long>(stats.failed);
    out["pendingAfterRun"] = static_cast<long long>(stats.pending);
    out["stoppedByBudget"] = stats.stoppedByBudget;
    out["timeBudgetMs"] = timeBudgetMs;
    out["maxObjects"] = static_cast<long long>(maxObjects);
    if (!error.empty()) out["error"] = error;
    return out;
}

taskforge::Json MemoryService::reindexEmbeddings(const std::string& projectId, bool force,
                                                 std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    auto& runtime = embeddingRuntimeLocked(projectId);
    taskforge::Json out = taskforge::Json::object();
    if (!s) {
        error = "banco de memoria indisponivel";
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    if (!runtime.provider || !runtime.provider->available()) {
        runtime.lastIndexError = "provider de embeddings indisponivel";
        error = runtime.lastIndexError;
        out["ok"] = false;
        out["error"] = error;
        return out;
    }
    invalidateBriefCache();
    runtime.reindexing = true;
    runtime.lastIndexError.clear();
    const int effectiveDim = runtime.provider->dim() > 0 ? runtime.provider->dim() : runtime.config.dim;
    const std::string fingerprint = embeddingFingerprint(
        runtime.config, effectiveDim, runtime.provider->revision());
    const auto stats = MemoryIndexer::index(*s, *runtime.provider, runtime.config,
                                            scopeId(projectId), force, 0, error);
    if (error.empty() && stats.failed > 0) {
        error = std::to_string(stats.failed) + " embedding(s) falharam";
    }
    runtime.reindexing = false;
    runtime.lastIndexError = error;
    if (error.empty()) runtime.cooldownUntil = std::chrono::steady_clock::time_point{};
    else embeddingStartCooldown(runtime.cooldownUntil);
    out["ok"] = error.empty();
    out["model"] = runtime.config.model;
    out["fingerprint"] = fingerprint;
    out["fingerprintShort"] = fingerprint.substr(0, 12);
    out["candidates"] = static_cast<long long>(stats.candidates);
    out["indexed"] = static_cast<long long>(stats.indexed);
    out["skipped"] = static_cast<long long>(stats.skipped);
    out["removed"] = static_cast<long long>(stats.removed);
    out["failed"] = static_cast<long long>(stats.failed);
    out["pending"] = static_cast<long long>(stats.pending);
    out["stoppedByBudget"] = stats.stoppedByBudget;
    out["error"] = error;
    return out;
}

taskforge::Json MemoryService::consolidate(const std::string& projectId,
                                           const ConsolidationOptions& options,
                                           std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    invalidateBriefCache();
    const auto report = MemoryConsolidator::run(*s, scopeId(projectId), options, error);
    taskforge::Json out = report.toJson();
    out["projectId"] = projectId;
    if (!error.empty()) out["error"] = error;
    return out;
}

bool MemoryService::configureMaintenance(const std::string& projectId,
                                         const MemoryMaintenanceConfig& config) {
    const std::string id = projectKey(projectId);
    stopMaintenance(id);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!ensureStore(id)) return false;
    auto persistentIt = persistent_.find(id);
    if (persistentIt == persistent_.end() || !persistentIt->second ||
        !config.enabled || config.mode == "off") {
        return true;
    }
    try {
        startMaintenanceLocked(id, config);
    } catch (const std::exception&) {
        maintenance_.erase(id);
        return false;
    }
    return true;
}

MemoryMaintenanceConfig MemoryService::maintenanceConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) return MemoryMaintenanceConfig{};
    std::lock_guard<std::mutex> runtimeLock(it->second->mutex);
    return it->second->config;
}

bool MemoryService::configurePathServing(const std::string& projectId,
                                         const PathServingConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    pathServing_[projectKey(projectId)] = config;
    invalidateBriefCache();
    return true;
}

PathServingConfig MemoryService::pathServingConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = pathServing_.find(projectKey(projectId));
    return it == pathServing_.end() ? PathServingConfig{} : it->second;
}

bool MemoryService::configureTraversal(const std::string& projectId,
                                       const TraversalConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    traversal_[projectKey(projectId)] = config;
    invalidateBriefCache();
    return true;
}

TraversalConfig MemoryService::traversalConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = traversal_.find(projectKey(projectId));
    return it == traversal_.end() ? TraversalConfig{} : it->second;
}

bool MemoryService::configureUtility(const std::string& projectId,
                                     const taskforge::Json& raw) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    utility_[projectKey(projectId)] = utilityConfigFromJson(raw);
    invalidateBriefCache();
    return true;
}

MemoryUtilityConfig MemoryService::utilityConfig(const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = utility_.find(projectKey(projectId));
    return it == utility_.end() ? MemoryUtilityConfig{} : it->second;
}

bool MemoryService::configureCreditAudit(const std::string& projectId,
                                         const taskforge::Json& raw) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    creditAudit_[projectKey(projectId)] = creditAuditConfigFromJson(raw);
    return true;
}

MemoryCreditAuditConfig MemoryService::creditAuditConfig(
    const std::string& projectId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = creditAudit_.find(projectKey(projectId));
    return it == creditAudit_.end() ? MemoryCreditAuditConfig{} : it->second;
}

taskforge::Json MemoryService::searchGraph(const std::string& projectId,
                                           const std::string& text, size_t limit,
                                           const std::string& taskType,
                                           const std::string& runId,
                                           const std::string& nodeId,
                                           std::vector<MemoryHitRef>* hits) {
    const auto config = traversalConfig(projectId);
    taskforge::Json out{{"ok", true}, {"enabled", config.enabled}, {"abstained", true},
                        {"text", std::string()}, {"coverage", taskforge::Json::object()},
                        {"nodes", taskforge::Json::array()}};
    if (!config.enabled) return out;
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json{{"ok", false}, {"enabled", true}};
    TraversalOptions options = config.options;
    options.maxResults = std::max<size_t>(1, std::min(limit, options.maxResults));
    auto result = MemoryTraversal::run(*s, scopeId(projectId), text, taskType, options, {});
    if (hits) {
        for (const auto& node : result["nodes"]) {
            if (!node.is_object()) continue;
            const std::string id = node.value("id", std::string());
            if (!id.empty()) hits->push_back(MemoryHitRef{id, "fact"});
        }
    }
    if (!runId.empty() && !nodeId.empty()) {
        recordTraceStep(projectId, runId, nodeId, "", taskType, "", "",
                        taskforge::Json{{"kind", "traversal"},
                                        {"queryHash", project::RunnerSecurity::sha256(text)},
                                        {"coverage", result["coverage"]},
                                        {"candidates", result["nodes"]}});
    }
    result["enabled"] = true;
    return result;
}

taskforge::Json MemoryService::routingStats(const std::string& projectId,
                                            const std::string& taskType) {
    taskforge::Json out{{"success", 0LL}, {"failure", 0LL}, {"coverage", 0.0}};
    auto* t = ensureTelemetry(projectId);
    if (!t || taskType.empty()) return out;
    const auto outcomes = t->validationOutcomes(scopeId(projectId), taskType, 100);
    long long success = 0;
    long long failure = 0;
    for (bool ok : outcomes) {
        if (ok) ++success;
        else ++failure;
    }
    out["success"] = success;
    out["failure"] = failure;
    return out;
}

void MemoryService::noteMaintenanceEpisode(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) return;
    auto* runtime = it->second.get();
    std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
    if (runtime->config.mode == "off") return;
    ++runtime->episodes;
    if (runtime->config.mode == "idle") {
        runtime->idleDeadline = std::chrono::steady_clock::now() +
                                std::chrono::seconds(runtime->config.idleSeconds);
    }
    runtime->cv.notify_all();
}

void MemoryService::startMaintenanceLocked(const std::string& projectId,
                                           const MemoryMaintenanceConfig& config) {
    auto runtime = std::make_unique<MaintenanceRuntime>();
    runtime->config = config;
    MaintenanceRuntime* raw = runtime.get();
    maintenance_[projectId] = std::move(runtime);
    raw->thread = std::thread([this, projectId, raw]() {
        maintenanceLoop(projectId, raw);
    });
}

void MemoryService::stopMaintenance(const std::string& projectId) {
    std::unique_ptr<MaintenanceRuntime> runtime;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = maintenance_.find(projectKey(projectId));
        if (it == maintenance_.end()) return;
        runtime = std::move(it->second);
        maintenance_.erase(it);
    }
    if (!runtime) return;
    {
        std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
        runtime->stop = true;
    }
    runtime->cv.notify_all();
    if (runtime->thread.joinable()) runtime->thread.join();
}

void MemoryService::stopAllMaintenance() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& entry : maintenance_) ids.push_back(entry.first);
    }
    for (const auto& id : ids) stopMaintenance(id);
}

void MemoryService::maintenanceLoop(const std::string& projectId,
                                    MaintenanceRuntime* runtime) {
    std::unique_lock<std::mutex> lock(runtime->mutex);
    while (!runtime->stop) {
        const MemoryMaintenanceConfig config = runtime->config;
        if (config.mode == "after_episodes") {
            runtime->cv.wait(lock, [&]() {
                return runtime->stop || runtime->episodes >= config.episodeThreshold;
            });
            if (runtime->stop) break;
        } else if (config.mode == "idle") {
            runtime->cv.wait(lock, [&]() {
                return runtime->stop || runtime->episodes > 0;
            });
            if (runtime->stop) break;
            auto deadline = runtime->idleDeadline;
            while (!runtime->stop && runtime->episodes > 0) {
                const auto state = runtime->cv.wait_until(lock, deadline);
                if (state == std::cv_status::timeout) break;
                if (runtime->stop) break;
                deadline = runtime->idleDeadline;
            }
            if (runtime->stop || runtime->episodes == 0) continue;
        } else {
            break;
        }

        runtime->busy = true;
        ++runtime->triggers;
        runtime->episodes = 0;
        const MemoryMaintenanceConfig runConfig = runtime->config;
        lock.unlock();

        std::string error;
        taskforge::Json report = consolidate(projectId, runConfig.options, error);
        // E3: a manutencao tambem drena pendencias de embeddings, fora da busca.
        report["embeddingIndex"] = indexPending(projectId, 10000, 64);
        if (runConfig.claimExtractor.enabled) {
            report["claimExtraction"] = extractClaims(projectId, runConfig.claimExtractor,
                                                      runConfig.claimMaxPassages);
        }
        report["facets"] = rebuildFacets(projectId, runConfig.lensesEnabled);
        report["paths"] = rebuildPaths(projectId, runConfig.pathsEnabled, runConfig.pathOptions);
        report["bridges"] = rebuildBridges(projectId, runConfig.bridgesEnabled,
                                           runConfig.bridgeOptions);

        lock.lock();
        runtime->busy = false;
        ++runtime->runs;
        runtime->lastRunAt = isoNow();
        runtime->lastReason = runConfig.mode;
        if (error.empty()) {
            runtime->lastError.clear();
        } else {
            ++runtime->failures;
            runtime->lastError = error;
        }
        runtime->lastReport = report;
        runtime->cv.notify_all();
    }
}

taskforge::Json MemoryService::maintenanceStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it == maintenance_.end() || !it->second) {
        return taskforge::Json{{"enabled", false}, {"mode", "off"}};
    }
    MaintenanceRuntime* runtime = it->second.get();
    std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
    taskforge::Json out = maintenanceConfigToJson(runtime->config);
    out["busy"] = runtime->busy;
    out["episodesSinceRun"] = runtime->episodes;
    out["triggers"] = runtime->triggers;
    out["runs"] = runtime->runs;
    out["failures"] = runtime->failures;
    out["lastRunAt"] = runtime->lastRunAt;
    out["lastReason"] = runtime->lastReason;
    out["lastError"] = runtime->lastError;
    out["lastReport"] = runtime->lastReport;
    if (runtime->config.mode == "after_episodes") {
        out["nextInEpisodes"] = std::max<long long>(
            0, runtime->config.episodeThreshold - runtime->episodes);
    }
    if (runtime->config.mode == "idle" && runtime->episodes > 0) {
        const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
            runtime->idleDeadline - std::chrono::steady_clock::now()).count();
        out["nextInSeconds"] = std::max<long long>(0, remaining);
    }
    return out;
}

taskforge::Json MemoryService::runMaintenanceOnce(const std::string& projectId,
                                                  const std::string& reason,
                                                  std::string& error) {
    MemoryMaintenanceConfig config;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        auto it = maintenance_.find(projectKey(projectId));
        if (it != maintenance_.end() && it->second) {
            std::lock_guard<std::mutex> runtimeLock(it->second->mutex);
            config = it->second->config;
        }
    }
    taskforge::Json report = consolidate(projectId, config.options, error);
    report["embeddingIndex"] = indexPending(projectId, 10000, 64);
    if (config.claimExtractor.enabled) {
        report["claimExtraction"] = extractClaims(projectId, config.claimExtractor,
                                                  config.claimMaxPassages);
    }
    report["facets"] = rebuildFacets(projectId, config.lensesEnabled);
    report["paths"] = rebuildPaths(projectId, config.pathsEnabled, config.pathOptions);
    report["bridges"] = rebuildBridges(projectId, config.bridgesEnabled,
                                       config.bridgeOptions);

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = maintenance_.find(projectKey(projectId));
    if (it != maintenance_.end() && it->second) {
        MaintenanceRuntime* runtime = it->second.get();
        std::lock_guard<std::mutex> runtimeLock(runtime->mutex);
        ++runtime->runs;
        runtime->lastRunAt = isoNow();
        runtime->lastReason = reason;
        runtime->lastReport = report;
        if (error.empty()) {
            runtime->lastError.clear();
        } else {
            ++runtime->failures;
            runtime->lastError = error;
        }
    }
    return report;
}

taskforge::Json MemoryService::consolidationStatus(const std::string& projectId) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    const std::string scope = scopeId(projectId);
    taskforge::Json out = s->consolidationSummary(scope);
    out["maintenance"] = maintenanceStatus(projectId);
    taskforge::Json edges = taskforge::Json::array();
    const auto names = s->nodeNames(scope);
    for (const auto& edge : s->listEdges(scope, "", 20000)) {
        if (edge.relation != "CONTRADICTS") continue;
        auto valueOpt = taskforge::jsonutil::tryParse(edge.valueJson);
        taskforge::Json value = valueOpt && valueOpt->is_object()
            ? *valueOpt : taskforge::Json::object();
        auto src = names.find(edge.srcId);
        auto dst = names.find(edge.dstId);
        edges.push_back(taskforge::Json{
            {"id", edge.id},
            {"srcId", edge.srcId},
            {"srcName", src == names.end() ? std::string() : src->second},
            {"dstId", edge.dstId},
            {"dstName", dst == names.end() ? std::string() : dst->second},
            {"edgeId", value.value("edgeId", std::string())},
            {"edgeStatus", value.value("status", std::string())},
            {"confidence", value.value("confidence", edge.confidence)},
            {"status", edge.status}});
    }
    out["conflictEdges"] = edges;
    return out;
}

taskforge::Json MemoryService::listPlaybooks(const std::string& projectId,
                                             const std::string& taskType,
                                             const std::string& status, size_t limit) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    taskforge::Json out = taskforge::Json::array();
    if (!s) return out;
    for (const auto& playbook : s->listPlaybooks(scopeId(projectId), taskType, status, limit)) {
        auto steps = taskforge::jsonutil::tryParse(playbook.stepsJson);
        auto preconditions = taskforge::jsonutil::tryParse(playbook.preconditionsJson);
        auto evidence = taskforge::jsonutil::tryParse(playbook.evidenceJson);
        out.push_back(taskforge::Json{
            {"id", playbook.id},
            {"evidenceCount", static_cast<long long>(
                s->evidenceCount(scopeId(projectId), "playbook", playbook.id))},
            {"taskType", playbook.taskType},
            {"title", playbook.title},
            {"steps", steps && steps->is_array() ? *steps : taskforge::Json::array()},
            {"preconditions", preconditions && preconditions->is_object()
                ? *preconditions : taskforge::Json::object()},
            {"successCount", playbook.successCount},
            {"failureCount", playbook.failureCount},
            {"confidence", playbook.confidence},
            {"status", playbook.status},
            {"sourceRef", playbook.sourceRef},
            {"evidence", evidence && evidence->is_array() ? *evidence : taskforge::Json::array()},
            {"createdAt", playbook.createdAt},
            {"updatedAt", playbook.updatedAt}});
    }
    return out;
}

std::string MemoryService::itemKeyFor(const std::string& scope, const std::string& kind,
                                      const std::string& id, MemoryStore* store) {
    if (kind == "lesson") return "lesson:" + id;
    if (kind == "playbook") return "playbook:" + id;
    if (kind == "path") return "path:" + id;
    if (kind == "passage") return "psg:" + id;
    if (kind == "facet") return "facet:" + id;
    if (store && !store->isReadOnly()) {
        MemoryEdgeRecord edge;
        if (store->getEdge(scope, id, edge) && !edge.claimKey.empty()) {
            return edge.claimKey;
        }
    }
    return "edge:" + id;
}

bool MemoryService::hybridRecall(const std::string& projectId, const std::string& text,
                                 size_t limit, const std::vector<std::string>& domains,
                                 std::vector<RankedMemory>& output,
                                 taskforge::Json* traceCandidates,
                                 const std::vector<std::string>& lenses,
                                 const std::string& taskType) {
    if (text.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto* s = ensureStore(projectId);
    if (!s) return false;
    auto& runtime = embeddingRuntimeLocked(projectId);
    if (!runtime.config.enabled || !runtime.provider || !runtime.provider->available()) return false;
    if (embeddingInCooldown(runtime.cooldownUntil)) return false;
    if (runtime.reindexing || !runtime.lastIndexError.empty()) return false;

    // A8 v2: stale/pending/failed caem para lexical. Em partial (indice
    // incompleto) a parte vetorial cobre os itens indexados e os demais entram
    // somente pela parte lexical da fusao; a busca nunca indexa.
    const int effectiveDim = runtime.provider->dim() > 0 ? runtime.provider->dim() : runtime.config.dim;
    const std::string fingerprint = embeddingFingerprint(
        runtime.config, effectiveDim, runtime.provider->revision());
    size_t candidates = 0;
    for (const auto& object : s->embeddingObjects(scopeId(projectId))) {
        if (!object.text.empty()) ++candidates;
    }
    const size_t indexed = s->embeddingCount(scopeId(projectId), runtime.config.model, fingerprint);
    if (indexed == 0) return false;

    // C4: a busca e somente leitura; indexacao acontece em reindex/importacao.
    std::vector<Embedding> queryEmbeddings;
    std::string embedError;
    if (!runtime.provider->embed({text}, true, queryEmbeddings, embedError) ||
        queryEmbeddings.empty()) {
        embeddingStartCooldown(runtime.cooldownUntil);
        return false;
    }

    const size_t candidateLimit = std::max<size_t>(limit * 3, 20);
    auto lexical = s->lexicalCandidates(scopeId(projectId), text, candidateLimit, domains);
    auto vector = MemoryVectorIndex::search(*s, scopeId(projectId), runtime.config.model,
                                            queryEmbeddings[0].values, candidateLimit,
                                            fingerprint, domains);
    output = MemoryRanker::fuse(lexical, vector, limit);
    if (!lenses.empty()) {
        const auto allowedEdges = s->edgeIdsForLenses(scopeId(projectId), lenses);
        const std::set<std::string> allowed(allowedEdges.begin(), allowedEdges.end());
        output.erase(std::remove_if(output.begin(), output.end(), [&](const RankedMemory& memory) {
            return memory.kind == "fact" && allowed.count(memory.id) == 0;
        }), output.end());
    }
    if (utilityConfig(projectId).source == "ledger") {
        if (auto* telemetry = ensureTelemetry(projectId)) {
            const std::string scope = scopeId(projectId);
            const std::string lookupTask = taskType.empty() ? "*" : taskType;
            for (auto& memory : output) {
                const std::string itemKey = itemKeyFor(scope, memory.kind, memory.id, s);
                const auto utility = telemetry->utilityFor(scope, itemKey, lookupTask);
                memory.score *= 0.5 + utility.value("utility", 0.5);
            }
            std::sort(output.begin(), output.end(), [](const RankedMemory& a, const RankedMemory& b) {
                if (a.score != b.score) return a.score > b.score;
                return a.confidence > b.confidence;
            });
        }
    }
    if (traceCandidates) {
        std::map<std::string, TraceCandidateData> traceMap;
        for (size_t i = 0; i < lexical.size(); ++i) {
            addTraceCandidate(traceMap, lexical[i].id, lexical[i].kind,
                              1.0 / (60.0 + static_cast<double>(i + 1)), 0.0, 0.0);
        }
        for (size_t i = 0; i < vector.size(); ++i) {
            addTraceCandidate(traceMap, vector[i].objectId, vector[i].objectType,
                              0.0, 1.0 / (60.0 + static_cast<double>(i + 1)), 0.0);
        }
        for (const auto& memory : output) {
            addTraceCandidate(traceMap, memory.id, memory.kind, 0.0, 0.0, memory.score);
        }
        *traceCandidates = traceCandidatesToJson(traceMap, {});
    }
    return true;
}

bool MemoryService::hybridSearch(const std::string& projectId, const std::string& text,
                                 size_t limit, const std::vector<std::string>& domains,
                                 std::vector<MemoryHitRef>* hits, std::string& output,
                                 taskforge::Json* traceCandidates,
                                 const std::vector<std::string>& lenses,
                                 const std::string& taskType) {
    std::vector<RankedMemory> ranked;
    if (!hybridRecall(projectId, text, limit, domains, ranked, traceCandidates, lenses, taskType))
        return false;
    output = formatRanked(ranked, limit, hits);
    return true;
}

int MemoryService::recordConsultedHits(const std::string& projectId, const std::string& runId,
                                       const std::string& nodeId,
                                       const std::vector<MemoryHitRef>& refs) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->recordConsultedHits(scopeId(projectId), runId, nodeId, refs) : 0;
}

bool MemoryService::invalidateEdge(const std::string& projectId, const std::string& edgeId,
                                   const std::string& validTo, const std::string& reason,
                                   const std::string& actor) {
    auto* s = ensureStore(projectId);
    const bool ok = s && s->invalidateEdge(edgeId, validTo, reason, actor);
    if (ok) {
        invalidateBriefCache();
        publishPrefixEpoch(projectId, "default", "", "");
    }
    return ok;
}

bool MemoryService::recordUsage(const MemoryUsageRecord& usage) {
    if (usage.scopeId.empty()) return false;
    auto* t = ensureTelemetry(usage.scopeId);
    if (!t) return false;
    MemoryUsageRecord copy = usage;
    copy.scopeId = scopeId(usage.scopeId);
    return t->recordUsage(copy);
}

taskforge::Json MemoryService::usageSummary(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->usageSummary(scopeId(projectId)) : taskforge::Json::object();
}

std::string MemoryService::episodeIdFor(const std::string& projectId, const std::string& runId,
                                        const std::string& nodeId) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->episodeIdFor(scopeId(projectId), runId, nodeId) : std::string();
}

std::string MemoryService::recordValidation(const std::string& projectId,
                                            const MemoryValidationRecord& validation,
                                            bool reinforce) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return "";
    drainCreditOutbox(projectId);
    MemoryValidationRecord copy = validation;
    if (copy.scopeId.empty()) copy.scopeId = scopeId(projectId);
    const std::string scope = scopeId(projectId);

    // O episodio e a fonte do task_type e do run/no quando o chamador so
    // conhece o episodeId (ou o contrario).
    if (!copy.episodeId.empty() && (copy.runId.empty() || copy.nodeId.empty())) {
        const taskforge::Json episode = t->getEpisode(scope, copy.episodeId);
        if (copy.runId.empty()) copy.runId = episode.value("runId", std::string());
        if (copy.nodeId.empty()) copy.nodeId = episode.value("nodeId", std::string());
    }
    if (copy.episodeId.empty() && !copy.runId.empty() && !copy.nodeId.empty()) {
        copy.episodeId = t->episodeIdFor(scope, copy.runId, copy.nodeId);
    }

    const std::string id = t->recordValidation(copy);
    // Fase 5: credito fracionario no livro da telemetria. A citacao continua
    // sendo [MEMO:<id da aresta>]; o evento guarda o item_key resolvido.
    if (!id.empty() && !copy.runId.empty() && !copy.nodeId.empty()) {
        std::string taskType;
        if (!copy.episodeId.empty()) {
            const taskforge::Json episode = t->getEpisode(scope, copy.episodeId);
            taskType = episode.value("taskType", std::string());
        }
        const auto hits = t->listHits(scope, copy.runId, copy.nodeId);
        std::set<std::string> citedKeys;
        std::set<std::string> deliveredKeys;
        auto* knowledge = existingStore(projectId);
        for (const auto& hit : hits) {
            const std::string key = itemKeyFor(scope, hit.kind, hit.memoryId, knowledge);
            if (hit.injected || hit.consulted) deliveredKeys.insert(key);
            if (hit.used) citedKeys.insert(key);
        }
        const auto& keys = citedKeys.empty() ? deliveredKeys : citedKeys;
        if (!keys.empty()) {
            double validatorFactor = 1.0;
            if (!copy.validatorId.empty()) {
                validatorFactor = t->validatorAuditStats(scope, copy.validatorId)
                    .value("weight", 1.0);
            }
            const double delta = (copy.ok ? 1.0 : -1.0) * validatorFactor /
                static_cast<double>(keys.size());
            const auto utility = utilityConfig(projectId);
            const std::string since = isoDaysAgo(utility.originWindowDays);
            const std::string lookupTask = taskType.empty() ? "*" : taskType;
            const std::string traceId = t->getTrace(scope, copy.runId, copy.nodeId)
                .value("traceId", copy.runId);
            std::vector<MemoryCreditEvent> events;
            events.reserve(keys.size());
            for (const auto& key : keys) {
                MemoryCreditEvent event;
                event.traceId = traceId;
                event.itemKey = key;
                event.taskType = lookupTask;
                event.kind = citedKeys.empty() ? "presence_low_res" : "citation";
                event.origin = copy.runId;
                double effective = delta;
                if (utility.maxCreditPerOrigin > 0.0) {
                    const double used = t->creditTotalForOrigin(
                        scope, key, lookupTask, copy.runId, since);
                    const double remaining = std::max(0.0,
                        utility.maxCreditPerOrigin - used);
                    if (std::abs(effective) > remaining) {
                        effective = (effective < 0 ? -1.0 : 1.0) * remaining;
                        event.capped = true;
                    }
                }
                if (copy.ok) event.deltaSuccess = effective;
                else event.deltaFailure = effective;
                events.push_back(std::move(event));
            }
            if (!t->recordCreditEvents(scope, events)) {
                appendCreditOutbox(projectId, events);
            }
        }
    }
    // E1: com o interruptor ligado, o veredito e o unico caminho de reforco.
    if (reinforce && !id.empty() && !copy.runId.empty() && !copy.nodeId.empty()) {
        if (auto* knowledge = existingStore(projectId); knowledge && !knowledge->isReadOnly()) {
            std::vector<std::string> ids;
            for (const auto& ref : t->usedHitRefs(scope, copy.runId, copy.nodeId)) {
                ids.push_back(ref.id);
            }
            if (!ids.empty()) {
                knowledge->reinforceFromHits(scope, ids, copy.ok ? "success" : "failure");
            }
        }
    }
    // B5/B6: validacao independente promove; falhas consecutivas/excesso de
    // falhas despromovem. O gate usa so o ok.
    if (!id.empty() && !copy.episodeId.empty()) {
        if (auto* knowledge = existingStore(projectId); knowledge && !knowledge->isReadOnly()) {
            const taskforge::Json episode = t->getEpisode(scope, copy.episodeId);
            const std::string taskType = episode.value("taskType", std::string());
            const auto auditConfig = creditAuditConfig(projectId);
            bool validatorSuspended = false;
            bool validatorBootstrap = false;
            if (auditConfig.enabled && !copy.validatorId.empty()) {
                const auto stats = t->validatorAuditStats(scope, copy.validatorId);
                const long long audits = stats.value("audits", 0LL);
                validatorBootstrap = audits < auditConfig.minValidatorAudits;
                validatorSuspended = stats.value("divergence", 0.0) >
                    auditConfig.maxValidatorDivergence;
            }
            if (copy.ok && !validatorBootstrap && !validatorSuspended) {
                const auto samples = t->validationGateSamples(scope, taskType, 20);
                std::string promotedId;
                std::string reason;
                knowledge->autoPromotePlaybook(scope, copy.episodeId,
                                               copy.validatorId, copy.validatorVersion,
                                               samples, promotedId, reason);
            }
            const auto outcomes = t->validationOutcomes(scope, taskType, 10);
            std::string demoteReason;
            knowledge->autoDemotePlaybooks(scope, taskType, outcomes, demoteReason);
            invalidateBriefCache();
        }
    }
    return id;
}

std::vector<MemoryValidationRecord> MemoryService::listValidations(const std::string& projectId,
                                                                   const std::string& episodeId,
                                                                   size_t limit) {
    auto* t = ensureTelemetry(projectId);
    return t ? t->listValidations(scopeId(projectId), episodeId, limit)
             : std::vector<MemoryValidationRecord>();
}

taskforge::Json MemoryService::seedLegacyCredit(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    if (t->getMeta("legacy_credit_seed") == "1") {
        return taskforge::Json{{"ok", true}, {"alreadySeeded", true}, {"events", 0LL}};
    }
    const std::string scope = scopeId(projectId);
    std::map<std::string, std::pair<double, double>> totals;
    for (const auto& edge : s->listEdges(scope, "", 20000)) {
        if (edge.successCount == 0 && edge.failureCount == 0) continue;
        const std::string key = edge.claimKey.empty() ? "edge:" + edge.id : edge.claimKey;
        auto& total = totals[key];
        total.first += static_cast<double>(edge.successCount);
        total.second += static_cast<double>(edge.failureCount);
    }
    for (const auto& playbook : s->listPlaybooks(scope, "", "", 20000)) {
        if (playbook.successCount == 0 && playbook.failureCount == 0) continue;
        auto& total = totals["playbook:" + playbook.id];
        total.first += static_cast<double>(playbook.successCount);
        total.second += static_cast<double>(playbook.failureCount);
    }
    std::vector<MemoryCreditEvent> events;
    events.reserve(totals.size());
    for (const auto& [key, total] : totals) {
        MemoryCreditEvent event;
        event.traceId = "legacy:" + key;
        event.itemKey = key;
        event.taskType = "*";
        event.kind = "legacy_seed";
        event.origin = "legacy";
        event.deltaSuccess = total.first;
        event.deltaFailure = total.second;
        events.push_back(std::move(event));
    }
    if (!t->recordCreditEvents(scope, events)) {
        return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    }
    t->setMeta("legacy_credit_seed", "1");
    return taskforge::Json{{"ok", true}, {"alreadySeeded", false},
                           {"events", static_cast<long long>(events.size())}};
}

taskforge::Json MemoryService::rebuildUtility(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const std::string scope = scopeId(projectId);
    if (!t->rebuildItemUtility(scope)) {
        return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    }
    return taskforge::Json{{"ok", true}, {"summary", t->creditSummary(scope)}};
}

taskforge::Json MemoryService::rekeyCreditItems(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    auto* t = ensureTelemetry(projectId);
    if (!s || !t) return taskforge::Json{{"ok", false}, {"error", "memoria indisponivel"}};
    const std::string scope = scopeId(projectId);
    size_t moved = 0;
    size_t edges = 0;
    for (const auto& edge : s->listEdges(scope, "", 20000)) {
        if (edge.claimKey.empty()) continue;
        const std::string from = "edge:" + edge.id;
        if (from == edge.claimKey) continue;
        const size_t count = t->rekeyCreditItem(scope, from, edge.claimKey);
        if (count > 0) {
            moved += count;
            ++edges;
        }
    }
    return taskforge::Json{{"ok", true}, {"moved", static_cast<long long>(moved)},
                           {"edges", static_cast<long long>(edges)},
                           {"summary", t->creditSummary(scope)}};
}

taskforge::Json MemoryService::creditSummary(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    return taskforge::Json{{"ok", true}, {"summary", t->creditSummary(scopeId(projectId))}};
}

void MemoryService::setCreditAuditExecutor(CreditAuditExecutorFn executor) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    creditAuditExecutor_ = std::move(executor);
}

bool MemoryService::hasCreditAuditExecutor() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return static_cast<bool>(creditAuditExecutor_);
}

void MemoryService::setCreditAuditContainerProbe(std::function<bool()> probe) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    creditAuditContainerProbe_ = std::move(probe);
}

taskforge::Json MemoryService::auditCredit(const std::string& projectId,
                                           const taskforge::Json& options) {
    CreditAuditExecutorFn executor;
    std::function<bool()> containerProbe;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        executor = creditAuditExecutor_;
        containerProbe = creditAuditContainerProbe_;
    }
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const auto config = creditAuditConfig(projectId);
    taskforge::Json out{{"ok", true}, {"enabled", config.enabled},
                        {"selected", 0LL}, {"applied", 0LL},
                        {"notAuditable", 0LL}, {"sampledOut", 0LL},
                        {"budgetExhausted", 0LL}, {"candidates", taskforge::Json::array()},
                        {"audits", taskforge::Json::array()}};
    if (!config.enabled) {
        out["reason"] = "disabled";
        return out;
    }
    const bool containerAvailable = !containerProbe || containerProbe();
    out["containerAvailable"] = containerAvailable;
    const std::string scope = scopeId(projectId);
    const bool dryRun = options.value("dryRun", false);
    const int limit = std::max(0, std::min(1000,
        options.value("limit", config.maxAuditsPerDay)));
    const std::string dayStart = isoStartOfDay();
    const auto usage = t->creditAuditUsage(scope, dayStart);
    long long auditsToday = usage.value("audits", 0LL);
    long long tokensToday = usage.value("tokens", 0LL);
    const auto validations = t->listValidations(scope, "", 500);

    // Primeira passagem: itens entregues, nao citados e recorrentes em falhas.
    std::map<std::string, int> suspectCounts;
    for (const auto& validation : validations) {
        if (validation.ok || validation.runId.empty() || validation.nodeId.empty()) continue;
        const auto hits = t->listHits(scope, validation.runId, validation.nodeId);
        for (const auto& hit : hits) {
            if (!(hit.injected || hit.consulted) || hit.used) continue;
            const std::string key = itemKeyFor(
                scope, hit.kind, hit.memoryId, existingStore(projectId));
            if (!key.empty()) ++suspectCounts[key];
        }
    }

    struct Candidate {
        MemoryValidationRecord validation;
        std::string traceId;
        std::string taskType;
        std::string reason;
        std::vector<std::string> delivered;
        std::vector<std::string> cited;
        std::vector<std::string> suspects;
    };
    std::vector<Candidate> candidates;
    int sampledOut = 0;
    for (const auto& validation : validations) {
        if (validation.ok || validation.runId.empty() || validation.nodeId.empty()) continue;
        const auto trace = t->getTrace(scope, validation.runId, validation.nodeId);
        const std::string traceId = trace.value("traceId", validation.runId);
        if (traceId.empty()) continue;
        if (!t->getCreditAudit(scope, traceId).empty()) continue;
        auto episode = t->getEpisode(scope, validation.episodeId);
        Candidate candidate;
        candidate.validation = validation;
        candidate.traceId = traceId;
        candidate.taskType = trace.value("taskType", episode.value("taskType", std::string()));
        const auto hits = t->listHits(scope, validation.runId, validation.nodeId);
        std::set<std::string> delivered;
        std::set<std::string> cited;
        std::set<std::string> suspects;
        for (const auto& hit : hits) {
            const std::string key = itemKeyFor(
                scope, hit.kind, hit.memoryId, existingStore(projectId));
            if (key.empty()) continue;
            if (hit.injected || hit.consulted) delivered.insert(key);
            if (hit.used) cited.insert(key);
            if ((hit.injected || hit.consulted) && !hit.used) {
                const auto it = suspectCounts.find(key);
                if (it != suspectCounts.end() &&
                    it->second >= config.minSuspectRejections) {
                    suspects.insert(key);
                }
            }
        }
        candidate.delivered.assign(delivered.begin(), delivered.end());
        candidate.cited.assign(cited.begin(), cited.end());
        candidate.suspects.assign(suspects.begin(), suspects.end());
        if (candidate.delivered.empty()) continue;

        if (!containerAvailable) {
            candidate.reason = "no_container";
        } else {
            const std::string episodeTools = episode.value(
                "toolCalls", taskforge::Json::array()).dump();
            if (options.value("externalEffects", false) ||
                trace.value("externalEffects", false) || trace.value("usedMcp", false) ||
                episodeTools.find("\"mcp") != std::string::npos ||
                episodeTools.find("call_agent") != std::string::npos) {
                candidate.reason = "external_effects";
            }
            if (candidate.reason.empty() && !validation.validatorId.empty()) {
                const auto stats = t->validatorAuditStats(scope, validation.validatorId);
                if (stats.value("audits", 0LL) >= config.minValidatorAudits &&
                    stats.value("divergence", 0.0) > config.maxValidatorDivergence) {
                    candidate.reason = "validator_suspended";
                }
            }
            const auto snapshot = t->getTaskSnapshot(scope, traceId);
            if (candidate.reason.empty() && snapshot.empty()) {
                candidate.reason = "no_snapshot";
            } else if (candidate.reason.empty() &&
                       snapshot.value("expiresAt", std::string()) < isoNow()) {
                candidate.reason = "snapshot_expired";
            } else if (candidate.reason.empty() &&
                       snapshot.value("input", taskforge::Json::object()).dump().find("[MEMO:") !=
                           std::string::npos) {
                candidate.reason = "package_in_input";
            }
        }

        const bool suspect = !candidate.suspects.empty();
        const bool sampled = stableUnitHash(traceId) < config.sampleRate;
        if (!suspect && !sampled) {
            ++sampledOut;
            continue;
        }
        candidates.push_back(std::move(candidate));
    }

    int selected = 0;
    int processed = 0;
    int notAuditable = 0;
    int budgetExhausted = 0;
    int applied = 0;
    auto recordStatus = [&](const std::string& traceId, const std::string& status,
                            const std::string& reason, long long tokens) {
        MemoryCreditAuditRecord record;
        record.id = "credit-audit:" + traceId;
        record.scopeId = scope;
        record.traceId = traceId;
        record.status = status;
        record.reason = reason;
        record.tokens = tokens;
        record.armsJson = "[]";
        return t->saveCreditAudit(record);
    };
    for (auto& candidate : candidates) {
        if (processed >= limit) break;
        ++processed;
        if (dryRun) {
            out["candidates"].push_back(taskforge::Json{
                {"traceId", candidate.traceId},
                {"taskType", candidate.taskType},
                {"delivered", candidate.delivered.size()},
                {"cited", candidate.cited.size()},
                {"suspects", candidate.suspects.size()},
                {"reason", candidate.reason}});
            ++selected;
            continue;
        }
        if (!candidate.reason.empty()) {
            if (recordStatus(candidate.traceId, "not_auditable", candidate.reason, 0)) {
                ++notAuditable;
            }
            continue;
        }
        if (auditsToday >= config.maxAuditsPerDay ||
            tokensToday >= config.maxTokensPerDay ||
            !executor) {
            const std::string reason = !executor
                ? "no_executor" : "budget_exhausted";
            recordStatus(candidate.traceId, "budget_exhausted", reason,
                         tokensToday >= config.maxTokensPerDay ? tokensToday : 0);
            ++budgetExhausted;
            break;
        }
        const auto trace = t->getTrace(scope, candidate.validation.runId,
                                       candidate.validation.nodeId);
        const auto snapshot = t->getTaskSnapshot(scope, candidate.traceId);
        CreditAuditExecutionRequest request;
        request.projectId = projectId;
        request.scopeId = scope;
        request.traceId = candidate.traceId;
        request.taskType = candidate.taskType;
        request.nodeId = candidate.validation.nodeId;
        request.validator = candidate.validation.validatorId;
        request.inputHash = trace.value("inputHash",
            snapshot.value("inputHash", std::string()));
        request.snapshot = snapshot;
        request.trace = trace;
        request.containerPlan = creditAuditContainerPlan(projectId, taskforge::Json{
            {"workspaceRef", snapshot.value("workspaceRef", std::string())}});
        request.delivered = candidate.delivered;
        request.cited = candidate.cited;
        request.suspectItems = candidate.suspects;
        request.repetitions = config.repetitions;
        request.pairwiseTopM = config.pairwiseTopM;
        const auto result = executor(request);
        if (!result.ok) {
            recordStatus(candidate.traceId, "inconclusive", "executor_error",
                         result.tokens);
            continue;
        }
        if (tokensToday + result.tokens > config.maxTokensPerDay) {
            recordStatus(candidate.traceId, "budget_exhausted", "max_tokens", result.tokens);
            ++budgetExhausted;
            break;
        }
        taskforge::Json auditRequest{
            {"traceId", candidate.traceId},
            {"taskType", candidate.taskType},
            {"validator", candidate.validation.validatorId},
            {"delivered", candidate.delivered},
            {"cited", candidate.cited},
            {"arms", result.arms},
            {"tokens", result.tokens},
            {"snapshotRef", "snapshot:" + candidate.traceId},
            {"inputHash", request.inputHash}};
        const auto audit = runCreditAudit(projectId, auditRequest);
        if (!audit.value("ok", false)) continue;
        ++selected;
        ++auditsToday;
        tokensToday += result.tokens;
        out["audits"].push_back(audit);
        if (audit.value("status", std::string()) == "applied") ++applied;
    }
    out["selected"] = selected;
    out["sampledOut"] = sampledOut;
    out["notAuditable"] = notAuditable;
    out["budgetExhausted"] = budgetExhausted;
    out["applied"] = applied;
    out["usage"] = taskforge::Json{{"audits", auditsToday}, {"tokens", tokensToday}};
    return out;
}

taskforge::Json MemoryService::runCreditAudit(const std::string& projectId,
                                              const taskforge::Json& request) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const std::string scope = scopeId(projectId);
    const std::string traceId = request.value("traceId", std::string());
    if (traceId.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "traceId ausente"}};
    }
    const std::string auditId = "credit-audit:" + traceId;
    const std::string taskType = request.value("taskType", std::string());
    const std::string validator = request.value("validator", std::string());
    const double weight = std::max(0.0, std::min(10.0,
        request.value("weight", validator.empty() ? 1.0 : validatorWeight(projectId, validator))));
    const int repetitions = std::max(1, request.value("repetitions", 1));

    std::vector<std::string> delivered;
    std::vector<std::string> cited;
    if (request.contains("delivered") && request["delivered"].is_array()) {
        for (const auto& item : request["delivered"]) {
            if (item.is_string()) delivered.push_back(item.get<std::string>());
        }
    }
    if (request.contains("cited") && request["cited"].is_array()) {
        for (const auto& item : request["cited"]) {
            if (item.is_string()) cited.push_back(item.get<std::string>());
        }
    }

    bool baselinePresent = false;
    bool baselineOk = false;
    bool noMemoryPresent = false;
    bool noMemoryOk = false;
    bool infraFailure = false;
    std::map<std::string, int> itemApprovals;
    std::map<std::string, std::vector<std::string>> itemSets;
    std::map<std::string, int> setApprovals;
    taskforge::Json arms = taskforge::Json::array();
    if (request.contains("arms") && request["arms"].is_array()) {
        arms = request["arms"];
        for (const auto& arm : request["arms"]) {
            if (!arm.is_object()) continue;
            const std::string kind = arm.value("kind", std::string());
            if (arm.value("infra", false)) infraFailure = true;
            const bool ok = arm.value("ok", false);
            if (kind == "baseline") {
                baselinePresent = true;
                baselineOk = ok;
            } else if (kind == "no_memory") {
                noMemoryPresent = true;
                noMemoryOk = ok;
            } else if (kind == "ablate" || kind == "pair" || kind == "pairwise") {
                std::vector<std::string> items;
                if (arm.contains("items") && arm["items"].is_array()) {
                    for (const auto& id : arm["items"]) {
                        if (id.is_string()) items.push_back(id.get<std::string>());
                    }
                }
                if (items.empty()) continue;
                std::string setKey;
                for (const auto& id : items) setKey += id + "|";
                itemSets[setKey] = items;
                if (ok) {
                    ++setApprovals[setKey];
                    for (const auto& id : items) ++itemApprovals[id];
                }
            }
        }
    }
    (void)baselinePresent;

    std::vector<std::string> decisive;
    std::string reason;
    std::string status;
    if (infraFailure) {
        status = "inconclusive";
        reason = "infra_failure";
    } else if (baselineOk) {
        status = "not_rejected";
        reason = "baseline_approved";
    } else if (noMemoryPresent && !noMemoryOk) {
        status = "no_adjustment";
        reason = "no_memory_also_failed";
    } else {
        for (const auto& [setKey, items] : itemSets) {
            if (items.size() != 1) continue;
            const int approvals = setApprovals[setKey];
            if (approvals * 2 > repetitions) decisive.push_back(items.front());
        }
        if (decisive.empty() && noMemoryPresent && noMemoryOk) {
            for (const auto& [setKey, items] : itemSets) {
                if (items.size() < 2) continue;
                const int approvals = setApprovals[setKey];
                if (approvals * 2 > repetitions) {
                    for (const auto& id : items) decisive.push_back(id);
                    reason = "pairwise";
                }
            }
        }
        if (decisive.empty()) {
            status = "inconclusive";
            if (reason.empty()) reason = "no_single_reversal";
        } else {
            status = "applied";
            if (reason.empty()) reason = "single_reversal";
        }
    }

    std::vector<MemoryCreditEvent> events;
    std::string origin = "audit:" + traceId;
    const auto& credited = cited.empty() ? delivered : cited;
    if (status == "applied" && !credited.empty()) {
        const double share = weight / static_cast<double>(credited.size());
        std::set<std::string> unique(credited.begin(), credited.end());
        for (const auto& key : unique) {
            MemoryCreditEvent event;
            event.traceId = traceId;
            event.itemKey = key;
            event.taskType = taskType;
            event.kind = "audit_reversal";
            event.origin = origin;
            event.auditId = auditId;
            event.deltaFailure = -share;
            events.push_back(std::move(event));
        }
        const double assignment = weight / static_cast<double>(decisive.size());
        for (const auto& key : decisive) {
            MemoryCreditEvent event;
            event.traceId = traceId;
            event.itemKey = key;
            event.taskType = taskType;
            event.kind = "audit_assignment";
            event.origin = origin;
            event.auditId = auditId;
            event.deltaFailure = assignment;
            events.push_back(std::move(event));
        }
    }

    MemoryCreditAuditRecord record;
    record.id = auditId;
    record.scopeId = scope;
    record.traceId = traceId;
    record.status = status;
    record.armsJson = arms.dump();
    record.tokens = request.value("tokens", 0LL);
    record.snapshotRef = request.value("snapshotRef", std::string());
    record.inputHash = request.value("inputHash", std::string());
    if (!t->saveCreditAudit(record)) {
        return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    }
    bool persisted = true;
    if (!events.empty() && !t->recordCreditEvents(scope, events)) {
        persisted = appendCreditOutbox(projectId, events);
    }
    taskforge::Json applied = taskforge::Json::array();
    for (const auto& id : decisive) applied.push_back(id);
    return taskforge::Json{
        {"ok", persisted},
        {"auditId", auditId},
        {"status", status},
        {"reason", reason},
        {"decisive", applied},
        {"events", static_cast<long long>(events.size())},
        {"tokens", record.tokens}};
}

taskforge::Json MemoryService::listCreditAudits(const std::string& projectId,
                                                const std::string& status,
                                                size_t limit) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    return taskforge::Json{{"ok", true},
                           {"audits", t->listCreditAudits(scopeId(projectId), status, limit)}};
}

taskforge::Json MemoryService::recordValidatorAudit(const std::string& projectId,
                                                    const taskforge::Json& request) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    MemoryValidatorAuditRecord record;
    record.scopeId = scopeId(projectId);
    record.validator = request.value("validator", std::string());
    record.traceId = request.value("traceId", std::string());
    record.reviewer = request.value("reviewer", std::string());
    record.verdict = request.value("verdict", std::string());
    if (record.validator.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "validator ausente"}};
    }
    if (record.verdict != "accepted" && record.verdict != "rejected") {
        return taskforge::Json{{"ok", false}, {"error", "verdict invalido"}};
    }
    const std::string id = t->recordValidatorAudit(record);
    if (id.empty()) return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    return taskforge::Json{{"ok", true}, {"id", id}};
}

taskforge::Json MemoryService::validatorAuditSummary(const std::string& projectId,
                                                     const std::string& validator) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const std::string scope = scopeId(projectId);
    taskforge::Json out{{"ok", true}};
    if (!validator.empty()) {
        out["validator"] = t->validatorAuditStats(scope, validator);
        return out;
    }
    out["validators"] = t->validatorAuditReport(scope);
    return out;
}

double MemoryService::validatorWeight(const std::string& projectId,
                                      const std::string& validator) const {
    if (validator.empty()) return 1.0;
    auto* t = const_cast<MemoryService*>(this)->ensureTelemetry(projectId);
    if (!t) return 1.0;
    return t->validatorAuditStats(scopeId(projectId), validator).value("weight", 1.0);
}

taskforge::Json MemoryService::creditAuditContainerPlan(
    const std::string& projectId, const taskforge::Json& request) {
    const auto config = creditAuditConfig(projectId);
    std::string image = request.value("image", config.containerImage);
    if (image.empty()) {
        const char* envImage = std::getenv("ARESTRIX_SANDBOX_IMAGE");
        if (envImage && *envImage) image = envImage;
    }
    if (image.empty()) image = "debian:stable-slim";
    const std::string workspaceRef = request.value("workspaceRef", std::string());
    auto tools = taskforge::Json::array();
    for (const char* tool : {"read", "write", "edit", "validate_svg", "retrieve", "bash"}) {
        tools.push_back(tool);
    }
    auto disabled = taskforge::Json::array();
    for (const char* tool : {"memory_search", "memory_propose_lesson", "memory_state_get",
                             "memory_state_set", "memory_state_list", "memory_state_history",
                             "call_agent"}) {
        disabled.push_back(tool);
    }
    auto notes = taskforge::Json::array();
    notes.push_back("model calls happen in the host process, never inside the container");
    notes.push_back("each arm gets its own ephemeral container and workspace copy");
    notes.push_back("MCP servers, call_agent, interaction nodes and memory tools are disabled");
    notes.push_back("container outputs are discarded after the audit");
    return taskforge::Json{
        {"image", image},
        {"isolation", "container"},
        {"network", "egress_llm_only"},
        {"networkMode", "none"},
        {"rootFilesystem", "read_only"},
        {"tmpfs", taskforge::Json::array({"/tmp"})},
        {"capDrop", taskforge::Json::array({"ALL"})},
        {"noNewPrivileges", true},
        {"pidsLimit", config.containerPidsLimit},
        {"memoryMB", config.containerMemoryMB},
        {"timeoutSeconds", config.containerTimeoutSeconds},
        {"allowFallbackToHost", false},
        {"workspace", taskforge::Json{{"path", "/workspace"},
                                      {"source", workspaceRef},
                                      {"mode", "rw"}}},
        {"user", "1000:1000"},
        {"allowedTools", tools},
        {"disabledTools", disabled},
        {"mcpServers", taskforge::Json::array()},
        {"secretsMounted", false},
        {"memoryDatabasesMounted", false},
        {"oneContainerPerArm", true},
        {"discardOutputs", true},
        {"modelCalls", "host_only"},
        {"notes", notes}};
}

taskforge::Json MemoryService::prepareCreditAuditWorkspaces(
    const std::string& projectId, const std::string& traceId,
    const taskforge::Json& arms) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    if (traceId.empty()) return taskforge::Json{{"ok", false}, {"error", "traceId ausente"}};
    if (!arms.is_array() || arms.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "arms vazio"}};
    }
    const std::string scope = scopeId(projectId);
    const auto snapshot = t->getTaskSnapshot(scope, traceId);
    if (snapshot.empty()) {
        return taskforge::Json{{"ok", false}, {"reason", "no_snapshot"}};
    }
    const std::string source = snapshot.value("workspaceRef", std::string());
    if (source.empty()) {
        return taskforge::Json{{"ok", false}, {"reason", "no_workspace"}};
    }
    std::error_code ec;
    const fs::path base(source);
    if (!fs::is_directory(base, ec)) {
        return taskforge::Json{{"ok", false}, {"reason", "workspace_missing"}};
    }
    const fs::path telemetry(telemetryDatabasePath(projectId));
    if (telemetry.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "telemetria sem caminho"}};
    }
    const fs::path root = telemetry.parent_path() / "audit-workspaces" / safeName(traceId);
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (ec) return taskforge::Json{{"ok", false}, {"error", ec.message()}};

    const std::set<std::string> skipped = {
        ".arestrix", ".git", "node_modules", "build", "build-onnx", "storage",
        "target", ".venv", "__pycache__", ".cache"};
    auto copyTree = [&](const fs::path& from, const fs::path& to) -> bool {
        fs::create_directories(to, ec);
        if (ec) return false;
        fs::recursive_directory_iterator it(
            from, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path rel = fs::relative(it->path(), from, ec);
            if (ec || rel.empty()) return false;
            bool skip = false;
            for (const auto& part : rel) {
                if (skipped.count(part.string()) != 0) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_symlink(ec)) continue;
            const fs::path dest = to / rel;
            if (it->is_directory(ec)) {
                fs::create_directories(dest, ec);
            } else if (it->is_regular_file(ec)) {
                fs::create_directories(dest.parent_path(), ec);
                if (!ec) fs::copy_file(it->path(), dest,
                                       fs::copy_options::overwrite_existing, ec);
            }
            if (ec) return false;
        }
        return !ec;
    };

    taskforge::Json outArms = taskforge::Json::array();
    for (const auto& arm : arms) {
        std::string id;
        if (arm.is_string()) id = arm.get<std::string>();
        else if (arm.is_object()) id = arm.value("id", std::string());
        if (id.empty()) continue;
        const fs::path armDir = root / safeName(id);
        const fs::path workspace = armDir / "workspace";
        const fs::path tmp = armDir / "tmp";
        if (!copyTree(base, workspace)) {
            fs::remove_all(root, ec);
            return taskforge::Json{{"ok", false}, {"error", "workspace_copy_failed"},
                                   {"arm", id}};
        }
        fs::create_directories(tmp, ec);
        outArms.push_back(taskforge::Json{
            {"id", id},
            {"workspace", workspace.string()},
            {"tmp", tmp.string()},
            {"mode", "copy"},
            {"workspaceKind", snapshot.value("workspaceKind", std::string("path"))}});
    }
    if (outArms.empty()) {
        fs::remove_all(root, ec);
        return taskforge::Json{{"ok", false}, {"error", "arms sem id"}};
    }
    const auto plan = creditAuditContainerPlan(projectId, taskforge::Json{
        {"workspaceRef", root.string()}});
    return taskforge::Json{{"ok", true}, {"root", root.string()},
                           {"container", plan}, {"arms", outArms}};
}

taskforge::Json MemoryService::cleanupCreditAuditWorkspaces(
    const std::string& projectId, const std::string& traceId) {
    if (traceId.empty()) return taskforge::Json{{"ok", false}, {"error", "traceId ausente"}};
    const fs::path telemetry(telemetryDatabasePath(projectId));
    if (telemetry.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "telemetria sem caminho"}};
    }
    const fs::path root = telemetry.parent_path() / "audit-workspaces" / safeName(traceId);
    std::error_code ec;
    const bool existed = fs::exists(root, ec);
    fs::remove_all(root, ec);
    if (ec) return taskforge::Json{{"ok", false}, {"error", ec.message()}};
    return taskforge::Json{{"ok", true}, {"root", root.string()}, {"removed", existed}};
}

taskforge::Json MemoryService::saveTaskSnapshot(const std::string& projectId,
                                                const taskforge::Json& request) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const auto config = creditAuditConfig(projectId);
    MemoryTaskSnapshotRecord snapshot;
    snapshot.scopeId = scopeId(projectId);
    snapshot.traceId = request.value("traceId", std::string());
    snapshot.inputHash = request.value("inputHash", std::string());
    snapshot.inputJson = request.value("input", taskforge::Json::object()).dump();
    snapshot.workspaceKind = request.value("workspaceKind", std::string());
    snapshot.workspaceRef = request.value("workspaceRef", std::string());
    snapshot.bytes = request.value("bytes", 0LL);
    snapshot.expiresAt = request.value("expiresAt", std::string());
    if (snapshot.traceId.empty()) {
        return taskforge::Json{{"ok", false}, {"error", "traceId ausente"}};
    }
    if (snapshot.bytes > config.maxSnapshotBytes) {
        return taskforge::Json{{"ok", false}, {"reason", "snapshot_too_large"}};
    }
    if (snapshot.expiresAt.empty()) {
        snapshot.expiresAt = isoDaysFromNow(config.snapshotRetentionDays);
    }
    if (!t->saveTaskSnapshot(snapshot)) {
        return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    }
    return taskforge::Json{{"ok", true}, {"snapshot", t->getTaskSnapshot(
        snapshot.scopeId, snapshot.traceId)}};
}

taskforge::Json MemoryService::purgeExpiredSnapshots(const std::string& projectId) {
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    const long long removed = t->purgeExpiredSnapshots(scopeId(projectId), "");
    return taskforge::Json{{"ok", true}, {"removed", removed},
                           {"summary", t->snapshotSummary(scopeId(projectId))}};
}

namespace {
std::string creditOutboxPath(const std::string& telemetryPath) {
    if (telemetryPath.empty()) return std::string();
    const fs::path db(telemetryPath);
    return (db.parent_path() / "credit-outbox.jsonl").string();
}
} // namespace

bool MemoryService::appendCreditOutbox(const std::string& projectId,
                                       const std::vector<MemoryCreditEvent>& events) {
    if (events.empty()) return true;
    const std::string path = creditOutboxPath(telemetryDatabasePath(projectId));
    if (path.empty()) return false;
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) return false;
    for (const auto& event : events) {
        out << taskforge::Json{
                   {"traceId", event.traceId},
                   {"itemKey", event.itemKey},
                   {"taskType", event.taskType},
                   {"modelTier", event.modelTier},
                   {"deltaSuccess", event.deltaSuccess},
                   {"deltaFailure", event.deltaFailure},
                   {"kind", event.kind},
                   {"origin", event.origin},
                   {"auditId", event.auditId},
                   {"capped", event.capped}}.dump()
            << "\n";
        out.flush();
    }
    return out.good();
}

taskforge::Json MemoryService::drainCreditOutbox(const std::string& projectId) {
    const std::string path = creditOutboxPath(telemetryDatabasePath(projectId));
    if (path.empty()) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        return taskforge::Json{{"ok", true}, {"drained", 0LL}};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return taskforge::Json{{"ok", false}, {"error", "outbox ilegivel"}};
    std::vector<MemoryCreditEvent> events;
    std::string line;
    while (std::getline(in, line)) {
        const auto parsed = taskforge::jsonutil::tryParse(line);
        if (!parsed || !parsed->is_object()) continue;
        MemoryCreditEvent event;
        event.traceId = parsed->value("traceId", std::string());
        event.itemKey = parsed->value("itemKey", std::string());
        event.taskType = parsed->value("taskType", std::string());
        event.modelTier = parsed->value("modelTier", std::string());
        event.deltaSuccess = parsed->value("deltaSuccess", 0.0);
        event.deltaFailure = parsed->value("deltaFailure", 0.0);
        event.kind = parsed->value("kind", std::string());
        event.origin = parsed->value("origin", std::string());
        event.auditId = parsed->value("auditId", std::string());
        event.capped = parsed->value("capped", false);
        if (!event.traceId.empty() && !event.itemKey.empty()) {
            events.push_back(std::move(event));
        }
    }
    in.close();
    auto* t = ensureTelemetry(projectId);
    if (!t) return taskforge::Json{{"ok", false}, {"error", "telemetria indisponivel"}};
    if (!t->recordCreditEvents(scopeId(projectId), events)) {
        return taskforge::Json{{"ok", false}, {"error", t->lastError()}};
    }
    fs::remove(path, ec);
    return taskforge::Json{{"ok", true},
                           {"drained", static_cast<long long>(events.size())}};
}

std::vector<MemoryEvidenceRecord> MemoryService::evidenceFor(const std::string& projectId,
                                                             const std::string& targetKind,
                                                             const std::string& targetId,
                                                             size_t limit) {
    auto* s = ensureStore(projectId);
    return s ? s->listEvidence(scopeId(projectId), targetKind, targetId, limit)
             : std::vector<MemoryEvidenceRecord>();
}

taskforge::Json MemoryService::evidenceSummary(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->evidenceSummary(scopeId(projectId)) : taskforge::Json::object();
}

bool MemoryService::unpromotePlaybook(const std::string& projectId, const std::string& playbookId,
                                      const std::string& reason, std::string& error) {
    auto* s = existingStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    return s->unpromotePlaybook(scopeId(projectId), playbookId, reason, error);
}

size_t MemoryService::repairOverlaps(const std::string& projectId, std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return 0;
    }
    invalidateBriefCache();
    return s->repairOverlaps(scopeId(projectId), error);
}

taskforge::Json MemoryService::listMultiClosures(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    return s ? s->listMultiClosures(scopeId(projectId)) : taskforge::Json::array();
}

bool MemoryService::reopenEdge(const std::string& projectId, const std::string& edgeId,
                               const std::string& reason, std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de conhecimento indisponivel";
        return false;
    }
    invalidateBriefCache();
    return s->reopenEdge(scopeId(projectId), edgeId, reason, error);
}

taskforge::Json MemoryService::doctor(const std::string& projectId) {
    const std::string scope = scopeId(projectId);
    taskforge::Json checks = taskforge::Json::array();
    bool ok = true;
    if (auto* knowledge = ensureStore(projectId)) {
        const auto report = knowledge->diagnostics(scope);
        const auto list = report.value("checks", taskforge::Json::array());
        for (size_t i = 0; i < list.size(); ++i) {
            if (i == 5) continue;   // a verificacao 6 usa a telemetria
            checks.push_back(list[i]);
            if (list[i].value("violations", 0LL) > 0) ok = false;
        }
        const auto lineage = knowledge->lineageSummary(scope);
        const long long links = lineage.value("links", 0LL);
        const long long orphans = lineage.value("orphanEdges", 0LL) +
            lineage.value("orphanProposals", 0LL) +
            lineage.value("orphanFacets", 0LL) +
            lineage.value("orphanPlaybooks", 0LL);
        const long long violations = links > 0 ? orphans : 0;
        checks.push_back(taskforge::Json{
            {"id", "lineage_orphans"}, {"ok", violations == 0},
            {"critical", true}, {"violations", violations}, {"details", lineage}});
        if (violations > 0) ok = false;
    }
    taskforge::Json telemetryCheck;
    if (auto* telemetry = telemetryStore(projectId)) {
        const auto report = telemetry->diagnostics(scope);
        const auto list = report.value("checks", taskforge::Json::array());
        if (list.size() > 5) telemetryCheck = list[5];
    }
    if (telemetryCheck.is_object()) {
        checks.push_back(telemetryCheck);
        if (telemetryCheck.value("violations", 0LL) > 0) ok = false;
    }
    taskforge::Json out{{"ok", ok}, {"projectId", projectId}, {"checks", checks},
                        {"catalogFingerprint", MemoryCatalog::instance().fingerprint()},
                        {"catalogMode", MemoryCatalog::instance().mode()}};
    if (auto* knowledge = existingStore(projectId)) {
        out["jobs"] = knowledge->jobSummary(scope);
        out["tombstones"] = knowledge->tombstoneSummary(scope);
        out["lineage"] = knowledge->lineageSummary(scope);
    }
    return out;
}

taskforge::Json MemoryService::toJson(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    taskforge::Json out = s->toJson(scopeId(projectId));
    auto* t = ensureTelemetry(projectId);
    if (t) {
        out["usage"] = t->usageSummary(scopeId(projectId));
        out["hits"] = t->hitSummary(scopeId(projectId));
    }
    out["projectId"] = projectId;
    out["persistent"] = isPersistent(projectId);
    out["database"] = databasePath(projectId);
    out["telemetry"] = telemetryDatabasePath(projectId);
    out["maintenance"] = maintenanceStatus(projectId);
    out["evidence"] = s->evidenceSummary(scopeId(projectId));
    out["sources"] = s->sourceSummary(scopeId(projectId));
    out["facets"] = s->facetSummary(scopeId(projectId));
    out["paths"] = s->pathSummary(scopeId(projectId));
    out["jobs"] = s->jobSummary(scopeId(projectId));
    out["tombstones"] = s->tombstoneSummary(scopeId(projectId));
    out["pathRecords"] = listPaths(projectId);
    out["pathServing"] = pathServingConfigToJson(pathServingConfig(projectId));
    out["traversal"] = traversalConfigToJson(traversalConfig(projectId));
    return out;
}

taskforge::Json MemoryService::toCognitiveJson(const std::string& projectId) {
    auto* s = ensureStore(projectId);
    if (!s) return taskforge::Json::object();
    taskforge::Json out = s->toCognitiveJson(scopeId(projectId));
    out["projectId"] = projectId;
    return out;
}

bool MemoryService::importCognitiveJson(const std::string& projectId, const taskforge::Json& data,
                                        std::string& error) {
    auto* s = ensureStore(projectId);
    if (!s) {
        error = "banco de memoria indisponivel";
        return false;
    }
    const bool imported = s->importCognitiveJson(scopeId(projectId), data, error);
    if (imported) invalidateBriefCache();
    return imported;
}

bool MemoryService::clearSession(const std::string& projectId) {
    if (isPersistent(projectId)) return true;
    stopMaintenance(projectId);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (auto* s = ensureStore(projectId)) {
        if (s->resetInMemory()) return true;
        // Defesa em profundidade: nunca purgar um store de arquivo. Se isto
        // acontecer, o regime nao persistente abriu um arquivo por engano.
        std::cerr << "[MEMORIA] clearSession ignorado: store nao efemero ("
                  << s->path() << ")\n";
        return false;
    }
    return false;
}

void MemoryService::closeProject(const std::string& projectId) {
    invalidateBriefCache();
    stopMaintenance(projectId);
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    stores_.erase(projectKey(projectId));
    persistent_.erase(projectKey(projectId));
    paths_.erase(projectKey(projectId));
    pathServing_.erase(projectKey(projectId));
    traversal_.erase(projectKey(projectId));
    utility_.erase(projectKey(projectId));
    readOnly_.erase(projectKey(projectId));
    telemetry_.erase(projectKey(projectId));
    telemetryPaths_.erase(projectKey(projectId));
    embeddings_.erase(projectKey(projectId));
    projectWorkspace_.erase(projectKey(projectId));
}

} // namespace memory
} // namespace taskforge
