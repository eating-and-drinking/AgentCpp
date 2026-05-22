#pragma once
#include <api/Types.hpp>
#include <tools/Tool.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace agentcpp::api      { class ClaudeClient; }
namespace agentcpp::skills   { class SkillRegistry; }
namespace agentcpp::memory   { class MemoryEngine; }
namespace agentcpp::agent    { class MetacognitionEngine; }
namespace agentcpp::agent    { class SelfModelMemoryAdapter; }

namespace agentcpp::agent {

using namespace agentcpp::api;
using namespace agentcpp::tools;

namespace ev {

struct TextDelta    { std::string text; };
struct ToolStart    { std::string id; std::string name; std::string input_preview; };
struct ToolEnd      { std::string id; std::string name; std::string result; bool is_error; };
struct TurnComplete { StopReason stop_reason; Usage usage; };
struct Error        { std::string message; };
struct RequestStart {};

} // namespace ev

using AgentEvent = std::variant<
    ev::TextDelta,
    ev::ToolStart,
    ev::ToolEnd,
    ev::TurnComplete,
    ev::Error,
    ev::RequestStart
>;

using AgentEventCallback = std::function<void(const AgentEvent&)>;

struct QueryConfig {
    std::string               model;
    int                       max_tokens     = 8096;
    int                       max_turns      = 100;
    std::string               system_prompt;
    std::vector<std::string>  allowed_tools;
    bool                      auto_approve   = false;
    bool                      headless       = false;
    ToolContext               tool_ctx;
};

class QueryEngine {
public:
    QueryEngine(
        std::shared_ptr<agentcpp::api::ClaudeClient> client,
        ToolRegistry& registry
    );

    void setEventCallback(AgentEventCallback cb) { on_event_ = std::move(cb); }

    void setSkillRegistry(const agentcpp::skills::SkillRegistry* r) { skills_ = r; }
    void setMemoryEngine (const agentcpp::memory::MemoryEngine*  m) { memory_ = m; }

    // Metacognition (Layers 1-4 of the MERIT framework) is owned by the engine
    // and enabled by default. Pass nullptr to disable entirely (e.g. for an
    // ablation baseline); pass a custom-configured engine to override.
    void setMetacognitionEngine(std::unique_ptr<MetacognitionEngine> m);
    void disableMetacognition();
    MetacognitionEngine*       metacognitionEngine()       { return metacog_.get(); }
    const MetacognitionEngine* metacognitionEngine() const { return metacog_.get(); }

    // Opt-in Layer 3 persistence: wires the SelfModelStore inside the current
    // MetacognitionEngine to a MemoryEngine bank so self-knowledge survives
    // process restarts. Takes a non-const reference because MentalModel
    // upserts mutate the memory store. Safe to call multiple times — replaces
    // any existing adapter.
    void enableSelfModelPersistence(
        agentcpp::memory::MemoryEngine& engine,
        std::string bank_id = "metacog_self_model");
    void disableSelfModelPersistence();

    std::string runTurn(
        std::vector<Message>&   conversation,
        const std::string&      user_input,
        const QueryConfig&      config
    );

    void abort();
    bool isAborted() const;

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    ToolRegistry&                           registry_;
    AgentEventCallback                      on_event_;
    std::atomic<bool>                       aborted_{false};

    const agentcpp::skills::SkillRegistry*  skills_ = nullptr;
    const agentcpp::memory::MemoryEngine*   memory_ = nullptr;

    std::unique_ptr<MetacognitionEngine>    metacog_;
    std::unique_ptr<SelfModelMemoryAdapter> sm_adapter_;

    std::vector<Message> executeTools(
        const std::vector<ContentBlock>& content,
        const QueryConfig&               config
    );

    void emit(const AgentEvent& ev);
    std::string buildSystemPrompt(const QueryConfig& config) const;
};

} // namespace agentcpp::agent
