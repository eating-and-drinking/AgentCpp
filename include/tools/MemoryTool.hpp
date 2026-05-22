#pragma once
#include "Tool.hpp"
#include <memory/MemoryEngine.hpp>

namespace agentcpp::tools {

class MemoryRetainTool : public Tool {
public:
    explicit MemoryRetainTool(agentcpp::memory::MemoryEngine& engine) : engine_(engine) {}
    std::string    name()        const override { return "MemoryRetain"; }
    std::string    category()    const override { return "memory"; }
    std::string    description() const override;
    json           inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    agentcpp::memory::MemoryEngine& engine_;
};

class MemoryRecallTool : public Tool {
public:
    explicit MemoryRecallTool(agentcpp::memory::MemoryEngine& engine) : engine_(engine) {}
    std::string    name()        const override { return "MemoryRecall"; }
    std::string    category()    const override { return "memory"; }
    std::string    description() const override;
    json           inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    agentcpp::memory::MemoryEngine& engine_;
};

class MemoryReflectTool : public Tool {
public:
    explicit MemoryReflectTool(agentcpp::memory::MemoryEngine& engine) : engine_(engine) {}
    std::string    name()        const override { return "MemoryReflect"; }
    std::string    category()    const override { return "memory"; }
    std::string    description() const override;
    json           inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    agentcpp::memory::MemoryEngine& engine_;
};

class MemoryListTool : public Tool {
public:
    explicit MemoryListTool(agentcpp::memory::MemoryEngine& engine) : engine_(engine) {}
    std::string    name()        const override { return "MemoryList"; }
    std::string    category()    const override { return "memory"; }
    std::string    description() const override;
    json           inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
private:
    agentcpp::memory::MemoryEngine& engine_;
};

} // namespace agentcpp::tools
