#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "util/Json.hpp"

namespace taskforge {
namespace rt {
namespace compress {

// Token com o intervalo de bytes correspondente no texto original.
struct ProseToken {
    int id = 0;
    size_t start = 0;
    size_t end = 0;
    bool special = false;
};

// Tokenizador compativel com o `tokenizer.json` do HuggingFace:
// - BPE + ByteLevel (ModernBERT/Kompress), com added_tokens e merges;
// - WordPiece (BERT classico), mantido para outros modelos.
//
// Limitacao conhecida: a normalizacao NFC e tratada como identidade (texto pre-composto
// funciona; sequencias com marcas combinantes podem divergir do HF). Strip de acentos no
// WordPiece cobre Latin-1/Extended-A.
class ProseTokenizer {
public:
    static std::optional<ProseTokenizer> fromJson(const Json& tokenizerJson);
    static std::optional<ProseTokenizer> fromJsonFile(const std::string& path);

    std::vector<ProseToken> tokenize(std::string_view text, size_t maxTokens) const;

    int clsId() const { return clsId_; }
    int sepId() const { return sepId_; }
    int unkId() const { return unkId_; }
    size_t vocabSize() const { return vocab_.size(); }
    bool isByteLevelBpe() const { return bpe_; }

private:
    struct AddedToken {
        std::string content;
        int id = 0;
        bool special = false;
    };

    std::string normalize(std::string_view text, std::vector<size_t>& startMap,
                          std::vector<size_t>& endMap) const;
    std::vector<ProseToken> wordPiece(std::string_view word, size_t originalStart, size_t originalEnd,
                                      size_t maxTokens) const;
    std::vector<ProseToken> tokenizeBpe(std::string_view text, size_t maxTokens) const;
    std::vector<ProseToken> bpeSegment(std::string_view text, size_t start, size_t end,
                                       size_t maxTokens) const;

    std::unordered_map<std::string, int> vocab_;
    std::unordered_map<std::string, int> mergeRanks_;
    std::vector<AddedToken> added_;
    int unkId_ = 0;
    int clsId_ = -1;
    int sepId_ = -1;
    bool bpe_ = false;
    bool lowercase_ = true;
    bool stripAccents_ = true;
    bool cleanText_ = true;
    std::string contPrefix_ = "##";
    size_t maxCharsPerWord_ = 100;
};

} // namespace compress
} // namespace rt
} // namespace taskforge
