#include <agent/PlanGraph.hpp>
#include <algorithm>
#include <sstream>

namespace agentcpp::agent {

std::string stepStatusToString(StepStatus s) {
    switch (s) {
        case StepStatus::Pending:    return "pending";
        case StepStatus::InProgress: return "in_progress";
        case StepStatus::Done:       return "done";
        case StepStatus::Skipped:    return "skipped";
        case StepStatus::Failed:     return "failed";
    }
    return "unknown";
}

StepStatus parseStepStatus(const std::string& s) {
    if (s == "pending")     return StepStatus::Pending;
    if (s == "in_progress") return StepStatus::InProgress;
    if (s == "done")        return StepStatus::Done;
    if (s == "skipped")     return StepStatus::Skipped;
    if (s == "failed")      return StepStatus::Failed;
    return StepStatus::Pending;
}

const PlanStep* PlanGraph::findStep(const std::string& id) const {
    for (const auto& s : steps) if (s.id == id) return &s;
    return nullptr;
}

PlanStep* PlanGraph::nextRunnable() {
    for (auto& s : steps) {
        if (s.status != StepStatus::Pending) continue;
        bool ok = true;
        for (const auto& bid : s.blockers) {
            const PlanStep* b = findStep(bid);
            if (!b || b->status != StepStatus::Done) { ok = false; break; }
        }
        if (ok) return &s;
    }
    return nullptr;
}

void PlanGraph::markStatus(const std::string& id, StepStatus st, std::string note) {
    for (auto& s : steps) {
        if (s.id == id) {
            s.status = st;
            if (!note.empty()) s.notes = std::move(note);
            return;
        }
    }
}

bool PlanGraph::allDone() const {
    for (const auto& s : steps) {
        if (s.status == StepStatus::Pending || s.status == StepStatus::InProgress)
            return false;
    }
    return !steps.empty();
}

std::string PlanGraph::toMarkdown() const {
    std::ostringstream ss;
    ss << "## Plan";
    if (revision > 0) ss << " (revision " << revision << ")";
    ss << "\n";
    if (!goal.empty()) ss << "Goal: " << goal << "\n";
    ss << "\n";
    for (const auto& s : steps) {
        const char* mark = "[ ]";
        switch (s.status) {
            case StepStatus::Done:       mark = "[x]"; break;
            case StepStatus::InProgress: mark = "[~]"; break;
            case StepStatus::Skipped:    mark = "[-]"; break;
            case StepStatus::Failed:     mark = "[!]"; break;
            default: break;
        }
        ss << "- " << mark << " " << s.id << "  " << s.subject;
        if (s.status == StepStatus::Done)
            ss << "  (" << stepStatusToString(s.status) << ")";
        else if (!s.acceptance.empty())
            ss << "  — " << s.acceptance;
        ss << "\n";

        if (!s.tool_hints.empty()) {
            ss << "      tools:";
            for (const auto& t : s.tool_hints) ss << " " << t;
            ss << "\n";
        }
        if (!s.blockers.empty()) {
            ss << "      blocked by:";
            for (const auto& b : s.blockers) ss << " " << b;
            ss << "\n";
        }
        if (!s.notes.empty()) {
            ss << "      note: " << s.notes << "\n";
        }
    }
    return ss.str();
}

nlohmann::json PlanGraph::toJson() const {
    nlohmann::json out;
    out["goal"]     = goal;
    out["revision"] = revision;
    nlohmann::json st = nlohmann::json::array();
    for (const auto& s : steps) {
        nlohmann::json j;
        j["id"]         = s.id;
        j["subject"]    = s.subject;
        j["acceptance"] = s.acceptance;
        j["status"]     = stepStatusToString(s.status);
        nlohmann::json hints = nlohmann::json::array();
        for (const auto& t : s.tool_hints) hints.push_back(t);
        j["tool_hints"] = hints;
        nlohmann::json blk = nlohmann::json::array();
        for (const auto& b : s.blockers)   blk.push_back(b);
        j["blockers"]   = blk;
        j["notes"]      = s.notes;
        st.push_back(j);
    }
    out["steps"] = st;
    return out;
}

PlanGraph PlanGraph::fromJson(const nlohmann::json& j) {
    PlanGraph g;
    g.goal     = j.value("goal", std::string{});
    g.revision = j.value("revision", 0);
    if (j.contains("steps") && j["steps"].is_array()) {
        for (const auto& sj : j["steps"]) {
            PlanStep s;
            s.id         = sj.value("id", "");
            s.subject    = sj.value("subject", "");
            s.acceptance = sj.value("acceptance", "");
            s.status     = parseStepStatus(sj.value("status", "pending"));
            s.notes      = sj.value("notes", "");
            if (sj.contains("tool_hints") && sj["tool_hints"].is_array()) {
                for (const auto& t : sj["tool_hints"])
                    s.tool_hints.push_back(t.get<std::string>());
            }
            if (sj.contains("blockers") && sj["blockers"].is_array()) {
                for (const auto& b : sj["blockers"])
                    s.blockers.push_back(b.get<std::string>());
            }
            g.steps.push_back(std::move(s));
        }
    }
    return g;
}

} // namespace agentcpp::agent
