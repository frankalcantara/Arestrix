// arestrix.memoria, arestrix.tempo e arestrix.git (somente leitura).
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

#include "native/NativeTools.hpp"
#include "platform/Process.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace native {

namespace {

using rt::ToolResult;

ToolResult fail(const std::string& m) { return {m, true}; }
ToolResult ok(const std::string& m) { return {m, false}; }
std::string str(const Json& a, const char* key, const std::string& def = "") {
    return a.contains(key) && a[key].is_string() ? a[key].str() : def;
}
double num(const Json& a, const char* key, double def = 0) {
    return a.contains(key) && a[key].is_number() ? a[key].get<double>() : def;
}

// ============================================================================================================
// Memoria: chave-valor do projeto, em .arestrix/memory.json
// ============================================================================================================
constexpr size_t kMaxKeys = 1000;
constexpr size_t kMaxValueBytes = 64 * 1024;
std::mutex g_memoryMutex;

bool validKey(const std::string& k) {
    if (k.empty() || k.size() > 128) return false;
    return std::all_of(k.begin(), k.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '.' || c == ':' || c == '-'; });
}

fs::path memoryFile(const Context& ctx) { return ctx.workspaceRoot / ".arestrix" / "memory.json"; }

Json loadMemory(const Context& ctx) {
    std::error_code ec;
    if (!fs::is_regular_file(memoryFile(ctx), ec)) return Json::object();
    auto parsed = jsonutil::tryParse(jsonutil::readFile(memoryFile(ctx)));
    return parsed && parsed->is_object() ? *parsed : Json::object();
}

bool saveMemory(const Context& ctx, const Json& data) {
    std::error_code ec;
    fs::create_directories(memoryFile(ctx).parent_path(), ec);
    fs::path tmp = memoryFile(ctx);
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data.dump(2);
    }
    fs::remove(memoryFile(ctx), ec);
    fs::rename(tmp, memoryFile(ctx), ec);
    return !ec;
}

// ============================================================================================================
// Tempo
// ============================================================================================================
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {   // Howard Hinnant
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, unsigned& m, unsigned& d) {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t yy = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp + (mp < 10 ? 3 : -9);
    y = static_cast<int>(yy + (m <= 2));
}

struct Instant {
    int64_t epoch = 0;          // segundos desde 1970-01-01T00:00:00Z
    int offsetMinutes = 0;      // deslocamento do fuso do texto original
};

bool parseIso(const std::string& s, Instant& out) {
    int Y, M, D, h = 0, mi = 0, sec = 0;
    int n = 0;
    if (std::sscanf(s.c_str(), "%4d-%2d-%2d%n", &Y, &M, &D, &n) < 3) return false;
    size_t pos = static_cast<size_t>(n);
    if (pos < s.size() && (s[pos] == 'T' || s[pos] == ' ')) {
        int n2 = 0;
        int got = std::sscanf(s.c_str() + pos + 1, "%2d:%2d:%2d%n", &h, &mi, &sec, &n2);
        if (got < 2) return false;
        if (got == 2) { std::sscanf(s.c_str() + pos + 1, "%2d:%2d%n", &h, &mi, &n2); sec = 0; }
        pos += 1 + static_cast<size_t>(n2);
    }
    int offset = 0;
    if (pos < s.size()) {
        if (s[pos] == 'Z' && pos + 1 == s.size()) offset = 0;
        else if (s[pos] == '+' || s[pos] == '-') {
            int oh = 0, om = 0;
            if (std::sscanf(s.c_str() + pos + 1, "%2d:%2d", &oh, &om) < 2) return false;
            offset = (oh * 60 + om) * (s[pos] == '-' ? -1 : 1);
        } else return false;
    }
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || mi > 59 || sec > 60) return false;
    out.epoch = daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D)) * 86400 + h * 3600 + mi * 60 + sec - offset * 60;
    out.offsetMinutes = offset;
    return true;
}

