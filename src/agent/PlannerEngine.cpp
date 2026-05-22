#include <agent/PlannerEngine.hpp>
#include <agent/Persona.hpp>
#include <api/ClaudeClient.hpp>
#include <utils/Logger.hpp>
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace agentcpp::agent {

namespace api = agentcpp::api;

// ── Heuristic: when to spend tokens on planning ──────────────────────────────
bool PlannerEngine::shouldPlan(const std::string& prompt, const Persona* persona) {
    // 1) Persona opt-in via extras.plan_by_default == true
    if (persona && persona->extras.is_object()
        && persona->extras.contains("plan_by_default")
        && persona->extras["plan_by_default"].is_boolean()
        && persona->extras["plan_by_default"].get<bool>()) {
        return true;
    }

    // 2) Long prompts almost always benefit from a plan
    if (prompt.size() > 240) return true;

    // 3) Multi-step verb markers — English + Chinese
    static const std::regex multi_step(
        R"((then|after\s*that|first|second|third|finally|step\s*\d+|and\s+then|然后|接着|之后|最后))",
        std::regex::icase);
    if (std::regex_search(prompt, multi_step)) return true;

    // 4) Rough imperative-verb count. Scan for common action verbs.
    static const std::vector<std::string> verbs = {
        "create", "make", "build", "write", "fetch", "compute",
        "analyze", "search", "find", "summarize", "generate",
        "convert", "extract", "compare", "plot", "render"
    };
    int hits = 0;
    std::string lower(prompt);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    for (const auto& v : verbs) if (lower.find(v) != std::string::npos) ++hits;
    return hits >= 3;
}

// ── Tool definition the model must call ─────────────────────────────────────
api::ToolDefinition PlannerEngine::planToolDef(int max_steps) {
    api::ToolDefinition def;
    def.name        = "plan";
    def.description =
        "Emit a structured execution plan. Call this tool exactly once with "
        "your decomposition of the user's request. Choose 2-" + std::to_string(max_steps)
        + " concrete steps. Set tool_hints from the tools available to the "
        "agent (omit if unsure). Set blockers when a step truly depends on "
        "another step finishing first.";

    nlohmann::json step_schema = {
        {"type", "object"},
        {"properties", {
            {"id",         {{"type","string"}, {"description","Stable id like S1, S2, S2.1"}}},
            {"subject",    {{"type","string"}, {"description","Imperative one-liner"}}},
            {"acceptance", {{"type","string"}, {"description","How to know this step is complete"}}},
            {"tool_hints", {{"type","array"},  {"items", {{"type","string"}}}}},
            {"blockers",   {{"type","array"},  {"items", {{"type","string"}}}}}
        }},
        {"required", nlohmann::json::array({"id","subject","acceptance"})}
    };

    def.input_schema = {
        {"type", "object"},
        {"properties", {
            {"goal",  {{"type","string"}, {"description","Restate the user's overall goal"}}},
            {"steps", {{"type","array"},  {"items", step_schema},
                       {"description","Ordered steps to reach the goal"}}}
        }},
        {"required", nlohmann::json::array({"goal","steps"})}
    };
    return def;
}

// ── Single model round-trip with the plan tool forced ────────────────────────
nlohmann::json PlannerEngine::callPlanTool(const std::string& system_prompt,
                                           const std::string& user_text,
                                           int max_steps)
{
    api::ApiRequest req;
    req.model      = model_;
    req.max_tokens = max_tokens_;
    req.system     = system_prompt;
    req.stream     = false;
    req.tools      = { planToolDef(max_steps) };
    req.messages   = { api::Message::userText(user_text) };

    api::ApiResponse resp;
    try {
        resp = client_->request(req);
    } catch (const std::exception& e) {
        LOG_WARN(std::string("planner: API call failed: ") + e.what());
        return nlohmann::json{};
    }

    // Find the tool_use block named "plan"
    for (const auto& cb : resp.content) {
        if (auto* tu = std::get_if<api::ToolUseBlock>(&cb)) {
            if (tu->name == "plan") return tu->input;
        }
    }
    LOG_WARN("planner: model did not call the 'plan' tool");
    return nlohmann::json{};
}

PlanGraph PlannerEngine::plan(const std::string& goal,
                              const std::string& persona_mission,
                              const std::vector<std::string>& tool_hints_universe,
                              int max_steps)
{
    std::ostringstream sys;
    sys << "You are a planning module. Produce a concrete, minimal "
           "execution plan for the user's request. Call the `plan` tool "
           "exactly once. Do NOT do the work yourself — only plan.\n";
    if (!persona_mission.empty()) {
        sys << "\nPersona context:\n" << persona_mission << "\n";
    }
    if (!tool_hints_universe.empty()) {
        sys << "\nThe executor has these tools available:";
        for (std::size_t i = 0; i < tool_hints_universe.size(); ++i) {
            sys << (i ? ", " : " ") << tool_hints_universe[i];
        }
        sys << "\n";
    }

    auto args = callPlanTool(sys.str(), goal, max_steps);
    if (args.is_null() || (args.is_object() && args.empty())) {
        PlanGraph empty;
        empty.goal = goal;
        return empty;
    }
    PlanGraph g = PlanGraph::fromJson(args);
    if (g.goal.empty()) g.goal = goal;
    return g;
}

PlanGraph PlannerEngine::replan(const PlanGraph& current,
                                const std::string& revision_reason,
                                const std::string& recent_observations)
{
    std::ostringstream sys;
    sys << "You are a planning module revising an existing plan in light of "
           "new observations. Call the `plan` tool exactly once with the "
           "REVISED plan. Preserve completed steps where still relevant; "
           "drop or split blocked steps; add new steps as needed.\n";
    sys << "\nCurrent plan (revision " << current.revision << "):\n";
    sys << current.toMarkdown();
    if (!revision_reason.empty()) {
        sys << "\nReason for revision:\n" << revision_reason << "\n";
    }
    if (!recent_observations.empty()) {
        sys << "\nRecent observations:\n" << recent_observations << "\n";
    }

    auto args = callPlanTool(sys.str(), current.goal, /*max_steps=*/(int)current.steps.size() + 6);
    if (args.is_null() || (args.is_object() && args.empty())) {
        PlanGraph carry = current;
        carry.revision += 1;
        return carry;
    }
    PlanGraph next = PlanGraph::fromJson(args);
    if (next.goal.empty()) next.goal = current.goal;
    next.revision = current.revision + 1;
    return next;
}

} // namespace agentcpp::agent
