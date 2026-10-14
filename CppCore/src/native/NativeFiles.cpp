// arestrix.arquivos - arquivos do projeto: ler, gravar, listar, informar e buscar texto.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "native/NativeTools.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace native {

namespace {

using rt::ToolResult;

constexpr size_t kMaxReadBytes = 1024 * 1024;
constexpr size_t kMaxWriteBytes = 2 * 1024 * 1024;
constexpr size_t kMaxListEntries = 500;
constexpr size_t kMaxSearchMatches = 100;

ToolResult fail(const std::string& m) { return {m, true}; }
ToolResult ok(const std::string& m) { return {m, false}; }

std::string str(const Json& a, const char* key, const std::string& def = "") {
    return a.contains(key) && a[key].is_string() ? a[key].str() : def;
}
bool flag(const Json& a, const char* key) { return a.contains(key) && a[key].is_boolean() && a[key].get<bool>(); }

std::string firstComponent(const fs::path& root, const fs::path& p) {
    std::error_code ec;
    fs::path rel = p.lexically_relative(fs::weakly_canonical(root, ec));
    return rel.empty() ? std::string() : rel.begin()->generic_string();
}

std::string relPath(const fs::path& root, const fs::path& p) {
    std::error_code ec;
    return p.lexically_relative(fs::weakly_canonical(root, ec)).generic_string();
}

bool looksBinary(const std::string& s) {
    size_t n = std::min<size_t>(s.size(), 8192);
    return s.find('\0') < n;
}

std::string isoUtc(fs::file_time_type t) {
    auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool skipDir(const std::string& name) {
    return name == ".git" || name == ".arestrix" || name == "node_modules" || name == "build" || name == "dist";
}

// Resolve um caminho do projeto; devolve "" e preenche `error` se for invalido ou apontar para .git.
fs::path resolveOrError(const Context& ctx, const std::string& rel, std::string& error, bool allowGit = false) {
    auto p = rt::resolveInWorkspace(ctx.workspaceRoot, rel.empty() ? "." : rel);
    if (!p) { error = "Caminho fora do projeto ou invalido: " + rel; return {}; }
    if (!allowGit && firstComponent(ctx.workspaceRoot, *p) == ".git") { error = "A pasta .git nao e acessivel."; return {}; }
    return *p;
}

} // namespace

Server makeFilesServer() {
    Server s;
    s.id = std::string(kIdPrefix) + "arquivos";
    s.name = "Arquivos do projeto";
    s.description = "Ler, gravar, listar e buscar texto nos arquivos do projeto. Tudo confinado a pasta do projeto.";
    s.category = "Arquivos";

    s.tools.push_back({"read_file", "Le um arquivo de texto do projeto (ate 1 MB).",
                       schema({{"path", "string", "Caminho relativo ao projeto"}}, {"path"}), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           std::error_code ec;
                           if (!fs::is_regular_file(p, ec)) return fail("Arquivo nao encontrado: " + str(a, "path"));
                           if (fs::file_size(p, ec) > kMaxReadBytes) return fail("Arquivo grande demais (limite de 1 MB).");
                           std::string text = jsonutil::readFile(p);
                           if (looksBinary(text)) return fail("O arquivo parece binario; so texto pode ser lido.");
                           return ok(text);
                       }});

