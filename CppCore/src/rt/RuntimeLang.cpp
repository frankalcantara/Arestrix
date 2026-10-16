#include "rt/RuntimeLang.hpp"

#include <cctype>
#include <cstring>

namespace taskforge {
namespace rt {

namespace {

std::string lowerAscii(const std::string& s) {
    std::string t = s;
    for (char& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t;
}

// Palavras fortes e exclusivas de cada lingua (>= 3 letras; nada de palavras ambiguas como "use", "no", "as").
const char* const kPtWords[] = {
    "você", "voce", "não", "nao", "uma", "para", "como", "será", "sera", "instruções", "instrucoes",
    "arquivo", "arquivos", "diretório", "diretorio", "ferramenta", "ferramentas", "tarefa", "execução",
    "execucao", "resumo", "sempre", "também", "tambem", "apenas", "devolve", "caminho", "conteúdo",
    "conteudo", "deve", "devem", "seja", "quando", "depois", "antes", "gravar", "gravado", "ler",
    "editar", "sua", "suas", "após", "apos", "criar", "criado", "cada", "entre", "este", "esta"
};

const char* const kEnWords[] = {
    "the", "and", "your", "you", "are", "file", "files", "directory", "tool", "tools", "task", "tasks",
    "summary", "output", "input", "please", "should", "with", "this", "that", "instructions", "content",
    "path", "make", "sure", "write", "read", "edit", "every", "before", "after", "never", "always",
    "working", "relative", "reply", "done", "error", "result", "results", "check", "run", "create"
};

int countWord(const std::string& lowerText, const char* word) {
    const size_t len = std::strlen(word);
    if (len < 3) return 0;
    int hits = 0;
    size_t pos = 0;
    while ((pos = lowerText.find(word, pos)) != std::string::npos) {
        const bool leftOk = pos == 0 || !std::isalnum(static_cast<unsigned char>(lowerText[pos - 1]));
        const bool rightOk = pos + len >= lowerText.size() ||
                             !std::isalnum(static_cast<unsigned char>(lowerText[pos + len]));
        if (leftOk && rightOk) ++hits;
        pos += len;
    }
    return hits;
}

// Pontuacao de idioma: > 0 = ingles, < 0 = portugues, 0 = sem sinal (abstencao na agregacao).
int langScore(const std::string& text) {
    if (text.empty()) return 0;

    int pt = 0;
    int en = 0;

    // Acentos (bytes altos do UTF-8) sao um sinal forte e barato de portugues.
    for (unsigned char c : text)
        if (c >= 0xC0) ++pt;

    const std::string t = lowerAscii(text);
    for (const char* w : kPtWords) pt += countWord(t, w);
    for (const char* w : kEnWords) en += countWord(t, w);

    return en - pt;
}

} // namespace

RuntimeLang detectRuntimeLang(const std::string& text) {
    return langScore(text) > 0 ? RuntimeLang::En : RuntimeLang::Pt;
}

RuntimeLang detectRuntimeLang(const std::vector<std::string>& texts) {
    long long score = 0;
    for (const auto& t : texts) score += langScore(t);
    return score > 0 ? RuntimeLang::En : RuntimeLang::Pt;
}

} // namespace rt
} // namespace taskforge
