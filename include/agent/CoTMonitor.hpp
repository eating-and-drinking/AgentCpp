#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  CoTMonitor — Layer 2 of the MERIT framework: Process Monitoring.
//
//  Subscribes to the assistant's streaming text deltas plus tool-call inputs.
//  Detects three kinds of process pathology without consulting any external
//  model:
//
//    1.  Reasoning loop:    a step (≈ paragraph) repeats a recent step verbatim.
//    2.  Low-content step:  consecutive steps fall below a minimum length —
//                           a strong signal of "I'm just stalling".
//    3.  Tool-parameter loop:  the same tool is about to be called with the
//                              same canonical input as a very recent attempt.
//
//  Aggregates these into:
//    - a per-iteration scalar quality q_t ∈ [0,1]  (fed to MetaController as
//      a soft observation of the "reasoning" dimension), and
//    - an optional, ready-to-inject intervention prompt the MetacognitionEngine
//      can attach to the system prompt before the next LLM call.
//
//  No LLM, no fine-tuned classifier: just normalised-string comparisons over
//  a bounded sliding window.  This is by design — every layer in the MERIT
//  framework is training-free.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace agentcpp::agent {

class CoTMonitor {
public:
    struct Config {
        std::size_t step_min_chars     = 60;   // below this = "low-content step"
        std::size_t repeat_window      = 4;    // look back N steps for repetition
        std::size_t loop_window        = 5;    // look back N tool calls
        double      low_quality_thresh = 0.4;  // q below this → intervene
        bool        enable_injection   = true;
    };

    CoTMonitor();
    explicit CoTMonitor(Config c);

    // Lifecycle ──────────────────────────────────────────────────────────────
    void onTurnStart();
    void resetIteration();

    // Stream hooks ───────────────────────────────────────────────────────────
    void onCoTDelta  (const std::string& delta);
    void onCoTBlockEnd();                                 // finalize + compute q
    void onToolCall  (const std::string& tool_name,
                      const std::string& input_canonical);

    // Read-only per-iteration aggregates ─────────────────────────────────────
    double quality()        const { return last_quality_; }
    bool   loopDetected()   const { return loop_detected_; }
    bool   lowQuality()     const { return low_quality_;   }
    bool   anyTextContent() const { return any_text_;      }

    // Returns an intervention prompt iff some pathology fired this iteration
    // and the Config has injection enabled.
    std::optional<std::string> proposeIntervention() const;

private:
    Config                    cfg_;
    std::string               current_buf_;    // accumulating current step
    std::deque<std::string>   recent_steps_;   // normalised step history
    std::deque<std::string>   recent_tools_;   // "name|canonical_input"

    bool   loop_detected_ = false;
    bool   low_quality_   = false;
    bool   any_text_      = false;
    double last_quality_  = 0.5;

    void               finalizeStep();
    static std::string normalize(const std::string& s);
};

} // namespace agentcpp::agent
