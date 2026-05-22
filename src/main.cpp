#include <CLI/CLI.hpp>
#include <api/ClaudeClient.hpp>
#include <agent/QueryEngine.hpp>
#include <tools/Tool.hpp>
#include <tools/BashTool.hpp>
#include <tools/FileReadTool.hpp>
#include <tools/FileWriteTool.hpp>
#include <tools/FileEditTool.hpp>
#include <tools/GlobTool.hpp>
#include <tools/GrepTool.hpp>
#include <tools/SkillTool.hpp>
#include <tools/MemoryTool.hpp>
#include <skills/SkillRegistry.hpp>
#include <memory/MemoryEngine.hpp>
#include <tools/McpTool.hpp>
#include <tools/TaskTool.hpp>
#include <tools/ChannelTool.hpp>
#include <tools/ComputerTool.hpp>
#include <mcp/McpManager.hpp>
#include <tui/App.hpp>
#include <utils/Config.hpp>
#include <utils/Logger.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

// ── Tool registration ─────────────────────────────────────────────────────────
static void registerAllTools(agentcpp::tools::ToolRegistry& reg) {
    reg.registerTool(std::make_shared<agentcpp::tools::BashTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileReadTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileWriteTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileEditTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::GlobTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::GrepTool>());
}

int main(int argc, char** argv) {
    CLI::App cli{"Claude Code (C++) — high-performance AI coding agent"};
    cli.set_version_flag("--version,-v", "1.0.0");

    // ── CLI flags ─────────────────────────────────────────────────────────────
    std::string print_prompt;
    std::string model;
    std::string api_key;
    std::string base_url;
    std::string system_prompt;
    std::string cwd_override;
    std::string dotenv_path;
    int  max_tokens   = 0;
    int  max_turns    = 100;
    bool read_only    = false;
    bool debug_mode   = false;
    bool auto_approve = false;
    bool list_tools   = false;
    bool list_skills  = false;
    bool no_memory    = false;
    std::string mcp_config_path;
    bool list_mcp     = false;
    bool computer_use_beta = false;
    std::string skills_dir_override;
    std::string memory_dir_override;
    std::vector<std::string> extra_skill_dirs;

    cli.add_option("-p,--print", print_prompt,
        "Run in headless/print mode with this prompt (non-interactive)");
    cli.add_option("--model,-m", model,
        "Model name (overrides CLAUDE_MODEL env var)");
    cli.add_option("--api-key", api_key,
        "Anthropic API key (overrides ANTHROPIC_API_KEY env var)");
    cli.add_option("--base-url", base_url,
        "Base URL for the API (overrides ANTHROPIC_BASE_URL env var)");
    cli.add_option("--system-prompt", system_prompt,
        "Custom system prompt");
    cli.add_option("--cwd", cwd_override,
        "Working directory (default: current directory)");
    cli.add_option("--env-file", dotenv_path,
        "Path to .env file (default: .env in cwd or home)");
    cli.add_option("--max-tokens", max_tokens,
        "Maximum output tokens per turn");
    cli.add_option("--max-turns", max_turns,
        "Maximum number of agent turns per conversation");
    cli.add_flag("--read-only", read_only,
        "Disable all file writes and bash commands");
    cli.add_flag("--debug", debug_mode,
        "Enable verbose debug logging");
    cli.add_flag("--auto-approve,-y", auto_approve,
        "Automatically approve permission prompts (use with care)");
    cli.add_flag("--list-tools", list_tools,
        "List available tools and exit");
    cli.add_flag("--list-skills", list_skills,
        "List available skills and exit");
    cli.add_option("--skills-dir", skills_dir_override,
        "Primary skills root directory (overrides AGENTCPP_SKILLS_DIR)");
    cli.add_option("--add-skills-dir", extra_skill_dirs,
        "Additional skills root directory (may be repeated)");
    cli.add_option("--memory-dir", memory_dir_override,
        "Persistent memory directory (overrides AGENTCPP_MEMORY_DIR)");
    cli.add_flag("--no-memory", no_memory,
        "Disable the persistent memory subsystem");
    cli.add_option("--mcp-config", mcp_config_path,
        "Path to MCP server config (default ~/.agentcpp/mcp.json)");
    cli.add_flag("--list-mcp", list_mcp,
        "List MCP servers + their tools and exit");
    cli.add_flag("--computer-use", computer_use_beta,
        "Send anthropic-beta: computer-use header (lets the model see screenshots)");

    CLI11_PARSE(cli, argc, argv);

    // ── Load configuration ────────────────────────────────────────────────────
    auto& cfg = agentcpp::utils::Config::instance();
    cfg.load(dotenv_path.empty() ? "" : dotenv_path);

    // Apply logger settings
    auto& logger = agentcpp::utils::Logger::instance();
    if (debug_mode || cfg.debugMode()) {
        logger.setLevel(agentcpp::utils::LogLevel::Debug);
    }
    if (!print_prompt.empty()) {
        logger.setSilent(true); // don't pollute stdout in print mode
    }

    // Resolve effective config values (CLI > env > default)
    std::string effective_api_key  = api_key.empty()   ? cfg.apiKey()   : api_key;
    std::string effective_base_url = base_url.empty()  ? cfg.baseUrl()  : base_url;
    std::string effective_model    = model.empty()     ? cfg.model()    : model;
    int effective_max_tokens       = max_tokens > 0    ? max_tokens     : cfg.maxTokens();

    // ── Skills ────────────────────────────────────────────────────────────────
    agentcpp::skills::SkillRegistry skill_reg;
    {
        std::string primary = skills_dir_override.empty()
                                  ? cfg.skillsDir()
                                  : skills_dir_override;
        if (!primary.empty()) skill_reg.addRoot(primary);
        for (const auto& d : extra_skill_dirs) skill_reg.addRoot(d);
    }

    if (list_skills) {
        std::cout << "Available skills: " << skill_reg.size() << "\n\n";
        for (const auto& s : skill_reg.all()) {
            std::cout << "  " << s.name << "\n";
            std::cout << "    " << s.description << "\n";
            std::cout << "    (from " << s.skill_md_path.string() << ")\n\n";
        }
        return 0;
    }

    // ── Memory ────────────────────────────────────────────────────────────────
    bool memory_enabled = !no_memory && cfg.memoryEnabled();
    std::filesystem::path memory_root;
    if (!memory_dir_override.empty()) {
        memory_root = memory_dir_override;
    } else if (!cfg.memoryDir().empty()) {
        memory_root = cfg.memoryDir();
    } // else: MemoryEngine picks its own default

    agentcpp::memory::MemoryEngine memory_engine(memory_root, /*create=*/memory_enabled);

    // ── Register tools ──────────────────────────────────────────────────────────────
    auto& registry = agentcpp::tools::ToolRegistry::instance();
    registerAllTools(registry);

    // Skill tool only if there's at least one skill loaded
    if (skill_reg.size() > 0) {
        registry.registerTool(std::make_shared<agentcpp::tools::SkillTool>(skill_reg));
    }
    if (memory_enabled && memory_engine.isReady()) {
        registry.registerTool(std::make_shared<agentcpp::tools::MemoryRetainTool>(memory_engine));
        registry.registerTool(std::make_shared<agentcpp::tools::MemoryRecallTool>(memory_engine));
        registry.registerTool(std::make_shared<agentcpp::tools::MemoryReflectTool>(memory_engine));
        registry.registerTool(std::make_shared<agentcpp::tools::MemoryListTool>(memory_engine));
    }

    agentcpp::mcp::McpManager mcp_mgr;
    {
        std::filesystem::path cfgpath = mcp_config_path.empty()
            ? agentcpp::mcp::McpManager::defaultConfigPath()
            : std::filesystem::path(mcp_config_path);
        mcp_mgr.loadConfig(cfgpath);
        for (const auto& rt : mcp_mgr.tools()) {
            registry.registerTool(std::make_shared<agentcpp::tools::McpTool>(rt.prefixed_name, rt));
        }
    }

    if (list_mcp) {
        std::cout << "MCP servers: " << mcp_mgr.serverCount() << "\n";
        for (const auto& rt : mcp_mgr.tools()) {
            std::cout << "  " << rt.prefixed_name << " - " << rt.info.description << "\n";
        }
        return 0;
    }

    registry.registerTool(std::make_shared<agentcpp::tools::ChannelPublishTool>());
    registry.registerTool(std::make_shared<agentcpp::tools::ChannelReadTool>());
    registry.registerTool(std::make_shared<agentcpp::tools::ChannelListTool>());
    registry.registerTool(std::make_shared<agentcpp::tools::ComputerTool>());

    if (list_tools) {
        std::cout << "Available tools:\n\n";
        for (const auto& def : registry.definitions()) {
            std::cout << "  " << def.name << "\n"
                      << "    " << def.description << "\n\n";
        }
        return 0;
    }

    if (effective_api_key.empty()) {
        std::cerr << "Error: No API key found.\n";
        return 1;
    }

    std::filesystem::path cwd = cwd_override.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(cwd_override);
    if (!std::filesystem::exists(cwd)) {
        std::cerr << "Error: working directory does not exist: " << cwd << "\n";
        return 1;
    }
    cwd = std::filesystem::canonical(cwd);

    agentcpp::api::ClientConfig client_cfg;
    client_cfg.api_key           = effective_api_key;
    client_cfg.base_url          = effective_base_url;
    client_cfg.default_model     = effective_model;
    client_cfg.computer_use_beta = computer_use_beta;
    auto client = std::make_shared<agentcpp::api::ClaudeClient>(std::move(client_cfg));

    // Upgrade memory providers now that the Claude client exists. The
    // factory inspects ANTHROPIC_API_KEY / AGENTCPP_MEMORY_LLM /
    // AGENTCPP_MEMORY_EMBED_* and falls back to heuristics for any slot it
    // can't fill.
    if (memory_enabled && memory_engine.isReady()) {
        memory_engine.setProviders(agentcpp::memory::makeProvidersFromEnv(client));
        LOG_DEBUG("memory providers: fact=" + memory_engine.factExtractorName()
                  + " embed="   + memory_engine.embedderName()
                  + " rerank="  + memory_engine.rerankerName()
                  + " reflect=" + memory_engine.reflectComposerName());
    }

    // Task tool (needs client; channel tools don't)
    registry.registerTool(std::make_shared<agentcpp::tools::TaskTool>(
        client, registry, effective_model, effective_max_tokens, max_turns));

    agentcpp::tools::ToolContext tool_ctx;
    tool_ctx.cwd          = cwd;
    tool_ctx.read_only    = read_only;
    tool_ctx.auto_approve = auto_approve;
    tool_ctx.headless     = !print_prompt.empty();
    tool_ctx.askPermission = [](const std::string& msg) {
        std::cerr << "\n[Permission] " << msg << "\nAllow? [y/N] ";
        std::string r; std::getline(std::cin, r);
        return (!r.empty() && (r[0] == 'y' || r[0] == 'Y'));
    };

    agentcpp::agent::QueryConfig query_cfg;
    query_cfg.model         = effective_model;
    query_cfg.max_tokens    = effective_max_tokens;
    query_cfg.max_turns     = max_turns;
    query_cfg.system_prompt = system_prompt;
    query_cfg.auto_approve  = auto_approve;
    query_cfg.headless      = !print_prompt.empty();
    query_cfg.tool_ctx      = tool_ctx;

    agentcpp::tui::AppConfig app_cfg;
    app_cfg.query          = query_cfg;
    app_cfg.initial_prompt = "";
    app_cfg.print_mode     = !print_prompt.empty();
    app_cfg.skills         = (skill_reg.size() > 0) ? &skill_reg : nullptr;
    app_cfg.memory         = (memory_enabled && memory_engine.isReady())
                                 ? &memory_engine : nullptr;

    agentcpp::tui::App app(client, registry, app_cfg);
    if (!print_prompt.empty()) return app.runHeadless(print_prompt);
    return app.run();
}
