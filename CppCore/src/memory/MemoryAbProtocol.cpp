#include "MemoryAbProtocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "project/RunnerSecurity.hpp"
#include "rt/MemoryFailure.hpp"

namespace taskforge {
namespace memory {

namespace {

bool isHexHash(const std::string& value) {
    if (value.size() != 64) return false;
    for (unsigned char c : value) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

double mean(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double value : values) sum += value;
    return sum / static_cast<double>(values.size());
}

double stddev(const std::vector<double>& values) {
    if (values.size() < 2) return 0.0;
    const double m = mean(values);
    double sum = 0.0;
    for (double value : values) sum += (value - m) * (value - m);
    return std::sqrt(sum / static_cast<double>(values.size() - 1));
}

double tCritical95(size_t df) {
    static const double table[] = {0.0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447,
                                   2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160,
                                   2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
                                   2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052,
                                   2.048, 2.045, 2.042};
    if (df >= sizeof(table) / sizeof(table[0])) return 1.96;
    return table[df];
}

taskforge::Json interval(const std::vector<double>& values) {
    const double m = mean(values);
    const double sd = stddev(values);
    const double margin = values.size() < 2
        ? 0.0
        : tCritical95(values.size() - 1) * sd / std::sqrt(static_cast<double>(values.size()));
    return taskforge::Json{{"mean", m}, {"ciLow", m - margin}, {"ciHigh", m + margin},
                           {"n", static_cast<long long>(values.size())}};
}

std::string stringField(const taskforge::Json& object, const char* key,
                        const std::string& fallback = "") {
    return object.is_object() && object.contains(key) && object[key].is_string()
        ? object[key].str() : fallback;
}

double numberField(const taskforge::Json& object, const char* key, double fallback = 0.0) {
    return object.is_object() && object.contains(key) && object[key].is_number()
        ? object[key].get<double>() : fallback;
}

} // namespace

taskforge::Json MemoryAbProtocol::validateSuite(const taskforge::Json& suite, std::string& error) {
    error.clear();
    if (!suite.is_object() || !suite.contains("tasks") || !suite["tasks"].is_array()) {
        error = "conjunto A/B sem lista de tarefas";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const std::string version = stringField(suite, "version");
    if (version.empty()) {
        error = "conjunto A/B sem versao";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }

    std::set<std::string> ids;
    std::set<std::string> hashes;
    size_t relevant = 0;
    size_t control = 0;
    size_t knownFailure = 0;
    for (const auto& task : suite["tasks"]) {
        if (!task.is_object()) {
            error = "tarefa A/B invalida";
            return taskforge::Json{{"ok", false}, {"error", error}};
        }
        const std::string id = stringField(task, "id");
        const std::string hash = stringField(task, "inputHash");
        const std::string category = stringField(task, "category");
        if (id.empty() || !isHexHash(hash)) {
            error = "tarefa A/B com id ou input_hash invalido";
            return taskforge::Json{{"ok", false}, {"error", error}};
        }
        if (!ids.insert(id).second) {
            error = "id de tarefa A/B duplicado: " + id;
            return taskforge::Json{{"ok", false}, {"error", error}};
        }
        if (!hashes.insert(hash).second) {
            error = "input_hash duplicado no conjunto A/B";
            return taskforge::Json{{"ok", false}, {"error", error}};
        }
        if (task.contains("input") && task["input"].is_string()) {
            const std::string expected = project::RunnerSecurity::sha256(
                version + "|" + id + "|" + task["input"].str());
            if (expected != hash) {
                error = "input_hash nao corresponde a entrada da tarefa: " + id;
                return taskforge::Json{{"ok", false}, {"error", error}};
            }
        }
        if (category == "relevant") ++relevant;
        else if (category == "control") ++control;
        else if (category == "known_failure") ++knownFailure;
        else {
            error = "categoria A/B desconhecida: " + category;
            return taskforge::Json{{"ok", false}, {"error", error}};
        }
    }
    if (suite["tasks"].size() != 20 || relevant != 12 || control != 4 || knownFailure != 4) {
        error = "conjunto A/B deve ter 20 tarefas: 12 relevantes, 4 controle e 4 falhas conhecidas";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    return taskforge::Json{{"ok", true},
                           {"version", version},
                           {"tasks", static_cast<long long>(suite["tasks"].size())},
                           {"relevant", static_cast<long long>(relevant)},
                           {"control", static_cast<long long>(control)},
                           {"knownFailure", static_cast<long long>(knownFailure)}};
}

taskforge::Json MemoryAbProtocol::buildRunOrder(const taskforge::Json& suite, int repetitions,
                                                uint32_t seed, std::string& error) {
    const taskforge::Json validation = validateSuite(suite, error);
    if (!validation.value("ok", false)) return validation;
    if (repetitions <= 0 || repetitions > 20) {
        error = "repeticoes devem ficar entre 1 e 20";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    std::vector<std::string> taskIds;
    for (const auto& task : suite["tasks"]) taskIds.push_back(stringField(task, "id"));
    std::mt19937 rng(seed);
    taskforge::Json order = taskforge::Json::array();
    for (int repetition = 1; repetition <= repetitions; ++repetition) {
        std::shuffle(taskIds.begin(), taskIds.end(), rng);
        for (const auto& taskId : taskIds) {
            std::vector<std::string> arms{"on", "off"};
            std::shuffle(arms.begin(), arms.end(), rng);
            for (const auto& arm : arms) {
                order.push_back(taskforge::Json{{"taskId", taskId},
                                                {"arm", arm},
                                                {"repetition", repetition}});
            }
        }
    }
    return taskforge::Json{{"ok", true}, {"suiteVersion", validation["version"]},
                           {"repetitions", repetitions}, {"seed", static_cast<long long>(seed)},
                           {"order", order}};
}

taskforge::Json MemoryAbProtocol::analyze(const taskforge::Json& suite,
                                          const taskforge::Json& observations,
                                          int repetitions, std::string& error) {
    error.clear();
    const taskforge::Json validation = validateSuite(suite, error);
    if (!validation.value("ok", false)) return validation;
    if (repetitions <= 0 || repetitions > 20) {
        error = "repeticoes devem ficar entre 1 e 20";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    if (!observations.is_object() || !observations.contains("observations") ||
        !observations["observations"].is_array()) {
        error = "observacoes A/B ausentes";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }

    struct ArmSamples {
        std::vector<bool> ok;
        std::vector<double> tokens;
        std::vector<double> turns;
        std::vector<double> cost;
        std::vector<double> latency;
        size_t infra = 0;
    };
    std::map<std::string, std::map<std::string, ArmSamples>> byTask;
    size_t infraEvents = 0;
    for (const auto& observation : observations["observations"]) {
        if (!observation.is_object()) continue;
        const std::string taskId = stringField(observation, "taskId");
        const std::string arm = stringField(observation, "arm");
        if (arm != "on" && arm != "off") continue;
        const std::string errorClass = stringField(observation, "errorClass");
        if (taskforge::rt::isInfrastructureErrorClass(errorClass)) {
            ++byTask[taskId][arm].infra;
            ++infraEvents;
            continue;
        }
        ArmSamples& samples = byTask[taskId][arm];
        samples.ok.push_back(observation.value("ok", false));
        samples.tokens.push_back(numberField(observation, "tokens"));
        samples.turns.push_back(numberField(observation, "turns"));
        samples.cost.push_back(numberField(observation, "cost"));
        samples.latency.push_back(numberField(observation, "latencyMs"));
    }

    std::vector<double> approvalDeltas;
    std::vector<double> tokenDeltas;
    std::vector<double> turnDeltas;
    std::vector<double> costDeltas;
    std::vector<double> latencyDeltas;
    std::vector<double> onApproval;
    std::vector<double> offApproval;
    std::vector<double> onTokens;
    std::vector<double> offTokens;
    std::vector<double> onTurns;
    std::vector<double> offTurns;
    std::vector<double> onCost;
    std::vector<double> offCost;
    std::vector<double> onLatency;
    std::vector<double> offLatency;
    taskforge::Json pairs = taskforge::Json::array();
    taskforge::Json excluded = taskforge::Json::array();

    auto rate = [](const std::vector<bool>& values) {
        if (values.empty()) return 0.0;
        size_t ok = 0;
        for (bool value : values) if (value) ++ok;
        return static_cast<double>(ok) / static_cast<double>(values.size());
    };
    auto armMean = [](const std::vector<double>& values) {
        return values.empty() ? 0.0 : mean(values);
    };

    for (const auto& task : suite["tasks"]) {
        const std::string taskId = stringField(task, "id");
        auto found = byTask.find(taskId);
        if (found == byTask.end()) {
            excluded.push_back(taskforge::Json{{"taskId", taskId}, {"reason", "sem observacoes"}});
            continue;
        }
        const ArmSamples& on = found->second["on"];
        const ArmSamples& off = found->second["off"];
        if (on.ok.size() < static_cast<size_t>(repetitions) ||
            off.ok.size() < static_cast<size_t>(repetitions)) {
            excluded.push_back(taskforge::Json{{"taskId", taskId}, {"reason", "repeticoes insuficientes"},
                                               {"on", static_cast<long long>(on.ok.size())},
                                               {"off", static_cast<long long>(off.ok.size())}});
            continue;
        }
        const double onRate = rate(on.ok);
        const double offRate = rate(off.ok);
        const double delta = onRate - offRate;
        approvalDeltas.push_back(delta);
        onApproval.push_back(onRate);
        offApproval.push_back(offRate);
        const double onToken = armMean(on.tokens);
        const double offToken = armMean(off.tokens);
        tokenDeltas.push_back(onToken - offToken);
        onTokens.push_back(onToken);
        offTokens.push_back(offToken);
        const double onTurn = armMean(on.turns);
        const double offTurn = armMean(off.turns);
        turnDeltas.push_back(onTurn - offTurn);
        onTurns.push_back(onTurn);
        offTurns.push_back(offTurn);
        const double onC = armMean(on.cost);
        const double offC = armMean(off.cost);
        costDeltas.push_back(onC - offC);
        onCost.push_back(onC);
        offCost.push_back(offC);
        const double onL = armMean(on.latency);
        const double offL = armMean(off.latency);
        latencyDeltas.push_back(onL - offL);
        onLatency.push_back(onL);
        offLatency.push_back(offL);
        pairs.push_back(taskforge::Json{{"taskId", taskId},
                                        {"category", stringField(task, "category")},
                                        {"onApproval", onRate},
                                        {"offApproval", offRate},
                                        {"delta", delta},
                                        {"onTokens", onToken},
                                        {"offTokens", offToken},
                                        {"onTurns", onTurn},
                                        {"offTurns", offTurn},
                                        {"onCost", onC},
                                        {"offCost", offC},
                                        {"onLatencyMs", onL},
                                        {"offLatencyMs", offL}});
    }

    if (approvalDeltas.empty()) {
        error = "nenhuma tarefa A/B completa";
        return taskforge::Json{{"ok", false}, {"error", error}};
    }
    const taskforge::Json primary = taskforge::Json{
        {"metric", "validator_approval"},
        {"on", mean(onApproval)},
        {"off", mean(offApproval)},
        {"delta", mean(approvalDeltas)},
        {"ciLow", interval(approvalDeltas)["ciLow"]},
        {"ciHigh", interval(approvalDeltas)["ciHigh"]},
        {"n", static_cast<long long>(approvalDeltas.size())}};
    const taskforge::Json secondary = taskforge::Json{
        {"tokens", taskforge::Json{{"on", mean(onTokens)}, {"off", mean(offTokens)},
                                   {"delta", mean(tokenDeltas)},
                                   {"ciLow", interval(tokenDeltas)["ciLow"]},
                                   {"ciHigh", interval(tokenDeltas)["ciHigh"]}}},
        {"turns", taskforge::Json{{"on", mean(onTurns)}, {"off", mean(offTurns)},
                                  {"delta", mean(turnDeltas)},
                                  {"ciLow", interval(turnDeltas)["ciLow"]},
                                  {"ciHigh", interval(turnDeltas)["ciHigh"]}}},
        {"cost", taskforge::Json{{"on", mean(onCost)}, {"off", mean(offCost)},
                                 {"delta", mean(costDeltas)},
                                 {"ciLow", interval(costDeltas)["ciLow"]},
                                 {"ciHigh", interval(costDeltas)["ciHigh"]}}},
        {"latencyMs", taskforge::Json{{"on", mean(onLatency)}, {"off", mean(offLatency)},
                                      {"delta", mean(latencyDeltas)},
                                      {"ciLow", interval(latencyDeltas)["ciLow"]},
                                      {"ciHigh", interval(latencyDeltas)["ciHigh"]}}}};
    return taskforge::Json{{"ok", true},
                           {"suiteVersion", validation["version"]},
                           {"repetitions", repetitions},
                           {"infraEvents", static_cast<long long>(infraEvents)},
                           {"primary", primary},
                           {"secondary", secondary},
                           {"pairs", pairs},
                           {"excluded", excluded}};
}

} // namespace memory
} // namespace taskforge
