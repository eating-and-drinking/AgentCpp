#pragma once
//
// PlanGraph — the structured representation of an agent's plan.
//
// A plan is an ordered list of steps. Each step has:
//   - a stable id ("S1", "S1.1", "S2", ...)
//   - a human-readable subject (one imperative sentence)
//   - an acceptance criterion (how the agent knows it's done)
//   - optional tool_hints (recommended tool names) and blockers (other step ids)
//   - a status (Pending / InProgress / Done / Skipped / Failed)
//
// PlanGraphs are produced by PlannerEngine via the model and injected into
// the agent's system prompt during the Act phase. The Reflector may update
// step statuses or trigger replan().
//
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace agentcpp::agent {

enum class StepStatus { Pending, InProgress, Done, Skipped, Failed };

std::string stepStatusToString(StepStatus s);
StepStatus  parseStepStatus(const std::string& s);

struct PlanStep {
    std::string              id;
    std::string              subject;
    std::string              acceptance;
    std::vector<std::string> tool_hints;
    std::vector<std::string> blockers;       // step ids this depends on
    StepStatus               status = StepStatus::Pending;
    std::string              notes;
};

struct PlanGraph {
    std::string             goal;
    std::vector<PlanStep>   steps;
    int                     revision = 0;    // bumped by replan()

    // First runnable step (Pending, all blockers Done). nullptr if none.
    PlanStep*       nextRunnable();
    const PlanStep* findStep(const std::string& id) const;
    void            markStatus(const std::string& id, StepStatus s, std::string note = {});
    bool            allDone() const;

    // Render as markdown for the system prompt. Format:
    //   ## Plan (revision N)
    //   Goal: ...
    //   - [x] S1  Subject  (done)
    //   - [ ] S2  Subject  — acceptance
    //         tools: Bash, Read
    //         blocked by: S1
    std::string     toMarkdown() const;

    nlohmann::json  toJson() const;
    static PlanGraph fromJson(const nlohmann::json& j);
};

} // namespace agentcpp::agent
