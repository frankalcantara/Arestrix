#include "search/ProjectSearch.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>

#include "search/Synonyms.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace search {

using Doc = ProjectSearchIndex::Doc;
using Field = ProjectSearchIndex::Field;

namespace {

constexpr size_t kMaxFieldBytes = 30000;     // texto indexado por campo
constexpr size_t kMaxFileBytes = 2 * 1024 * 1024;
constexpr size_t kMaxSkillFiles = 500;
constexpr int kMaxGraphDepth = 8;
constexpr float kK1 = 1.2f;
constexpr float kB = 0.75f;

// ---------------------------------------------------------------------------------------------
// UTF-8 / normalizacao
// ---------------------------------------------------------------------------------------------

void decodeNext(const std::string& s, size_t& i, char32_t& cp) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { cp = c; ++i; return; }
    int extra = 0;
    if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { cp = 0xFFFD; ++i; return; }
    size_t j = i + 1;
    for (int k = 0; k < extra; ++k, ++j) {
        if (j >= s.size() || (static_cast<unsigned char>(s[j]) & 0xC0) != 0x80) { cp = 0xFFFD; i = j; return; }
        cp = (cp << 6) | (static_cast<unsigned char>(s[j]) & 0x3F);
    }
    i = j;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// U+00C0..U+00FF sem acento. ' ' marca os sinais (x e divisao), que separam termos.
const char kLatin1Fold[] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUYTsaaaaaaaceeeeiiiidnooooo ouuuuyty";

struct FoldedChar {
    bool alnum = false;
    bool upper = false;
    std::string text; // vazio quando nao e alnum
};

FoldedChar foldChar(char32_t cp) {
    FoldedChar r;
    if (cp < 0x80) {
        if (cp >= 'A' && cp <= 'Z') { r.alnum = true; r.upper = true; r.text = std::string(1, static_cast<char>(cp - 'A' + 'a')); }
        else if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) { r.alnum = true; r.text = std::string(1, static_cast<char>(cp)); }
        return r;
    }
    if (cp < 0xC0) return r; // pontuacao Latin-1
    if (cp <= 0xFF) {
        char c = kLatin1Fold[cp - 0xC0];
        if (c == ' ') return r;
        r.alnum = true;
        r.upper = cp < 0xDF;
        r.text = std::string(1, static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        return r;
    }
    if ((cp >= 0x2000 && cp <= 0x2BFF) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
        cp == 0xFFFD || cp >= 0x1F000) {
        return r; // pontuacao tipografica, setas, simbolos, emojis
    }
    r.alnum = true;
    appendUtf8(r.text, cp);
    return r;
}

const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> sw = {
        "a", "o", "as", "os", "um", "uma", "de", "do", "da", "dos", "das", "em", "no", "na", "nos", "nas", "por",
        "para", "com", "sem", "que", "ou", "se", "ao", "aos", "the", "of", "and", "or", "to", "in", "on", "for",
        "with", "is", "are", "be", "this", "that", "it", "at", "by", "an", "ele", "ela", "mais", "foi", "sao"};
    return sw;
}

