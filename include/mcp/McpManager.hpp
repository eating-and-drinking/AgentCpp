#pragma once
#include <mcp/McpClient.hpp>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>

namespace agentcpp::mcp {

// One MCP tool, paired with the client that owns it.
struct ResolvedTool {
    std::string        prefixed_name;  // "<server-id>__<tool-name>"
    McpToolInfo        info;           // original (un-prefixed)
    McpClient*         client;         // non-owning
};

// Owns all spawned MCP clients and provides aggregate access to their tools.
//
// Config file format (Claude Desktop compatible):
//   { "mcpServers": {
//       "<id>": { "command": "...", "args": [...], "env": {"K":"V"} }
//     }
//   }
//
// Tools are exposed as "<server-id>__<tool-name>" to avoid collisions.
class McpManager {
public:
    McpManager() = default;
    ~McpManager() = default;

    McpManager(const McpManager&) = delete;
    McpManager& operator=(const McpManager&) = delete;

    // Load servers from JSON config. Missing file is not an error (returns 0).
    // Returns the number of servers successfully started.
    int loadConfig(const std::filesystem::path& path);

    // Resolve a config path: honours `explicit_path` if non-empty, else
    // $AGENTCPP_MCP_CONFIG, else ~/.agentcpp/mcp.json.
    static std::filesystem::path defaultConfigPath();

    const std::vector<ResolvedTool>& tools() const { return tools_; }
    std::size_t serverCount() const { return clients_.size(); }

private:
    std::vector<std::unique_ptr<McpClient>>  clients_;
    std::vector<ResolvedTool>                tools_;
};

} // namespace agentcpp::mcp
