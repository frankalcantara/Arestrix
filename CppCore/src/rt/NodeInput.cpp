#include "rt/NodeInput.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "rt/Runtime.hpp"
#include "rt/Tools.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

constexpr size_t kMaxTextBytes = 12000;

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

} // namespace

std::string gatherNodeInput(const Json& source, const std::string& label) {
    fs::path root = workspaceRoot();
    std::string kind = source.value("kind", std::string("previous"));
    if (kind == "none") return "";
    if (kind == "file") {
        std::string rel = source.value("path", std::string());
        auto p = resolveInWorkspace(root, rel);
        std::error_code ec;
        if (rel.empty() || !p || !fs::is_regular_file(*p, ec)) {
            throw std::runtime_error("Vertice '" + label + "': arquivo de entrada nao encontrado no projeto: " +
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
        throw std::runtime_error("Vertice '" + label + "': o vertice anterior nao produziu texto. Ligue-o a um agente ou a um "
                                 "vertice MCP, ou escolha 'um arquivo do projeto' como entrada.");
    }
    return truncateText(text);
}

void writeNodeOutput(const std::string& nodeId, const std::string& text) {
    if (!safeId(nodeId)) return;
    std::error_code ec;
    fs::path dir = workspaceRoot() / ".arestrix" / "out";
    fs::create_directories(dir, ec);
    std::ofstream(dir / (nodeId + ".txt"), std::ios::binary | std::ios::trunc) << text;
}

} // namespace rt
} // namespace taskforge