std::string formatIso(int64_t epoch, int offsetMinutes) {
    int64_t local = epoch + offsetMinutes * 60;
    int64_t days = local >= 0 ? local / 86400 : -((-local + 86399) / 86400);
    int64_t rem = local - days * 86400;
    int y; unsigned m, d;
    civilFromDays(days, y, m, d);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02d", y, m, d, static_cast<int>(rem / 3600), static_cast<int>((rem % 3600) / 60),
                  static_cast<int>(rem % 60));
    std::string out = buf;
    if (offsetMinutes == 0) return out + "Z";
    int a = std::abs(offsetMinutes);
    std::snprintf(buf, sizeof buf, "%c%02d:%02d", offsetMinutes < 0 ? '-' : '+', a / 60, a % 60);
    return out + buf;
}

std::string weekday(int64_t epoch, int offsetMinutes, rt::RuntimeLang lang) {
    static const char* pt[] = {"quinta", "sexta", "sabado", "domingo", "segunda", "terca", "quarta"};   // 1970-01-01 foi quinta
    static const char* en[] = {"Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday"};
    int64_t local = epoch + offsetMinutes * 60;
    int64_t days = local >= 0 ? local / 86400 : -((-local + 86399) / 86400);
    const size_t idx = ((days % 7) + 7) % 7;
    return rt::langPick(lang, pt[idx], en[idx]);
}

int localOffsetMinutes(std::time_t t) {
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    // os campos locais lidos como se fossem UTC, menos o instante real, dao o deslocamento (inclui horario de verao)
    int64_t asUtc = daysFromCivil(lt.tm_year + 1900, static_cast<unsigned>(lt.tm_mon + 1), static_cast<unsigned>(lt.tm_mday)) * 86400 +
                    lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return static_cast<int>((asUtc - static_cast<int64_t>(t)) / 60);
}

// ============================================================================================================
// Git (somente leitura)
// ============================================================================================================
bool safeRev(const std::string& r) {
    if (r.empty() || r.size() > 100 || !std::isalnum(static_cast<unsigned char>(r[0]))) return false;
    return std::all_of(r.begin(), r.end(), [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '/' || c == '~' || c == '^' || c == '@' || c == '-'; });
}

ToolResult runGit(const Context& ctx, std::vector<std::string> args) {
    if (!platform::commandExists("git"))
        return fail(rt::langPick(ctx.lang, "O Git nao foi encontrado no PATH desta maquina.", "Git was not found in the PATH of this machine."));
    platform::ProcessSpec spec;
    spec.argv = {"git", "-C", ctx.workspaceRoot.string(), "-c", "core.fsmonitor=false", "-c", "core.pager=cat",
                 "-c", "protocol.ext.allow=never", "--no-pager"};
    for (auto& a : args) spec.argv.push_back(std::move(a));
    spec.workingDir = ctx.workspaceRoot;
    spec.env["GIT_TERMINAL_PROMPT"] = "0";
    spec.env["GIT_OPTIONAL_LOCKS"] = "0";
    spec.scrubSecrets = true;
    platform::RunResult r = platform::run(spec, 20000, {}, nullptr, 256 * 1024);
    if (!r.error.empty()) return fail(rt::langPick(ctx.lang, "Falha ao executar o Git: ", "Failed to run Git: ") + r.error);
    if (r.timedOut) return fail(rt::langPick(ctx.lang, "O Git demorou demais.", "Git took too long."));
    if (r.exitCode != 0) return fail(rt::langPick(ctx.lang, "O Git recusou o pedido: ", "Git refused the request: ") + r.output.substr(0, 400));
    return ok(r.output.empty() ? std::string("(sem saida)") : r.output);
}

} // namespace

