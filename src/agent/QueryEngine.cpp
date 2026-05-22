#include <agent/QueryEngine.hpp>
#include <agent/MetacognitionEngine.hpp>
#include <agent/SelfModelMemoryAdapter.hpp>
#include <agent/Persona.hpp>
#include <agent/PlannerEngine.hpp>
#include <agent/Reflector.hpp>
#include <api/ClaudeClient.hpp>
#include <api/Capabilities.hpp>
#include <skills/SkillRegistry.hpp>
#include <memory/MemoryEngine.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <chrono>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace agentcpp::agent {

QueryEngine::QueryEngine(
    std::shared_ptr<agentcpp::api::ClaudeClient> client,
    tools::ToolRegistry& registry
) : client_(std::move(client)),
    registry_(registry),
    metacog_(std::make_unique<MetacognitionEngine>())
{}

void QueryEngine::abort() { aborted_.store(true); }
bool QueryEngine::isAborted() const { return aborted_.load(); }

void QueryEngine::setMetacognitionEngine(std::unique_ptr<MetacognitionEngine> m) {
    sm_adapter_.reset();
    metacog_ = std::move(m);
}

void QueryEngine::disableMetacognition() {
    sm_adapter_.reset();
    metacog_.reset();
}

void QueryEngine::setPlannerEnabled(bool en, std::string model) {
    if (!en) { planner_.reset(); return; }
    if (model.empty()) {
        // Reuse the most recently seen model from runTurn calls. If none yet,
        // defer creation until runTurn; we mark planner_ as nullptr and let
        // runTurn lazily construct.
        planner_ = nullptr;
        return;
    }
    planner_ = std::make_unique<PlannerEngine>(client_, std::move(model));
}

void QueryEngine::setReflectorEnabled(bool en, std::string model) {
    if (!en) { reflector_.reset(); return; }
    if (model.empty()) {
        reflector_ = nullptr;
        return;
    }
    reflector_ = std::make_unique<Reflector>(client_, std::move(model), metacog_.get());
}

void QueryEngine::enableSelfModelPersistence(
    agentcpp::memory::MemoryEngine& engine,
    std::string bank_id) {
    if (!metacog_) {
        LOG_WARN("enableSelfModelPersistence called with metacognition disabled");
        return;
    }
    sm_adapter_.reset();
    sm_adapter_ = std::make_unique<SelfModelMemoryAdapter>(engine, std::move(bank_id));
    sm_adapter_->wire(metacog_->storeMut());
    LOG_INFO("Layer 3 persistence wired to bank " + sm_adapter_->bankId());
}

void QueryEngine::disableSelfModelPersistence() {
    sm_adapter_.reset();
}

void QueryEngine::emit(const AgentEvent& ev) {
    if (on_event_) on_event_(ev);
}

