#pragma once
#include <api/Types.hpp>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>

namespace agentcpp::tools {

using namespace agentcpp::api;

struct ToolContext {
    std::filesystem::path cwd;
    bool                  read_only     = false;
    bool                  headless      = false;
    bool                  auto_approve  = false;
    int                   subagent_depth = 0;
    int                   max_subagent_depth = 2;
    std::function<bool(const std::string& message)> askPermission;
};

// Tool execution result. PR3 extends this with optional extra image and
// data blocks so tools can return rich multimodal output (chart PNG +
// table CSV + summary text).  Legacy `image_b64` / `image_media_type`
// remain for backward compatibility; new code should prefer `extra_images`
// and `extra_data`.
struct ToolCallResult {
    std::string             content;
    bool                    is_error          = false;
    std::string             image_b64;
    std::string             image_media_type;
    std::vector<ImageBlock> extra_images;
    std::vector<DataBlock>  extra_data;

    static ToolCallResult ok(std::string content) {
        ToolCallResult r; r.content = std::move(content); return r;
    }
    static ToolCallResult error(std::string message) {
        ToolCallResult r; r.content = std::move(message); r.is_error = true; return r;
    }
    static ToolCallResult okWithImage(std::string content,
                                      std::string image_b64,
                                      std::string image_media_type = "image/png") {
        ToolCallResult r;
        r.content          = std::move(content);
        r.image_b64        = std::move(image_b64);
        r.image_media_type = std::move(image_media_type);
        return r;
    }
    static ToolCallResult okMulti(std::string content,
                                  std::vector<ImageBlock> images,
                                  std::vector<DataBlock>  datas) {
        ToolCallResult r;
        r.content      = std::move(content);
        r.extra_images = std::move(images);
        r.extra_data   = std::move(datas);
        return r;
    }
    bool hasMultiparts() const {
        return !extra_images.empty() || !extra_data.empty();
    }
};

class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json        inputSchema() const = 0;
    virtual std::string category() const { return "core"; }
    virtual ToolCallResult execute(const json& input, const ToolContext& ctx) = 0;
    ToolDefinition definition() const;
    virtual std::string validateInput(const json& input) const;
};

using ToolPtr = std::shared_ptr<Tool>;

class ToolRegistry {
public:
    static ToolRegistry& instance();

    void        registerTool(ToolPtr tool);
    ToolPtr     findTool(const std::string& name) const;
    std::vector<ToolDefinition> definitions() const;
    const std::vector<ToolPtr>& tools() const { return tools_; }

    std::vector<std::string>     listGroups() const;
    std::vector<std::string>     toolsInGroup(const std::string& group) const;
    std::vector<ToolDefinition>  definitionsForPersona(
        const std::vector<std::string>& allowed_groups,
        const std::vector<std::string>& extra_enable,
        const std::vector<std::string>& extra_disable,
        const std::vector<std::string>& explicit_tool_whitelist = {}) const;

private:
    ToolRegistry() = default;
    std::vector<ToolPtr> tools_;
};

struct AutoRegister {
    explicit AutoRegister(ToolPtr tool) {
        ToolRegistry::instance().registerTool(std::move(tool));
    }
};

} // namespace agentcpp::tools
