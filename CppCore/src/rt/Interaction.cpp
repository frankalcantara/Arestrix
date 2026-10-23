#include "rt/Interaction.hpp"
#include "rt/NodeInput.hpp"
#include "rt/Runtime.hpp"
#include "platform/Process.hpp"
#include "sandbox/AgentTelemetryEmitter.hpp"
#include "util/Json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace taskforge {
namespace rt {

namespace fs = std::filesystem;

namespace {

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    }
    return true;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Arquivo nao encontrado: " + path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string textValue(const Json& value) {
    if (value.is_string()) return value.str();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number() || value.is_null()) return value.dump();
    if (value.is_array()) {
        std::ostringstream out;
        bool first = true;
        for (const auto& item : value) {
            if (!first) out << ", ";
            out << textValue(item);
            first = false;
        }
        return out.str();
    }
    return value.dump();
}

bool optionContains(const Json& field, const std::string& value) {
    if (!field.contains("options") || !field["options"].is_array()) return true;
    for (const auto& option : field["options"]) {
        if (option.is_object() && option.value("value", std::string()) == value) return true;
        if (option.is_string() && option.str() == value) return true;
    }
    return false;
}

bool isBlankValue(const Json& value) {
    if (value.is_null()) return true;
    if (value.is_string()) {
        const std::string& text = value.str();
        return text.find_first_not_of(" \t\r\n") == std::string::npos;
    }
    if (value.is_array()) return value.empty();
    return false;
}

// Canal do host (Studio/Runner): o processo filho escreve [ARX_INTERACTION] no stdout e
// espera [ARX_ANSWER] no stdin. Uma unica thread le o stdin e distribui as linhas; assim
// o timeout nao deixa uma leitura bloqueada no meio do pipeline.
enum class HostReadStatus { Data, Timeout, Eof };

class HostLineReader {
public:
    static HostLineReader& instance() {
        static HostLineReader reader;
        return reader;
    }

    HostReadStatus next(std::string& line, int timeoutMs) {
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait_for(lock, std::chrono::milliseconds(std::max(0, timeoutMs)),
                     [&] { return !lines_.empty() || eof_; });
        if (!lines_.empty()) {
            line = std::move(lines_.front());
            lines_.pop_front();
            return HostReadStatus::Data;
        }
        return eof_ ? HostReadStatus::Eof : HostReadStatus::Timeout;
    }

private:
    HostLineReader() {
        thread_ = std::thread([this] {
            std::string line;
            while (std::getline(std::cin, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    lines_.push_back(std::move(line));
                }
                cv_.notify_all();
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                eof_ = true;
            }
            cv_.notify_all();
        });
    }

    ~HostLineReader() {
        if (thread_.joinable()) thread_.detach();
    }

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> lines_;
    bool eof_ = false;
};

struct HostReply {
    bool answered = false;
    bool cancelled = false;
    bool timedOut = false;
    bool closed = false;
    std::string error;
    Json answers = Json::object();
};

HostReply waitForHostAnswer(const std::string& requestId, int timeoutMs) {
    HostReply reply;
    const std::string prefix = "[ARX_ANSWER] ";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            reply.timedOut = true;
            return reply;
        }
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        std::string line;
        HostReadStatus status = HostLineReader::instance().next(line, remaining);
        if (status == HostReadStatus::Timeout) {
            reply.timedOut = true;
            return reply;
        }
        if (status == HostReadStatus::Eof) {
            reply.closed = true;
            return reply;
        }
        if (line.rfind(prefix, 0) != 0) continue;   // linhas de outro canal/host: ignora

        auto parsed = jsonutil::tryParse(line.substr(prefix.size()));
        if (!parsed || !parsed->is_object()) continue;
        const std::string id = parsed->value("requestId", std::string());
        if (!id.empty() && id != requestId) continue;
        if (parsed->value("cancelled", false)) {
            reply.cancelled = true;
            return reply;
        }
        if (parsed->value("timedOut", false)) {
            reply.timedOut = true;
            return reply;
        }
        const std::string error = parsed->value("error", std::string());
        if (!error.empty()) {
            reply.error = error;
            return reply;
        }
        if (parsed->contains("answers")) {
            if (!(*parsed)["answers"].is_object()) {
                reply.error = "Host devolveu 'answers' que nao e um objeto JSON.";
                return reply;
            }
            reply.answers = (*parsed)["answers"];
            reply.answered = true;
            return reply;
        }
    }
}

