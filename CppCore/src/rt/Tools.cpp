#include "rt/Tools.hpp"
#include "memory/MemoryService.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "sandbox/SandboxManager.hpp"
#include "util/Json.hpp"

namespace taskforge {
namespace rt {

namespace fs = std::filesystem;
using json = taskforge::Json;

namespace {

constexpr size_t kMaxToolOutput = 32 * 1024;
constexpr size_t kMaxReadBytes = 512 * 1024;

std::string clip(std::string s, RuntimeLang lang, size_t limit = kMaxToolOutput) {
    if (s.size() > limit) {
        s.resize(limit);
        s += langPick(lang, "\n[... saida truncada ...]", "\n[... output truncated ...]");
    }
    return s;
}

ToolResult err(const std::string& m) { return ToolResult{m, true}; }

bool isAllowed(const ToolContext& ctx, const std::string& name) {
    return std::find(ctx.allowed.begin(), ctx.allowed.end(), name) != ctx.allowed.end();
}

std::string stringArg(const json& a, const char* key) {
    if (a.is_object() && a.contains(key) && a[key].is_string()) return a[key].get<std::string>();
    return std::string();
}

} // namespace

std::optional<fs::path> resolveInWorkspace(const fs::path& root, const std::string& relative) {
    if (relative.empty()) return std::nullopt;
    // Fluxos alternativos NTFS (arquivo:stream) e caracteres de controle.
    for (size_t i = 0; i < relative.size(); ++i) {
        unsigned char c = (unsigned char)relative[i];
        if (c < 0x20) return std::nullopt;
        if (c == ':' && !(i == 1 && std::isalpha((unsigned char)relative[0]))) return std::nullopt;
    }
    std::error_code ec;
    fs::path canonRoot = fs::weakly_canonical(root, ec);
    if (ec) return std::nullopt;

    fs::path p(relative);
    fs::path joined = p.is_absolute() ? p : canonRoot / p;
    fs::path canon = fs::weakly_canonical(joined, ec);   // resolve symlinks do trecho existente
    if (ec) return std::nullopt;

    fs::path rel = canon.lexically_relative(canonRoot);
    if (rel.empty() || *rel.begin() == "..") return std::nullopt;
    return canon;
}

bool isProtectedPath(const fs::path& root, const fs::path& resolved) {
    std::error_code ec;
    fs::path canonRoot = fs::weakly_canonical(root, ec);
    if (ec) return true;
    fs::path rel = resolved.lexically_relative(canonRoot);
    if (rel.empty() || rel.begin() == rel.end()) return true;
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string first = lower(rel.begin()->generic_string());
    static const char* kProtectedDirs[] = {".git", ".workspace", ".githooks", ".arestrix", "pipelines"};
    for (const char* d : kProtectedDirs)
        if (first == d) return true;
    if (std::next(rel.begin()) == rel.end()) {   // arquivo na raiz do projeto
        if (first == "arestrix.project.json" || first == "taskforge.project.json" || first == "arestrix-mcp-servers.json") return true;
    }
    return false;
}

std::vector<ToolDef> toolDefinitions(const std::vector<std::string>& allowed, RuntimeLang lang) {
    std::vector<ToolDef> all;
    all.push_back({"read",
                   langPick(lang,
                            "Le um arquivo de texto do workspace e devolve o conteudo.",
                            "Reads a text file from the workspace and returns its content."),
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", langPick(lang, "Caminho relativo ao workspace", "Path relative to the workspace")}}}}},
                        {"required", {"path"}}}});
    all.push_back({"write",
                   langPick(lang,
                            "Cria ou sobrescreve um arquivo do workspace com o conteudo informado (cria as pastas necessarias).",
                            "Creates or overwrites a workspace file with the given content (creates parent folders)."),
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", langPick(lang, "Caminho relativo ao workspace", "Path relative to the workspace")}}},
                                        {"content", {{"type", "string"}, {"description", langPick(lang, "Conteudo completo do arquivo", "Full file content")}}}}},
                        {"required", {"path", "content"}}}});
    all.push_back({"edit",
                   langPick(lang,
                            "Substitui UMA ocorrencia exata de old_text por new_text em um arquivo existente.",
                            "Replaces ONE exact occurrence of old_text with new_text in an existing file."),
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}}},
                                        {"old_text", {{"type", "string"}, {"description", langPick(lang, "Trecho exato a substituir (deve ser unico)", "Exact text to replace (must be unique)")}}},
                                        {"new_text", {{"type", "string"}}}}},
                        {"required", {"path", "old_text", "new_text"}}}});
    all.push_back({"bash",
                   langPick(lang,
                            "Executa um comando de shell no workspace (sujeito a politica de sandbox) e devolve a saida combinada.",
                            "Runs a shell command in the workspace (subject to the sandbox policy) and returns the combined output."),
                   json{{"type", "object"},
                        {"properties", {{"command", {{"type", "string"}}}}},
                        {"required", {"command"}}}});
    all.push_back({"validate_svg",
                   langPick(lang,
                            "Valida um arquivo SVG (XML, viewBox, ids, referencias SMIL e regra tipografica) e lista os problemas.",
                            "Validates an SVG file (XML, viewBox, ids, SMIL references and typography rule) and lists the problems."),
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", langPick(lang, "Caminho do .svg", "Path of the .svg")}}}}},
                        {"required", {"path"}}}});

    all.push_back({"memory_search",
                   langPick(lang,
                            "Busca licoes e fatos na memoria do projeto. Devolve dados historicos nao confiaveis.",
                            "Searches lessons and facts in project memory. Returns untrusted historical data."),
                   json{{"type", "object"},
                        {"properties", {{"query", {{"type", "string"}, {"description", langPick(lang, "Texto a buscar", "Text to search")}}},
                                        {"limit", {{"type", "integer"}, {"description", langPick(lang, "Maximo de resultados (1-20)", "Maximum results (1-20)")}}}}},
                        {"required", {"query"}}}});
    all.push_back({"memory_record_lesson",
                   langPick(lang,
                            "Registra uma licao preventiva na memoria do projeto, com dominio, gatilho e regra.",
                            "Records a preventive lesson in project memory with domain, trigger and rule."),
                   json{{"type", "object"},
                        {"properties", {{"domain", {{"type", "string"}, {"description", langPick(lang, "Dominio da licao", "Lesson domain")}}},
                                        {"rule", {{"type", "string"}, {"description", langPick(lang, "Regra preventiva", "Preventive rule")}}},
                                        {"trigger", {{"type", "string"}, {"description", langPick(lang, "Erro ou situacao que motivou a licao", "Error or situation that motivated the lesson")}}}}},
                        {"required", {"domain", "rule"}}}});

    std::vector<ToolDef> out;
    for (auto& t : all) {
        if (std::find(allowed.begin(), allowed.end(), t.name) != allowed.end()) out.push_back(std::move(t));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Validador de SVG (analisador XML minimo, suficiente para detectar erros estruturais)
// ---------------------------------------------------------------------------
std::vector<std::string> validateSvg(const std::string& svg, RuntimeLang lang) {
    auto T = [&](const char* pt, const char* en) { return langPick(lang, pt, en); };

    std::vector<std::string> issues;
    if (svg.find_first_not_of(" \t\r\n") == std::string::npos) {
        issues.push_back(T("XML: arquivo vazio", "XML: empty file"));
        return issues;
    }

    struct Open { std::string name; };
    std::vector<Open> stack;
    std::string rootName;
    bool rootClosed = false;
    std::string rootAttrs;
    std::vector<std::string> ids;
    std::vector<std::pair<std::string, std::string>> timingRefs;   // (atributo, valor)
    bool hasText = false;
    std::vector<std::string> textDecorations;

    size_t i = 0, n = svg.size();
    auto lineOf = [&](size_t pos) {
        return 1 + (int)std::count(svg.begin(), svg.begin() + std::min(pos, n), '\n');
    };

    while (i < n) {
        size_t lt = svg.find('<', i);
        if (lt == std::string::npos) break;
        i = lt;
        if (svg.compare(i, 4, "<!--") == 0) {
            size_t e = svg.find("-->", i + 4);
            if (e == std::string::npos) { issues.push_back(T("XML: comentario nao fechado (linha ", "XML: unclosed comment (line ") + std::to_string(lineOf(i)) + ")"); return issues; }
            i = e + 3; continue;
        }
        if (svg.compare(i, 9, "<![CDATA[") == 0) {
            size_t e = svg.find("]]>", i + 9);
            if (e == std::string::npos) { issues.push_back(T("XML: CDATA nao fechado (linha ", "XML: unclosed CDATA (line ") + std::to_string(lineOf(i)) + ")"); return issues; }
            i = e + 3; continue;
        }
        if (svg.compare(i, 2, "<?") == 0) {
            size_t e = svg.find("?>", i + 2);
            if (e == std::string::npos) { issues.push_back(T("XML: instrucao de processamento nao fechada", "XML: unclosed processing instruction")); return issues; }
            i = e + 2; continue;
        }
        if (svg.compare(i, 2, "<!") == 0) {   // DOCTYPE
            size_t e = svg.find('>', i + 2);
            if (e == std::string::npos) { issues.push_back(T("XML: declaracao nao fechada", "XML: unclosed declaration")); return issues; }
            i = e + 1; continue;
        }

        // Tag: encontra o '>' respeitando aspas.
        size_t j = i + 1;
        char quote = 0;
        for (; j < n; ++j) {
            char c = svg[j];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
        }
        if (j >= n) { issues.push_back(T("XML: tag nao fechada (linha ", "XML: unclosed tag (line ") + std::to_string(lineOf(i)) + ")"); return issues; }
        std::string tag = svg.substr(i + 1, j - i - 1);
        size_t tagPos = i;
        i = j + 1;

        if (!tag.empty() && tag[0] == '/') {
            std::string name = tag.substr(1);
            while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
            if (stack.empty() || stack.back().name != name) {
                issues.push_back("XML: </" + name + "> " +
                                 T("sem abertura correspondente (linha ", "without a matching opening tag (line ") +
                                 std::to_string(lineOf(tagPos)) + ")");
                return issues;
            }
            stack.pop_back();
            if (stack.empty()) rootClosed = true;
            continue;
        }

        bool selfClose = !tag.empty() && tag.back() == '/';
        if (selfClose) tag.pop_back();
        size_t sp = 0;
        while (sp < tag.size() && !isspace((unsigned char)tag[sp])) ++sp;
        std::string name = tag.substr(0, sp);
        std::string attrs = tag.substr(sp);

        if (stack.empty()) {
            if (!rootName.empty()) {
                issues.push_back(T("XML: mais de um elemento raiz (linha ", "XML: more than one root element (line ") + std::to_string(lineOf(tagPos)) + ")");
                return issues;
            }
            rootName = name;
            rootAttrs = attrs;
        }
        if (name == "text" || name == "tspan") hasText = true;

        static const std::regex attrRe(R"re(([A-Za-z_:][-A-Za-z0-9_:.]*)\s*=\s*("([^"]*)"|'([^']*)'))re");
        for (std::sregex_iterator it(attrs.begin(), attrs.end(), attrRe), end; it != end; ++it) {
            std::string key = (*it)[1];
            std::string val = (*it)[3].matched ? (*it)[3].str() : (*it)[4].str();
            if (key == "id") ids.push_back(val);
            if (key == "begin" || key == "end") timingRefs.emplace_back(key, val);
            if (name == "text" || name == "tspan") {
                if (key == "filter") textDecorations.push_back(T("filter em <", "filter on <") + name + ">");
                if (key == "style" && (val.find("text-shadow") != std::string::npos ||
                                       val.find("drop-shadow") != std::string::npos ||
                                       val.find("filter") != std::string::npos)) {
                    textDecorations.push_back(T("style com sombra/filtro em <", "style with shadow/filter on <") + name + ">");
                }
            }
        }
        if (!selfClose) stack.push_back({name});
        else if (stack.empty()) rootClosed = true;
    }

    if (!stack.empty()) issues.push_back("XML: <" + stack.back().name + "> " + T("nao foi fechada", "was not closed"));
    if (rootName.empty()) { issues.push_back(T("XML: nenhum elemento encontrado", "XML: no element found")); return issues; }
    if (rootName != "svg") issues.push_back(T("Raiz deve ser <svg>, encontrado <", "Root must be <svg>, found <") + rootName + ">");
    (void)rootClosed;

    if (rootAttrs.find("viewBox") == std::string::npos &&
        !(rootAttrs.find("width") != std::string::npos && rootAttrs.find("height") != std::string::npos)) {
        issues.push_back(T("SVG: faltam viewBox (ou width+height) no elemento raiz", "SVG: missing viewBox (or width+height) on the root element"));
    }

    std::set<std::string> seen, dup;
    for (auto& id : ids) if (!seen.insert(id).second) dup.insert(id);
    for (auto& d : dup) issues.push_back(T("SVG: id duplicado \"", "SVG: duplicate id \"") + d + "\"");

    // begin="idAnterior.end + 0.2s" / "a.begin; b.end"
    static const std::regex refRe(R"(([A-Za-z_][-A-Za-z0-9_:]*)\.(begin|end|repeat\([0-9]+\)|click|mouseover))");
    for (auto& [key, val] : timingRefs) {
        for (std::sregex_iterator it(val.begin(), val.end(), refRe), end; it != end; ++it) {
            std::string ref = (*it)[1];
            if (!seen.count(ref)) issues.push_back("SMIL: " + key + "=\"" + val + "\" " + T("referencia id inexistente \"", "references nonexistent id \"") + ref + "\"");
        }
    }

    // Regra tipografica: nada de sombra/glow em texto.
    if (hasText) {
        const std::string typo = T("TIPOGRAFIA: ", "TYPOGRAPHY: ");
        for (auto& d : textDecorations) issues.push_back(typo + d);
        static const std::regex cssRe(R"(text-shadow\s*:)");
        if (std::regex_search(svg, cssRe)) issues.push_back(T("TIPOGRAFIA: text-shadow encontrado", "TYPOGRAPHY: text-shadow found"));
        if (svg.find("feDropShadow") != std::string::npos || svg.find("feGaussianBlur") != std::string::npos) {
            issues.push_back(T("TIPOGRAFIA: filtros feDropShadow/feGaussianBlur presentes (proibidos em texto)",
                               "TYPOGRAPHY: feDropShadow/feGaussianBlur filters present (forbidden in text)"));
        }
    }
    return issues;
}

// ---------------------------------------------------------------------------
// Execucao
// ---------------------------------------------------------------------------
ToolResult executeTool(const ToolContext& ctx, const std::string& name, const json& args) {
    const RuntimeLang lang = ctx.lang;
    auto T = [&](const char* pt, const char* en) { return langPick(lang, pt, en); };

    if (!isAllowed(ctx, name)) return err(T("Ferramenta nao autorizada para este agente: ", "Tool not authorized for this agent: ") + name);

    try {
        if (name == "read") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err(T("Caminho fora do workspace ou invalido.", "Path outside the workspace or invalid."));
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err(T("Arquivo nao encontrado: ", "File not found: ") + stringArg(args, "path"));
            auto size = fs::file_size(*p, ec);
            if (size > kMaxReadBytes) return err(T("Arquivo grande demais para leitura direta (", "File too large for direct reading (") + std::to_string(size) + " bytes).");
            return ToolResult{clip(jsonutil::readFile(*p), lang), false};
        }

        if (name == "write") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err(T("Caminho fora do workspace ou invalido.", "Path outside the workspace or invalid."));
            if (isProtectedPath(ctx.workspaceRoot, *p))
                return err(T("Caminho protegido (pipelines, configuracao e .git nao podem ser gravados por agentes): ",
                             "Protected path (pipelines, configuration and .git cannot be written by agents): ") + stringArg(args, "path"));
            if (!args.contains("content") || !args["content"].is_string()) return err(T("Parametro 'content' ausente.", "Missing parameter 'content'."));
            std::string content = args["content"].get<std::string>();
            std::error_code ec;
            fs::create_directories(p->parent_path(), ec);
            std::ofstream out(*p, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return err(T("Nao foi possivel gravar o arquivo.", "Could not write the file."));
            out << content;
            out.close();
            return ToolResult{"OK: " + std::to_string(content.size()) + T(" bytes gravados em ", " bytes written to ") + stringArg(args, "path"), false};
        }

        if (name == "edit") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err(T("Caminho fora do workspace ou invalido.", "Path outside the workspace or invalid."));
            if (isProtectedPath(ctx.workspaceRoot, *p))
                return err(T("Caminho protegido (pipelines, configuracao e .git nao podem ser editados por agentes): ",
                             "Protected path (pipelines, configuration and .git cannot be edited by agents): ") + stringArg(args, "path"));
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err(T("Arquivo nao encontrado.", "File not found."));
            std::string oldText = stringArg(args, "old_text");
            std::string newText = stringArg(args, "new_text");
            if (oldText.empty()) return err(T("old_text nao pode ser vazio.", "old_text cannot be empty."));
            std::string content = jsonutil::readFile(*p);
            size_t first = content.find(oldText);
            if (first == std::string::npos) return err(T("old_text nao encontrado no arquivo.", "old_text not found in the file."));
            if (content.find(oldText, first + 1) != std::string::npos)
                return err(T("old_text aparece mais de uma vez; forneca um trecho unico.", "old_text appears more than once; provide a unique snippet."));
            content.replace(first, oldText.size(), newText);
            std::ofstream out(*p, std::ios::binary | std::ios::trunc);
            out << content;
            return ToolResult{T("OK: edicao aplicada.", "OK: edit applied."), false};
        }

        if (name == "validate_svg") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err(T("Caminho fora do workspace ou invalido.", "Path outside the workspace or invalid."));
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err(T("Arquivo nao encontrado: ", "File not found: ") + stringArg(args, "path"));
            auto issues = validateSvg(jsonutil::readFile(*p), lang);
            if (issues.empty()) return ToolResult{T("SVG valido: nenhum problema encontrado.", "Valid SVG: no problems found."), false};
            std::ostringstream ss;
            ss << issues.size() << T(" problema(s) encontrado(s):\n", " problem(s) found:\n");
            for (auto& s : issues) ss << "- " << s << "\n";
            return ToolResult{ss.str(), false};
        }

        if (name == "memory_search") {
            if (ctx.projectId.empty()) return err(T("Memoria nao configurada para este projeto.", "Memory is not configured for this project."));
            std::string query = stringArg(args, "query");
            if (query.empty()) return err(T("Parametro 'query' ausente.", "Missing parameter 'query'."));
            int limit = 10;
            if (args.contains("limit") && args["limit"].is_number()) limit = static_cast<int>(args["limit"].get<double>());
            limit = std::max(1, std::min(20, limit));
            std::vector<std::string> hitIds;
            auto& memory = taskforge::memory::MemoryService::getInstance();
            std::string result = memory.searchContext(ctx.projectId, query, static_cast<size_t>(limit), &hitIds);
            if (!hitIds.empty() && !ctx.runId.empty()) {
                memory.markHitsUsed(ctx.projectId, ctx.runId, ctx.agentId, hitIds);
            }
            if (result.empty()) return ToolResult{T("Nenhuma memoria encontrada para: ", "No memory found for: ") + query, false};
            return ToolResult{result, false};
        }

        if (name == "memory_record_lesson") {
            if (ctx.projectId.empty()) return err(T("Memoria nao configurada para este projeto.", "Memory is not configured for this project."));
            if (!ctx.memoryWriteEnabled) return err(T("Escrita de licoes nao autorizada para este agente.", "Lesson recording is not authorized for this agent."));
            std::string domain = stringArg(args, "domain");
            std::string rule = stringArg(args, "rule");
            std::string trigger = stringArg(args, "trigger");
            if (rule.empty()) return err(T("Parametro 'rule' ausente.", "Missing parameter 'rule'."));
            if (!ctx.memoryDomains.empty() &&
                std::find(ctx.memoryDomains.begin(), ctx.memoryDomains.end(), domain.empty() ? "geral" : domain) == ctx.memoryDomains.end()) {
                return err(T("Dominio nao autorizado para este agente: ", "Domain not authorized for this agent: ") + domain);
            }
            std::string id = taskforge::memory::MemoryService::getInstance().recordLesson(
                ctx.projectId, domain.empty() ? "geral" : domain, trigger, rule, ctx.agentId);
            if (id.empty()) return err(T("Nao foi possivel registrar a licao.", "Could not record the lesson."));
            return ToolResult{T("OK: licao registrada (", "OK: lesson recorded (") + id + ")", false};
        }

        if (name == "bash") {
            std::string cmd = stringArg(args, "command");
            if (cmd.empty()) return err(T("Parametro 'command' ausente.", "Missing parameter 'command'."));
            sandbox::ExecutionParams params;
            params.agentId = ctx.agentId;
            params.command = cmd;
            params.useShell = true;
            params.workingDir = ctx.workspaceRoot.string();
            params.policy = ctx.sandbox;
            auto res = sandbox::SandboxManager::instance().execute(params);
            std::ostringstream ss;
            ss << clip(res.stdoutOutput, lang);
            if (!res.errorMessage.empty()) ss << "\n" << T("[erro] ", "[error] ") << res.errorMessage;
            ss << "\n[exit code: " << res.exitCode << "]";
            return ToolResult{ss.str(), !res.isSuccess()};
        }
    } catch (const std::exception& e) {
        return err(std::string(T("Falha ao executar a ferramenta: ", "Failed to execute the tool: ")) + e.what());
    }
    return err(T("Ferramenta desconhecida: ", "Unknown tool: ") + name);
}

} // namespace rt
} // namespace taskforge