std::string QueryEngine::buildSystemPrompt(const QueryConfig& cfg) const {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream date_ss;
    date_ss << std::put_time(&tm_buf, "%A, %B %d, %Y");
    std::string cwd_str = cfg.tool_ctx.cwd.string();

    std::ostringstream ss;
    const Persona* persona = nullptr;
    if (cfg.system_prompt.empty() && personas_) {
        std::string pid = cfg.persona_id.empty()
                              ? PersonaRegistry::defaultPersonaId()
                              : cfg.persona_id;
        persona = personas_->find(pid);
        if (!persona && !personas_->all().empty()) {
            persona = &personas_->all().front();
        }
    }

    if (!cfg.system_prompt.empty()) {
        ss << cfg.system_prompt;
    } else if (persona) {
        ss << persona->toSystemPromptSection() << "\n";
    } else {
        ss << "You are an AI coding assistant running as a command-line tool.\n\n"
              "Use the available tools to help the user. Be concise.\n";
    }

    ss << "\n## Context\n"
       << "Date: " << date_ss.str() << "\n"
       << "Working directory: " << cwd_str << "\n";

    if (skills_ && skills_->size() > 0) {
        ss << "\n## Available skills\n\n"
              "Call the Skill tool with a skill name to load it.\n";
        for (const auto& s : skills_->all()) {
            ss << "- " << s.name << ": " << s.description << "\n";
        }
    }

    if (memory_ && memory_->isReady()) {
        ss << "\n## Persistent memory (hindsight-style)\n\n"
              "Memory root: " << memory_->root().string() << "\n"
              "Tools: MemoryRetain, MemoryRecall, MemoryReflect, MemoryList.\n"
              "Default bank id: \"" << agentcpp::memory::kDefaultBankId << "\".\n";
        auto bank_ids = memory_->listBanks();
        if (bank_ids.empty()) {
            ss << "(no banks yet — MemoryRetain will create one on first use)\n";
        } else {
            ss << "Banks:\n";
            std::size_t shown = 0;
            for (const auto& bid : bank_ids) {
                auto stats = memory_->getBankStats(bid);
                ss << "- " << bid
                   << ": units=" << stats.units
                   << " (world=" << stats.world
                   << ", experience=" << stats.experience
                   << ", observation=" << stats.observation << ")"
                   << ", entities=" << stats.entities
                   << ", links=" << stats.links
                   << "\n";
                if (++shown >= 10) { ss << "- ...\n"; break; }
            }
        }
    }

    // PR4: inject current plan if non-empty
    if (!current_plan_.steps.empty()) {
        ss << "\n" << current_plan_.toMarkdown();
    }
    return ss.str();
}

bool QueryEngine::decideShouldPlan(const std::string& prompt, const QueryConfig& cfg) const {
    using PM = QueryConfig::PlanMode;
    if (cfg.plan_mode == PM::Never)  return false;
    if (cfg.plan_mode == PM::Always) return true;
    if (cfg.plan_mode == PM::Only)   return true;
    // Auto: heuristic + persona opt-in
    const Persona* p = nullptr;
    if (personas_) {
        std::string pid = cfg.persona_id.empty()
                              ? PersonaRegistry::defaultPersonaId()
                              : cfg.persona_id;
        p = personas_->find(pid);
    }
    return PlannerEngine::shouldPlan(prompt, p);
}

std::vector<std::string> QueryEngine::toolNamesForRequest(const QueryConfig& cfg) const {
    std::vector<std::string> persona_groups;
    if (personas_) {
        const std::string& pid = cfg.persona_id.empty()
                                     ? PersonaRegistry::defaultPersonaId()
                                     : cfg.persona_id;
        if (const Persona* p = personas_->find(pid)) persona_groups = p->toolsets;
    }
    auto defs = registry_.definitionsForPersona(persona_groups,
                                                cfg.enable_toolsets,
                                                cfg.disable_toolsets,
                                                cfg.allowed_tools);
    std::vector<std::string> names;
    names.reserve(defs.size());
    for (const auto& d : defs) names.push_back(d.name);
    return names;
}