bool endsWith(const std::string& s, const char* suf) {
    size_t n = std::char_traits<char>::length(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

std::string stem(const std::string& s) {
    size_t n = s.size();
    if (n > 5 && endsWith(s, "coes")) return s.substr(0, n - 4) + "cao";
    if (n > 6 && endsWith(s, "mente")) return s.substr(0, n - 5);
    if (n > 4 && endsWith(s, "ies")) return s.substr(0, n - 3) + "y";
    if (n > 4 && endsWith(s, "ns")) return s.substr(0, n - 2) + "m";
    if (n > 5 && endsWith(s, "res")) return s.substr(0, n - 2);
    if (n > 3 && s.back() == 's' && !endsWith(s, "ss") && !endsWith(s, "us") && !endsWith(s, "is")) return s.substr(0, n - 1);
    return s;
}

struct Tok {
    std::string raw;  // sem acento, minusculo
    std::string base; // com stemming
};

std::vector<Tok> tokenizeFull(const std::string& text) {
    std::vector<Tok> out;
    std::string cur;
    bool prevLowerOrDigit = false;
    auto flush = [&]() {
        if (cur.size() >= 2 && !stopwords().count(cur)) out.push_back({cur, stem(cur)});
        cur.clear();
    };
    size_t i = 0;
    while (i < text.size()) {
        char32_t cp;
        decodeNext(text, i, cp);
        FoldedChar f = foldChar(cp);
        if (!f.alnum) { flush(); prevLowerOrDigit = false; continue; }
        if (f.upper && prevLowerOrDigit) flush(); // camelCase
        cur += f.text;
        prevLowerOrDigit = !f.upper;
    }
    flush();
    return out;
}

std::vector<std::string> trigramsOf(const std::string& raw) {
    std::vector<std::string> out;
    if (raw.size() < 3) return out;
    std::string s = "#" + raw + "#";
    for (size_t i = 0; i + 3 <= s.size(); ++i) out.push_back(s.substr(i, 3));
    return out;
}

// Grupos pequenos de sinonimos pt/en, normalizados com o mesmo pipeline do indice.
const std::vector<std::vector<std::string>>& synonymGroups() {
    static const std::vector<std::vector<std::string>> raw = {
        {"erro", "falha", "error", "fail", "failure", "bug", "excecao", "exception"},
        {"validar", "validacao", "valida", "validate", "validation", "verificar", "check"},
        {"modelo", "model", "llm"},
        {"agente", "agent"},
        {"condicao", "decisao", "condition", "decision", "ramo", "branch"},
        {"fila", "queue"},
        {"banco", "database", "sqlite", "db"},
        {"memoria", "memory"},
        {"ferramenta", "tool"},
        {"executar", "rodar", "run", "execute"},
        {"arquivo", "file"},
        {"skill", "habilidade"},
        {"imagem", "image", "visao", "vision"},
        {"paralelo", "parallel", "concorrente", "concurrent", "fork"},
        {"gpu", "cuda"},
        {"consenso", "consensus", "sintese"},
        {"prompt", "instrucao"},
        {"subfluxo", "subflow"},
        {"modulo", "module"}};
    static const std::vector<std::vector<std::string>> norm = [] {
        std::vector<std::vector<std::string>> g;
        for (const auto& grp : raw) {
            std::vector<std::string> n;
            for (const auto& w : grp) {
                auto t = tokenizeFull(w);
                if (!t.empty()) n.push_back(t[0].base);
            }
            g.push_back(std::move(n));
        }
        return g;
    }();
    return norm;
}

// Vocabulario bilingue dos tipos de vertice: faz "decisao" achar um vertice condition, etc.
std::string typeWords(const std::string& type) {
    if (type == "static") return "static tarefa estatica task";
    if (type == "condition") return "condition condicao decisao ramo branch";
    if (type == "subflow") return "subflow subfluxo";
    if (type == "module") return "module modulo";
    if (type == "critical") return "critical secao critica mutex exclusao";
    if (type == "cuda") return "cuda gpu kernel";
    if (type == "agent") return "agent agente ia llm";
    if (type == "fork") return "fork bifurcacao paralelo";
    if (type == "join") return "join juncao barreira sincronizacao";
    return type;
}

std::string truncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6"; // reticencias
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------------------------
// Arquivos e extracao de documentos
// ---------------------------------------------------------------------------------------------

bool readText(const fs::path& p, std::string& out) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (ec || sz > kMaxFileBytes) return false;
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

void collectStrings(const Json& j, std::string& out, int depth) {
    if (depth > 5) return;
    if (j.is_string()) {
        out += j.str();
        out += '\n';
    } else if (j.is_array() || j.is_object()) {
        for (auto it = j.begin(); it != j.end(); ++it) collectStrings(*it, out, depth + 1);
    }
}

std::string scalarText(const Json& j) {
    if (j.is_string()) return j.str();
    if (j.is_null()) return "";
    return j.dump();
}

struct GraphCtx {
    std::string pipelineId, pipelineName, file;
};

std::string joinPath(const std::string& head, const std::vector<std::string>& labels) {
    std::string s = head;
    for (const auto& l : labels) s += " \xE2\x80\xBA " + l; // " › "
    return s;
}

void addField(Doc& d, const std::string& text, float w, bool snippet = true) {
    if (text.empty()) return;
    d.fields.push_back({text.size() > kMaxFieldBytes ? text.substr(0, kMaxFieldBytes) : text, w, snippet});
}

void indexGraph(const Json& graph, const GraphCtx& ctx, std::vector<std::string>& path,
                std::vector<std::string>& labels, std::vector<Doc>& out, int depth) {
    const Json& nodes = graph["nodes"];
    if (!nodes.is_array()) return;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Json& n = nodes[i];
        if (!n.is_object()) continue;
        Doc d;
        d.kind = "node";
        d.nodeId = n.value("id", std::string());
        d.nodeType = n.value("type", std::string());
        std::string label = n.value("label", std::string());
        d.title = label.empty() ? d.nodeId : label;
        d.file = ctx.file;
        d.pipelineId = ctx.pipelineId;
        d.nodePath = path;
        d.subtitle = joinPath(ctx.pipelineName, labels);

        addField(d, d.title, 3.0f, false);
        addField(d, typeWords(d.nodeType), 2.0f, false);
        addField(d, d.nodeId, 0.5f, false);
        addField(d, n.value("templateId", std::string()), 1.0f, false);
        addField(d, d.subtitle, 0.4f, false);

        const Json& params = n["parameters"];
        if (params.is_object()) {
            std::string ptxt;
            for (auto it = params.begin(); it != params.end(); ++it) ptxt += it.key() + " " + scalarText(*it) + "\n";
            addField(d, ptxt, 1.0f);
        }
        addField(d, n.value("customLambdaBody", std::string()), 1.0f);
        const Json& headers = n["requiredHeaders"];
        if (headers.is_array()) {
            std::string h;
            collectStrings(headers, h, 0);
            addField(d, h, 0.5f);
        }
        const Json& agent = n["agentSettings"];
        if (agent.is_object()) {
            std::string a;
            collectStrings(agent, a, 0);
            addField(d, a, 1.0f);
        }
        out.push_back(std::move(d));

        const Json& child = n["childGraph"];
        if (child.is_object() && depth < kMaxGraphDepth) {
            path.push_back(n.value("id", std::string()));
            labels.push_back(label.empty() ? n.value("id", std::string()) : label);
            indexGraph(child, ctx, path, labels, out, depth + 1);
            path.pop_back();
            labels.pop_back();
        }
    }
}

std::vector<Doc> docsFromPipeline(const std::string& content, const GraphCtx& ctx) {
    std::vector<Doc> out;
    auto parsed = jsonutil::tryParse(content);
    if (!parsed || !parsed->is_object()) return out;
    const Json& g = *parsed;

    Doc p;
    p.kind = "pipeline";
    p.file = ctx.file;
    p.pipelineId = ctx.pipelineId;
    std::string gname = g["metadata"].value("graphName", std::string());
    p.title = !ctx.pipelineName.empty() ? ctx.pipelineName : (!gname.empty() ? gname : ctx.pipelineId);
    p.subtitle = ctx.file;
    addField(p, p.title, 3.0f, false);
    addField(p, gname, 1.5f, false);
    addField(p, g["metadata"].value("description", std::string()), 1.0f);
    out.push_back(std::move(p));

    std::vector<std::string> path, labels;
    indexGraph(g, ctx, path, labels, out, 0);
    return out;
}

// Frontmatter simples (name/description) e corpo de um SKILL.md.
std::vector<Doc> docsFromSkill(const std::string& content, const std::string& rel) {
    Doc d;
    d.kind = "skill";
    d.file = rel;
    d.subtitle = rel;

    std::string name, description, body = content;
    if (content.rfind("---", 0) == 0) {
        size_t end = content.find("\n---", 3);
        if (end != std::string::npos) {
            std::istringstream fm(content.substr(3, end - 3));
            std::string line;
            while (std::getline(fm, line)) {
                size_t c = line.find(':');
                if (c == std::string::npos) continue;
                std::string key = trim(line.substr(0, c));
                std::string val = trim(line.substr(c + 1));
                if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') && val.back() == val.front())
                    val = val.substr(1, val.size() - 2);
                if (key == "name") name = val;
                else if (key == "description") description = val;
            }
            body = content.substr(end + 4);
        }
    }
    if (name.empty()) {
        fs::path parent = fs::path(rel).parent_path().filename();
        name = parent.empty() ? "SKILL" : parent.string();
    }
    d.title = name;
    addField(d, name, 3.0f, false);
    addField(d, description, 2.0f);
    addField(d, body, 1.0f);
    return {std::move(d)};
}

Json docToJson(const Doc& d) {
    Json j;
    j["kind"] = d.kind;
    j["title"] = d.title;
    j["subtitle"] = d.subtitle;
    j["file"] = d.file;
    j["pipelineId"] = d.pipelineId;
    j["nodeId"] = d.nodeId;
    j["nodeType"] = d.nodeType;
    Json path = Json::array();
    for (const auto& p : d.nodePath) path.push_back(p);
    j["nodePath"] = path;
    Json fields = Json::array();
    for (const auto& f : d.fields) {
        Json fj;
        fj["t"] = f.text;
        fj["w"] = static_cast<double>(f.weight);
        fj["s"] = f.snippet;
        fields.push_back(fj);
    }
    j["fields"] = fields;
    return j;
}

Doc docFromJson(const Json& j) {
    Doc d;
    d.kind = j.value("kind", std::string());
    d.title = j.value("title", std::string());
    d.subtitle = j.value("subtitle", std::string());
    d.file = j.value("file", std::string());
    d.pipelineId = j.value("pipelineId", std::string());
    d.nodeId = j.value("nodeId", std::string());
    d.nodeType = j.value("nodeType", std::string());
    const Json& path = j["nodePath"];
    for (size_t i = 0; i < path.size(); ++i) d.nodePath.push_back(path[i].get<std::string>());
    const Json& fields = j["fields"];
    for (size_t i = 0; i < fields.size(); ++i)
        d.fields.push_back({fields[i].value("t", std::string()), fields[i].value("w", 1.0f), fields[i].value("s", true)});
    return d;
}

bool skipDir(const std::string& name) {
    return name == "node_modules" || name == ".git" || name == "build" || name == "dist" || name == "__pycache__";
}

struct Source {
    std::string rel;     // caminho relativo (chave)
    bool isSkill = false;
    std::string id, name;
};

void collectSkillFiles(const fs::path& root, const fs::path& dir, std::vector<Source>& out) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (out.size() >= kMaxSkillFiles) break;
        if (it.depth() > 8) { it.disable_recursion_pending(); continue; }
        if (it->is_directory(ec)) {
            if (skipDir(it->path().filename().string())) it.disable_recursion_pending();
            continue;
        }
        std::string fn = it->path().filename().string();
        std::string lower = fn;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower != "skill.md") continue;
        Source s;
        s.rel = fs::relative(it->path(), root, ec).generic_string();
        s.isSkill = true;
        out.push_back(std::move(s));
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Utilitarios publicos
// ---------------------------------------------------------------------------------------------

