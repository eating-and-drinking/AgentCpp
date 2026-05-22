#include <agent/QueryEngine.hpp>
#include <agent/MetacognitionEngine.hpp>
#include <agent/SelfModelMemoryAdapter.hpp>
#include <api/ClaudeClient.hpp>
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
    metacog_(std::make_unique<MetacognitionEngine>())  // MERIT default-on
{}

void QueryEngine::abort() { aborted_.store(true); }
bool QueryEngine::isAborted() const { return aborted_.load(); }

void QueryEngine::setMetacognitionEngine(std::unique_ptr<MetacognitionEngine> m) {
    sm_adapter_.reset();  // drop lambdas referencing the soon-replaced store
    metacog_ = std::move(m);
}

void QueryEngine::disableMetacognition() {
    sm_adapter_.reset();
    metacog_.reset();
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
    if (!cfg.system_prompt.empty()) {
        ss << cfg.system_prompt;
    } else {
        ss << "You are an AI coding assistant running as a command-line tool.\n\n"
           << "Current date: " << date_ss.str() << "\n"
           << "Working directory: " << cwd_str << "\n\n"
           << "Use the available tools to help the user. Be concise.\n";
    }

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
    return ss.str();
}

std::string QueryEngine::runTurn(
    std::vector<api::Message>& conversation,
    const std::string& user_input,
    const QueryConfig& config
) {
    aborted_.store(false);
    conversation.push_back(api::Message::userText(user_input));

    if (metacog_) metacog_->onTurnStart(user_input);

    std::string self_model_prompt;
    if (metacog_) self_model_prompt = metacog_->selfModelPromptSection();

    std::string accumulated_text;
    int turn = 0;

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

        for (const auto& def : registry_.definitions()) {
            if (config.allowed_tools.empty()) {
                req.tools.push_back(def);
            } else {
                for (const auto& a : config.allowed_tools) {
                    if (def.name == a) { req.tools.push_back(def); break; }
                }
            }
        }

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
        if (response.stop_reason != api::StopReason::ToolUse) break;

        auto results = executeTools(response.content, config);
        for (auto& m : results) conversation.push_back(std::move(m));
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
        if (r.image_b64.empty()) {
            out.push_back(api::Message::toolResult(tu->id, r.content, r.is_error));
        } else {
            out.push_back(api::Message::toolResultWithImage(
                tu->id, r.content, r.image_b64, r.image_media_type, r.is_error));
        }
    }
    return out;
}

} // namespace agentcpp::agent
