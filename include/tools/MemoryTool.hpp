#pragma once
//
// Memory tools exposed to the agent.
//
// The new lineup mirrors hindsight's three core operations — retain, recall,
// and reflect — instead of the legacy read/write/list trio. A list tool
// is kept for introspection.
//
//   * MemoryRetainTool  → hindsight: retain (ingest one item)
//   * MemoryRecallTool  → hindsight: recall (top-k relevant facts)
//   * MemoryReflectTool → hindsight: reflect (composed answer + based_on)
//   * MemoryListTool    → list bank's memory units (debugging / overview)
//
#include "Tool.hpp"
#include <memory/MemoryEngine.hpp>

namespace agentcpp::tools {

class MemoryRetainTool : public Tool {
public:
    explicit MemoryRetainTool(agentcpp::memory::MemoryEngine& engine) : engine_(engine) {}

    std::string    name()        const override { return "MemoryRetain"; }
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
    std::string    description() const override;
    json           inputSchema() const override;
    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    agentcpp::memory::MemoryEngine& engine_;
};

} // namespace agentcpp::tools