std::string foldText(const std::string& utf8) {
    std::string out;
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp;
        decodeNext(utf8, i, cp);
        FoldedChar f = foldChar(cp);
        out += f.alnum ? f.text : std::string(1, ' ');
    }
    return out;
}

std::vector<std::string> tokenize(const std::string& utf8) {
    std::vector<std::string> out;
    for (auto& t : tokenizeFull(utf8)) out.push_back(t.base);
    return out;
}

std::string hashHex(const std::string& data) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : data) { h ^= c; h *= 1099511628211ull; }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

// ---------------------------------------------------------------------------------------------
// ProjectSearchIndex
// ---------------------------------------------------------------------------------------------

void ProjectSearchIndex::derive(Doc& d) {
    d.tfWord.clear();
    d.tfTri.clear();
    d.lenWord = d.lenTri = 0.f;
    for (const auto& f : d.fields) {
        for (const auto& t : tokenizeFull(f.text)) {
            d.tfWord[t.base] += f.weight;
            d.lenWord += f.weight;
            for (const auto& tri : trigramsOf(t.raw)) {
                d.tfTri[tri] += f.weight;
                d.lenTri += f.weight;
            }
        }
    }
    d.foldedTitle = foldText(d.title);
}

void ProjectSearchIndex::rebuildInverted() {
    docs_.clear();
    postWord_.clear();
    postTri_.clear();
    double sumW = 0, sumT = 0;
    for (const auto& [rel, fe] : files_)
        for (const auto& d : fe.docs) docs_.push_back(&d);
    for (uint32_t i = 0; i < docs_.size(); ++i) {
        const Doc& d = *docs_[i];
        sumW += d.lenWord;
        sumT += d.lenTri;
        for (const auto& [t, tf] : d.tfWord) postWord_[t].push_back({i, tf});
        for (const auto& [t, tf] : d.tfTri) postTri_[t].push_back({i, tf});
    }
    avgLenWord_ = docs_.empty() ? 1.f : static_cast<float>(std::max(1.0, sumW / docs_.size()));
    avgLenTri_ = docs_.empty() ? 1.f : static_cast<float>(std::max(1.0, sumT / docs_.size()));
}

