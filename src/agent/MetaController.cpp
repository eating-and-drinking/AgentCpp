#include <agent/MetaController.hpp>

#include <utils/Logger.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace agentcpp::agent {

const char* metaActionName(MetaAction a) {
    switch (a) {
        case MetaAction::Act:       return "Act";
        case MetaAction::Reflect:   return "Reflect";
        case MetaAction::Decompose: return "Decompose";
        case MetaAction::Escalate:  return "Escalate";
        case MetaAction::Abort:     return "Abort";
    }
    return "?";
}

MetaController::MetaController(MetaConfig cfg) : cfg_(cfg) {}

void MetaController::reset() {
    belief_ = SelfBelief{};
    iter_                 = 0;
    consecutive_errors_   = 0;
    stall_count_          = 0;
    total_reflects_       = 0;
    total_aborts_         = 0;
    turn_started_         = false;
    last_user_input_.clear();
}

void MetaController::onTurnStart(const std::string& user_input) {
    turn_started_      = true;
    last_user_input_   = user_input;
    consecutive_errors_ = 0;
    stall_count_        = 0;
    iter_               = 0;
}

void MetaController::recordObservation(const IterationObservation& obs) {
    iter_ = obs.turn;
    // Stall heuristic: error or zero-tool no-progress increments the stall
    // counter; a clean tool-using iteration resets it.
    const bool clean = (obs.tool_errors == 0) && obs.any_progress
                       && (obs.tool_calls > 0);
    if (clean) {
        stall_count_ = 0;
    } else if (obs.tool_errors > 0 || !obs.any_progress) {
        stall_count_ += 1;
    }
    if (obs.loop_detected) stall_count_ += 1;
}

void MetaController::observeToolResult(const std::string& tool_name, bool is_error) {
    // Map tool name to a specific competence dimension.  Unknown tool names
    // only update the generic "tool_use" dimension — Layer 4 would add new
    // dimensions for novel tool families.
    std::string specific = "tool_use";
    if (tool_name == "FileEdit" || tool_name == "FileWrite") specific = "code_edit";
    else if (tool_name == "Grep"  || tool_name == "Glob")    specific = "search";

    if (is_error) {
        belief_.observeFailure("tool_use");
        belief_.observeFailure(specific);
        consecutive_errors_ += 1;
    } else {
        belief_.observeSuccess("tool_use");
        belief_.observeSuccess(specific);
        consecutive_errors_ = 0;
    }
}

void MetaController::observeCoTQuality(double q) {
    q = std::clamp(q, 0.0, 1.0);
    belief_.observeMixed("reasoning", q, 1.0);
}

void MetaController::observeProgress(bool made) {
    if (made) {
        belief_.observeSuccess("task_progress");
    } else {
        // Weak negative signal — absence of progress is less diagnostic than
        // an outright error.
        belief_.observeFailure("task_progress", 0.5);
    }
}

double MetaController::currentGamma() const {
    const double g = cfg_.gamma_init
                   * std::pow(cfg_.gamma_decay, static_cast<double>(iter_));
    return std::max(g, cfg_.gamma_min);
}

