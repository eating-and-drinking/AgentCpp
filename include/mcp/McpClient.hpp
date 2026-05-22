#pragma once
#include <mcp/JsonRpc.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace agentcpp::mcp {

// One discovered tool exposed by an MCP server.
struct McpToolInfo {
    std::string name;         // server-side name
    std::string description;
    json        input_schema; // JSON Schema object
};

// Server-side config for spawning one MCP server subprocess.
struct ServerConfig {
    std::string                                   id;       // arbitrary identifier, used as tool prefix
    std::string                                   command;  // e.g. "npx" or absolute path
    std::vector<std::string>                      args;
    std::unordered_map<std::string, std::string>  env;
    std::chrono::milliseconds                     init_timeout{ std::chrono::seconds{ 15 } };
    std::chrono::milliseconds                     call_timeout{ std::chrono::seconds{ 60 } };
};

// One MCP client = one subprocess speaking newline-delimited JSON-RPC 2.0 over stdio.
// Thread-compatible: callers must serialise calls to one client. The agent loop
// is single-threaded already, so this is fine.
class McpClient {
public:
    explicit McpClient(ServerConfig cfg);
    ~McpClient();

    McpClient(const McpClient&) = delete;
    McpClient& operator=(const McpClient&) = delete;

    // Spawn the subprocess and perform `initialize` + `notifications/initialized`.
    // Returns empty string on success, error message on failure.
    std::string start();

    // List the server's tools (calls `tools/list`). Caches the result.
    const std::vector<McpToolInfo>& tools() const { return tools_; }

    // Invoke a tool. Returns the text content of the result, or sets is_error
    // and returns the error message.
    std::string callTool(const std::string& name, const json& arguments, bool& is_error);

    // Whether the subprocess is still running.
    bool isAlive() const;

    // Server identifier (from ServerConfig).
    const std::string& id() const { return cfg_.id; }

    // Stop the subprocess (close stdin, wait briefly, kill on timeout).
    void stop();

private:
    ServerConfig                cfg_;
    int                         in_fd_  = -1;  // write to server stdin
    int                         out_fd_ = -1;  // read from server stdout
    int                         child_pid_ = -1;
    int                         next_id_ = 1;
    std::string                 read_buf_;
    std::vector<McpToolInfo>    tools_;

    // Send a request and block until the matching response arrives. Ignores
    // any messages whose id doesn't match (notifications, mismatched ids).
    Response sendRequest(const std::string& method, const json& params,
                         std::chrono::milliseconds timeout);

    // Send a notification (no response expected).
    void     sendNotification(const std::string& method, const json& params);

    // Write one JSON line (followed by \n) to the server.
    bool     writeLine(const std::string& s);

    // Block until one complete line is read or timeout. Returns empty on timeout/EOF.
    std::string readLine(std::chrono::milliseconds timeout);

    // Run tools/list and populate tools_.
    std::string loadTools();
};

} // namespace agentcpp::mcp