std::string newRequestId() {
    static std::atomic<unsigned long long> counter{0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto ticks = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    return "req-" + std::to_string(ticks) + "-" + std::to_string(++counter);
}

bool envEnabled(const std::string& value) {
    if (value.empty()) return false;
    std::string v = value;
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v != "0" && v != "false" && v != "no" && v != "off";
}

// O Studio mostra um modal por vez. Interações em ramos paralelos são serializadas
// no processo filho; a segunda só emite o pedido quando a primeira termina.
std::mutex& hostInteractionMutex() {
    static std::mutex mutex;
    return mutex;
}

void validateField(const Json& field, const Json& value) {
    const std::string type = field.value("type", std::string("text"));
    if (type == "text" || type == "textarea" || type == "date") {
        if (!value.is_string()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser texto.");
        }
        return;
    }
    if (type == "number") {
        if (!value.is_number()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser numero.");
        }
        return;
    }
    if (type == "boolean") {
        if (!value.is_boolean()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser booleano.");
        }
        return;
    }
    if (type == "choice") {
        if (!value.is_string()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser uma escolha.");
        }
        if (!optionContains(field, value.str())) {
            throw std::runtime_error("Opcao invalida para o campo '" + field.value("label", std::string("?")) + "': " + value.str());
        }
        return;
    }
    if (type == "multi_choice") {
        if (!value.is_array()) {
            throw std::runtime_error("Campo '" + field.value("label", std::string("?")) + "' deve ser uma lista de escolhas.");
        }
        for (const auto& item : value) {
            if (!item.is_string() || !optionContains(field, item.str())) {
                throw std::runtime_error("Opcao invalida no campo '" + field.value("label", std::string("?")) + "'.");
            }
        }
        return;
    }
    throw std::runtime_error("Tipo de campo desconhecido: " + type);
}

} // namespace