Server makeMemoryServer(rt::RuntimeLang lang) {
    Server s;
    s.id = std::string(kIdPrefix) + "memoria";
    s.name = "Memoria do projeto";
    s.description = "Guarda e consulta pequenos valores de texto entre vertices e entre execucoes (.arestrix/memory.json).";
    s.descriptionEn = "Stores and reads small text values across vertices and across runs (.arestrix/memory.json).";
    s.category = "Memoria";
    s.tools.push_back({"memory_set",
                       "Guarda um valor de texto (ate 64 KB) sob uma chave (letras, numeros, _ . : -).",
                       "Stores a text value (up to 64 KB) under a key (letters, digits, _ . : -).",
                       schema({{"key", "string", "Chave", "Key"}, {"value", "string", "Valor", "Value"}}, {"key", "value"}, lang), "moderate",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string key = str(a, "key");
                           if (!validKey(key))
                               return fail(rt::langPick(ctx.lang,
                                                        "Chave invalida: use ate 128 caracteres entre letras, numeros e _ . : -",
                                                        "Invalid key: use up to 128 characters among letters, digits and _ . : -"));
                           if (!a["value"].is_string())
                               return fail(rt::langPick(ctx.lang, "'value' deve ser texto.", "'value' must be text."));
                           if (a["value"].str().size() > kMaxValueBytes)
                               return fail(rt::langPick(ctx.lang, "Valor grande demais (limite de 64 KB).", "Value too large (64 KB limit)."));
                           std::lock_guard<std::mutex> lock(g_memoryMutex);
                           Json data = loadMemory(ctx);
                           if (!data.contains(key) && data.size() >= kMaxKeys)
                               return fail(rt::langPick(ctx.lang, "Memoria cheia (limite de 1000 chaves).", "Memory full (1000 keys limit)."));
                           data[key] = a["value"].str();
                           return saveMemory(ctx, data) ? ok("OK: " + key)
                                                        : fail(rt::langPick(ctx.lang, "Nao foi possivel gravar a memoria.", "Could not write the memory."));
                       }});
    s.tools.push_back({"memory_get",
                       "Le o valor guardado sob uma chave.",
                       "Reads the value stored under a key.",
                       schema({{"key", "string", "Chave", "Key"}}, {"key"}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string key = str(a, "key");
                           std::lock_guard<std::mutex> lock(g_memoryMutex);
                           Json data = loadMemory(ctx);
                           if (!data.contains(key))
                               return fail(rt::langPick(ctx.lang, "Chave nao encontrada: ", "Key not found: ") + key);
                           return ok(data[key].str());
                       }});
    s.tools.push_back({"memory_list",
                       "Lista as chaves guardadas (opcionalmente com um prefixo).",
                       "Lists the stored keys (optionally with a prefix).",
                       schema({{"prefix", "string", "So as chaves que comecam assim", "Only keys starting with this"}}, {}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string prefix = str(a, "prefix");
                           std::lock_guard<std::mutex> lock(g_memoryMutex);
                           Json data = loadMemory(ctx);
                           Json keys = Json::array();
                           for (auto it = data.begin(); it != data.end(); ++it)
                               if (it.key().rfind(prefix, 0) == 0) keys.push_back(it.key());
                           return ok(keys.dump());
                       }});
    s.tools.push_back({"memory_delete",
                       "Apaga uma chave.",
                       "Deletes a key.",
                       schema({{"key", "string", "Chave", "Key"}}, {"key"}, lang), "moderate",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string key = str(a, "key");
                           std::lock_guard<std::mutex> lock(g_memoryMutex);
                           Json data = loadMemory(ctx);
                           if (!data.erase(key))
                               return fail(rt::langPick(ctx.lang, "Chave nao encontrada: ", "Key not found: ") + key);
                           return saveMemory(ctx, data)
                                      ? ok("OK: " + key + rt::langPick(ctx.lang, " apagada", " deleted"))
                                      : fail(rt::langPick(ctx.lang, "Nao foi possivel gravar a memoria.", "Could not write the memory."));
                       }});
    return s;
}

