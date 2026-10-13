#include "McpRegistry.hpp"
#include "util/Json.hpp"
#include <fstream>
#include <iostream>

namespace taskforge {
namespace mcp {

McpRegistry& McpRegistry::getInstance() {
    static McpRegistry instance;
    return instance;
}

McpRegistry::McpRegistry() {
    if (!loadFromDisk()) {
        initializeDefaults();
        saveToDisk();
    }
}

void McpRegistry::initializeDefaults() {
    McpServerConfig fs;
    fs.id = "mock-filesystem";
    fs.name = "Filesystem Explorer (MCP simulado)";
    fs.transport = McpTransportType::Stdio;
    fs.command = "mock";
    fs.args = {"--dir", "."};
    fs.isEnabled = true;
    fs.cachedTools = client_.listTools(fs);
    servers_[fs.id] = fs;

    McpServerConfig fetch;
    fetch.id = "mock-fetch";
    fetch.name = "Web & Fetch Gateway (MCP simulado)";
    fetch.transport = McpTransportType::Stdio;
    fetch.command = "mock";
    fetch.args = {"--timeout", "10"};
    fetch.isEnabled = true;
    fetch.cachedTools = client_.listTools(fetch);
    servers_[fetch.id] = fetch;
}

bool McpRegistry::registerServer(const McpServerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    servers_[config.id] = config;
    saveToDisk();
    return true;
}

bool McpRegistry::removeServer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(id);
    if (it != servers_.end()) {
        servers_.erase(it);
        saveToDisk();
        return true;
    }
    return false;
}

bool McpRegistry::getServer(const std::string& id, McpServerConfig& outServer) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(id);
    if (it != servers_.end()) {
        outServer = it->second;
        return true;
    }
    return false;
}

std::vector<McpServerConfig> McpRegistry::listServers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<McpServerConfig> list;
    for (const auto& pair : servers_) {
        list.push_back(pair.second);
    }
    return list;
}

taskforge::Json McpRegistry::testServer(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(id);
    if (it == servers_.end()) {
        return {
            {"success", false},
            {"error", "Servidor MCP '" + id + "' nao encontrado no registro"}
        };
    }

    try {
        auto tools = client_.listTools(it->second);
        it->second.cachedTools = tools;
        saveToDisk();

        return {
            {"success", true},
            {"tools", tools},
            {"count", tools.size()},
            {"server", it->second}
        };
    } catch (const std::exception& ex) {
        return {
            {"success", false},
            {"error", std::string("Falha ao comunicar com o servidor: ") + ex.what()}
        };
    }
}

McpCallResult McpRegistry::callTool(const std::string& serverId,
                                    const std::string& toolName,
                                    const taskforge::Json& args) {
    McpServerConfig cfg;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = servers_.find(serverId);
        if (it == servers_.end()) {
            McpCallResult res;
            res.isError = true;
            res.errorMessage = "Servidor MCP nao encontrado: " + serverId;
            return res;
        }
        cfg = it->second;
    }

    return client_.callTool(cfg, toolName, args);
}

std::vector<McpTool> McpRegistry::getAllTools() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<McpTool> all;
    for (const auto& pair : servers_) {
        if (pair.second.isEnabled) {
            all.insert(all.end(), pair.second.cachedTools.begin(), pair.second.cachedTools.end());
        }
    }
    return all;
}

std::vector<McpTool> McpRegistry::getToolsForServer(const std::string& serverId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = servers_.find(serverId);
    if (it != servers_.end()) {
        return it->second.cachedTools;
    }
    return {};
}

bool McpRegistry::loadFromDisk(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (path == "arestrix-mcp-servers.json") {
            file.open("taskforge-mcp-servers.json");
        }
        if (!file.is_open()) return false;
    }

    try {
        taskforge::Json j = taskforge::jsonutil::parseStream(file);
        if (j.is_array()) {
            std::lock_guard<std::mutex> lock(mutex_);
            servers_.clear();
            for (const auto& item : j) {
                McpServerConfig cfg = item.get<McpServerConfig>();
                servers_[cfg.id] = cfg;
            }
            return true;
        }
    } catch (const std::exception& e) {
        std::cerr << "[MCP Registry] Erro ao carregar servidores do disco: " << e.what() << "\n";
    }
    return false;
}

bool McpRegistry::saveToDisk(const std::string& path) const {
    try {
        taskforge::Json j = taskforge::Json::array();
        for (const auto& pair : servers_) {
            j.push_back(pair.second);
        }
        std::ofstream file(path);
        if (file.is_open()) {
            file << j.dump(2);
            return true;
        }
    } catch (...) {}
    return false;
}

void McpRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    servers_.clear();
}

} // namespace mcp
} // namespace taskforge
