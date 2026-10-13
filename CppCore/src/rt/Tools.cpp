#include "rt/Tools.hpp"

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

std::string clip(std::string s, size_t limit = kMaxToolOutput) {
    if (s.size() > limit) {
        s.resize(limit);
        s += "\n[... saida truncada ...]";
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

std::vector<ToolDef> toolDefinitions(const std::vector<std::string>& allowed) {
    std::vector<ToolDef> all;
    all.push_back({"read", "Le um arquivo de texto do workspace e devolve o conteudo.",
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", "Caminho relativo ao workspace"}}}}},
                        {"required", {"path"}}}});
    all.push_back({"write", "Cria ou sobrescreve um arquivo do workspace com o conteudo informado (cria as pastas necessarias).",
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", "Caminho relativo ao workspace"}}},
                                        {"content", {{"type", "string"}, {"description", "Conteudo completo do arquivo"}}}}},
                        {"required", {"path", "content"}}}});
    all.push_back({"edit", "Substitui UMA ocorrencia exata de old_text por new_text em um arquivo existente.",
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}}},
                                        {"old_text", {{"type", "string"}, {"description", "Trecho exato a substituir (deve ser unico)"}}},
                                        {"new_text", {{"type", "string"}}}}},
                        {"required", {"path", "old_text", "new_text"}}}});
    all.push_back({"bash", "Executa um comando de shell no workspace (sujeito a politica de sandbox) e devolve a saida combinada.",
                   json{{"type", "object"},
                        {"properties", {{"command", {{"type", "string"}}}}},
                        {"required", {"command"}}}});
    all.push_back({"validate_svg", "Valida um arquivo SVG (XML, viewBox, ids, referencias SMIL e regra tipografica) e lista os problemas.",
                   json{{"type", "object"},
                        {"properties", {{"path", {{"type", "string"}, {"description", "Caminho do .svg"}}}}},
                        {"required", {"path"}}}});

    std::vector<ToolDef> out;
    for (auto& t : all) {
        if (std::find(allowed.begin(), allowed.end(), t.name) != allowed.end()) out.push_back(std::move(t));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Validador de SVG (analisador XML minimo, suficiente para detectar erros estruturais)
// ---------------------------------------------------------------------------
std::vector<std::string> validateSvg(const std::string& svg) {
    std::vector<std::string> issues;
    if (svg.find_first_not_of(" \t\r\n") == std::string::npos) {
        issues.push_back("XML: arquivo vazio");
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
            if (e == std::string::npos) { issues.push_back("XML: comentario nao fechado (linha " + std::to_string(lineOf(i)) + ")"); return issues; }
            i = e + 3; continue;
        }
        if (svg.compare(i, 9, "<![CDATA[") == 0) {
            size_t e = svg.find("]]>", i + 9);
            if (e == std::string::npos) { issues.push_back("XML: CDATA nao fechado (linha " + std::to_string(lineOf(i)) + ")"); return issues; }
            i = e + 3; continue;
        }
        if (svg.compare(i, 2, "<?") == 0) {
            size_t e = svg.find("?>", i + 2);
            if (e == std::string::npos) { issues.push_back("XML: instrucao de processamento nao fechada"); return issues; }
            i = e + 2; continue;
        }
        if (svg.compare(i, 2, "<!") == 0) {   // DOCTYPE
            size_t e = svg.find('>', i + 2);
            if (e == std::string::npos) { issues.push_back("XML: declaracao nao fechada"); return issues; }
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
        if (j >= n) { issues.push_back("XML: tag nao fechada (linha " + std::to_string(lineOf(i)) + ")"); return issues; }
        std::string tag = svg.substr(i + 1, j - i - 1);
        size_t tagPos = i;
        i = j + 1;

        if (!tag.empty() && tag[0] == '/') {
            std::string name = tag.substr(1);
            while (!name.empty() && isspace((unsigned char)name.back())) name.pop_back();
            if (stack.empty() || stack.back().name != name) {
                issues.push_back("XML: </" + name + "> sem abertura correspondente (linha " + std::to_string(lineOf(tagPos)) + ")");
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
                issues.push_back("XML: mais de um elemento raiz (linha " + std::to_string(lineOf(tagPos)) + ")");
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
                if (key == "filter") textDecorations.push_back("filter em <" + name + ">");
                if (key == "style" && (val.find("text-shadow") != std::string::npos ||
                                       val.find("drop-shadow") != std::string::npos ||
                                       val.find("filter") != std::string::npos)) {
                    textDecorations.push_back("style com sombra/filtro em <" + name + ">");
                }
            }
        }
        if (!selfClose) stack.push_back({name});
        else if (stack.empty()) rootClosed = true;
    }

    if (!stack.empty()) issues.push_back("XML: <" + stack.back().name + "> nao foi fechada");
    if (rootName.empty()) { issues.push_back("XML: nenhum elemento encontrado"); return issues; }
    if (rootName != "svg") issues.push_back("Raiz deve ser <svg>, encontrado <" + rootName + ">");
    (void)rootClosed;

    if (rootAttrs.find("viewBox") == std::string::npos &&
        !(rootAttrs.find("width") != std::string::npos && rootAttrs.find("height") != std::string::npos)) {
        issues.push_back("SVG: faltam viewBox (ou width+height) no elemento raiz");
    }

    std::set<std::string> seen, dup;
    for (auto& id : ids) if (!seen.insert(id).second) dup.insert(id);
    for (auto& d : dup) issues.push_back("SVG: id duplicado \"" + d + "\"");

    // begin="idAnterior.end + 0.2s" / "a.begin; b.end"
    static const std::regex refRe(R"(([A-Za-z_][-A-Za-z0-9_:]*)\.(begin|end|repeat\([0-9]+\)|click|mouseover))");
    for (auto& [key, val] : timingRefs) {
        for (std::sregex_iterator it(val.begin(), val.end(), refRe), end; it != end; ++it) {
            std::string ref = (*it)[1];
            if (!seen.count(ref)) issues.push_back("SMIL: " + key + "=\"" + val + "\" referencia id inexistente \"" + ref + "\"");
        }
    }

    // Regra tipografica: nada de sombra/glow em texto.
    if (hasText) {
        for (auto& d : textDecorations) issues.push_back("TIPOGRAFIA: " + d);
        static const std::regex cssRe(R"(text-shadow\s*:)");
        if (std::regex_search(svg, cssRe)) issues.push_back("TIPOGRAFIA: text-shadow encontrado");
        if (svg.find("feDropShadow") != std::string::npos || svg.find("feGaussianBlur") != std::string::npos) {
            issues.push_back("TIPOGRAFIA: filtros feDropShadow/feGaussianBlur presentes (proibidos em texto)");
        }
    }
    return issues;
}

// ---------------------------------------------------------------------------
// Execucao
// ---------------------------------------------------------------------------
ToolResult executeTool(const ToolContext& ctx, const std::string& name, const json& args) {
    if (!isAllowed(ctx, name)) return err("Ferramenta nao autorizada para este agente: " + name);

    try {
        if (name == "read") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err("Caminho fora do workspace ou invalido.");
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err("Arquivo nao encontrado: " + stringArg(args, "path"));
            auto size = fs::file_size(*p, ec);
            if (size > kMaxReadBytes) return err("Arquivo grande demais para leitura direta (" + std::to_string(size) + " bytes).");
            return ToolResult{clip(jsonutil::readFile(*p)), false};
        }

        if (name == "write") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err("Caminho fora do workspace ou invalido.");
            if (!args.contains("content") || !args["content"].is_string()) return err("Parametro 'content' ausente.");
            std::string content = args["content"].get<std::string>();
            std::error_code ec;
            fs::create_directories(p->parent_path(), ec);
            std::ofstream out(*p, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return err("Nao foi possivel gravar o arquivo.");
            out << content;
            out.close();
            return ToolResult{"OK: " + std::to_string(content.size()) + " bytes gravados em " + stringArg(args, "path"), false};
        }

        if (name == "edit") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err("Caminho fora do workspace ou invalido.");
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err("Arquivo nao encontrado.");
            std::string oldText = stringArg(args, "old_text");
            std::string newText = stringArg(args, "new_text");
            if (oldText.empty()) return err("old_text nao pode ser vazio.");
            std::string content = jsonutil::readFile(*p);
            size_t first = content.find(oldText);
            if (first == std::string::npos) return err("old_text nao encontrado no arquivo.");
            if (content.find(oldText, first + 1) != std::string::npos) return err("old_text aparece mais de uma vez; forneca um trecho unico.");
            content.replace(first, oldText.size(), newText);
            std::ofstream out(*p, std::ios::binary | std::ios::trunc);
            out << content;
            return ToolResult{"OK: edicao aplicada.", false};
        }

        if (name == "validate_svg") {
            auto p = resolveInWorkspace(ctx.workspaceRoot, stringArg(args, "path"));
            if (!p) return err("Caminho fora do workspace ou invalido.");
            std::error_code ec;
            if (!fs::is_regular_file(*p, ec)) return err("Arquivo nao encontrado: " + stringArg(args, "path"));
            auto issues = validateSvg(jsonutil::readFile(*p));
            if (issues.empty()) return ToolResult{"SVG valido: nenhum problema encontrado.", false};
            std::ostringstream ss;
            ss << issues.size() << " problema(s) encontrado(s):\n";
            for (auto& s : issues) ss << "- " << s << "\n";
            return ToolResult{ss.str(), false};
        }

        if (name == "bash") {
            std::string cmd = stringArg(args, "command");
            if (cmd.empty()) return err("Parametro 'command' ausente.");
            sandbox::ExecutionParams params;
            params.agentId = ctx.agentId;
            params.command = cmd;
            params.useShell = true;
            params.workingDir = ctx.workspaceRoot.string();
            params.policy = ctx.sandbox;
            auto res = sandbox::SandboxManager::instance().execute(params);
            std::ostringstream ss;
            ss << clip(res.stdoutOutput);
            if (!res.errorMessage.empty()) ss << "\n[erro] " << res.errorMessage;
            ss << "\n[exit code: " << res.exitCode << "]";
            return ToolResult{ss.str(), !res.isSuccess()};
        }
    } catch (const std::exception& e) {
        return err(std::string("Falha ao executar a ferramenta: ") + e.what());
    }
    return err("Ferramenta desconhecida: " + name);
}

} // namespace rt
} // namespace taskforge