void ProjectSearchIndex::loadCache(const std::string& root) {
    try {
        fs::path p = fs::path(root) / ".arestrix" / "search-index.json";
        std::string content;
        if (!readText(p, content)) return;
        auto parsed = jsonutil::tryParse(content);
        if (!parsed || parsed->value("version", 0) != 2) return;
        cachedHash_ = parsed->value("projectHash", std::string());
        const Json& files = (*parsed)["files"];
        if (!files.is_object()) return;
        for (auto it = files.begin(); it != files.end(); ++it) {
            FileEntry fe;
            fe.hash = (*it).value("hash", std::string());
            const Json& docs = (*it)["docs"];
            for (size_t i = 0; i < docs.size(); ++i) {
                Doc d = docFromJson(docs[i]);
                derive(d);
                fe.docs.push_back(std::move(d));
            }
            files_[it.key()] = std::move(fe);
        }
    } catch (...) {
        files_.clear();
        cachedHash_.clear();
    }
}

void ProjectSearchIndex::saveCache(const std::string& root) const {
    try {
        Json j;
        j["version"] = 2;
        j["projectHash"] = projectHash_;
        Json files = Json::object();
        for (const auto& [rel, fe] : files_) {
            Json f;
            f["hash"] = fe.hash;
            Json docs = Json::array();
            for (const auto& d : fe.docs) docs.push_back(docToJson(d));
            f["docs"] = docs;
            files[rel] = f;
        }
        j["files"] = files;

        fs::path dir = fs::path(root) / ".arestrix";
        fs::create_directories(dir);
        fs::path tmp = dir / "search-index.json.tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out << j.dump();
        }
        std::error_code ec;
        fs::rename(tmp, dir / "search-index.json", ec);
        if (ec) {
            fs::remove(dir / "search-index.json", ec);
            fs::rename(tmp, dir / "search-index.json", ec);
        }
    } catch (...) {
        // o cache e opcional
    }
}