    s.tools.push_back({"write_file",
                       "Cria ou sobrescreve um arquivo de texto do projeto (ou acrescenta ao final). Pipelines, configuracao e .git sao protegidos.",
                       schema({{"path", "string", "Caminho relativo ao projeto"}, {"content", "string", "Conteudo"},
                               {"append", "boolean", "true para acrescentar ao final em vez de sobrescrever"}}, {"path", "content"}),
                       "moderate", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           if (rt::isProtectedPath(ctx.workspaceRoot, p))
                               return fail("Caminho protegido (pipelines, configuracao e .git nao podem ser gravados): " + str(a, "path"));
                           if (!a["content"].is_string()) return fail("'content' deve ser texto.");
                           const std::string& content = a["content"].str();
                           if (content.size() > kMaxWriteBytes) return fail("Conteudo grande demais (limite de 2 MB).");
                           std::error_code ec;
                           if (fs::is_directory(p, ec)) return fail("O caminho e uma pasta.");
                           fs::create_directories(p.parent_path(), ec);
                           std::ofstream out(p, std::ios::binary | (flag(a, "append") ? std::ios::app : std::ios::trunc));
                           if (!out) return fail("Nao foi possivel gravar o arquivo.");
                           out << content;
                           return ok("OK: " + std::to_string(content.size()) + " bytes " + (flag(a, "append") ? "acrescentados a " : "gravados em ") +
                                     relPath(ctx.workspaceRoot, p));
                       }});

    s.tools.push_back({"list_directory", "Lista os arquivos e pastas de um diretorio do projeto (ate 500 itens).",
                       schema({{"path", "string", "Pasta relativa ao projeto (padrao: a raiz)"},
                               {"recursive", "boolean", "true para incluir subpastas"}}, {}),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path dir = resolveOrError(ctx, str(a, "path", "."), e);
                           if (dir.empty()) return fail(e);
                           std::error_code ec;
                           if (!fs::is_directory(dir, ec)) return fail("Nao e uma pasta: " + str(a, "path", "."));
                           std::ostringstream out;
                           size_t n = 0;
                           bool truncated = false;
                           auto emit = [&](const fs::directory_entry& entry) {
                               if (n >= kMaxListEntries) { truncated = true; return; }
                               std::error_code ec2;
                               std::string rel = relPath(ctx.workspaceRoot, entry.path());
                               if (entry.is_directory(ec2)) out << "d " << rel << "/\n";
                               else out << "f " << rel << " (" << entry.file_size(ec2) << " bytes)\n";
                               ++n;
                           };
                           if (flag(a, "recursive")) {
                               fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                               for (; !ec && it != end; it.increment(ec)) {
                                   if (it->is_directory(ec) && skipDir(it->path().filename().string())) { it.disable_recursion_pending(); continue; }
                                   emit(*it);
                               }
                           } else {
                               for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                                   if (skipDir(it->path().filename().string())) continue;
                                   emit(*it);
                               }
                           }
                           if (truncated) out << "[... lista limitada a 500 itens ...]\n";
                           return ok(n == 0 ? std::string("(vazio)") : out.str());
                       }});

    s.tools.push_back({"file_info", "Informa tipo, tamanho e data de modificacao de um arquivo ou pasta do projeto.",
                       schema({{"path", "string", "Caminho relativo ao projeto"}}, {"path"}), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           std::error_code ec;
                           if (!fs::exists(p, ec)) return ok(Json{{"exists", false}}.dump());
                           Json info;
                           info["exists"] = true;
                           info["type"] = fs::is_directory(p, ec) ? "directory" : "file";
                           if (fs::is_regular_file(p, ec)) info["size"] = static_cast<int64_t>(fs::file_size(p, ec));
                           info["modified"] = isoUtc(fs::last_write_time(p, ec));
                           return ok(info.dump());
                       }});

    s.tools.push_back({"make_directory", "Cria uma pasta (e as pastas intermediarias) no projeto.",
                       schema({{"path", "string", "Pasta relativa ao projeto"}}, {"path"}), "moderate",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           if (rt::isProtectedPath(ctx.workspaceRoot, p)) return fail("Caminho protegido: " + str(a, "path"));
                           std::error_code ec;
                           fs::create_directories(p, ec);
                           if (ec) return fail("Nao foi possivel criar a pasta: " + ec.message());
                           return ok("OK: " + relPath(ctx.workspaceRoot, p) + "/");
                       }});

    s.tools.push_back({"search_text",
                       "Procura um trecho de texto (sem diferenciar maiusculas) nos arquivos de texto do projeto e devolve arquivo:linha:texto (ate 100 resultados).",
                       schema({{"query", "string", "Texto a procurar (minimo 2 caracteres)"},
                               {"path", "string", "Pasta onde procurar (padrao: a raiz do projeto)"},
                               {"max_results", "number", "Maximo de resultados (padrao 50, limite 100)"}}, {"query"}),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string query = str(a, "query");
                           if (query.size() < 2) return fail("'query' precisa de pelo menos 2 caracteres.");
                           std::string e;
                           fs::path dir = resolveOrError(ctx, str(a, "path", "."), e);
                           if (dir.empty()) return fail(e);
                           size_t maxResults = 50;
                           if (a.contains("max_results") && a["max_results"].is_number())
                               maxResults = static_cast<size_t>(std::clamp(a["max_results"].get<int>(), 1, static_cast<int>(kMaxSearchMatches)));
                           auto lower = [](std::string s2) {
                               for (char& c : s2) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                               return s2;
                           };
                           const std::string needle = lower(query);
                           std::ostringstream out;
                           size_t found = 0;
                           std::error_code ec;
                           fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                           for (; !ec && it != end && found < maxResults; it.increment(ec)) {
                               if (it->is_directory(ec)) {
                                   if (skipDir(it->path().filename().string())) it.disable_recursion_pending();
                                   continue;
                               }
                               if (!it->is_regular_file(ec) || it->file_size(ec) > kMaxReadBytes) continue;
                               std::string text = jsonutil::readFile(it->path());
                               if (looksBinary(text)) continue;
                               std::istringstream in(text);
                               std::string line;
                               size_t lineNo = 0;
                               while (std::getline(in, line) && found < maxResults) {
                                   ++lineNo;
                                   if (lower(line).find(needle) == std::string::npos) continue;
                                   if (line.size() > 200) line = line.substr(0, 200) + "...";
                                   out << relPath(ctx.workspaceRoot, it->path()) << ":" << lineNo << ": " << line << "\n";
                                   ++found;
                               }
                           }
                           return ok(found == 0 ? std::string("(nenhum resultado)") : out.str());
                       }});
    return s;
}

} // namespace native
} // namespace taskforge
