#include "rt/compress/ProseTokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <fstream>
#include <iterator>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

struct Codepoint {
    uint32_t cp = 0;
    size_t start = 0;
    size_t end = 0;
};

std::vector<Codepoint> decodeUtf8(std::string_view text) {
    std::vector<Codepoint> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t cp = c;
        size_t len = 1;
        if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            len = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(text[i + 3]) & 0x3F);
            len = 4;
        }
        out.push_back(Codepoint{cp, i, i + len});
        i += len;
    }
    return out;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
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

bool isSpaceCp(uint32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x00A0;
}

bool isCjk(uint32_t cp) {
    return (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF);
}

bool isAsciiPunct(uint32_t cp) {
    return cp < 0x80 && std::ispunct(static_cast<int>(cp)) != 0;
}

bool isPunctCp(uint32_t cp) {
    if (isAsciiPunct(cp)) return true;
    return (cp >= 0x2010 && cp <= 0x2027) || (cp >= 0x2030 && cp <= 0x205E) || cp == 0x00AB ||
           cp == 0x00BB || cp == 0x00BF || cp == 0x00A1;
}

uint32_t toLower(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 32;
    if (cp >= 0x0100 && cp <= 0x017F) {
        if (cp == 0x0130) return 'i';
        if (cp % 2 == 0) return cp + 1;
    }
    return cp;
}

uint32_t stripAccent(uint32_t cp) {
    switch (cp) {
        case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: case 0x00C4: case 0x00C5: return 'a';
        case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: case 0x00E4: case 0x00E5: return 'a';
        case 0x00C7: case 0x00E7: return 'c';
        case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: return 'e';
        case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
        case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: return 'i';
        case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
        case 0x00D1: case 0x00F1: return 'n';
        case 0x00D2: case 0x00D3: case 0x00D4: case 0x00D5: case 0x00D6: return 'o';
        case 0x00F2: case 0x00F3: case 0x00F4: case 0x00F5: case 0x00F6: return 'o';
        case 0x00D9: case 0x00DA: case 0x00DB: case 0x00DC: return 'u';
        case 0x00F9: case 0x00FA: case 0x00FB: case 0x00FC: return 'u';
        case 0x00DD: case 0x00FD: case 0x00FF: return 'y';
        default: return cp;
    }
}

// Tabela GPT-2 de bytes -> caracteres unicode (ByteLevel).
const std::vector<uint32_t>& byteToUnicode() {
    static const std::vector<uint32_t> table = [] {
        std::vector<uint32_t> t(256, 0);
        std::vector<int> bs;
        for (int b = 33; b <= 126; ++b) bs.push_back(b);
        for (int b = 161; b <= 172; ++b) bs.push_back(b);
        for (int b = 174; b <= 255; ++b) bs.push_back(b);
        std::vector<int> cs = bs;
        int n = 0;
        for (int b = 0; b < 256; ++b) {
            if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
                bs.push_back(b);
                cs.push_back(256 + n);
                ++n;
            }
        }
        for (size_t i = 0; i < bs.size(); ++i) t[static_cast<size_t>(bs[i])] = static_cast<uint32_t>(cs[i]);
        return t;
    }();
    return table;
}

bool isLetterCp(uint32_t cp) {
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
    return cp >= 0x80 && !isPunctCp(cp) && !isSpaceCp(cp) && !(cp >= '0' && cp <= '9');
}

bool isDigitCp(uint32_t cp) {
    return cp >= '0' && cp <= '9';
}

enum class RunKind { Space, Letter, Digit, Other };

RunKind kindOf(uint32_t cp) {
    if (isSpaceCp(cp)) return RunKind::Space;
    if (isLetterCp(cp)) return RunKind::Letter;
    if (isDigitCp(cp)) return RunKind::Digit;
    return RunKind::Other;
}

