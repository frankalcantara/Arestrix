#pragma once

#include "McpTypes.hpp"
#include "McpClient.hpp"
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace taskforge {
namespace mcp {

class McpRegistry {
public:
    static McpRegistry& getInstance();

    // Gerenciamento de servidores
    bool registerServer(const McpServerConfig& config);
    bool removeServer(const std::string& id);
    bool getServer(const std::string& id, McpServerConfig& outServer) const;
    std::vector<McpServerConfig> listServers() const;

    // Descoberta e execução
    nlohmann::json testServer(const std::string& id);
    McpCallResult callTool(const std::string& serverId,
                           const std::string& toolName,
                           const nlohmann::json& args);

    std::vector<McpTool> getAllTools() const;
    std::vector<McpTool> getToolsForServer(const std::string& serverId) const;

    // Persistência em disco
    bool loadFromDisk(const std::string& path = "arestrix-mcp-servers.json");
    bool saveToDisk(const std::string& path = "arestrix-mcp-servers.json") const;

    // Reset para testes
    void clear();

private:
    McpRegistry();
    ~McpRegistry() = default;

    McpRegistry(const McpRegistry&) = delete;
    McpRegistry& operator=(const McpRegistry&) = delete;

    void initializeDefaults();

    mutable std::mutex mutex_;
    std::map<std::string, McpServerConfig> servers_;
    McpClient client_;
};

} // namespace mcp
} // namespace taskforge
