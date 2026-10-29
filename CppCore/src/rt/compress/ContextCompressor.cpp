#include "rt/compress/ContextCompressor.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "rt/compress/CodeCompressor.hpp"
#include "rt/compress/ContentRouter.hpp"
#include "rt/compress/LogCompressor.hpp"
#include "rt/compress/ProseCompressor.hpp"
#include "rt/compress/SmartCrusher.hpp"
#include "rt/compress/Tokens.hpp"

namespace taskforge {
namespace rt {
namespace compress {
namespace {

void addKind(CompressionStats& stats, const char* kind) {
    if (stats.kinds.empty()) {
        stats.kinds = kind;
        return;
    }
    if (stats.kinds.find(kind) == std::string::npos) {
        stats.kinds += ",";
        stats.kinds += kind;
    }
}

std::string retrievalNote(RuntimeLang lang, const std::string& token, size_t before, size_t after) {
    const std::string sizes = "~" + std::to_string(before) + " -> ~" + std::to_string(after) + " tokens";
    return langPick(lang,
                    "[CCR:" + token + "] Trecho comprimido (" + sizes +
                        "). Use a ferramenta `retrieve` com esse token para ler o original.",
                    "[CCR:" + token + "] Compressed block (" + sizes +
                        "). Use the `retrieve` tool with this token to read the original.");
}

// Marcador estavel que impede recomprimir o que ja foi comprimido.
bool alreadyCompressed(const std::string& text) {
    return text.find("[CCR:") != std::string::npos;
}

std::string normalizePath(const std::string& path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) out += (c == '\\') ? '/' : c;
    while (out.rfind("./", 0) == 0) out = out.substr(2);
    return out;
}

} // namespace

ContextCompressor::ContextCompressor(CompressionSettings settings, CcrStore* ccr, RuntimeLang lang,
                                     const ProseTokenizer* proseTokenizer, ProseModel* proseModel)
    : settings_(settings),
      ccr_(ccr),
      lang_(lang),
      proseTokenizer_(proseTokenizer),
      proseModel_(proseModel) {}

CompressionStats ContextCompressor::apply(std::vector<ChatMessage>& messages) {
    CompressionStats stats;
    if (!settings_.enabled || ccr_ == nullptr || messages.empty()) return stats;

    size_t totalTokens = 0;
    for (const auto& message : messages) totalTokens += estimateTokens(message.text);
    if (totalTokens < settings_.minRunTokens) return stats;

    // Origem de cada observacao (toolCallId -> ferramenta/caminho) para a regra de exclusao:
    // arquivo que o agente ja escreveu/editou nao pode ser comprimido.
    struct CallInfo {
        std::string name;
        std::string path;
    };
    std::map<std::string, CallInfo> callById;
    std::set<std::string> editedPaths;
    for (const auto& message : messages) {
        if (message.role != "assistant") continue;
        for (const auto& call : message.toolCalls) {
            std::string path;
            if (call.args.is_object() && call.args.contains("path") && call.args["path"].is_string()) {
                path = call.args["path"].get<std::string>();
            }
            callById[call.id] = CallInfo{call.name, path};
            if (call.name == "write" || call.name == "edit") {
                const std::string normalized = normalizePath(path);
                if (!normalized.empty()) editedPaths.insert(normalized);
            }
        }
    }

    JsonOptions jsonOptions;
    jsonOptions.maxArrayItems = settings_.maxArrayItems;
    jsonOptions.maxStringTokens = settings_.maxStringTokens;

    LogOptions logOptions;
    logOptions.maxLines = settings_.maxLogLines;

    for (size_t i = 0; i + 1 < messages.size(); ++i) {
        ChatMessage& message = messages[i];
        // So observacoes de ferramenta: o texto do assistente e a entrada do chamador ficam intactos.
        if (message.role != "tool") continue;
        if (estimateTokens(message.text) < settings_.minBlockTokens) continue;
        if (alreadyCompressed(message.text)) continue;

        const Classification classification = classifyContent(message.text);
        CompressResult result;
        switch (classification.kind) {
            case ContentKind::Json:
                result = crushJson(message.text, jsonOptions);
                break;
            case ContentKind::Log:
                result = compressLog(message.text, logOptions);
                break;
            case ContentKind::Code: {
                if (!settings_.code) continue;
                const auto origin = callById.find(message.toolCallId);
                if (origin != callById.end()) {
                    const std::string source = normalizePath(origin->second.path);
                    if (!source.empty() && editedPaths.count(source) > 0) continue;
                }
                result = compressCode(message.text, CodeOptions{});
                break;
            }
            case ContentKind::Prose: {
                if (!settings_.prose || proseTokenizer_ == nullptr || proseModel_ == nullptr ||
                    !proseModel_->available()) {
                    continue;
                }
                result = compressProse(message.text, *proseTokenizer_, *proseModel_, ProseOptions{});
                break;
            }
            case ContentKind::Skip:
                continue;
        }
        if (!result.changed || result.bytesAfter >= result.bytesBefore) continue;

        const size_t tokensBefore = estimateTokens(message.text);
        const size_t tokensAfter = estimateTokens(result.text);
        if (tokensAfter >= tokensBefore) continue;

        // Sem original guardado a compressao nao pode ser usada: o agente perderia o dado.
        const std::string token = ccr_->store(message.text);
        if (token.empty()) continue;

        stats.blocks += 1;
        stats.tokensBefore += tokensBefore;
        stats.tokensAfter += tokensAfter;
        addKind(stats, contentKindName(classification.kind));

        message.text = result.text;
        message.text += "\n\n";
        message.text += retrievalNote(lang_, token, tokensBefore, tokensAfter);
    }

    return stats;
}

} // namespace compress
} // namespace rt
} // namespace taskforge