IndexStats ProjectSearchIndex::refresh(const std::string& root, const std::vector<PipelineSource>& pipelines) {
    std::lock_guard<std::mutex> lock(mutex_);
    IndexStats stats;

    if (root != root_) {
        root_ = root;
        files_.clear();
        docs_.clear();
        projectHash_.clear();
        cachedHash_.clear();
        built_ = false;
        loadCache(root);
    }

    // 1. fontes: pipelines do manifesto, outros *.json em pipelines/ e SKILL.md do workspace
    std::vector<Source> sources;
    std::set<std::string> seen;
    for (const auto& p : pipelines) {
        if (p.file.empty()) continue;
        Source s;
        s.rel = fs::path(p.file).generic_string();
        s.id = p.id;
        s.name = p.name;
        if (seen.insert(s.rel).second) sources.push_back(std::move(s));
    }
    std::error_code ec;
    fs::path pipeDir = fs::path(root) / "pipelines";
    if (fs::is_directory(pipeDir, ec)) {
        for (fs::directory_iterator it(pipeDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
            Source s;
            s.rel = fs::relative(it->path(), root, ec).generic_string();
            s.id = it->path().stem().string();
            s.name = s.id;
            if (seen.insert(s.rel).second) sources.push_back(std::move(s));
        }
    }
    std::vector<Source> skills;
    collectSkillFiles(root, fs::path(root) / ".workspace" / "skills", skills);
    collectSkillFiles(root, fs::path(root) / ".agents", skills);
    for (auto& s : skills)
        if (seen.insert(s.rel).second) sources.push_back(std::move(s));

    // 2. reaproveita o que nao mudou (hash do conteudo) e reindexa o resto
    std::map<std::string, FileEntry> next;
    for (const auto& s : sources) {
        std::string content;
        if (!readText(fs::path(root) / s.rel, content)) continue;
        std::string h = hashHex(content + "\x1f" + s.id + "\x1f" + s.name);
        auto old = files_.find(s.rel);
        if (old != files_.end() && old->second.hash == h) {
            next[s.rel] = std::move(old->second);
            continue;
        }
        FileEntry fe;
        fe.hash = h;
        fe.docs = s.isSkill ? docsFromSkill(content, s.rel) : docsFromPipeline(content, {s.id, s.name, s.rel});
        for (auto& d : fe.docs) derive(d);
        next[s.rel] = std::move(fe);
        ++stats.filesReindexed;
    }
    stats.filesTotal = next.size();

    // 3. hash do projeto = combinacao dos hashes dos arquivos
    std::string combined;
    for (const auto& [rel, fe] : next) combined += rel + '\0' + fe.hash + '\n';
    std::string newHash = hashHex(combined);

    files_ = std::move(next);
    if (built_ && newHash == projectHash_) {
        stats.rebuilt = false;
    } else {
        projectHash_ = newHash;
        rebuildInverted();
        built_ = true;
        stats.rebuilt = true;
        if (newHash != cachedHash_) {
            saveCache(root);
            cachedHash_ = newHash;
        }
    }
    stats.documents = docs_.size();
    stats.projectHash = projectHash_;
    return stats;
}

size_t ProjectSearchIndex::documentCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return docs_.size();
}

