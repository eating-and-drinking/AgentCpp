#pragma once
#include <agent/QueryEngine.hpp>
#include <api/ClaudeClient.hpp>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <deque>
#include <thread>

namespace agentcpp::skills { class SkillRegistry; }
namespace agentcpp::memory { class MemoryEngine;  }
namespace agentcpp::agent  { class PersonaRegistry; }

namespace agentcpp::tui {

using namespace agentcpp::api;
using namespace agentcpp::agent;

struct ChatEntry {
    enum class Kind { User, AssistantText, ToolCall, ToolResult, SystemInfo, Error };
    Kind        kind;
    std::string text;
    std::string tool_name;
    bool        is_error = false;
};

struct AppConfig {
    QueryConfig query;
    std::string initial_prompt;
    bool        print_mode = false;
    const agentcpp::skills::SkillRegistry*  skills   = nullptr;
    const agentcpp::memory::MemoryEngine*   memory   = nullptr;
    const agentcpp::agent::PersonaRegistry* personas = nullptr;
    std::vector<agentcpp::api::ContentBlock> attachments;
};

class App {
public:
    App(std::shared_ptr<agentcpp::api::ClaudeClient> client,
        tools::ToolRegistry& registry,
        AppConfig config);
    ~App();

    int run();
    int runHeadless(const std::string& prompt);

private:
    std::shared_ptr<agentcpp::api::ClaudeClient> client_;
    tools::ToolRegistry&                   registry_;
    AppConfig                              config_;
    agent::QueryEngine                     engine_;

    std::vector<Message>         conversation_;
    std::deque<ChatEntry>        chat_log_;
    std::mutex                   log_mu_;

    std::string   input_buf_;
    std::string   status_line_;
    bool          thinking_  = false;
    std::string   partial_text_;

    std::atomic<bool> worker_running_{false};
    std::atomic<bool> quit_{false};

    void submitTurn(const std::string& user_input);
    void addEntry(ChatEntry entry);
    void handleEvent(const AgentEvent& agent_ev);

    struct FtxuiImpl;
    std::unique_ptr<FtxuiImpl> impl_;
};

} // namespace agentcpp::tui
