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
    if (!p) {
        error = rt::langPick(ctx.lang, "Caminho fora do projeto ou invalido: ", "Path outside the project or invalid: ") + rel;
        return {};
    }
    if (!allowGit && firstComponent(ctx.workspaceRoot, *p) == ".git") {
        error = rt::langPick(ctx.lang, "A pasta .git nao e acessivel.", "The .git folder is not accessible.");
        return {};
    }
    return *p;
}

} // namespace

Server makeFilesServer(rt::RuntimeLang lang) {
    Server s;
    s.id = std::string(kIdPrefix) + "arquivos";
    s.name = "Arquivos do projeto";
    s.description = "Ler, gravar, listar e buscar texto nos arquivos do projeto. Tudo confinado a pasta do projeto.";
    s.descriptionEn = "Read, write, list and search text in the project files. Everything is confined to the project folder.";
    s.category = "Arquivos";

    s.tools.push_back({"read_file",
                       "Le um arquivo de texto do projeto (ate 1 MB).",
                       "Reads a text file from the project (up to 1 MB).",
                       schema({{"path", "string", "Caminho relativo ao projeto", "Path relative to the project"}}, {"path"}, lang), "safe",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           std::error_code ec;
                           if (!fs::is_regular_file(p, ec))
                               return fail(rt::langPick(ctx.lang, "Arquivo nao encontrado: ", "File not found: ") + str(a, "path"));
                           if (fs::file_size(p, ec) > kMaxReadBytes)
                               return fail(rt::langPick(ctx.lang, "Arquivo grande demais (limite de 1 MB).", "File too large (1 MB limit)."));
                           std::string text = jsonutil::readFile(p);
                           if (looksBinary(text))
                               return fail(rt::langPick(ctx.lang, "O arquivo parece binario; so texto pode ser lido.",
                                                        "The file looks binary; only text can be read."));
                           return ok(text);
                       }});

    s.tools.push_back({"write_file",
                       "Cria ou sobrescreve um arquivo de texto do projeto (ou acrescenta ao final). Pipelines, configuracao e .git sao protegidos.",
                       "Creates or overwrites a text file in the project (or appends at the end). Pipelines, configuration and .git are protected.",
                       schema({{"path", "string", "Caminho relativo ao projeto", "Path relative to the project"},
                               {"content", "string", "Conteudo", "Content"},
                               {"append", "boolean", "true para acrescentar ao final em vez de sobrescrever", "true to append at the end instead of overwriting"}},
                              {"path", "content"}, lang),
                       "moderate", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           if (rt::isProtectedPath(ctx.workspaceRoot, p))
                               return fail(rt::langPick(ctx.lang,
                                                        "Caminho protegido (pipelines, configuracao e .git nao podem ser gravados): ",
                                                        "Protected path (pipelines, configuration and .git cannot be written): ") + str(a, "path"));
                           if (!a["content"].is_string())
                               return fail(rt::langPick(ctx.lang, "'content' deve ser texto.", "'content' must be text."));
                           const std::string& content = a["content"].str();
                           if (content.size() > kMaxWriteBytes)
                               return fail(rt::langPick(ctx.lang, "Conteudo grande demais (limite de 2 MB).", "Content too large (2 MB limit)."));
                           std::error_code ec;
                           if (fs::is_directory(p, ec))
                               return fail(rt::langPick(ctx.lang, "O caminho e uma pasta.", "The path is a folder."));
                           fs::create_directories(p.parent_path(), ec);
                           std::ofstream out(p, std::ios::binary | (flag(a, "append") ? std::ios::app : std::ios::trunc));
                           if (!out) return fail(rt::langPick(ctx.lang, "Nao foi possivel gravar o arquivo.", "Could not write the file."));
                           out << content;
                           return ok("OK: " + std::to_string(content.size()) + " bytes " +
                                     rt::langPick(ctx.lang,
                                                  (flag(a, "append") ? "acrescentados a " : "gravados em "),
                                                  (flag(a, "append") ? "appended to " : "written to ")) +
                                     relPath(ctx.workspaceRoot, p));
                       }});

    s.tools.push_back({"list_directory",
                       "Lista os arquivos e pastas de um diretorio do projeto (ate 500 itens).",
                       "Lists the files and folders of a project directory (up to 500 items).",
                       schema({{"path", "string", "Pasta relativa ao projeto (padrao: a raiz)", "Folder relative to the project (default: the root)"},
                               {"recursive", "boolean", "true para incluir subpastas", "true to include subfolders"}}, {}, lang),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path dir = resolveOrError(ctx, str(a, "path", "."), e);
                           if (dir.empty()) return fail(e);
                           std::error_code ec;
                           if (!fs::is_directory(dir, ec))
                               return fail(rt::langPick(ctx.lang, "Nao e uma pasta: ", "Not a folder: ") + str(a, "path", "."));
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
                           if (truncated)
                               out << rt::langPick(ctx.lang, "[... lista limitada a 500 itens ...]\n", "[... list limited to 500 items ...]\n");
                           return ok(n == 0 ? std::string("(vazio)") : out.str());
                       }});

    s.tools.push_back({"file_info",
                       "Informa tipo, tamanho e data de modificacao de um arquivo ou pasta do projeto.",
                       "Reports the type, size and modification date of a project file or folder.",
                       schema({{"path", "string", "Caminho relativo ao projeto", "Path relative to the project"}}, {"path"}, lang), "safe",
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

    s.tools.push_back({"make_directory",
                       "Cria uma pasta (e as pastas intermediarias) no projeto.",
                       "Creates a folder (and its parents) in the project.",
                       schema({{"path", "string", "Pasta relativa ao projeto", "Folder relative to the project"}}, {"path"}, lang), "moderate",
                       [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string e;
                           fs::path p = resolveOrError(ctx, str(a, "path"), e);
                           if (p.empty()) return fail(e);
                           if (rt::isProtectedPath(ctx.workspaceRoot, p))
                               return fail(rt::langPick(ctx.lang, "Caminho protegido: ", "Protected path: ") + str(a, "path"));
                           std::error_code ec;
                           fs::create_directories(p, ec);
                           if (ec) return fail(rt::langPick(ctx.lang, "Nao foi possivel criar a pasta: ", "Could not create the folder: ") + ec.message());
                           return ok("OK: " + relPath(ctx.workspaceRoot, p) + "/");
                       }});

    s.tools.push_back({"search_text",
                       "Procura um trecho de texto (sem diferenciar maiusculas) nos arquivos de texto do projeto e devolve arquivo:linha:texto (ate 100 resultados).",
                       "Searches for a text snippet (case-insensitive) in the project text files and returns file:line:text (up to 100 results).",
                       schema({{"query", "string", "Texto a procurar (minimo 2 caracteres)", "Text to search for (at least 2 characters)"},
                               {"path", "string", "Pasta onde procurar (padrao: a raiz do projeto)", "Folder to search in (default: the project root)"},
                               {"max_results", "number", "Maximo de resultados (padrao 50, limite 100)", "Maximum results (default 50, limit 100)"}}, {"query"}, lang),
                       "safe", [](const Json& a, const Context& ctx) -> ToolResult {
                           std::string query = str(a, "query");
                           if (query.size() < 2)
                               return fail(rt::langPick(ctx.lang, "'query' precisa de pelo menos 2 caracteres.",
                                                        "'query' needs at least 2 characters."));
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