std::string ProjectSearchIndex::snippetFor(const Doc& d, const std::unordered_map<std::string, float>& terms) {
    std::string best;
    int bestScore = 0;
    float bestWeight = -1.f;
    for (size_t fi = 0; fi < d.fields.size(); ++fi) {
        const Field& f = d.fields[fi];
        if (!f.snippet) continue; // rotulo, tipo, id... ja aparecem no resultado
        std::istringstream in(f.text);
        std::string line;
        int guard = 0;
        while (std::getline(in, line) && ++guard < 2000) {
            std::string t = trim(line);
            if (t.empty()) continue;
            int score = 0;
            for (const auto& tok : tokenizeFull(t))
                if (terms.count(tok.base)) ++score;
            if (score > bestScore || (score == bestScore && score > 0 && f.weight > bestWeight)) {
                bestScore = score;
                bestWeight = f.weight;
                best = t;
            }
        }
    }
    if (best.empty()) {
        for (size_t fi = 0; fi < d.fields.size() && best.empty(); ++fi) {
            if (!d.fields[fi].snippet) continue;
            std::istringstream in(d.fields[fi].text);
            std::string line;
            while (std::getline(in, line)) {
                std::string t = trim(line);
                if (!t.empty()) { best = t; break; }
            }
        }
    }
    return truncateUtf8(best, 140);
}

