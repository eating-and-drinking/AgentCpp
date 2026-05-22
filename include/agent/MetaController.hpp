#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  MetaController — Layer 1 of the MERIT framework.
//
//  Maintains the self-belief b_t (SelfBelief) and selects a meta-action a^m
//  from {Act, Reflect, Decompose, Escalate, Abort} by minimising Expected
//  Free Energy:
//
//      F(a) = c(a)  −  E_b[ U(a, s) ]  −  γ · I(a; θ | s)
//             └ cost  └ pragmatic value └ epistemic value
//
//  No model training: c, U, I are all closed-form / heuristic given b_t and
//  per-iteration counters (errors, stalls, loop signals).  γ is annealed by
//  iteration so the early-task agent is more exploratory and the late-task
//  agent is more decisive.
// ─────────────────────────────────────────────────────────────────────────────
#include <agent/SelfBelief.hpp>
#include <api/Types.hpp>
#include <string>

namespace agentcpp::agent {

// The five-action meta space.
enum class MetaAction {
    Act,        // proceed normally — default
    Reflect,    // inject a reflection prompt before the next LLM call
    Decompose,  // inject a task-decomposition prompt
    Escalate,   // suggest asking the user / using a stronger model
    Abort,      // terminate this turn early
};

const char* metaActionName(MetaAction a);

struct IterationObservation {
    int  turn            = 0;
    int  tool_calls      = 0;
    int  tool_errors     = 0;
    bool any_progress    = true;
    bool loop_detected   = false;
    bool low_quality_cot = false;
    api::StopReason stop_reason = api::StopReason::Unknown;
};

struct MetaDecision {
    MetaAction  action    = MetaAction::Act;
    std::string injection;
    std::string reason;
    double      efe_score = 0.0;
};

struct MetaConfig {
    double cost_act       = 1.0;
    double cost_reflect   = 1.4;
    double cost_decompose = 2.0;
    double cost_escalate  = 0.3;
    double cost_abort     = 0.1;

    double progress_weight = 5.0;

    double gamma_init   = 1.5;
    double gamma_min    = 0.1;
    double gamma_decay  = 0.85;

    int    abort_min_iters        = 4;
    double abort_progress_thresh  = 0.25;
    int    reflect_after_failures = 2;
    int    decompose_after_stalls = 3;

    bool   allow_abort = true;
};

class MetaController {
public:
    explicit MetaController(MetaConfig cfg = MetaConfig{});

    void reset();
    void onTurnStart(const std::string& user_input);
    void recordObservation(const IterationObservation& obs);

    void observeToolResult (const std::string& tool_name, bool is_error);
    void observeCoTQuality (double quality_in_unit_interval);
    void observeProgress   (bool made_progress);

    MetaDecision decide(int iter_index);

    const SelfBelief& belief()            const { return belief_; }
    SelfBelief&       beliefMut()               { return belief_; }
    int               consecutiveErrors() const { return consecutive_errors_; }
    int               stallCount()        const { return stall_count_; }
    int               totalReflects()     const { return total_reflects_; }
    int               totalAborts()       const { return total_aborts_; }
    const MetaConfig& config()            const { return cfg_; }

private:
    MetaConfig  cfg_;
    SelfBelief  belief_;

    int  iter_               = 0;
    int  consecutive_errors_ = 0;
    int  stall_count_        = 0;
    int  total_reflects_     = 0;
    int  total_aborts_       = 0;
    bool turn_started_       = false;
    std::string last_user_input_;

    double currentGamma() const;
    double expectedFreeEnergy(MetaAction a) const;
    std::string buildReflectionPrompt() const;
    std::string buildDecomposePrompt()  const;
    std::string buildEscalatePrompt()   const;
};

} // namespace agentcpp::agent
