#include "rt/Decision.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

#include "rt/Runtime.hpp"
#include "rt/Tools.hpp"
#include "search/ProjectSearch.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

constexpr size_t kMaxTextBytes = 12000;

// Minusculo, sem acentos, sem pontuacao e com espacos simples; sempre com um espaco nas pontas para
// permitir a busca por palavra inteira.
std::string normalizeForMatch(const std::string& s) {
    std::string folded = search::foldText(s);
    std::string out = " ";
    for (char c : folded) {
        if (c == ' ') {
            if (out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

bool containsWord(const std::string& paddedText, const std::string& paddedNeedle) {
    return paddedNeedle.size() > 2 && paddedText.find(paddedNeedle) != std::string::npos;
}

std::string truncateText(const std::string& s) {
    if (s.size() <= kMaxTextBytes) return s;
    size_t cut = kMaxTextBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

// Texto que alimenta a decisao: arquivo do workspace ou resposta final dos vertices anteriores.
std::string gatherText(const Json& source, const std::string& label) {
    fs::path root = workspaceRoot();
    std::string kind = source.value("kind", std::string("previous"));
    if (kind == "file") {
        std::string rel = source.value("path", std::string());
        auto p = resolveInWorkspace(root, rel);
        std::error_code ec;
        if (rel.empty() || !p || !fs::is_regular_file(*p, ec)) {
            throw std::runtime_error("Switch '" + label + "': arquivo de entrada nao encontrado no projeto: " +
                                     (rel.empty() ? std::string("(nao informado)") : rel));
        }
        return truncateText(jsonutil::readFile(*p));
    }
    std::string text;
    const Json& from = source["from"];
    for (size_t i = 0; i < from.size(); ++i) {
        if (!from[i].is_string() || !safeId(from[i].str())) continue;
        fs::path f = root / ".arestrix" / "out" / (from[i].str() + ".txt");
        std::error_code ec;
        if (!fs::is_regular_file(f, ec)) continue;
        if (!text.empty()) text += "\n\n";
        text += jsonutil::readFile(f);
    }
    if (text.empty()) {
        throw std::runtime_error("Switch '" + label + "': o vertice anterior nao produziu texto. Ligue-o a um agente, "
                                 "ou escolha 'um arquivo do projeto' como entrada.");
    }
    return truncateText(text);
}

} // namespace

int pickByKeywords(const std::string& text, const std::vector<std::vector<std::string>>& rules, int defaultIndex) {
    const std::string padded = normalizeForMatch(text);
    for (size_t i = 0; i < rules.size(); ++i) {
        for (const auto& kw : rules[i]) {
            std::string needle = normalizeForMatch(kw);
            if (containsWord(padded, needle)) return static_cast<int>(i);
        }
    }
    return defaultIndex;
}

int matchLabel(const std::string& reply, const std::vector<std::string>& labels, int defaultIndex) {
    const std::string r = normalizeForMatch(reply);
    int best = -1;
    size_t bestLen = 0;
    for (size_t i = 0; i < labels.size(); ++i) {
        std::string l = normalizeForMatch(labels[i]);
        if (l.size() <= 2) continue;
        if (l == r) return static_cast<int>(i); // igualdade exata
        if (containsWord(r, l) && l.size() > bestLen) {
            best = static_cast<int>(i);
            bestLen = l.size();
        }
    }
    return best >= 0 ? best : defaultIndex;
}

std::string decisionSystemPrompt() {
    return "You are the routing step of an automated workflow. You receive a text and a list of options. "
           "Reply with exactly one option, copied exactly as written, and nothing else: no explanation, "
           "no punctuation, no quotes.";
}

std::string decisionUserPrompt(const std::string& instruction, const std::vector<std::string>& labels,
                               const std::string& text) {
    std::string o;
    o += instruction.empty() ? std::string("Choose the option that best describes the text.") : instruction;
    o += "\n\nOptions:\n";
    for (size_t i = 0; i < labels.size(); ++i) {
        o += "- " + labels[i];
        if (i + 1 == labels.size() && labels.size() > 1) o += "   (use this one when none of the others fits)";
        o += "\n";
    }
    o += "\nText:\n<<<\n" + text + "\n>>>\n\nAnswer with one option only.";
    return o;
}

int decideCase(const std::string& nodeId, const std::string& label, const std::string& specJson) {
    (void)nodeId;
    Json spec = jsonutil::parse(specJson);
    std::vector<std::string> labels;
    const Json& jl = spec["labels"];
    for (size_t i = 0; i < jl.size(); ++i) labels.push_back(jl[i].get<std::string>());
    if (labels.empty()) return 0;
    const int def = static_cast<int>(labels.size()) - 1;

    const std::string mode = spec.value("mode", std::string("model"));
    const std::string text = gatherText(spec["source"], label);

    int idx = def;
    if (mode == "rules") {
        std::vector<std::vector<std::string>> rules;
        const Json& jr = spec["rules"];
        for (size_t i = 0; i < jr.size(); ++i) {
            std::vector<std::string> kws;
            for (size_t k = 0; k < jr[i].size(); ++k) kws.push_back(jr[i][k].get<std::string>());
            rules.push_back(std::move(kws));
        }
        idx = pickByKeywords(text, rules, def);
    } else {
        CompletionRequest req;
        req.provider = spec.value("provider", std::string("deepseek"));
        req.model = spec.value("model", std::string());
        req.system = decisionSystemPrompt();
        req.user = decisionUserPrompt(spec.value("instruction", std::string()), labels, text);
        req.maxTokens = 256;
        req.maxAttempts = 3;
        if (spec["temperature"].is_number()) {
            req.hasTemperature = true;
            req.temperature = spec["temperature"].get<double>();
        }
        if (req.model.empty()) {
            throw std::runtime_error("Switch '" + label + "': nenhum modelo configurado (use o botao Modelo do vertice "
                                     "ou os Padroes Globais).");
        }
        CompletionResult res = completeText(req);
        if (!res.ok) throw std::runtime_error("Switch '" + label + "': falha ao consultar o modelo: " + res.error);
        idx = matchLabel(res.text, labels, def);
        if (idx == def && matchLabel(res.text, labels, -1) < 0) {
            std::cout << "[Switch " << label << "] resposta nao reconhecida; seguindo pelo default.\n";
        }
    }
    std::cout << "[Switch " << label << "] caminho: " << labels[static_cast<size_t>(idx)] << "\n";
    return idx;
}

} // namespace rt
} // namespace taskforge