std::vector<Hit> ProjectSearchIndex::query(const std::string& text, size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Hit> hits;
    if (docs_.empty() || limit == 0) return hits;

    auto qtoks = tokenizeFull(text);
    if (qtoks.empty()) return hits;

    // termos da consulta: exatos (1.0), sinonimos (0.5) e prefixos do ultimo termo (0.6)
    std::unordered_map<std::string, float> terms;
    auto addTerm = [&](const std::string& t, float w) {
        auto it = terms.find(t);
        if (it == terms.end() || it->second < w) terms[t] = w;
    };
    const SynonymGroups installed = installedSynonymGroups(); // gerados por idioma (SynonymAgent)
    auto expand = [&](const std::vector<std::vector<std::string>>& groups, const std::string& base) {
        for (const auto& grp : groups) {
            if (std::find(grp.begin(), grp.end(), base) == grp.end()) continue;
            for (const auto& w : grp) addTerm(w, 0.5f);
        }
    };
    for (const auto& q : qtoks) {
        addTerm(q.base, 1.0f);
        expand(synonymGroups(), q.base);
        expand(installed, q.base);
    }
    const Tok& last = qtoks.back();
    if (last.raw.size() >= 2) {
        for (const auto& [term, post] : postWord_)
            if (term.size() > last.base.size() && term.compare(0, last.base.size(), last.base) == 0) addTerm(term, 0.6f);
    }

    const double N = static_cast<double>(docs_.size());
    std::vector<double> score(docs_.size(), 0.0);

    for (const auto& [term, w] : terms) {
        auto it = postWord_.find(term);
        if (it == postWord_.end()) continue;
        double df = static_cast<double>(it->second.size());
        double idf = std::log(1.0 + (N - df + 0.5) / (df + 0.5));
        for (const auto& [di, tf] : it->second) {
            double norm = tf * (kK1 + 1.0) / (tf + kK1 * (1.0 - kB + kB * docs_[di]->lenWord / avgLenWord_));
            score[di] += w * idf * norm;
        }
    }

    // trigramas: tolera erro de digitacao e termos parciais
    std::set<std::string> qtris;
    for (const auto& q : qtoks)
        for (auto& tri : trigramsOf(q.raw)) qtris.insert(tri);
    if (!qtris.empty()) {
        std::vector<double> tri(docs_.size(), 0.0);
        std::vector<int> matched(docs_.size(), 0);
        for (const auto& t : qtris) {
            auto it = postTri_.find(t);
            if (it == postTri_.end()) continue;
            double df = static_cast<double>(it->second.size());
            double idf = std::log(1.0 + (N - df + 0.5) / (df + 0.5));
            for (const auto& [di, tf] : it->second) {
                double norm = tf * (kK1 + 1.0) / (tf + kK1 * (1.0 - kB + kB * docs_[di]->lenTri / avgLenTri_));
                tri[di] += idf * norm;
                ++matched[di];
            }
        }
        const double total = static_cast<double>(qtris.size());
        for (size_t i = 0; i < docs_.size(); ++i)
            if (matched[i] / total >= 0.5) score[i] += 0.6 * tri[i] / total;
    }

    // consulta contida no titulo (busca enquanto digita)
    std::string foldedQuery;
    {
        std::string f = foldText(text);
        // normaliza espacos
        bool sp = true;
        for (char c : f) {
            if (c == ' ') { if (!sp) foldedQuery += ' '; sp = true; }
            else { foldedQuery += c; sp = false; }
        }
        while (!foldedQuery.empty() && foldedQuery.back() == ' ') foldedQuery.pop_back();
    }
    if (foldedQuery.size() >= 2) {
        for (size_t i = 0; i < docs_.size(); ++i) {
            // titulo e comparado sem pontuacao, com espacos normalizados
            std::string ft;
            bool sp = true;
            for (char c : docs_[i]->foldedTitle) {
                if (c == ' ') { if (!sp) ft += ' '; sp = true; }
                else { ft += c; sp = false; }
            }
            size_t pos = ft.find(foldedQuery);
            if (pos == std::string::npos) continue;
            score[i] += 3.0 + (pos == 0 ? 2.0 : 0.0) + (ft == foldedQuery ? 3.0 : 0.0);
        }
    }

    std::vector<uint32_t> order;
    for (uint32_t i = 0; i < docs_.size(); ++i)
        if (score[i] >= 0.25) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (score[a] != score[b]) return score[a] > score[b];
        return docs_[a]->title < docs_[b]->title;
    });
    if (order.size() > limit) order.resize(limit);

    for (uint32_t i : order) {
        const Doc& d = *docs_[i];
        Hit h;
        h.kind = d.kind;
        h.title = d.title;
        h.subtitle = d.subtitle;
        h.snippet = snippetFor(d, terms);
        h.file = d.file;
        h.pipelineId = d.pipelineId;
        h.nodeId = d.nodeId;
        h.nodeType = d.nodeType;
        h.nodePath = d.nodePath;
        h.score = std::round(score[i] * 1000.0) / 1000.0;
        hits.push_back(std::move(h));
    }
    return hits;
}

} // namespace search
} // namespace taskforge
