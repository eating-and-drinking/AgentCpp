#include <agent/MetacognitionEngine.hpp>

#include <utils/Logger.hpp>

#include <sstream>

namespace agentcpp::agent {

MetacognitionEngine::MetacognitionEngine()
    : MetacognitionEngine(Config{}) {}

MetacognitionEngine::MetacognitionEngine(Config cfg)
    : cfg_(cfg),
      controller_(cfg.layer1),
      cot_(cfg.layer2),
      reviser_(cfg.layer4) {}

MetacognitionEngine::MetacognitionEngine(MetaConfig l1, CoTMonitor::Config l2)
    : MetacognitionEngine([&]{
        Config c;
        c.layer1 = l1;
        c.layer2 = l2;
        return c;
    }()) {}

void MetacognitionEngine::emit(const std::string& kind, const std::string& detail) {
    if (on_event_) on_event_(MetacognitionEvent{kind, detail});
}

void MetacognitionEngine::onTurnStart(const std::string& user_input) {
    controller_.reset();
    cot_.onTurnStart();
    controller_.onTurnStart(user_input);

    iter_                = 0;
    tool_calls_in_iter_  = 0;
    tool_errors_in_iter_ = 0;
    turn_tool_calls_     = 0;
    turn_tool_errors_    = 0;
    current_task_        = user_input;

    std::string snippet = user_input.substr(0, 80);
    emit("turn_start", std::string("user_input=\"") + snippet + "\"");
    LOG_DEBUG("MetacognitionEngine: turn start");
}

void MetacognitionEngine::onTurnEnd() {
    if (cfg_.enable_layer3) {
        runReflectionAtTurnEnd();
    }
    if (cfg_.enable_layer4) {
        reviser_.noteEpisodeComplete();
        runSchemaRevisionIfDue();
    }
    // Layer 3 persistence: writes through to MemoryEngine if an adapter has
    // been wired via SelfModelStore::setPersistFn (no-op otherwise).
    if (cfg_.enable_layer3) {
        store_.saveToExternal();
    }
    LOG_DEBUG(std::string("MetacognitionEngine: turn end — ") + snapshot());
}

void MetacognitionEngine::onCoTDelta(const std::string& delta) {
    cot_.onCoTDelta(delta);
}

void MetacognitionEngine::onCoTEnd() {
    cot_.onCoTBlockEnd();
    controller_.observeCoTQuality(cot_.quality());
    if (cot_.lowQuality() || cot_.loopDetected()) {
        std::ostringstream ss;
        ss << "quality=" << cot_.quality()
           << " loop=" << (cot_.loopDetected() ? "1" : "0");
        emit("cot_warn", ss.str());
    }
}

void MetacognitionEngine::onToolUse(const std::string& name,
                                    const std::string& canonical_input) {
    cot_.onToolCall(name, canonical_input);
    ++tool_calls_in_iter_;
    ++turn_tool_calls_;
}

void MetacognitionEngine::onToolResult(const std::string& name, bool is_error) {
    controller_.observeToolResult(name, is_error);
    controller_.observeProgress(!is_error);
    if (is_error) {
        ++tool_errors_in_iter_;
        ++turn_tool_errors_;
        if (cfg_.enable_layer4) {
            FailureEvent ev;
            ev.tool        = name;
            ev.description = name + " failed";
            ev.task_type   = current_task_.substr(0, 40);
            reviser_.recordFailure(std::move(ev));
        }
    } else {
        if (cfg_.enable_layer4) reviser_.recordSuccess(name);
    }
}

MetaDecision MetacognitionEngine::beforeNextIteration(int iter_index) {
    iter_ = iter_index;

    IterationObservation obs;
    obs.turn            = iter_index;
    obs.tool_calls      = tool_calls_in_iter_;
    obs.tool_errors     = tool_errors_in_iter_;
    obs.any_progress    = tool_calls_in_iter_ > 0
                          && tool_errors_in_iter_ < tool_calls_in_iter_;
    obs.loop_detected   = cot_.loopDetected();
    obs.low_quality_cot = cot_.lowQuality();
    controller_.recordObservation(obs);

    MetaDecision decision = controller_.decide(iter_index);

    if (decision.action == MetaAction::Act) {
        if (auto pm = cot_.proposeIntervention()) {
            decision.injection = *pm;
            decision.action    = MetaAction::Reflect;
            std::string why;
            if (cot_.loopDetected()) why += "loop ";
            if (cot_.lowQuality())   why += "low_quality";
            decision.reason    = "process-monitor: " + why;
        }
    }

    tool_calls_in_iter_  = 0;
    tool_errors_in_iter_ = 0;
    cot_.resetIteration();

    std::ostringstream ss;
    ss << metaActionName(decision.action)
       << " efe=" << decision.efe_score
       << " reason=" << decision.reason;
    emit("decision", ss.str());
    if (decision.action == MetaAction::Abort) emit("abort", decision.reason);
    if (!decision.injection.empty())
        emit("intervention", decision.injection.substr(0, 100));

    return decision;
}

std::string MetacognitionEngine::selfModelPromptSection() const {
    if (!cfg_.enable_layer3) return {};
    return store_.renderForPrompt(current_task_, cfg_.prompt_top_k);
}

void MetacognitionEngine::runReflectionAtTurnEnd() {
    if (turn_tool_calls_ == 0) return;
    double err_rate = static_cast<double>(turn_tool_errors_)
                    / static_cast<double>(turn_tool_calls_);

    std::ostringstream tx;
    std::vector<std::string> tags;
    if (err_rate >= 0.5) {
        tx << "Tasks resembling \""
           << current_task_.substr(0, 60)
           << "\" tend to produce a high tool-error rate ("
           << static_cast<int>(err_rate * 100) << "%) for me.";
        tags = {"high_error_rate"};
    } else if (turn_tool_calls_ >= 4 && err_rate <= 0.1) {
        tx << "Tasks resembling \""
           << current_task_.substr(0, 60)
           << "\" I have completed competently before (err<=10%).";
        tags = {"competent"};
    } else {
        return;
    }

    SelfProposition p;
    p.text       = tx.str();
    p.tags       = std::move(tags);
    p.confidence = 0.5;
    store_.addProposition(std::move(p));
    emit("reflect", p.text.substr(0, 100));
}

void MetacognitionEngine::runSchemaRevisionIfDue() {
    if (!reviser_.shouldReview()) return;
    auto proposals = reviser_.proposeRevisions(controller_.belief(), store_);
    int applied = 0;
    for (auto& p : proposals) {
        if (reviser_.apply(p, beliefMut(), store_)) ++applied;
    }
    if (applied > 0) {
        std::ostringstream ss;
        ss << "applied=" << applied << " of " << proposals.size();
        emit("schema_revise", ss.str());
    }
}

std::string MetacognitionEngine::snapshot() const {
    std::ostringstream ss;
    ss << "Metacog[iter=" << iter_
       << " " << controller_.belief().toString()
       << " errs_streak=" << controller_.consecutiveErrors()
       << " stalls="      << controller_.stallCount()
       << " reflects="    << controller_.totalReflects()
       << " aborts="      << controller_.totalAborts()
       << " props="       << store_.size()
       << " buf_fail="    << reviser_.bufferedFailures()
       << "]";
    return ss.str();
}

} // namespace agentcpp::agent