bool contractionAt(std::string_view text, size_t i, size_t& length) {
    static const char* kSuffixes[] = {"'s", "'t", "'re", "'ve", "'m", "'ll", "'d"};
    for (const char* suffix : kSuffixes) {
        const size_t len = std::char_traits<char>::length(suffix);
        if (i + len <= text.size() && text.compare(i, len, suffix) == 0) {
            length = len;
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<ProseTokenizer> ProseTokenizer::fromJson(const Json& tokenizerJson) {
    if (!tokenizerJson.is_object() || !tokenizerJson.contains("model")) return std::nullopt;
    const Json& model = tokenizerJson["model"];
    if (!model.is_object()) return std::nullopt;

    ProseTokenizer tokenizer;
    if (!model.contains("vocab") || !model["vocab"].is_object()) return std::nullopt;
    for (auto it = model["vocab"].begin(); it != model["vocab"].end(); ++it) {
        if (!it.value().is_number_integer()) continue;
        tokenizer.vocab_[it.key()] = it.value().get<int>();
    }
    if (tokenizer.vocab_.empty()) return std::nullopt;

    const std::string type = model.value("type", std::string());
    tokenizer.bpe_ = type == "BPE";
    if (!tokenizer.bpe_ && type != "WordPiece") return std::nullopt;

    if (tokenizer.bpe_) {
        if (!model.contains("merges") || !model["merges"].is_array()) return std::nullopt;
        int rank = 0;
        for (const auto& merge : model["merges"]) {
            std::string key;
            if (merge.is_array() && merge.size() == 2 && merge[0].is_string() && merge[1].is_string()) {
                key = merge[0].get<std::string>() + " " + merge[1].get<std::string>();
            } else if (merge.is_string()) {
                key = merge.get<std::string>();
            } else {
                continue;
            }
            tokenizer.mergeRanks_[key] = rank++;
        }
        if (tokenizer.mergeRanks_.empty()) return std::nullopt;
    } else {
        const std::string unk = model.value("unk_token", std::string("[UNK]"));
        auto unkIt = tokenizer.vocab_.find(unk);
        tokenizer.unkId_ = unkIt != tokenizer.vocab_.end() ? unkIt->second : 0;
        auto clsIt = tokenizer.vocab_.find("[CLS]");
        auto sepIt = tokenizer.vocab_.find("[SEP]");
        tokenizer.clsId_ = clsIt != tokenizer.vocab_.end() ? clsIt->second : -1;
        tokenizer.sepId_ = sepIt != tokenizer.vocab_.end() ? sepIt->second : -1;
        tokenizer.contPrefix_ = model.value("continuing_subword_prefix", std::string("##"));
        tokenizer.maxCharsPerWord_ = static_cast<size_t>(
            std::max<int64_t>(1, std::min<int64_t>(1000, model.value("max_input_chars_per_word", 100))));

        if (tokenizerJson.contains("normalizer") && tokenizerJson["normalizer"].is_object()) {
            const Json& normalizer = tokenizerJson["normalizer"];
            tokenizer.cleanText_ = normalizer.value("clean_text", true);
            tokenizer.lowercase_ = normalizer.value("lowercase", true);
            if (normalizer.contains("strip_accents")) {
                if (normalizer["strip_accents"].is_boolean()) {
                    tokenizer.stripAccents_ = normalizer["strip_accents"].get<bool>();
                } else {
                    tokenizer.stripAccents_ = tokenizer.lowercase_;
                }
            }
        }
    }

    // added_tokens: especiais (CLS/SEP/UNK) e tokens fixos que o BPE deve respeitar.
    if (tokenizerJson.contains("added_tokens") && tokenizerJson["added_tokens"].is_array()) {
        for (const auto& item : tokenizerJson["added_tokens"]) {
            if (!item.is_object()) continue;
            const std::string content = item.value("content", std::string());
            if (content.empty()) continue;
            ProseTokenizer::AddedToken token;
            token.content = content;
            token.id = item.value("id", 0);
            token.special = item.value("special", false);
            tokenizer.added_.push_back(std::move(token));
        }
        // Mais longos primeiro: um run de espacos nao deve ser capturado por um prefixo.
        std::sort(tokenizer.added_.begin(), tokenizer.added_.end(),
                  [](const AddedToken& a, const AddedToken& b) { return a.content.size() > b.content.size(); });
        for (const auto& added : tokenizer.added_) {
            if (added.content == "[CLS]") tokenizer.clsId_ = added.id;
            else if (added.content == "[SEP]") tokenizer.sepId_ = added.id;
            else if (added.content == "[UNK]") tokenizer.unkId_ = added.id;
        }
    }
    if (tokenizerJson.contains("post_processor") && tokenizerJson["post_processor"].is_object()) {
        const Json& post = tokenizerJson["post_processor"];
        if (post.contains("special_tokens") && post["special_tokens"].is_object()) {
            const Json& specials = post["special_tokens"];
            auto pick = [&](const char* name) -> int {
                if (!specials.contains(name) || !specials[name].is_object()) return -1;
                const Json& ids = specials[name]["ids"];
                return ids.is_array() && ids.size() > 0 && ids[0].is_number_integer() ? ids[0].get<int>() : -1;
            };
            const int cls = pick("[CLS]");
            const int sep = pick("[SEP]");
            const int unk = pick("[UNK]");
            if (cls >= 0) tokenizer.clsId_ = cls;
            if (sep >= 0) tokenizer.sepId_ = sep;
            if (unk >= 0) tokenizer.unkId_ = unk;
        }
    }

    return tokenizer;
}

std::optional<ProseTokenizer> ProseTokenizer::fromJsonFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto parsed = jsonutil::tryParse(content);
    if (!parsed) return std::nullopt;
    return fromJson(*parsed);
}

std::string ProseTokenizer::normalize(std::string_view text, std::vector<size_t>& startMap,
                                      std::vector<size_t>& endMap) const {
    std::string out;
    out.reserve(text.size());
    startMap.clear();
    endMap.clear();
    startMap.reserve(text.size());
    endMap.reserve(text.size());

    auto append = [&](uint32_t cp, size_t originalStart, size_t originalEnd) {
        const size_t before = out.size();
        appendUtf8(out, cp);
        for (size_t i = before; i < out.size(); ++i) {
            startMap.push_back(originalStart);
            endMap.push_back(originalEnd);
        }
    };

    for (const auto& c : decodeUtf8(text)) {
        uint32_t cp = c.cp;
        if (cleanText_ && (cp == 0 || cp == 0xFFFD || (cp < 0x20 && cp != '\t' && cp != '\n' && cp != '\r'))) {
            cp = ' ';
        }
        if (isSpaceCp(cp)) {
            append(' ', c.start, c.end);
            continue;
        }
        if (lowercase_) cp = toLower(cp);
        if (stripAccents_) cp = stripAccent(cp);
        if (isCjk(cp)) {
            append(' ', c.start, c.start);
            append(cp, c.start, c.end);
            append(' ', c.end, c.end);
            continue;
        }
        append(cp, c.start, c.end);
    }
    return out;
}

std::vector<ProseToken> ProseTokenizer::wordPiece(std::string_view word, size_t originalStart,
                                                  size_t originalEnd, size_t maxTokens) const {
    std::vector<ProseToken> tokens;
    if (word.empty()) return tokens;
    if (word.size() > maxCharsPerWord_) {
        tokens.push_back(ProseToken{unkId_, originalStart, originalEnd, false});
        return tokens;
    }

    const size_t wordLen = word.size();
    auto offsetAt = [&](size_t index) {
        if (wordLen == 0) return originalStart;
        return originalStart + (originalEnd - originalStart) * index / wordLen;
    };

    size_t start = 0;
    bool first = true;
    while (start < word.size()) {
        size_t end = word.size();
        bool found = false;
        while (end > start) {
            std::string piece(word.substr(start, end - start));
            if (!first) piece = contPrefix_ + piece;
            auto it = vocab_.find(piece);
            if (it != vocab_.end()) {
                tokens.push_back(ProseToken{it->second, offsetAt(start), offsetAt(end), false});
                start = end;
                first = false;
                found = true;
                break;
            }
            --end;
        }
        if (!found) {
            tokens.push_back(ProseToken{unkId_, originalStart, originalEnd, false});
            break;
        }
        if (maxTokens > 0 && tokens.size() >= maxTokens) break;
    }
    return tokens;
}

std::vector<ProseToken> ProseTokenizer::bpeSegment(std::string_view text, size_t start, size_t end,
                                                   size_t maxTokens) const {
    struct Symbol {
        std::string text;
        size_t start;
        size_t end;
    };

    std::vector<Symbol> symbols;
    symbols.reserve(end - start);
    const auto& table = byteToUnicode();
    for (size_t i = start; i < end; ++i) {
        std::string sym;
        appendUtf8(sym, table[static_cast<unsigned char>(text[i])]);
        symbols.push_back(Symbol{std::move(sym), i, i + 1});
    }

    while (symbols.size() > 1) {
        int bestRank = INT_MAX;
        for (size_t i = 0; i + 1 < symbols.size(); ++i) {
            auto it = mergeRanks_.find(symbols[i].text + " " + symbols[i + 1].text);
            if (it != mergeRanks_.end() && it->second < bestRank) bestRank = it->second;
        }
        if (bestRank == INT_MAX) break;

        std::vector<Symbol> next;
        next.reserve(symbols.size());
        for (size_t i = 0; i < symbols.size();) {
            if (i + 1 < symbols.size()) {
                auto it = mergeRanks_.find(symbols[i].text + " " + symbols[i + 1].text);
                if (it != mergeRanks_.end() && it->second == bestRank) {
                    next.push_back(Symbol{symbols[i].text + symbols[i + 1].text, symbols[i].start,
                                          symbols[i + 1].end});
                    i += 2;
                    continue;
                }
            }
            next.push_back(std::move(symbols[i]));
            ++i;
        }
        symbols.swap(next);
    }

    std::vector<ProseToken> tokens;
    tokens.reserve(symbols.size());
    for (const auto& symbol : symbols) {
        if (maxTokens > 0 && tokens.size() >= maxTokens) break;
        auto it = vocab_.find(symbol.text);
        tokens.push_back(ProseToken{it != vocab_.end() ? it->second : unkId_, symbol.start, symbol.end, false});
    }
    return tokens;
}

std::vector<ProseToken> ProseTokenizer::tokenizeBpe(std::string_view text, size_t maxTokens) const {
    std::vector<ProseToken> tokens;
    const std::vector<Codepoint> codepoints = decodeUtf8(text);
    std::vector<size_t> indexByStart(text.size() + 1, SIZE_MAX);
    for (size_t k = 0; k < codepoints.size(); ++k) indexByStart[codepoints[k].start] = k;
    size_t i = 0;   // byte offset

    auto findCp = [&](size_t offset) -> const Codepoint* {
        if (offset >= indexByStart.size()) return nullptr;
        const size_t index = indexByStart[offset];
        return index == SIZE_MAX ? nullptr : &codepoints[index];
    };

    auto appendSegment = [&](size_t start, size_t end) {
        if (start >= end) return;
        auto pieces = bpeSegment(text, start, end, maxTokens == 0 ? 0 : maxTokens - tokens.size());
        tokens.insert(tokens.end(), pieces.begin(), pieces.end());
    };

    while (i < text.size()) {
        // 1. added_tokens (especiais e runs de espaco fixos).
        const AddedToken* match = nullptr;
        for (const auto& added : added_) {
            if (i + added.content.size() <= text.size() &&
                text.compare(i, added.content.size(), added.content) == 0) {
                match = &added;
                break;
            }
        }
        if (match) {
            tokens.push_back(ProseToken{match->id, i, i + match->content.size(), match->special});
            i += match->content.size();
            if (maxTokens > 0 && tokens.size() >= maxTokens) break;
            continue;
        }

        // 2. Contracao no estilo GPT-2 ('s, 't, 're...).
        size_t contractionLength = 0;
        if (contractionAt(text, i, contractionLength)) {
            appendSegment(i, i + contractionLength);
            i += contractionLength;
            if (maxTokens > 0 && tokens.size() >= maxTokens) break;
            continue;
        }

        // 3. Runs por categoria, com no maximo UM espaco absorvido pelo run seguinte.
        const Codepoint* current = findCp(i);
        if (!current) break;

        const RunKind kind = kindOf(current->cp);
        if (kind == RunKind::Space) {
            size_t j = i;
            while (j < text.size()) {
                const Codepoint* c = findCp(j);
                if (!c || kindOf(c->cp) != RunKind::Space) break;
                j = c->end;
            }
            const bool singleSpace = (j == current->end);
            const Codepoint* next = j < text.size() ? findCp(j) : nullptr;
            if (singleSpace && next && kindOf(next->cp) != RunKind::Space) {
                const RunKind nextKind = kindOf(next->cp);
                size_t k = j;
                while (k < text.size()) {
                    const Codepoint* c = findCp(k);
                    if (!c || kindOf(c->cp) != nextKind) break;
                    k = c->end;
                }
                appendSegment(i, k);
                i = k;
            } else {
                appendSegment(i, j);
                i = j;
            }
            if (maxTokens > 0 && tokens.size() >= maxTokens) break;
            continue;
        }

        size_t j = i;
        while (j < text.size()) {
            const Codepoint* c = findCp(j);
            if (!c || kindOf(c->cp) != kind) break;
            j = c->end;
        }
        appendSegment(i, j);
        i = j;
        if (maxTokens > 0 && tokens.size() >= maxTokens) break;
    }
    return tokens;
}

std::vector<ProseToken> ProseTokenizer::tokenize(std::string_view text, size_t maxTokens) const {
    if (bpe_) return tokenizeBpe(text, maxTokens);

    std::vector<size_t> startMap;
    std::vector<size_t> endMap;
    const std::string normalized = normalize(text, startMap, endMap);
    if (normalized.empty()) return {};

    std::vector<ProseToken> tokens;
    size_t i = 0;
    while (i < normalized.size()) {
        const unsigned char c = static_cast<unsigned char>(normalized[i]);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }
        if (c < 0x80 && std::ispunct(static_cast<int>(c))) {
            const size_t s = startMap[i];
            const size_t e = endMap[i];
            std::string piece(1, static_cast<char>(c));
            auto it = vocab_.find(piece);
            tokens.push_back(ProseToken{it != vocab_.end() ? it->second : unkId_, s, e, false});
            ++i;
            if (maxTokens > 0 && tokens.size() >= maxTokens) break;
            continue;
        }
        size_t j = i;
        while (j < normalized.size()) {
            const unsigned char d = static_cast<unsigned char>(normalized[j]);
            if (d == ' ' || d == '\t' || d == '\n' || d == '\r') break;
            if (d < 0x80 && std::ispunct(static_cast<int>(d))) break;
            j += (d < 0x80 ? 1 : (d < 0xE0 ? 2 : (d < 0xF0 ? 3 : 4)));
        }
        const size_t originalStart = startMap[i];
        const size_t originalEnd = endMap[j - 1];
        auto pieces = wordPiece(std::string_view(normalized).substr(i, j - i), originalStart, originalEnd,
                                maxTokens == 0 ? 0 : maxTokens - tokens.size());
        tokens.insert(tokens.end(), pieces.begin(), pieces.end());
        if (maxTokens > 0 && tokens.size() >= maxTokens) break;
        i = j;
    }
    return tokens;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