std::string runInteraction(const std::string& nodeId, const std::string& label,
                           const std::string& specJson,
                           const std::string& inputContext) {
    if (!safeId(nodeId)) throw std::runtime_error("Vertice de interacao com id invalido: " + nodeId);

    Json spec = jsonutil::parse(specJson);
    if (!spec.is_object()) throw std::runtime_error("Vertice de interacao '" + label + "': configuracao invalida.");
    if (spec.value("schemaSource", std::string("static")) != "static") {
        throw std::runtime_error("Vertice de interacao '" + label +
                                 "': formulario gerado por modelo ainda nao disponivel (Fase 2D).");
    }
    if (!spec.contains("form") || !spec["form"].is_object() ||
        !spec["form"].contains("fields") || !spec["form"]["fields"].is_array() ||
        spec["form"]["fields"].empty()) {
        throw std::runtime_error("Vertice de interacao '" + label + "': formulario sem campos.");
    }

    const Json& form = spec["form"];
    const Json& fields = form["fields"];
    const bool sensitive = spec.value("sensitive", false);
    const std::string channel = spec.value("channel", std::string("any"));
    const std::string onTimeout = spec.value("onTimeout", std::string("fail"));
    int timeoutSeconds = spec.value("timeoutSeconds", 300);
    if (timeoutSeconds < 1) timeoutSeconds = 300;
    if (timeoutSeconds > 86400) timeoutSeconds = 86400;

    const bool hostChannel = envEnabled(platform::getEnv("ARESTRIX_INTERACTION_HOST"));
    const std::string answersPath = platform::getEnv("ARESTRIX_INTERACTION_ANSWERS");
    const bool fileChannel = !answersPath.empty();

    bool useHost = false;
    bool useFile = false;
    if (channel == "cli") {
        useFile = fileChannel;
    } else if (channel == "studio" || channel == "runner") {
        useHost = hostChannel;
    } else {
        useHost = hostChannel;
        useFile = !useHost && fileChannel;
    }

    Json answers = Json::object();
    std::string answeredBy = "answers";
    bool haveAnswers = false;

    auto useDefaults = [&]() -> bool {
        if (onTimeout != "default") return false;
        answers = (spec.contains("defaultAnswers") && spec["defaultAnswers"].is_object())
                      ? spec["defaultAnswers"] : Json::object();
        answeredBy = "default";
        haveAnswers = true;
        return true;
    };

    if (!useHost && !useFile && !useDefaults()) {
        if (channel == "cli") {
            throw std::runtime_error("Vertice de interacao '" + label +
                                     "': canal 'cli' exige respostas headless (--answers).");
        }
        if (channel == "studio" || channel == "runner") {
            throw std::runtime_error("Vertice de interacao '" + label +
                                     "': canal '" + channel + "' exige Studio/Runner com host interativo.");
        }
        throw std::runtime_error("Vertice de interacao '" + label +
                                 "': nenhum canal de respostas disponivel. Use o Studio ou --answers <arquivo>.");
    }

    sandbox::AgentTelemetryEmitter::emitEvent(nodeId, "interaction_request", Json{
        {"nodeId", nodeId},
        {"channel", useHost ? "host" : (useFile ? "answers" : "default")},
        {"fields", fields.size()},
        {"sensitive", sensitive}
    });

    if (useHost && !haveAnswers) {
        std::lock_guard<std::mutex> hostLock(hostInteractionMutex());
        const std::string requestId = newRequestId();
        Json request{
            {"requestId", requestId},
            {"nodeId", nodeId},
            {"label", label},
            {"form", form},
            {"inputContext", inputContext},
            {"timeoutSeconds", timeoutSeconds},
            {"onTimeout", onTimeout},
            {"channel", channel},
            {"sensitive", sensitive}
        };
        if (spec.contains("defaultAnswers") && spec["defaultAnswers"].is_object()) {
            request["defaultAnswers"] = spec["defaultAnswers"];
        }
        std::cout << "[ARX_INTERACTION] " << request.dump() << std::endl;

        // O host devolve no prazo configurado; a margem cobre o transporte e evita
        // que os dois lados declarem timeout ao mesmo tempo.
        const int waitMs = timeoutSeconds * 1000 + 2000;
        HostReply reply = waitForHostAnswer(requestId, waitMs);
        if (!reply.error.empty()) {
            throw std::runtime_error("Vertice de interacao '" + label + "': " + reply.error);
        }
        if (reply.cancelled) {
            throw std::runtime_error("Vertice de interacao '" + label + "': cancelada pelo usuario.");
        }
        if (reply.closed) {
            throw std::runtime_error("Vertice de interacao '" + label +
                                     "': o canal do host encerrou antes da resposta.");
        }
        if (reply.timedOut) {
            if (onTimeout == "abort") {
                throw std::runtime_error("Vertice de interacao '" + label + "': timeout com politica abort.");
            }
            if (!useDefaults()) {
                throw std::runtime_error("Vertice de interacao '" + label + "': tempo esgotado sem resposta.");
            }
        } else if (reply.answered) {
            answers = reply.answers;
            answeredBy = "host";
            haveAnswers = true;
        }
    }

    if (useFile && !haveAnswers) {
        fs::path path(answersPath);
        if (path.is_relative()) path = workspaceRoot() / path;
        if (!fs::is_regular_file(path)) {
            if (!useDefaults()) {
                throw std::runtime_error("Arquivo de respostas nao encontrado: " + path.string());
            }
        } else {
            Json root = jsonutil::parse(readFile(path));
            if (!root.is_object()) throw std::runtime_error("Arquivo de respostas invalido: esperado um objeto JSON.");
            if (root.contains(nodeId)) {
                Json value = root[nodeId];
                if (value.is_array()) {
                    static std::mutex countsMutex;
                    static std::map<std::string, int> counts;
                    int index = 0;
                    {
                        std::lock_guard<std::mutex> lock(countsMutex);
                        index = counts[nodeId]++;
                    }
                    if (value.empty()) throw std::runtime_error("Lista de respostas vazia para '" + nodeId + "'.");
                    value = value[std::min<size_t>(static_cast<size_t>(index), value.size() - 1)];
                }
                if (!value.is_object()) {
                    throw std::runtime_error("Resposta do vertice '" + nodeId + "' deve ser um objeto JSON.");
                }
                answers = value;
                answeredBy = "answers";
                haveAnswers = true;
            } else if (!useDefaults()) {
                throw std::runtime_error("Vertice de interacao '" + label +
                                         "': resposta ausente para '" + nodeId + "' no arquivo " + path.string() + ".");
            }
        }
    }

    if (!haveAnswers && !useDefaults()) {
        throw std::runtime_error("Vertice de interacao '" + label +
                                 "': nenhum canal de respostas disponivel. Use o Studio ou --answers <arquivo>.");
    }

    Json normalized = Json::object();
    for (const auto& field : fields) {
        if (!field.is_object()) continue;
        const std::string id = field.value("id", std::string());
        if (id.empty()) throw std::runtime_error("Vertice de interacao '" + label + "': campo sem id.");
        const bool required = field.value("required", false);
        bool hasValue = answers.contains(id) && !isBlankValue(answers[id]);
        Json value = hasValue ? answers[id] : Json();
        if (!hasValue && field.contains("defaultValue")) {
            value = field["defaultValue"];
            hasValue = !isBlankValue(value);
        }
        if (!hasValue) {
            if (required) {
                throw std::runtime_error("Vertice de interacao '" + label + "': campo obrigatorio nao respondido: " +
                                         field.value("label", id));
            }
            continue;
        }
        validateField(field, value);
        normalized[id] = value;
    }

    std::ostringstream text;
    if (!inputContext.empty()) text << "Contexto do chamador:\n" << inputContext << "\n\n";
    for (const auto& field : fields) {
        if (!field.is_object()) continue;
        const std::string id = field.value("id", std::string());
        if (!normalized.contains(id)) continue;
        text << field.value("label", id) << ": " << textValue(normalized[id]) << "\n";
    }

    Json result{
        {"nodeId", nodeId},
        {"status", "ok"},
        {"answers", normalized},
        {"text", text.str()},
        {"answeredBy", answeredBy},
        {"sensitive", sensitive}
    };

    writeNodeOutput(nodeId, text.str());
    std::error_code ec;
    fs::path outDir = workspaceRoot() / ".arestrix" / "out";
    fs::create_directories(outDir, ec);
    std::ofstream(outDir / (nodeId + ".json"), std::ios::binary | std::ios::trunc) << result.dump(2);

    sandbox::AgentTelemetryEmitter::emitEvent(nodeId, "interaction_answer", Json{
        {"nodeId", nodeId},
        {"answeredBy", answeredBy},
        {"fields", normalized.size()},
        {"sensitive", sensitive}
    });
    std::cout << "[Interaction " << label << "] respondido (" << normalized.size()
              << " campo(s), canal: " << answeredBy << ")\n";
    return result.dump();
}

} // namespace rt
} // namespace taskforge
