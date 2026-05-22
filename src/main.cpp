#include <CLI/CLI.hpp>
#include <api/ClaudeClient.hpp>
#include <agent/QueryEngine.hpp>
#include <agent/Persona.hpp>
#include <api/Capabilities.hpp>
#include <tools/Tool.hpp>
#include <tools/BashTool.hpp>
#include <tools/FileReadTool.hpp>
#include <tools/FileWriteTool.hpp>
#include <tools/FileEditTool.hpp>
#include <tools/GlobTool.hpp>
#include <tools/GrepTool.hpp>
#include <tools/SkillTool.hpp>
#include <tools/MemoryTool.hpp>
#include <tools/WebTool.hpp>
#include <tools/DataTool.hpp>
#include <tools/DocTool.hpp>
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
#include <sstream>
#include <fstream>

// ── Tool registration ─────────────────────────────────────────────────────────
// We register every tool we can construct. The QueryEngine then filters by the
// active persona's toolsets at runtime — so even though every persona shares
// the same registry, the model sees only its persona's tools.
static void registerAllTools(agentcpp::tools::ToolRegistry& reg,
                             const agentcpp::skills::SkillRegistry* skills) {
    // core / files / code
    reg.registerTool(std::make_shared<agentcpp::tools::BashTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileReadTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileWriteTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::FileEditTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::GlobTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::GrepTool>());

    // web
    reg.registerTool(std::make_shared<agentcpp::tools::WebFetchTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::WebSearchTool>());

    // data
    reg.registerTool(std::make_shared<agentcpp::tools::CsvReadTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::CsvWriteTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::SqlQueryTool>());
    reg.registerTool(std::make_shared<agentcpp::tools::ChartTool>());

    // doc (shell-out to skill scripts; tools are registered unconditionally
    // and report a friendly error at call time if python3 or the skill is
    // missing — see DocTool.cpp::dispatch())
    reg.registerTool(std::make_shared<agentcpp::tools::DocxReadTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::DocxWriteTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::PdfReadTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::XlsxReadTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::XlsxWriteTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::PptxReadTool>(skills));
    reg.registerTool(std::make_shared<agentcpp::tools::PptxWriteTool>(skills));
}

