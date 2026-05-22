#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  MetacognitionEngine — façade over the full four-layer MERIT stack:
//
//      Layer 1  Outcome Monitoring        (MetaController + SelfBelief)
//      Layer 2  Process Monitoring        (CoTMonitor)
//      Layer 3  Structured Self-Model     (SelfModelStore)
//      Layer 4  Self-Model Revision       (SchemaReviser)
//
//  All four layers are training-free.  Layers 1 and 2 act per-iteration; Layer
//  3 acts per-turn (retrieve relevant self-knowledge at the start, record
//  reflection at the end); Layer 4 acts per-N-episodes (cluster recent
//  failures and propose schema changes that the engine apply()-s under
//  stability checks).
//
//  Conceptual API used by QueryEngine each turn:
//
//    metacog.onTurnStart(user_input);
//    for each iteration i in the tool loop:
//        decision = metacog.beforeNextIteration(i);
//        if decision.action == Abort: break
//        // optional: prepend decision.injection to the system prompt
//        … stream Claude response …
//            metacog.onCoTDelta(text_delta);
//        metacog.onCoTEnd();
//        for each tool use t:
//            metacog.onToolUse(t.name, canonical_input);
//            … run tool …
//            metacog.onToolResult(t.name, is_error);
//    metacog.onTurnEnd();    // triggers Layer 3 reflection + Layer 4 review
//
//  Layer 3's relevant self-knowledge for the current task is exposed via
//  selfModelPromptSection() — QueryEngine appends it to the system prompt
//  at turn start.
// ─────────────────────────────────────────────────────────────────────────────
#include <agent/CoTMonitor.hpp>
#include <agent/MetaController.hpp>
#include <agent/SchemaReviser.hpp>
#include <agent/SelfModelStore.hpp>

#include <functional>
#include <string>

namespace agentcpp::agent {

struct MetacognitionEvent {
    std::string kind;    // "turn_start" | "decision" | "intervention" | "abort"
                         // | "cot_warn" | "reflect" | "schema_revise"
    std::string detail;
};

using MetacognitionEventCallback = std::function<void(const MetacognitionEvent&)>;

class MetacognitionEngine {
public:
    struct Config {
        MetaConfig            layer1;
        CoTMonitor::Config    layer2;
        SchemaReviser::Config layer4;
        std::size_t           prompt_top_k = 3;
        bool                  enable_layer3 = true;
        bool                  enable_layer4 = true;
    };

    MetacognitionEngine();
    explicit MetacognitionEngine(Config cfg);

    // Backwards-compatible 2-layer constructor (kept so QueryEngine's existing
    // `make_unique<MetacognitionEngine>()` call site does not break).
    MetacognitionEngine(MetaConfig l1, CoTMonitor::Config l2);

    void setEventCallback(MetacognitionEventCallback cb) { on_event_ = std::move(cb); }

    // Lifecycle ──────────────────────────────────────────────────────────────
    void onTurnStart(const std::string& user_input);
    void onTurnEnd  ();

    // Stream / tool hooks (per-iteration) ────────────────────────────────────
    void onCoTDelta  (const std::string& delta);
    void onCoTEnd    ();
    void onToolUse   (const std::string& name, const std::string& canonical_input);
    void onToolResult(const std::string& name, bool is_error);

    // Decision before next iteration (Layer 1 + Layer 2 coupling).
    MetaDecision beforeNextIteration(int iter_index);

    // Layer 3: ready-to-inject prompt section for the current turn.  Returns
    // empty if no relevant self-knowledge is found or Layer 3 is disabled.
    std::string selfModelPromptSection() const;

    // Manual hook so callers can seed propositions before a run.
    void addProposition(SelfProposition p) { store_.addProposition(std::move(p)); }

    // Read-only ──────────────────────────────────────────────────────────────
    const MetaController& controller() const { return controller_; }
    const CoTMonitor&     monitor()    const { return cot_; }
    const SelfModelStore& store()      const { return store_; }
    SelfModelStore&       storeMut()         { return store_; }
    const SchemaReviser&  reviser()    const { return reviser_; }
    SelfBelief&           beliefMut()        { return controller_.beliefMut(); }

    std::string           snapshot() const;

private:
    Config                         cfg_;
    MetaController                 controller_;
    CoTMonitor                     cot_;
    SelfModelStore                 store_;
    SchemaReviser                  reviser_;
    MetacognitionEventCallback     on_event_;

    int  iter_                  = 0;
    int  tool_calls_in_iter_    = 0;
    int  tool_errors_in_iter_   = 0;

    // Per-turn state for Layer 3/4.
    std::string                    current_task_;
    int                            turn_tool_errors_ = 0;
    int                            turn_tool_calls_  = 0;

    void emit(const std::string& kind, const std::string& detail);
    void runReflectionAtTurnEnd();
    void runSchemaRevisionIfDue();
};

} // namespace agentcpp::agent