// EFE(a) = c(a) − E_b[U(a,s)] − γ · I(a; θ).  Lower is better.
double MetaController::expectedFreeEnergy(MetaAction a) const {
    const double progress    = belief_.mean("task_progress");
    const double reasoning   = belief_.mean("reasoning");
    const double tool        = belief_.mean("tool_use");
    const double uncertainty = belief_.overallUncertainty();
    // Generic information gain: higher when belief is more uncertain.
    const double gen_ig      = uncertainty;

    double c = 0.0, U = 0.0, I = 0.0;
    switch (a) {
        case MetaAction::Act:
            c = cfg_.cost_act;
            U = cfg_.progress_weight * progress * 0.5 * (tool + reasoning);
            I = 0.5 * gen_ig;
            break;

        case MetaAction::Reflect:
            c = cfg_.cost_reflect;
            U = cfg_.progress_weight * (1.0 - reasoning) * 1.2;
            if (consecutive_errors_ >= cfg_.reflect_after_failures) U += 2.0;
            I = 1.2 * gen_ig;
            break;

        case MetaAction::Decompose:
            c = cfg_.cost_decompose;
            U = cfg_.progress_weight * (1.0 - progress) * 0.8;
            if (stall_count_ >= cfg_.decompose_after_stalls)        U += 2.0;
            I = 1.5 * gen_ig;
            break;

        case MetaAction::Escalate:
            c = cfg_.cost_escalate;
            U = cfg_.progress_weight * (1.0 - progress) * 0.5;
            if (iter_ >= 6 && progress < 0.3)                       U += 1.5;
            I = 0.0;
            break;

        case MetaAction::Abort:
            // Abort is forbidden before a minimum number of iterations or
            // when the safety switch is off.
            if (!cfg_.allow_abort || iter_ < cfg_.abort_min_iters) {
                return std::numeric_limits<double>::infinity();
            }
            c = cfg_.cost_abort;
            // Only attractive when progress is clearly poor AND we have a
            // reasonably confident estimate (low uncertainty).
            U  = (progress < cfg_.abort_progress_thresh) ? 3.0 : -2.0;
            U -= 3.0 * uncertainty;
            I  = 0.0;
            break;
    }
    return c - U - currentGamma() * I;
}

std::string MetaController::buildReflectionPrompt() const {
    std::ostringstream ss;
    ss << "[Metacognition / reflect] You have hit "
       << consecutive_errors_ << " consecutive tool error(s)";
    if (stall_count_ > 0) ss << " plus " << stall_count_ << " stalled iteration(s)";
    ss << ". Before the next action, briefly state:\n"
       << "  (a) what assumption you've been making that the failures suggest is wrong,\n"
       << "  (b) one concretely different strategy you will now try.\n"
       << "Then act on (b).";
    return ss.str();
}

std::string MetaController::buildDecomposePrompt() const {
    std::ostringstream ss;
    ss << "[Metacognition / decompose] Progress has been stuck for "
       << stall_count_ << " iterations. The current task may be too large to "
       "tackle directly. Decompose it into 2–4 smaller, independently "
       "verifiable subtasks and execute only the first one this iteration.";
    return ss.str();
}

std::string MetaController::buildEscalatePrompt() const {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << "[Metacognition / escalate] After " << iter_
       << " iterations, estimated progress probability is only "
       << belief_.mean("task_progress")
       << ". Consider asking the user one focused clarifying question rather "
          "than continuing exploratory tool use.";
    return ss.str();
}

MetaDecision MetaController::decide(int iter_index) {
    iter_ = iter_index;

    constexpr MetaAction kActions[] = {
        MetaAction::Act,       MetaAction::Reflect, MetaAction::Decompose,
        MetaAction::Escalate,  MetaAction::Abort,
    };
    MetaAction best   = MetaAction::Act;
    double     best_f = std::numeric_limits<double>::infinity();
    for (auto a : kActions) {
        const double f = expectedFreeEnergy(a);
        if (f < best_f) { best_f = f; best = a; }
    }

    MetaDecision d;
    d.action    = best;
    d.efe_score = best_f;

    switch (best) {
        case MetaAction::Act:
            d.reason = "belief stable; no intervention warranted";
            break;
        case MetaAction::Reflect:
            d.injection = buildReflectionPrompt();
            d.reason    = "errors / weak reasoning belief triggered reflection";
            ++total_reflects_;
            break;
        case MetaAction::Decompose:
            d.injection = buildDecomposePrompt();
            d.reason    = "stalled progress triggered decomposition";
            break;
        case MetaAction::Escalate:
            d.injection = buildEscalatePrompt();
            d.reason    = "low progress belief; suggesting user query";
            break;
        case MetaAction::Abort:
            d.reason = "low progress belief AND confident estimate → early stop";
            ++total_aborts_;
            break;
    }
    return d;
}

} // namespace agentcpp::agent
