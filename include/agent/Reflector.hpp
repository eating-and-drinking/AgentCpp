#pragma once
//
// Reflector — periodic introspection on plan progress.
//
// Called from QueryEngine every `reflect_every` turns (or on MERIT hint).
// Looks at the current PlanGraph + the last few messages, calls the model
// with a `reflect` tool, and decides:
//   - whether step statuses need updating
//   - whether the plan needs revision (drives PlannerEngine::replan)
//   - whether to record any self-knowledge propositions into MERIT
//
// This complements MERIT's MetaController, which decides act/reflect/decompose
// per iteration on heuristic grounds. Reflector is the "explicit prompt-driven"
// reflection path. The two coexist.
//
#include <agent/PlanGraph.hpp>
#include <api/Types.hpp>
#include <memory>
#include <string>
#include <vector>

namespace agentcpp::api    { class ClaudeClient; }
namespace agentcpp::agent  { class MetacognitionEngine; }

namespace agentcpp::agent {

struct ReflectionResult {
    bool                     plan_needs_revision = false;
    std::string              revision_reason;
    std::vector<std::string> propositions;        // pushed into MERIT SelfModelStore
    std::vector<std::pair<std::string, StepStatus>> step_updates; // step_id -> new status
    std::string              user_visible_note;   // emitted as ev::ReflectionDone
};

class Reflector {
public:
    Reflector(std::shared_ptr<agentcpp::api::ClaudeClient> client,
              std::string model,
              MetacognitionEngine* metacog,
              int max_tokens = 2048)
        : client_(std::move(client))
        , model_(std::move(model))
        , metacog_(metacog)
        , max_tokens_(max_tokens)
    {}

    ReflectionResult reflect(const PlanGraph& plan,
                             const std::vector<agentcpp::api::Message>& last_window,
                             int turn);

    void setModel(std::string m) { model_ = std::move(m); }

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    std::string         model_;
    MetacognitionEngine* metacog_;
    int                  max_tokens_;

    static agentcpp::api::ToolDefinition reflectToolDef();
    static std::string summarizeWindow(const std::vector<agentcpp::api::Message>& w);
};

} // namespace agentcpp::agent
