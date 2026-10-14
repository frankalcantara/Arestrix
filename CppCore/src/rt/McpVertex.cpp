#include "rt/McpVertex.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "mcp/McpClient.hpp"
#include "rt/McpTools.hpp"
#include "rt/NodeInput.hpp"
#include "rt/Runtime.hpp"
#include "rt/Tools.hpp"
#include "util/Json.hpp"

namespace fs = std::filesystem;

namespace taskforge {
namespace rt {

namespace {

constexpr int kCallTimeoutMs = 120000;

// {{input.a.b}} -> ["a","b"]; "{{input}}" -> []. false se nao for uma referencia pura ao longo da string inteira.
bool parsePureReference(const std::string& s, std::vector<std::string>& path) {
    if (s.size() < 9 || s.rfind("{{input", 0) != 0 || s.compare(s.size() - 2, 2, "}}") != 0) return false;
    std::string inner = s.substr(7, s.size() - 9);   // entre "{{input" e "}}"
    path.clear();
    if (inner.empty()) return true;
    if (inner[0] != '.') return false;
    size_t i = 1;
    while (i <= inner.size()) {
        size_t dot = inner.find('.', i);
        std::string part = inner.substr(i, dot == std::string::npos ? std::string::npos : dot - i);
        if (part.empty()) return false;
        path.push_back(part);
        if (dot == std::string::npos) break;
        i = dot + 1;
    }
    return true;
}

Json lookup(const Json& root, const std::vector<std::string>& path, const std::string& original) {
    const Json* cur = &root;
    for (const auto& key : path) {
        if (cur->is_object() && cur->contains(key)) {
            cur = &(*cur)[key];
        } else if (cur->is_array() && !key.empty() && std::isdigit(static_cast<unsigned char>(key[0])) &&
                   static_cast<size_t>(std::stoul(key)) < cur->size()) {
            cur = &(*cur)[static_cast<size_t>(std::stoul(key))];
        } else {
            throw std::runtime_error("a entrada nao tem o campo referenciado por " + original);
        }
    }
    return *cur;
}

std::string asText(const Json& v) {
    return v.is_string() ? v.str() : v.dump();
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
    return s;
}

Json render(const Json& v, const std::string& inputText, const Json* parsedInput) {
    if (v.is_string()) {
        const std::string& s = v.str();
        std::vector<std::string> path;
        if (parsePureReference(s, path)) {
            if (path.empty()) return Json(inputText);
            if (!parsedInput) throw std::runtime_error("a entrada nao e JSON, mas os argumentos usam " + s);
            return lookup(*parsedInput, path, s);      // preserva o tipo (numero, booleano, objeto)
        }
        // referencias dentro de um texto maior: {{input}} e {{input.campo}} viram texto
        std::string out = s;
        size_t pos = 0;
        while ((pos = out.find("{{input", pos)) != std::string::npos) {
            size_t end = out.find("}}", pos);
            if (end == std::string::npos) break;
            std::string ref = out.substr(pos, end + 2 - pos);
            std::vector<std::string> p;
            if (!parsePureReference(ref, p)) { pos = end + 2; continue; }
            std::string value;
            if (p.empty()) value = inputText;
            else if (!parsedInput) throw std::runtime_error("a entrada nao e JSON, mas os argumentos usam " + ref);
            else value = asText(lookup(*parsedInput, p, ref));
            out.replace(pos, ref.size(), value);
            pos += value.size();
        }
        return Json(out);
    }
    if (v.is_array()) {
        Json arr = Json::array();
        for (size_t i = 0; i < v.size(); ++i) arr.push_back(render(v[i], inputText, parsedInput));
        return arr;
    }
    if (v.is_object()) {
        Json obj = Json::object();
        for (auto it = v.begin(); it != v.end(); ++it) obj[it.key()] = render(*it, inputText, parsedInput);
        return obj;
    }
    return v;
}

} // namespace

Json renderMcpArguments(const Json& arguments, const std::string& inputText) {
    auto parsed = jsonutil::tryParse(inputText);
    const Json* parsedInput = (parsed && (parsed->is_object() || parsed->is_array())) ? &*parsed : nullptr;
    return render(arguments, inputText, parsedInput);
}

void callMcpTool(const std::string& nodeId, const std::string& label, const std::string& specJson) {
    Json spec = jsonutil::parse(specJson);
    const std::string server = spec.value("server", std::string());
    const std::string tool = spec.value("tool", std::string());
    if (server.empty() || tool.empty()) {
        throw std::runtime_error("Vertice MCP '" + label + "': escolha o servidor e a ferramenta (aba Geral do vertice).");
    }

    mcp::McpServerConfig cfg;
    try {
        cfg = McpToolbox::loadServerConfig(workspaceRoot(), server);
    } catch (const std::exception& ex) {
        throw std::runtime_error("Vertice MCP '" + label + "': " + ex.what());
    }

    const std::string input = gatherNodeInput(spec["source"], label);
    Json args;
    try {
        args = renderMcpArguments(spec["arguments"].is_object() ? spec["arguments"] : Json::object(), input);
    } catch (const std::exception& ex) {
        throw std::runtime_error("Vertice MCP '" + label + "': " + ex.what());
    }

    mcp::McpClient client;
    mcp::McpCallResult r = client.callTool(cfg, tool, args, kCallTimeoutMs);
    if (r.isError || !r.success) {
        std::string msg = r.errorMessage.empty() ? r.content : r.errorMessage;
        throw std::runtime_error("Vertice MCP '" + label + "': a ferramenta " + server + "." + tool + " falhou: " +
                                 (msg.empty() ? std::string("sem detalhes") : msg.substr(0, 600)));
    }

    writeNodeOutput(nodeId, r.content);
    const std::string outFile = spec["output"].value("file", std::string());
    if (!outFile.empty()) {
        auto p = resolveInWorkspace(workspaceRoot(), outFile);
        if (!p) throw std::runtime_error("Vertice MCP '" + label + "': o arquivo de saida deve ficar dentro do projeto: " + outFile);
        std::error_code ec;
        fs::create_directories(p->parent_path(), ec);
        std::ofstream(*p, std::ios::binary | std::ios::trunc) << r.content;
    }
    std::cout << "[MCP " << label << "] " << server << "." << tool << " ok (" << r.content.size() << " bytes)\n";
}

} // namespace rt
} // namespace taskforge
