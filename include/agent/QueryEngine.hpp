#pragma once
#include <agent/PlanGraph.hpp>
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
namespace agentcpp::agent    { class PersonaRegistry; }
namespace agentcpp::agent    { class PlannerEngine; }
namespace agentcpp::agent    { class Reflector; }

namespace agentcpp::agent {

using namespace agentcpp::api;
using namespace agentcpp::tools;

namespace ev {

struct TextDelta      { std::string text; };
struct ToolStart      { std::string id; std::string name; std::string input_preview; };
struct ToolEnd        { std::string id; std::string name; std::string result; bool is_error; };
struct TurnComplete   { StopReason stop_reason; Usage usage; };
struct Error          { std::string message; };
struct RequestStart   {};
// PR4 events
struct PlanReady      { std::string plan_markdown; };
struct PlanRevised    { std::string reason; std::string new_plan_markdown; };
struct ReflectionDone { std::string note; };

} // namespace ev

using AgentEvent = std::variant<
    ev::TextDelta,
    ev::ToolStart,
    ev::ToolEnd,
    ev::TurnComplete,
    ev::Error,
    ev::RequestStart,
    ev::PlanReady,
    ev::PlanRevised,
    ev::ReflectionDone
>;

using AgentEventCallback = std::function<void(const AgentEvent&)>;

struct QueryConfig {
    std::string               model;
    int                       max_tokens     = 8096;
    int                       max_turns      = 100;
    std::string               system_prompt;
    std::string               persona_id;
    std::vector<std::string>  allowed_tools;
    std::vector<std::string>  enable_toolsets;
    std::vector<std::string>  disable_toolsets;
    bool                      auto_approve   = false;
    bool                      headless       = false;
    ToolContext               tool_ctx;

    // ── PR4: Plan-Act-Reflect controls ───────────────────────────────────
    // plan_mode: Auto = heuristic (PlannerEngine::shouldPlan);
    //            Always = always plan; Never = skip; Only = plan + return.
    enum class PlanMode { Auto, Always, Never, Only };
    PlanMode plan_mode      = PlanMode::Auto;
    int      max_plan_steps = 12;
    int      reflect_every  = 4;        // 0 disables explicit Reflector
};

class QueryEngine {
public:
    QueryEngine(
        std::shared_ptr<agentcpp::api::ClaudeClient> client,
        ToolRegistry& registry
    );

    void setEventCallback(AgentEventCallback cb) { on_event_ = std::move(cb); }

    void setSkillRegistry  (const agentcpp::skills::SkillRegistry* r) { skills_   = r; }
    void setMemoryEngine   (const agentcpp::memory::MemoryEngine*  m) { memory_   = m; }
    void setPersonaRegistry(const PersonaRegistry*                p) { personas_ = p; }
    const PersonaRegistry* personaRegistry() const { return personas_; }

    void setPendingAttachments(std::vector<ContentBlock> parts) {
        pending_attachments_ = std::move(parts);
    }
    bool hasPendingAttachments() const { return !pending_attachments_.empty(); }

    void setMetacognitionEngine(std::unique_ptr<MetacognitionEngine> m);
    void disableMetacognition();
    MetacognitionEngine*       metacognitionEngine()       { return metacog_.get(); }
    const MetacognitionEngine* metacognitionEngine() const { return metacog_.get(); }

    // PR4: planner/reflector are owned by the engine and lazily wired the
    // first time setPlannerEnabled(true) is called (or implicitly when
    // QueryConfig::plan_mode != Never).
    void setPlannerEnabled(bool en, std::string model = "");
    void setReflectorEnabled(bool en, std::string model = "");
    const PlanGraph& currentPlan() const { return current_plan_; }

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

    const agentcpp::skills::SkillRegistry*  skills_   = nullptr;
    const agentcpp::memory::MemoryEngine*   memory_   = nullptr;
    const PersonaRegistry*                  personas_ = nullptr;

    std::vector<ContentBlock>               pending_attachments_;

    std::unique_ptr<MetacognitionEngine>    metacog_;
    std::unique_ptr<SelfModelMemoryAdapter> sm_adapter_;
    std::unique_ptr<PlannerEngine>          planner_;
    std::unique_ptr<Reflector>              reflector_;
    PlanGraph                               current_plan_;

    std::vector<Message> executeTools(
        const std::vector<ContentBlock>& content,
        const QueryConfig&               config
    );

    void emit(const AgentEvent& ev);
    std::string buildSystemPrompt(const QueryConfig& config) const;

    // PR4 helpers
    bool decideShouldPlan(const std::string& prompt, const QueryConfig& config) const;
    std::vector<std::string> toolNamesForRequest(const QueryConfig& config) const;
};

} // namespace agentcpp::agent