Server makeTimeServer(rt::RuntimeLang lang) {
    Server s;
    s.id = std::string(kIdPrefix) + "tempo";
    s.name = "Data e hora";
    s.description = "Data e hora atuais, somar duracoes e calcular diferencas (ISO 8601).";
    s.descriptionEn = "Current date and time, add durations and compute differences (ISO 8601).";
    s.category = "Utilidades";
    s.tools.push_back({"now",
                       "Devolve a data e hora atuais (ISO 8601), o dia da semana e o fuso.",
                       "Returns the current date and time (ISO 8601), the weekday and the timezone.",
                       schema({{"timezone", "string", "\"utc\" (padrao) ou \"local\"", "\"utc\" (default) or \"local\""}}, {}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::time_t t = std::time(nullptr);
                           int off = str(a, "timezone", "utc") == "local" ? localOffsetMinutes(t) : 0;
                           Json out;
                           out["iso"] = formatIso(t, off);
                           out["epoch"] = static_cast<int64_t>(t);
                           out["weekday"] = weekday(t, off, ctx.lang);
                           out["utcOffsetMinutes"] = off;
                           return ok(out.dump());
                       }});
    s.tools.push_back({"add_duration",
                       "Soma (ou subtrai, com valores negativos) dias, horas, minutos e segundos a uma data ISO 8601.",
                       "Adds (or subtracts, with negative values) days, hours, minutes and seconds to an ISO 8601 date.",
                       schema({{"iso", "string", "Data/hora, ex.: 2026-10-13T09:30:00-03:00", "Date/time, e.g.: 2026-10-13T09:30:00-03:00"},
                               {"days", "number", "Dias", "Days"}, {"hours", "number", "Horas", "Hours"},
                               {"minutes", "number", "Minutos", "Minutes"}, {"seconds", "number", "Segundos", "Seconds"}}, {"iso"}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           Instant in;
                           if (!parseIso(str(a, "iso"), in))
                               return fail(rt::langPick(ctx.lang,
                                                        "Data invalida; use o formato 2026-10-13T09:30:00Z ou 2026-10-13.",
                                                        "Invalid date; use the format 2026-10-13T09:30:00Z or 2026-10-13."));
                           int64_t delta = static_cast<int64_t>(num(a, "days") * 86400 + num(a, "hours") * 3600 + num(a, "minutes") * 60 + num(a, "seconds"));
                           int64_t result = in.epoch + delta;
                           Json out;
                           out["iso"] = formatIso(result, in.offsetMinutes);
                           out["weekday"] = weekday(result, in.offsetMinutes, ctx.lang);
                           return ok(out.dump());
                       }});
    s.tools.push_back({"diff",
                       "Calcula a diferenca entre duas datas ISO 8601 (to - from).",
                       "Computes the difference between two ISO 8601 dates (to - from).",
                       schema({{"from", "string", "Data inicial", "Start date"}, {"to", "string", "Data final", "End date"}}, {"from", "to"}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           Instant x, y;
                           if (!parseIso(str(a, "from"), x) || !parseIso(str(a, "to"), y))
                               return fail(rt::langPick(ctx.lang, "Data invalida; use o formato 2026-10-13T09:30:00Z.",
                                                        "Invalid date; use the format 2026-10-13T09:30:00Z."));
                           int64_t secs = y.epoch - x.epoch;
                           Json out;
                           out["seconds"] = secs;
                           out["minutes"] = static_cast<double>(secs) / 60.0;
                           out["hours"] = static_cast<double>(secs) / 3600.0;
                           out["days"] = static_cast<double>(secs) / 86400.0;
                           return ok(out.dump());
                       }});
    return s;
}

