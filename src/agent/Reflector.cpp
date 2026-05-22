#include <agent/Reflector.hpp>
#include <agent/MetacognitionEngine.hpp>
#include <api/ClaudeClient.hpp>
#include <utils/Logger.hpp>
#include <sstream>

namespace agentcpp::agent {

namespace api = agentcpp::api;

api::ToolDefinition Reflector::reflectToolDef() {
    api::ToolDefinition def;
    def.name        = "reflect";
    def.description =
        "Emit a structured reflection on the agent's progress against the "
        "current plan. Use this to flag plan revisions, propose status "
        "updates for completed/failed steps, and record any durable "
        "lessons. Call exactly once.";

    nlohmann::json step_update = {
        {"type", "object"},
        {"properties", {
            {"step_id", {{"type","string"}}},
            {"status",  {{"type","string"},
                         {"enum", nlohmann::json::array({"pending","in_progress","done","skipped","failed"})}}},
            {"note",    {{"type","string"}}}
        }},
        {"required", nlohmann::json::array({"step_id","status"})}
    };

    def.input_schema = {
        {"type", "object"},
        {"properties", {
            {"plan_needs_revision",
                {{"type","boolean"},
                 {"description","True iff the plan is wrong and should be redone"}}},
            {"revision_reason",
                {{"type","string"},
                 {"description","Why a revision is needed (if any)"}}},
            {"step_updates",
                {{"type","array"}, {"items", step_update}}},
            {"propositions",
                {{"type","array"}, {"items", {{"type","string"}}},
                 {"description","Durable self-knowledge to record (1 sentence each)"}}},
            {"user_visible_note",
                {{"type","string"},
                 {"description","Short note shown to the user in the TUI"}}}
        }}
    };
    return def;
}

std::string Reflector::summarizeWindow(const std::vector<api::Message>& w) {
    // Compact textual digest of the last few turns — keep it small to bound
    // reflector token cost.
    std::ostringstream ss;
    for (const auto& m : w) {
        ss << (m.role == api::Role::User ? "[user]" : "[assistant]") << " ";
        for (const auto& cb : m.content) {
            if (auto* t = std::get_if<api::TextBlock>(&cb)) {
                std::string s = t->text;
                if (s.size() > 280) s = s.substr(0, 280) + "...";
                ss << s << " ";
            } else if (auto* tu = std::get_if<api::ToolUseBlock>(&cb)) {
                ss << "<tool:" << tu->name << ">";
            } else if (auto* tr = std::get_if<api::ToolResultBlock>(&cb)) {
                std::string s = tr->content;
                if (s.size() > 200) s = s.substr(0, 200) + "...";
                ss << "<tool_result " << (tr->is_error ? "error" : "ok") << ": " << s << "> ";
            }
        }
        ss << "\n";
    }
    return ss.str();
}

ReflectionResult Reflector::reflect(const PlanGraph& plan,
                                    const std::vector<api::Message>& last_window,
                                    int turn)
{
    ReflectionResult r;

    api::ApiRequest req;
    req.model      = model_;
    req.max_tokens = max_tokens_;
    req.stream     = false;
    req.tools      = { reflectToolDef() };

    std::ostringstream sys;
    sys << "You are a reflection module reviewing agent progress on turn "
        << turn << ". Call the `reflect` tool exactly once.\n\n"
        << "## Current plan\n" << plan.toMarkdown() << "\n"
        << "## Recent activity\n" << summarizeWindow(last_window);
    req.system = sys.str();
    req.messages = { api::Message::userText(
        "Reflect on the plan progress. Update step statuses if obvious from "
        "the activity. Flag plan_needs_revision only if the plan is actively "
        "wrong (not just incomplete). Record at most 2 durable propositions.") };

    api::ApiResponse resp;
    try { resp = client_->request(req); }
    catch (const std::exception& e) {
        LOG_WARN(std::string("reflector: API call failed: ") + e.what());
        return r;
    }

    nlohmann::json args;
    for (const auto& cb : resp.content) {
        if (auto* tu = std::get_if<api::ToolUseBlock>(&cb)) {
            if (tu->name == "reflect") { args = tu->input; break; }
        }
    }
    if (args.is_null()) return r;

    r.plan_needs_revision = args.value("plan_needs_revision", false);
    r.revision_reason     = args.value("revision_reason",     std::string{});
    r.user_visible_note   = args.value("user_visible_note",   std::string{});

    if (args.contains("step_updates") && args["step_updates"].is_array()) {
        for (const auto& su : args["step_updates"]) {
            std::string id = su.value("step_id", "");
            std::string st = su.value("status", "pending");
            if (!id.empty()) {
                r.step_updates.emplace_back(id, parseStepStatus(st));
            }
        }
    }
    if (args.contains("propositions") && args["propositions"].is_array()) {
        for (const auto& p : args["propositions"]) {
            if (p.is_string()) r.propositions.push_back(p.get<std::string>());
        }
    }
    return r;
}

} // namespace agentcpp::agent