std::string QueryEngine::runTurn(
    std::vector<api::Message>& conversation,
    const std::string& user_input,
    const QueryConfig& config
) {
    aborted_.store(false);

    // PR3: prepend pending attachments into the user message
    if (!pending_attachments_.empty()) {
        conversation.push_back(api::Message::userWithParts(
            std::move(pending_attachments_), user_input));
        pending_attachments_.clear();
    } else {
        conversation.push_back(api::Message::userText(user_input));
    }

    if (metacog_) metacog_->onTurnStart(user_input);

    // ── PR4 Phase 1: PLAN ───────────────────────────────────────────────
    if (decideShouldPlan(user_input, config)) {
        // Lazy-construct planner with the active model if needed
        if (!planner_) {
            planner_ = std::make_unique<PlannerEngine>(client_, config.model);
        } else {
            planner_->setModel(config.model);
        }
        std::string persona_mission;
        if (personas_) {
            std::string pid = config.persona_id.empty()
                                  ? PersonaRegistry::defaultPersonaId()
                                  : config.persona_id;
            if (const Persona* p = personas_->find(pid)) persona_mission = p->mission;
        }
        current_plan_ = planner_->plan(user_input, persona_mission,
                                       toolNamesForRequest(config),
                                       config.max_plan_steps);
        if (!current_plan_.steps.empty()) {
            emit(ev::PlanReady{ current_plan_.toMarkdown() });
        }
        if (config.plan_mode == QueryConfig::PlanMode::Only) {
            // Plan-only: skip Act. Return plan markdown as the response.
            if (metacog_) metacog_->onTurnEnd();
            return current_plan_.toMarkdown();
        }
    }

    std::string self_model_prompt;
    if (metacog_) self_model_prompt = metacog_->selfModelPromptSection();

    std::string accumulated_text;
    int turn = 0;

    // ── PR4 Phase 2: ACT (existing turn loop, augmented) ────────────────
    while (turn < config.max_turns && !isAborted()) {
        ++turn;

        std::string metacog_injection;
        if (metacog_) {
            MetaDecision decision = metacog_->beforeNextIteration(turn);
            if (decision.action == MetaAction::Abort) {
                LOG_INFO("MetaController abort: " + decision.reason);
                emit(ev::Error{ "Metacognition early stop: " + decision.reason });
                break;
            }
            metacog_injection = decision.injection;
        }

        emit(ev::RequestStart{});

        api::ApiRequest req;
        req.model      = config.model;
        req.max_tokens = config.max_tokens;
        req.system     = buildSystemPrompt(config);
        req.messages   = conversation;
        req.stream     = true;

        if (!self_model_prompt.empty()) {
            req.system += "\n\n" + self_model_prompt;
        }
        if (!metacog_injection.empty()) {
            req.system += "\n\n## Metacognitive intervention (this call only)\n\n"
                        + metacog_injection;
        }

        // Tool filtering by persona (PR2)
        std::vector<std::string> persona_groups;
        if (personas_) {
            const std::string& pid = config.persona_id.empty()
                                         ? PersonaRegistry::defaultPersonaId()
                                         : config.persona_id;
            if (const Persona* p = personas_->find(pid)) {
                persona_groups = p->toolsets;
            }
        }
        req.tools = registry_.definitionsForPersona(
            persona_groups,
            config.enable_toolsets,
            config.disable_toolsets,
            config.allowed_tools);

        // PR3: downgrade unsupported multimodal blocks for this model
        api::caps::downgradeRequest(req);

        LOG_INFO("QueryEngine: turn=" + std::to_string(turn));

        api::ApiResponse response;
        try {
            response = client_->streamRequest(req, [&](const api::StreamEvent& ev) {
                if (auto* d = std::get_if<api::event::ContentBlockDelta>(&ev)) {
                    if (d->delta_type == "text_delta" && !d->text.empty()) {
                        accumulated_text += d->text;
                        if (metacog_) metacog_->onCoTDelta(d->text);
                        emit(ev::TextDelta{ d->text });
                    }
                }
            });
        } catch (const std::exception& e) {
            emit(ev::Error{ e.what() });
            if (metacog_) metacog_->onTurnEnd();
            return accumulated_text;
        }

        if (metacog_) metacog_->onCoTEnd();
        if (isAborted()) break;

        api::Message assistant_msg;
        assistant_msg.role    = api::Role::Assistant;
        assistant_msg.content = response.content;
        conversation.push_back(assistant_msg);

        emit(ev::TurnComplete{ response.stop_reason, response.usage });
        if (response.stop_reason != api::StopReason::ToolUse) {
            // ── PR4 Phase 3: REFLECT (final pass) ────────────────────────
            if (reflector_ && !current_plan_.steps.empty()) {
                reflector_->setModel(config.model);
                std::vector<api::Message> window;
                std::size_t k = std::min<std::size_t>(conversation.size(), 6);
                window.assign(conversation.end() - k, conversation.end());
                auto r = reflector_->reflect(current_plan_, window, turn);
                for (const auto& [id, st] : r.step_updates)
                    current_plan_.markStatus(id, st);
                if (metacog_) {
                    for (const auto& p : r.propositions)
                        metacog_->storeMut().addProposition({p});
                }
                if (!r.user_visible_note.empty())
                    emit(ev::ReflectionDone{ r.user_visible_note });
            }
            break;
        }

        auto results = executeTools(response.content, config);
        for (auto& m : results) conversation.push_back(std::move(m));

        // ── PR4 Phase 3: REFLECT (periodic) ──────────────────────────────
        if (reflector_ && !current_plan_.steps.empty() &&
            config.reflect_every > 0 && (turn % config.reflect_every == 0))
        {
            reflector_->setModel(config.model);
            std::vector<api::Message> window;
            std::size_t k = std::min<std::size_t>(conversation.size(), 6);
            window.assign(conversation.end() - k, conversation.end());
            auto r = reflector_->reflect(current_plan_, window, turn);
            for (const auto& [id, st] : r.step_updates)
                current_plan_.markStatus(id, st);
            if (metacog_) {
                for (const auto& p : r.propositions)
                    metacog_->storeMut().addProposition({p});
            }
            if (!r.user_visible_note.empty())
                emit(ev::ReflectionDone{ r.user_visible_note });
            if (r.plan_needs_revision && planner_) {
                std::ostringstream obs;
                for (const auto& m : window) {
                    for (const auto& cb : m.content) {
                        if (auto* t = std::get_if<api::TextBlock>(&cb))
                            obs << t->text << "\n";
                    }
                }
                current_plan_ = planner_->replan(current_plan_,
                                                 r.revision_reason, obs.str());
                emit(ev::PlanRevised{ r.revision_reason,
                                       current_plan_.toMarkdown() });
            }
        }
    }

    if (metacog_) metacog_->onTurnEnd();
    if (metacog_) LOG_DEBUG(metacog_->snapshot());
    if (turn >= config.max_turns) LOG_WARN("max_turns reached");
    return accumulated_text;
}

