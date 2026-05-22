#pragma once
#include <api/Types.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>

namespace agentcpp::tools {

using namespace agentcpp::api;

// Context passed to every tool invocation
struct ToolContext {
    std::filesystem::path cwd;
    bool                  read_only     = false;
    bool                  headless      = false;
    bool                  auto_approve  = false;  // bypass permission prompts
    int                   subagent_depth = 0;
    int                   max_subagent_depth = 2;
    // Callback for permission prompts in TUI mode
    std::function<bool(const std::string& message)> askPermission;
};

// Result returned by a tool
struct ToolCallResult {
    std::string content;
    bool        is_error = false;
    // Optional image attachment (base64). When non-empty, the result will be
    // sent to the model as a multi-part tool_result with both text and image.
    std::string image_b64;
    std::string image_media_type;

    static ToolCallResult ok(std::string content)
    { return {std::move(content), false, {}, {}}; }

    static ToolCallResult error(std::string message)
    { return {std::move(message), true, {}, {}}; }

    static ToolCallResult okWithImage(std::string content,
                                      std::string image_b64,
                                      std::string image_media_type = "image/png") {
        ToolCallResult r;
        r.content          = std::move(content);
        r.is_error         = false;
        r.image_b64        = std::move(image_b64);
        r.image_media_type = std::move(image_media_type);
        return r;
    }
};

// Abstract base class for all tools
class Tool {
public:
    virtual ~Tool() = default;

    // Name must match what Claude API tool_use blocks contain
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json        inputSchema() const = 0;

    // Execute the tool with validated JSON input
    virtual ToolCallResult execute(const json& input, const ToolContext& ctx) = 0;

    // Serialize the tool definition for the API request
    ToolDefinition definition() const;

    // Validate input against schema (basic required-field check)
    // Returns empty string on success, error message on failure
    virtual std::string validateInput(const json& input) const;
};

using ToolPtr = std::shared_ptr<Tool>;

// Registry of available tools
class ToolRegistry {
public:
    static ToolRegistry& instance();

    void        registerTool(ToolPtr tool);
    ToolPtr     findTool(const std::string& name) const;
    std::vector<ToolDefinition> definitions() const;
    const std::vector<ToolPtr>& tools() const { return tools_; }

private:
    ToolRegistry() = default;
    std::vector<ToolPtr> tools_;
};

// RAII helper for registering tools at startup
struct AutoRegister {
    explicit AutoRegister(ToolPtr tool) {
        ToolRegistry::instance().registerTool(std::move(tool));
    }
};

} // namespace agentcpp::tools
