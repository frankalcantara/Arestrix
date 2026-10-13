#include "search/SynonymAgent.hpp"

#include <cstdlib>
#include <mutex>
#include <set>

#include "rt/Runtime.hpp"
#include "search/Synonyms.hpp"

namespace taskforge {
namespace search {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

// Modelo usado quando so o provedor foi informado.
std::string defaultModelFor(const std::string& provider) {
    if (provider == "deepseek") return "deepseek-flash";
    if (provider == "openai") return "gpt-4o-mini";
    if (provider == "anthropic") return "claude-haiku-5-5";
    if (provider == "google") return "gemini-2.5-flash";
    if (provider == "groq") return "llama-3.3-70b-versatile";
    return "";
}

std::mutex g_busyMutex;
std::set<std::string> g_busy;

} // namespace

std::optional<StudioModel> studioDefaultModel() {
    std::string provider = env("ARESTRIX_DEFAULT_PROVIDER");
    std::string model = env("ARESTRIX_DEFAULT_MODEL");

    if (provider.empty()) {
        // primeiro provedor com chave configurada nesta maquina
        static const std::pair<const char*, const char*> order[] = {
            {"deepseek", "DEEPSEEK_API_KEY"}, {"openai", "OPENAI_API_KEY"}, {"anthropic", "ANTHROPIC_API_KEY"},
            {"google", "GEMINI_API_KEY"},     {"groq", "GROQ_API_KEY"}};
        for (const auto& [p, keyVar] : order) {
            if (!env(keyVar).empty()) { provider = p; break; }
        }
        if (provider.empty() && !model.empty() && !env("LOCAL_LLM_BASE_URL").empty()) provider = "local";
    }
    if (provider.empty()) return std::nullopt;
    if (model.empty()) model = defaultModelFor(provider);
    if (model.empty()) return std::nullopt; // jev/local exigem ARESTRIX_DEFAULT_MODEL
    return StudioModel{provider, model};
}

SynonymGenResult ensureSynonymsFor(const std::string& locale, bool force) {
    SynonymGenResult r;
    r.locale = locale;

    if (!force && hasSynonymFile(locale)) { r.status = "exists"; return r; }

    auto model = studioDefaultModel();
    if (!model) {
        r.status = "no_model";
        r.error = "Defina ARESTRIX_DEFAULT_PROVIDER/ARESTRIX_DEFAULT_MODEL ou uma chave de API (ex.: DEEPSEEK_API_KEY).";
        return r;
    }
    r.model = model->describe();

    {
        std::lock_guard<std::mutex> lock(g_busyMutex);
        if (!g_busy.insert(locale).second) { r.status = "busy"; return r; }
    }
    struct Release {
        const std::string& l;
        ~Release() { std::lock_guard<std::mutex> lock(g_busyMutex); g_busy.erase(l); }
    } release{locale};

    rt::CompletionRequest req;
    req.provider = model->provider;
    req.model = model->model;
    req.system = synonymSystemPrompt();
    req.user = synonymUserPrompt(locale);
    req.maxTokens = 8000;
    req.maxAttempts = 2;
    req.hasTemperature = true;
    req.temperature = 0.2;

    rt::CompletionResult res = rt::completeText(req);
    if (!res.ok) {
        r.status = "failed";
        r.error = res.error;
        return r;
    }
    SynonymGroups groups = parseSynonymResponse(res.text);
    if (groups.size() < 10) {
        r.status = "failed";
        r.error = "o modelo devolveu poucos grupos validos (" + std::to_string(groups.size()) + ")";
        return r;
    }
    std::string err;
    if (!writeSynonymFile(locale, r.model, groups, &err)) {
        r.status = "failed";
        r.error = err;
        return r;
    }
    r.status = "generated";
    r.groups = groups.size();
    return r;
}

} // namespace search
} // namespace taskforge