int main(int argc, char** argv) {
    CLI::App cli{"agentcpp — Universal Agent Runtime in C++20"};
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
    // ── Persona ───────────────────────────────────────────────────────────────
    // Empty means "use the legacy default" (coder, for backwards compatibility
    // with the pre-persona behavior). Persona subcommands set this explicitly.
    std::string persona_id;
    std::string persona_dir_override;
    bool        list_personas = false;
    std::vector<std::string> enable_toolsets;
    std::vector<std::string> disable_toolsets;
    bool        list_toolsets = false;
    std::vector<std::string> attachments;
    bool plan_flag = false;
    bool no_plan_flag = false;
    bool plan_only_flag = false;
    int  max_plan_steps_arg = 12;
    int  reflect_every_arg = 4;

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
    cli.add_option("--persona", persona_id,
        "Persona id: general | researcher | office | data | coder (default: coder)");
    cli.add_option("--persona-dir", persona_dir_override,
        "Directory of *.yaml persona overlays (default: ~/.agentcpp/personas)");
    cli.add_flag("--list-personas", list_personas,
        "List available personas and exit");
    cli.add_option("--toolset", enable_toolsets,
        "Additionally enable a tool group (repeatable: core/files/code/web/doc/data/memory/computer/mcp)");
    cli.add_option("--disable-toolset", disable_toolsets,
        "Disable a tool group for this run (repeatable)");
    cli.add_option("--attach", attachments, "Attach a file to the first user message (auto-detected by MIME). Repeatable.")->check(CLI::ExistingFile);
    cli.add_flag("--plan", plan_flag, "Always run the Planner before acting");
    cli.add_flag("--no-plan", no_plan_flag, "Skip the Planner phase");
    cli.add_flag("--plan-only", plan_only_flag, "Produce the plan and exit without acting");
    cli.add_option("--max-plan-steps", max_plan_steps_arg, "Maximum number of plan steps (default 12)");
    cli.add_option("--reflect-every", reflect_every_arg, "Run the Reflector every N turns (0 disables; default 4)");
    cli.add_flag("--list-toolsets", list_toolsets,
        "List tool groups and what's in each, then exit");

    // ── Persona subcommands ───────────────────────────────────────────────────
    // `agentcpp researcher -p "..."` is sugar for `agentcpp --persona researcher -p "..."`.
    // We register one subcommand per builtin id; YAML overlays added later are not
    // exposed as subcommands (they remain reachable via --persona <id>).
    //
    // fallthrough() lets parent flags like -p / --model appear AFTER the
    // subcommand on the command line. Without it CLI11 binds remaining args
    // to the subcommand, which has none of those options registered.
    {
        agentcpp::agent::PersonaRegistry tmp_reg;
        for (const auto& p : tmp_reg.all()) {
            auto* sub = cli.add_subcommand(p.id, p.display_name);
            sub->callback([&, id = p.id]{ persona_id = id; });
            sub->fallthrough();
        }
    }

    // ── PR4: dedicated `plan` subcommand ─────────────────────────────
    {
        auto* sub = cli.add_subcommand("plan", "Produce a plan and exit (no execution).");
        sub->callback([&]{ plan_only_flag = true; });
        sub->fallthrough();
    }

    CLI11_PARSE(cli, argc, argv);

    // ── Persona registry ──────────────────────────────────────────────────────
    // Constructed early so --list-personas can run before any API key is required.
    agentcpp::agent::PersonaRegistry persona_reg;
    {
        auto dir = persona_dir_override.empty()
                       ? agentcpp::agent::PersonaRegistry::defaultPersonaDir()
                       : std::filesystem::path(persona_dir_override);
        persona_reg.loadFromDir(dir);
    }
    if (list_personas) {
        std::cout << "Available personas: " << persona_reg.size() << "\n\n";
        for (const auto& p : persona_reg.all()) {
            std::cout << "  " << p.oneLineSummary() << "\n";
            if (!p.toolsets.empty()) {
                std::cout << "    toolsets: ";
                for (std::size_t i = 0; i < p.toolsets.size(); ++i)
                    std::cout << (i ? ", " : "") << p.toolsets[i];
                std::cout << "\n";
            }
            std::cout << "\n";
        }
        return 0;
    }

    // Backward compatibility: when no persona is selected, default to "coder",
    // which mirrors the pre-persona system prompt and toolset.
    if (persona_id.empty()) persona_id = "coder";
    if (!persona_reg.find(persona_id)) {
        std::cerr << "Error: unknown persona '" << persona_id << "'. "
                  << "Run with --list-personas to see options.\n";
        return 1;
    }

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
    }

    agentcpp::memory::MemoryEngine memory_engine(memory_root, /*create=*/memory_enabled);

    // ── Register tools ──────────────────────────────────────────────────────────────
    auto& registry = agentcpp::tools::ToolRegistry::instance();
    registerAllTools(registry, (skill_reg.size() > 0) ? &skill_reg : nullptr);

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

    if (list_toolsets) {
        std::cout << "Tool groups\n\n";
        const auto* p = persona_reg.find(persona_id);
        if (p) {
            std::cout << "Active persona '" << p->id << "' enables: ";
            for (std::size_t i = 0; i < p->toolsets.size(); ++i)
                std::cout << (i ? ", " : "") << p->toolsets[i];
            std::cout << "\n\n";
        }
        for (const auto& g : registry.listGroups()) {
            auto names = registry.toolsInGroup(g);
            std::cout << "  " << g << " (" << names.size() << ")\n";
            for (const auto& n : names) std::cout << "    - " << n << "\n";
            std::cout << "\n";
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

    if (memory_enabled && memory_engine.isReady()) {
        memory_engine.setProviders(agentcpp::memory::makeProvidersFromEnv(client));
        LOG_DEBUG("memory providers: fact=" + memory_engine.factExtractorName()
                  + " embed="   + memory_engine.embedderName()
                  + " rerank="  + memory_engine.rerankerName()
                  + " reflect=" + memory_engine.reflectComposerName());
    }

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
    query_cfg.model            = effective_model;
    query_cfg.max_tokens       = effective_max_tokens;
    query_cfg.max_turns        = max_turns;
    query_cfg.system_prompt    = system_prompt;
    query_cfg.persona_id       = persona_id;
    query_cfg.enable_toolsets  = enable_toolsets;
    query_cfg.disable_toolsets = disable_toolsets;
    using PM = agentcpp::agent::QueryConfig::PlanMode;
    if (plan_only_flag)      query_cfg.plan_mode = PM::Only;
    else if (no_plan_flag)   query_cfg.plan_mode = PM::Never;
    else if (plan_flag)      query_cfg.plan_mode = PM::Always;
    query_cfg.max_plan_steps = max_plan_steps_arg;
    query_cfg.reflect_every  = reflect_every_arg;
    query_cfg.auto_approve     = auto_approve;
    query_cfg.headless         = !print_prompt.empty();
    query_cfg.tool_ctx         = tool_ctx;

    agentcpp::tui::AppConfig app_cfg;
    app_cfg.query          = query_cfg;
    app_cfg.initial_prompt = "";
    app_cfg.print_mode     = !print_prompt.empty();
    app_cfg.skills         = (skill_reg.size() > 0) ? &skill_reg : nullptr;
    app_cfg.memory         = (memory_enabled && memory_engine.isReady())
                                 ? &memory_engine : nullptr;
    app_cfg.personas       = &persona_reg;

    // ── PR3: convert --attach paths to ContentBlocks ──────────────────
    // MIME inferred from extension. We base64-encode the file body and
    // build the appropriate block. .csv goes via DataBlock (parsed).
    if (!attachments.empty()) {
        auto mimeFromExt = [](const std::filesystem::path& p) -> std::string {
            auto e = p.extension().string();
            for (auto& c : e) c = std::tolower((unsigned char)c);
            if (e == ".png")  return "image/png";
            if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
            if (e == ".webp") return "image/webp";
            if (e == ".gif")  return "image/gif";
            if (e == ".mp3")  return "audio/mp3";
            if (e == ".wav")  return "audio/wav";
            if (e == ".ogg")  return "audio/ogg";
            if (e == ".pdf")  return "application/pdf";
            if (e == ".docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
            if (e == ".csv")  return "text/csv";
            if (e == ".tsv")  return "text/tab-separated-values";
            if (e == ".json") return "application/json";
            if (e == ".txt" || e == ".md") return "text/plain";
            return "application/octet-stream";
        };
        auto base64Encode = [](const std::string& bytes) -> std::string {
            static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string out; out.reserve(((bytes.size() + 2) / 3) * 4);
            for (std::size_t i = 0; i < bytes.size(); i += 3) {
                unsigned a = (unsigned char)bytes[i];
                unsigned b = i + 1 < bytes.size() ? (unsigned char)bytes[i+1] : 0;
                unsigned c = i + 2 < bytes.size() ? (unsigned char)bytes[i+2] : 0;
                unsigned trip = (a << 16) | (b << 8) | c;
                out.push_back(tbl[(trip >> 18) & 0x3F]);
                out.push_back(tbl[(trip >> 12) & 0x3F]);
                out.push_back(i + 1 < bytes.size() ? tbl[(trip >> 6) & 0x3F] : '=');
                out.push_back(i + 2 < bytes.size() ? tbl[trip & 0x3F]        : '=');
            }
            return out;
        };
        auto readBytes = [](const std::filesystem::path& p) -> std::string {
            std::ifstream f(p, std::ios::binary);
            std::stringstream b; b << f.rdbuf(); return b.str();
        };

        for (const auto& path_str : attachments) {
            std::filesystem::path path(path_str);
            std::string mime = mimeFromExt(path);
            std::string bytes = readBytes(path);
            if (mime.rfind("image/", 0) == 0) {
                agentcpp::api::ImageBlock blk;
                blk.media_type = mime;
                blk.data       = base64Encode(bytes);
                app_cfg.attachments.push_back(std::move(blk));
            } else if (mime.rfind("audio/", 0) == 0) {
                agentcpp::api::AudioBlock blk;
                blk.media_type = mime;
                blk.data       = base64Encode(bytes);
                app_cfg.attachments.push_back(std::move(blk));
            } else if (mime == "application/pdf" || mime.find("officedocument") != std::string::npos) {
                agentcpp::api::DocumentBlock blk;
                blk.media_type = mime;
                blk.filename   = path.filename().string();
                blk.data       = base64Encode(bytes);
                app_cfg.attachments.push_back(std::move(blk));
            } else if (mime == "text/csv" || mime == "text/tab-separated-values") {
                // Parse minimally — DataBlock content holds {columns, rows}
                agentcpp::api::DataBlock blk;
                blk.schema_id = "table/csv";
                blk.caption   = path.filename().string();
                std::stringstream ss(bytes);
                std::string line;
                nlohmann::json cols = nlohmann::json::array();
                nlohmann::json rows = nlohmann::json::array();
                bool first = true;
                char delim = (mime == "text/tab-separated-values") ? '\t' : ',;
                while (std::getline(ss, line)) {
                    nlohmann::json row = nlohmann::json::array();
                    std::stringstream ls(line);
                    std::string cell;
                    while (std::getline(ls, cell, delim)) row.push_back(cell);
                    if (first) { cols = row; first = false; } else { rows.push_back(row); }
                }
                blk.content = { {"columns", cols}, {"rows", rows} };
                app_cfg.attachments.push_back(std::move(blk));
            } else {
                // Generic text: dump body as a TextBlock
                agentcpp::api::TextBlock blk;
                blk.text = "[Attached file: " + path.filename().string() + "]\n" + bytes;
                app_cfg.attachments.push_back(std::move(blk));
            }
        }
    }

    agentcpp::tui::App app(client, registry, app_cfg);
    if (!print_prompt.empty()) return app.runHeadless(print_prompt);
    return app.run();
}