std::vector<api::Message> QueryEngine::executeTools(
    const std::vector<api::ContentBlock>& content,
    const QueryConfig& config
) {
    std::vector<api::Message> out;
    for (const auto& b : content) {
        if (isAborted()) break;
        const auto* tu = std::get_if<api::ToolUseBlock>(&b);
        if (!tu) continue;

        if (metacog_) metacog_->onToolUse(tu->name, tu->input.dump());
        emit(ev::ToolStart{ tu->id, tu->name, tu->input.dump().substr(0, 200) });

        tools::ToolCallResult r;
        auto tool = registry_.findTool(tu->name);
        if (!tool) {
            r = tools::ToolCallResult::error("Unknown tool: " + tu->name);
        } else {
            auto verr = tool->validateInput(tu->input);
            if (!verr.empty()) {
                r = tools::ToolCallResult::error(verr);
            } else {
                try { r = tool->execute(tu->input, config.tool_ctx); }
                catch (const std::exception& e) {
                    r = tools::ToolCallResult::error(std::string("threw: ") + e.what());
                } catch (...) {
                    r = tools::ToolCallResult::error("threw unknown");
                }
            }
        }

        if (metacog_) metacog_->onToolResult(tu->name, r.is_error);
        emit(ev::ToolEnd{ tu->id, tu->name, r.content, r.is_error });

        if (r.hasMultiparts()) {
            std::vector<api::ImageBlock> imgs = std::move(r.extra_images);
            if (!r.image_b64.empty()) {
                api::ImageBlock legacy;
                legacy.media_type = r.image_media_type.empty() ? "image/png" : r.image_media_type;
                legacy.data       = std::move(r.image_b64);
                imgs.push_back(std::move(legacy));
            }
            out.push_back(api::Message::toolResultMulti(
                tu->id, std::move(r.content), std::move(imgs),
                std::move(r.extra_data), r.is_error));
        } else if (!r.image_b64.empty()) {
            out.push_back(api::Message::toolResultWithImage(
                tu->id, std::move(r.content), std::move(r.image_b64),
                std::move(r.image_media_type), r.is_error));
        } else {
            out.push_back(api::Message::toolResult(
                tu->id, std::move(r.content), r.is_error));
        }
    }
    return out;
}

} // namespace agentcpp::agent
