#include <agent/CoTMonitor.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace agentcpp::agent {

CoTMonitor::CoTMonitor()         : cfg_(Config{}) {}
CoTMonitor::CoTMonitor(Config c) : cfg_(c)        {}

void CoTMonitor::onTurnStart() {
    current_buf_.clear();
    recent_steps_.clear();
    recent_tools_.clear();
    loop_detected_ = false;
    low_quality_   = false;
    any_text_      = false;
    last_quality_  = 0.5;
}

void CoTMonitor::resetIteration() {
    // Keep step history (we want cross-iteration loop detection) but reset
    // per-iteration aggregates and the current buffer.
    current_buf_.clear();
    loop_detected_ = false;
    low_quality_   = false;
    any_text_      = false;
    last_quality_  = 0.5;
}

std::string CoTMonitor::normalize(const std::string& s) {
    // Lowercased, single-spaced, trimmed. Cheap canonical form so paraphrase-
    // free repetition gets detected.
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

void CoTMonitor::onCoTDelta(const std::string& delta) {
    if (!delta.empty()) any_text_ = true;
    current_buf_ += delta;

    // Step boundary heuristic: a blank line (two consecutive newlines) closes
    // a step. Works for both plain prose and markdown CoT; structured CoT
    // would warrant a smarter parser, but the cheap heuristic catches the
    // common case.
    for (;;) {
        const auto pos = current_buf_.find("\n\n");
        if (pos == std::string::npos) break;
        recent_steps_.push_back(normalize(current_buf_.substr(0, pos)));
        current_buf_.erase(0, pos + 2);
        if (recent_steps_.size() > cfg_.repeat_window * 2) {
            recent_steps_.pop_front();
        }
    }
}

void CoTMonitor::finalizeStep() {
    if (!current_buf_.empty()) {
        recent_steps_.push_back(normalize(current_buf_));
        current_buf_.clear();
        if (recent_steps_.size() > cfg_.repeat_window * 2) {
            recent_steps_.pop_front();
        }
    }
}

void CoTMonitor::onCoTBlockEnd() {
    finalizeStep();

    // (1) verbatim-step repetition vs. the previous N steps
    int repeats = 0;
    if (recent_steps_.size() >= 2) {
        const auto& last = recent_steps_.back();
        const std::size_t scan =
            std::min<std::size_t>(cfg_.repeat_window, recent_steps_.size() - 1);
        for (std::size_t i = 1; i <= scan; ++i) {
            const auto& earlier = recent_steps_[recent_steps_.size() - 1 - i];
            if (!last.empty() && last == earlier) ++repeats;
        }
    }
    const bool repeating = repeats >= 1;

    // (2) recent N steps all too short
    std::size_t low_count = 0;
    const std::size_t scan_n =
        std::min<std::size_t>(cfg_.repeat_window, recent_steps_.size());
    for (std::size_t i = 0; i < scan_n; ++i) {
        const auto& s = recent_steps_[recent_steps_.size() - 1 - i];
        if (s.size() < cfg_.step_min_chars) ++low_count;
    }
    const bool too_short = scan_n > 0 && low_count >= scan_n;

    // Aggregate into [0,1]. Tuned conservatively: a single mild signal does
    // not push quality below the injection threshold, but two signals do.
    double q = 1.0;
    if (repeating) q -= 0.5;
    if (too_short) q -= 0.3;
    if (!any_text_) q = 0.5;
    last_quality_ = std::clamp(q, 0.0, 1.0);
    low_quality_  = last_quality_ < cfg_.low_quality_thresh;
}

void CoTMonitor::onToolCall(const std::string& tool_name,
                            const std::string& input_canonical) {
    const std::string key = tool_name + "|" + input_canonical;
    int matches = 0;
    for (const auto& k : recent_tools_) if (k == key) ++matches;
    recent_tools_.push_back(key);
    if (recent_tools_.size() > cfg_.loop_window) recent_tools_.pop_front();
    if (matches >= 1) loop_detected_ = true;
}

std::optional<std::string> CoTMonitor::proposeIntervention() const {
    if (!cfg_.enable_injection)           return std::nullopt;
    if (!low_quality_ && !loop_detected_) return std::nullopt;

    std::ostringstream ss;
    ss << "[Metacognition / process] ";
    if (loop_detected_) {
        ss << "You are about to call a tool with effectively the same input as "
              "a very recent attempt that did not advance the task. ";
    }
    if (low_quality_) {
        ss << "Your last reasoning step(s) were either repetitive or unusually "
              "thin. ";
    }
    ss << "Before the next action, briefly state in one line: (a) what changed "
          "since the prior attempt, and (b) what concretely different thing "
          "you will try now.";
    return ss.str();
}

} // namespace agentcpp::agent
