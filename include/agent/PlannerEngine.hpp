#pragma once
//
// PlannerEngine — converts a user goal into a structured PlanGraph.
//
// The Planner calls the LLM with a single tool definition named "plan"
// whose input schema mirrors PlanGraph. We instruct the model to call the
// tool exactly once; the tool_use input is parsed via PlanGraph::fromJson.
//
// shouldPlan() implements the heuristic from PR-design:
//   - persona.extras.plan_by_default == true
//   - prompt length > 240 chars
//   - prompt contains multi-step verb markers ("then", "after that",
//     "step N", Chinese "然后/接着" — case-insensitive substring scan)
//   - count of imperative verbs >= 3 (rough heuristic)
//
#include <agent/PlanGraph.hpp>
#include <api/Types.hpp>
#include <memory>
#include <string>
#include <vector>

namespace agentcpp::api    { class ClaudeClient; }
namespace agentcpp::agent  { struct Persona; }

namespace agentcpp::agent {

class PlannerEngine {
public:
    PlannerEngine(std::shared_ptr<agentcpp::api::ClaudeClient> client,
                  std::string model,
                  int max_tokens = 4096)
        : client_(std::move(client))
        , model_(std::move(model))
        , max_tokens_(max_tokens)
    {}

    // Produce a PlanGraph for the given goal. `tool_hints_universe` is the
    // list of tool names available to the executor (used by the model to
    // populate PlanStep.tool_hints; can be empty). Returns an empty
    // PlanGraph on failure (caller decides whether to abort or proceed).
    PlanGraph plan(const std::string& goal,
                   const std::string& persona_mission,
                   const std::vector<std::string>& tool_hints_universe,
                   int max_steps = 12);

    // Revise an existing plan in light of recent observations.
    // `recent_messages` is a flat textual summary the Reflector composed.
    PlanGraph replan(const PlanGraph& current,
                     const std::string& revision_reason,
                     const std::string& recent_observations);

    // ── Heuristic gate ───────────────────────────────────────────────────
    static bool shouldPlan(const std::string& prompt, const Persona* persona);

    void setModel(std::string m) { model_ = std::move(m); }

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    std::string model_;
    int         max_tokens_;

    // Build the "plan" tool definition with PlanGraph schema.
    static agentcpp::api::ToolDefinition planToolDef(int max_steps);

    // Call the model once with a single tool and capture its tool_use input.
    nlohmann::json callPlanTool(const std::string& system_prompt,
                                const std::string& user_text,
                                int max_steps);
};

} // namespace agentcpp::agent
