#pragma once

// Busca no projeto aberto (todos os pipelines, subfluxos e skills do workspace).
//
// Ranking lexical com normalizacao linguistica, sem modelo de IA e sem rede:
//   * texto sem acentos, minusculo, quebrado em camelCase/snake_case, sem stopwords (pt/en),
//     com stemming leve;
//   * BM25F: cada campo do vertice tem um peso (rotulo > tipo > prompt/codigo);
//   * trigramas de caracteres para tolerar erros de digitacao e termos parciais;
//   * prefixo no ultimo termo (busca enquanto digita) e pequenos grupos de sinonimos pt/en.
//
// O indice e reconstruido de forma incremental: cada arquivo tem um hash de conteudo, e o projeto
// tem um hash que e a combinacao dos arquivos. Se o hash do projeto nao mudou desde a ultima
// indexacao, nada e refeito. O estado fica em <raiz>/.arestrix/search-index.json.

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace taskforge {
namespace search {

struct PipelineSource {
    std::string id;
    std::string name;
    std::string file; // relativo a raiz do projeto
};

struct Hit {
    std::string kind;     // "node" | "skill" | "pipeline"
    std::string title;
    std::string subtitle; // caminho legivel: pipeline > subfluxo
    std::string snippet;
    std::string file;
    std::string pipelineId;
    std::string nodeId;
    std::string nodeType;
    std::vector<std::string> nodePath; // ids dos subfluxos/modulos que levam ao vertice
    double score = 0.0;
};

struct IndexStats {
    bool rebuilt = false;        // false: o hash do projeto nao mudou, nada foi refeito
    size_t filesTotal = 0;
    size_t filesReindexed = 0;   // arquivos cujo conteudo mudou (ou novos)
    size_t documents = 0;
    std::string projectHash;
};

// Utilitarios expostos para teste.
std::string foldText(const std::string& utf8);                 // minusculo, sem acentos
std::vector<std::string> tokenize(const std::string& utf8);    // termos normalizados (com stem)
std::string hashHex(const std::string& data);                  // FNV-1a 64 bits

class ProjectSearchIndex {
public:
    // Reindexa o que mudou. Se `root` for diferente da chamada anterior, descarta o estado em memoria
    // e tenta carregar o cache do disco.
    IndexStats refresh(const std::string& root, const std::vector<PipelineSource>& pipelines);

    std::vector<Hit> query(const std::string& text, size_t limit = 20) const;

    size_t documentCount() const;

    // Documento indexavel (publico para os extratores em ProjectSearch.cpp).
    struct Field {
        std::string text;
        float weight;
        bool snippet = true; // pode aparecer como trecho no resultado
    };
    struct Doc {
        std::string kind, title, subtitle, file, pipelineId, nodeId, nodeType;
        std::vector<std::string> nodePath;
        std::vector<Field> fields;
        // derivados (nao persistidos)
        std::unordered_map<std::string, float> tfWord;
        std::unordered_map<std::string, float> tfTri;
        float lenWord = 0.f;
        float lenTri = 0.f;
        std::string foldedTitle;
    };
    struct FileEntry {
        std::string hash;
        std::vector<Doc> docs;
    };

private:
    void rebuildInverted();
    static void derive(Doc& d);
    static std::string snippetFor(const Doc& d, const std::unordered_map<std::string, float>& terms);
    void loadCache(const std::string& root);
    void saveCache(const std::string& root) const;

    mutable std::mutex mutex_;
    std::string root_;
    std::string projectHash_;
    std::string cachedHash_;   // hash gravado no disco (para saber se precisa regravar)
    bool built_ = false;       // indice invertido consistente com files_
    std::map<std::string, FileEntry> files_; // por caminho relativo

    // Indice invertido (derivado)
    std::vector<const Doc*> docs_;
    std::unordered_map<std::string, std::vector<std::pair<uint32_t, float>>> postWord_;
    std::unordered_map<std::string, std::vector<std::pair<uint32_t, float>>> postTri_;
    float avgLenWord_ = 1.f;
    float avgLenTri_ = 1.f;
};

} // namespace search
} // namespace taskforge