Server makeGitServer(rt::RuntimeLang lang) {
    Server s;
    s.id = std::string(kIdPrefix) + "git";
    s.name = "Git (somente leitura)";
    s.description = "Consulta o repositorio Git do projeto: estado, historico, diferencas e arquivos de uma revisao. Nunca altera nada.";
    s.descriptionEn = "Queries the project Git repository: status, history, diffs and files of a revision. Never changes anything.";
    s.category = "Desenvolvimento";
    s.tools.push_back({"git_status",
                       "Mostra o estado do repositorio (branch e arquivos alterados).",
                       "Shows the repository status (branch and changed files).",
                       schema({}, {}, lang), "safe",
                       [](const Json&, const Context& ctx) { return runGit(ctx, {"status", "--porcelain=v1", "-b"}); }});
    s.tools.push_back({"git_log",
                       "Lista os ultimos commits (hash curto, data, autor e titulo).",
                       "Lists the latest commits (short hash, date, author and title).",
                       schema({{"max_count", "number", "Quantidade (padrao 20, limite 100)", "Count (default 20, limit 100)"},
                               {"path", "string", "So commits que tocaram este caminho", "Only commits that touched this path"}}, {}, lang),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           int n = static_cast<int>(std::clamp(num(a, "max_count", 20), 1.0, 100.0));
                           std::vector<std::string> args = {"log", "-n", std::to_string(n), "--no-color", "--date=short", "--format=%h %ad %an: %s"};
                           std::string path = str(a, "path");
                           if (!path.empty()) {
                               auto p = rt::resolveInWorkspace(ctx.workspaceRoot, path);
                               if (!p) return fail(rt::langPick(ctx.lang, "Caminho fora do projeto ou invalido.", "Path outside the project or invalid."));
                               args.push_back("--");
                               args.push_back(p->lexically_relative(fs::weakly_canonical(ctx.workspaceRoot)).generic_string());
                           }
                           return runGit(ctx, args);
                       }});
    s.tools.push_back({"git_diff",
                       "Mostra as diferencas ainda nao commitadas (ou as preparadas, com staged=true).",
                       "Shows the not-yet-committed diffs (or the staged ones, with staged=true).",
                       schema({{"path", "string", "So este caminho", "Only this path"},
                               {"staged", "boolean", "true para as alteracoes preparadas (staged)", "true for the staged changes"}}, {}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::vector<std::string> args = {"diff", "--no-color", "--no-ext-diff", "--no-textconv"};
                           if (a.contains("staged") && a["staged"].is_boolean() && a["staged"].get<bool>()) args.push_back("--cached");
                           std::string path = str(a, "path");
                           if (!path.empty()) {
                               auto p = rt::resolveInWorkspace(ctx.workspaceRoot, path);
                               if (!p) return fail(rt::langPick(ctx.lang, "Caminho fora do projeto ou invalido.", "Path outside the project or invalid."));
                               args.push_back("--");
                               args.push_back(p->lexically_relative(fs::weakly_canonical(ctx.workspaceRoot)).generic_string());
                           }
                           return runGit(ctx, args);
                       }});
    s.tools.push_back({"git_show_file",
                       "Mostra o conteudo de um arquivo em uma revisao (commit, branch ou tag).",
                       "Shows the content of a file at a revision (commit, branch or tag).",
                       schema({{"rev", "string", "Revisao, ex.: HEAD, main, abc1234", "Revision, e.g.: HEAD, main, abc1234"},
                               {"path", "string", "Caminho do arquivo no repositorio", "Path of the file in the repository"}}, {"rev", "path"}, lang),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string rev = str(a, "rev");
                           if (!safeRev(rev)) return fail(rt::langPick(ctx.lang, "Revisao invalida.", "Invalid revision."));
                           auto p = rt::resolveInWorkspace(ctx.workspaceRoot, str(a, "path"));
                           if (!p) return fail(rt::langPick(ctx.lang, "Caminho fora do projeto ou invalido.", "Path outside the project or invalid."));
                           std::string rel = p->lexically_relative(fs::weakly_canonical(ctx.workspaceRoot)).generic_string();
                           if (rel.rfind(".git/", 0) == 0 || rel == ".git")
                               return fail(rt::langPick(ctx.lang, "A pasta .git nao e acessivel.", "The .git folder is not accessible."));
                           return runGit(ctx, {"show", "--no-textconv", rev + ":" + rel});
                       }});
    return s;
}

} // namespace native
} // namespace taskforge
