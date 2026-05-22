#include <tui/App.hpp>
#include <utils/Logger.hpp>
#include <utils/StringUtils.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <iostream>
#include <thread>
#include <sstream>
#include <iomanip>
#include <ctime>

using namespace ftxui;

namespace agentcpp::tui {

// ── FTXUI implementation details ──────────────────────────────────────────────
struct App::FtxuiImpl {
    ScreenInteractive* screen = nullptr;
};

// ── Destructor (defined here where FtxuiImpl is complete) ───────────────────
App::~App() = default;

// ── Construction ──────────────────────────────────────────────────────────────
App::App(
    std::shared_ptr<agentcpp::api::ClaudeClient> client,
    tools::ToolRegistry& registry,
    AppConfig config
) : client_(std::move(client))
  , registry_(registry)
  , config_(std::move(config))
  , engine_(client_, registry_)
  , impl_(std::make_unique<FtxuiImpl>())
{
    // Wire optional subsystems into the engine for system-prompt injection
    engine_.setSkillRegistry(config_.skills);
    engine_.setMemoryEngine (config_.memory);

    engine_.setEventCallback([this](const AgentEvent& ev) {
        handleEvent(ev);
    });
}

// ── Thread-safe log append ────────────────────────────────────────────────────
void App::addEntry(ChatEntry entry) {
    std::lock_guard<std::mutex> lk(log_mu_);
    chat_log_.push_back(std::move(entry));
}

// ── Handle events from the worker thread ─────────────────────────────────────
void App::handleEvent(const AgentEvent& agent_ev) {
    std::visit([&](auto&& e) {
        using T = std::decay_t<decltype(e)>;
        namespace aev = agentcpp::agent::ev;

        if constexpr (std::is_same_v<T, aev::RequestStart>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                thinking_ = true;
                status_line_ = "Thinking...";
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
        else if constexpr (std::is_same_v<T, aev::TextDelta>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                partial_text_ += e.text;
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
        else if constexpr (std::is_same_v<T, aev::ToolStart>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                // Flush partial text first
                if (!partial_text_.empty()) {
                    chat_log_.push_back({ ChatEntry::Kind::AssistantText, partial_text_ });
                    partial_text_.clear();
                }
                std::string preview = e.name + "(" + utils::truncate(e.input_preview, 80) + ")";
                chat_log_.push_back({ ChatEntry::Kind::ToolCall, preview, e.name });
                status_line_ = "Running: " + e.name;
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
        else if constexpr (std::is_same_v<T, aev::ToolEnd>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                std::string result_preview = utils::truncate(e.result, 120);
                chat_log_.push_back({
                    ChatEntry::Kind::ToolResult,
                    result_preview,
                    e.name,
                    e.is_error
                });
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
        else if constexpr (std::is_same_v<T, aev::TurnComplete>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                // Flush any remaining partial text
                if (!partial_text_.empty()) {
                    chat_log_.push_back({ ChatEntry::Kind::AssistantText, partial_text_ });
                    partial_text_.clear();
                }
                thinking_    = false;
                status_line_ = "Ready  [tokens in:" +
                    std::to_string(e.usage.input_tokens) + " out:" +
                    std::to_string(e.usage.output_tokens) + "]";
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
        else if constexpr (std::is_same_v<T, aev::Error>) {
            {
                std::lock_guard<std::mutex> lk(log_mu_);
                if (!partial_text_.empty()) {
                    chat_log_.push_back({ ChatEntry::Kind::AssistantText, partial_text_ });
                    partial_text_.clear();
                }
                chat_log_.push_back({ ChatEntry::Kind::Error, "Error: " + e.message, "", true });
                thinking_    = false;
                status_line_ = "Error";
                worker_running_.store(false);
            }
            if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
        }
    }, agent_ev);
}

// ── Submit a turn asynchronously ──────────────────────────────────────────────
void App::submitTurn(const std::string& user_input) {
    if (worker_running_.load()) return;
    worker_running_.store(true);

    addEntry({ ChatEntry::Kind::User, user_input });

    // Capture what we need
    auto conv_copy = conversation_;

    std::thread([this, user_input, conv_copy]() mutable {
        engine_.runTurn(conv_copy, user_input, config_.query);

        {
            std::lock_guard<std::mutex> lk(log_mu_);
            conversation_ = std::move(conv_copy);
            worker_running_.store(false);
        }
        if (impl_->screen) impl_->screen->PostEvent(Event::Custom);
    }).detach();
}

// ── Render a single chat entry as an FTXUI Element ───────────────────────────
static Element renderEntry(const ChatEntry& entry) {
    switch (entry.kind) {
        case ChatEntry::Kind::User:
            return hbox({
                text("You  ") | bold | color(Color::Cyan),
                paragraph(entry.text)
            });

        case ChatEntry::Kind::AssistantText:
            return hbox({
                text("     ") ,
                paragraph(entry.text)
            });

        case ChatEntry::Kind::ToolCall:
            return hbox({
                text("⚙ ") | color(Color::Yellow),
                text(entry.tool_name.empty() ? "Tool" : entry.tool_name) | bold | color(Color::Yellow),
                text(": "),
                paragraph(entry.text) | color(Color::GrayLight)
            });

        case ChatEntry::Kind::ToolResult:
            return hbox({
                text(entry.is_error ? "✗ " : "✓ ") | color(entry.is_error ? Color::Red : Color::Green),
                paragraph(entry.text) | color(entry.is_error ? Color::Red : Color::GrayLight)
            });

        case ChatEntry::Kind::SystemInfo:
            return text(entry.text) | dim | color(Color::GrayLight);

        case ChatEntry::Kind::Error:
            return hbox({
                text("✗ ") | color(Color::Red),
                paragraph(entry.text) | color(Color::Red)
            });
    }
    return text("");
}

// ── Interactive TUI run ───────────────────────────────────────────────────────
int App::run() {
    auto screen = ScreenInteractive::Fullscreen();
    impl_->screen = &screen;

    addEntry({ ChatEntry::Kind::SystemInfo,
        "Claude Code (C++) — type your message and press Enter. Ctrl+C to quit." });

    // Input component
    auto input_component = Input(&input_buf_, "Type a message...");

    // Track scroll position for the chat log
    int scroll_offset = 0;

    // Main component: renders the full UI
    auto renderer = Renderer(input_component, [&]() {
        std::vector<Element> chat_elements;
        {
            std::lock_guard<std::mutex> lk(log_mu_);
            for (const auto& entry : chat_log_) {
                chat_elements.push_back(renderEntry(entry));
                chat_elements.push_back(separator() | dim);
            }
            // Show streaming partial text
            if (!partial_text_.empty()) {
                chat_elements.push_back(
                    hbox({ text("     "), paragraph(partial_text_) | dim })
                );
            }
            // Thinking indicator
            if (thinking_) {
                chat_elements.push_back(
                    text("  ● Thinking...") | dim | color(Color::GrayLight)
                );
            }
        }

        // Status bar
        std::string model_str;
        {
            std::lock_guard<std::mutex> lk(log_mu_);
            model_str = status_line_.empty() ? "Ready" : status_line_;
        }

        auto header = hbox({
            text(" Claude Code (C++) ") | bold | color(Color::Cyan),
            text("│") | dim,
            text(" " + config_.query.model + " ") | color(Color::GrayLight),
            text("│") | dim,
            text(" " + config_.query.tool_ctx.cwd.string() + " ") | color(Color::GrayLight) | flex
        }) | bgcolor(Color::Default);

        auto chat_pane = vbox(std::move(chat_elements)) | flex | vscroll_indicator | frame | yframe;

        auto input_line = hbox({
            text(" > ") | bold | color(Color::Cyan),
            input_component->Render() | flex
        });

        auto status_bar = hbox({
            text(" " + model_str + " ") | dim | flex
        }) | bgcolor(Color::Default);

        return vbox({
            header,
            separator(),
            chat_pane | flex,
            separator(),
            input_line,
            separator(),
            status_bar
        });
    });

    // Handle keyboard events
    auto main_component = CatchEvent(renderer, [&](Event ev) {
        if (ev == Event::Return) {
            std::string msg = utils::trim(input_buf_);
            input_buf_.clear();
            if (!msg.empty() && !worker_running_.load()) {
                submitTurn(msg);
            }
            return true;
        }
        if (ev == Event::Escape) {
            if (worker_running_.load()) {
                engine_.abort();
                {
                    std::lock_guard<std::mutex> lk(log_mu_);
                    status_line_ = "Aborted";
                    thinking_    = false;
                    worker_running_.store(false);
                }
            }
            return true;
        }
        return false;
    });

    // Run initial prompt if set
    if (!config_.initial_prompt.empty()) {
        std::thread([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            submitTurn(config_.initial_prompt);
        }).detach();
    }

    screen.Loop(main_component);
    impl_->screen = nullptr;
    return 0;
}

// ── Headless (--print) mode ───────────────────────────────────────────────────
int App::runHeadless(const std::string& prompt) {
    // Print mode: stream output to stdout
    std::string accumulated;

    engine_.setEventCallback([&](const AgentEvent& e) {
        std::visit([&](auto&& ev) {
            using T = std::decay_t<decltype(ev)>;
            if constexpr (std::is_same_v<T, agentcpp::agent::ev::TextDelta>) {
                std::cout << ev.text << std::flush;
                accumulated += ev.text;
            }
            else if constexpr (std::is_same_v<T, agentcpp::agent::ev::ToolStart>) {
                std::cerr << "\n[Tool: " << ev.name << "]\n";
            }
            else if constexpr (std::is_same_v<T, agentcpp::agent::ev::ToolEnd>) {
                if (ev.is_error) {
                    std::cerr << "[Error: " << utils::truncate(ev.result, 200) << "]\n";
                } else {
                    std::cerr << "[Done: " << ev.name << "]\n";
                }
            }
            else if constexpr (std::is_same_v<T, agentcpp::agent::ev::Error>) {
                std::cerr << "\nError: " << ev.message << "\n";
            }
        }, e);
    });

    engine_.runTurn(conversation_, prompt, config_.query);
    std::cout << "\n";
    return 0;
}

} // namespace agentcpp::tui
