# agentcpp

A high-performance C++20 rewrite of the [cc-haha](https://github.com/NanmiCoder/cc-haha) AI coding agent (itself based on Claude Code).

## Why C++?

| Metric | TypeScript/Bun | C++ |
|--------|----------------|-----|
| Cold-start time | ~500 ms JIT warmup | <10 ms native |
| Memory baseline | ~150 MB V8 heap | ~8 MB |
| Tool execution overhead | event-loop scheduling | zero-copy POSIX |
| Binary distribution | requires Bun runtime | single static binary |

## Features

- **Streaming API client** — libcurl SSE parser with zero-copy event dispatch
- **Identical tool set** — `Bash`, `Read`, `Write`, `Edit`, `Glob`, `Grep`
- **FTXUI terminal UI** — reactive, component-based TUI (same feel as React + Ink)
- **Headless / `--print` mode** — pipe-friendly non-interactive output
- **Any compatible API** — OpenRouter, MiniMax, local proxies via `ANTHROPIC_BASE_URL`
- **Zero runtime deps** — single binary after `cmake --build`

## Build

### Prerequisites

```bash
# macOS
brew install cmake curl

# Ubuntu / Debian
apt install cmake libcurl4-openssl-dev
```

CMake ≥ 3.25 and a C++20 compiler (Clang 14+ or GCC 12+) are required.  
All other dependencies (nlohmann/json, CLI11, FTXUI) are fetched automatically by CMake FetchContent.

### Compile

```bash
git clone <this-repo> agentcpp
cd agentcpp

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Binary is at:
./build/agentcpp --help
```

### Install globally (optional)

```bash
cmake --install build --prefix ~/.local
# Then add ~/.local/bin to your PATH
```

## Configuration

```bash
cp .env.example .env
# Edit .env and set ANTHROPIC_API_KEY
```

The agent reads configuration in priority order:

1. CLI flags (`--api-key`, `--model`, …)
2. Environment variables (`ANTHROPIC_API_KEY`, `CLAUDE_MODEL`, …)
3. `.env` file in the current directory
4. `.env` file in `$HOME`

### Key environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | — | **Required.** Your API key |
| `ANTHROPIC_BASE_URL` | `https://api.anthropic.com` | Custom API endpoint |
| `CLAUDE_MODEL` | `claude-opus-4-5` | Model name |
| `CLAUDE_MAX_TOKENS` | `8096` | Max output tokens per turn |
| `CLAUDE_DEBUG` | `0` | Enable debug logging |

## Usage

```bash
# Interactive TUI
./build/agentcpp

# Headless / print mode (pipe-friendly)
./build/agentcpp -p "Explain the main function in src/main.cpp"

# Custom model / endpoint
./build/agentcpp --model claude-sonnet-4-5 --base-url https://openrouter.ai/api/v1

# Read-only mode (no file writes or shell commands)
./build/agentcpp --read-only

# List available tools
./build/agentcpp --list-tools

# Override working directory
./build/agentcpp --cwd /path/to/project
```

## Architecture

```
agentcpp/
├── include/
│   ├── api/
│   │   ├── Types.hpp         # Message, ContentBlock, StreamEvent types
│   │   └── ClaudeClient.hpp  # libcurl streaming client
│   ├── agent/
│   │   └── QueryEngine.hpp   # Main agentic loop (tool-use ↔ API)
│   ├── tools/
│   │   ├── Tool.hpp          # Abstract base + ToolRegistry
│   │   ├── BashTool.hpp      # Shell command execution (fork/exec + poll)
│   │   ├── FileReadTool.hpp  # File/directory reading with line numbers
│   │   ├── FileWriteTool.hpp # Atomic file creation / overwrite
│   │   ├── FileEditTool.hpp  # Exact-string replacement edits
│   │   ├── GlobTool.hpp      # Recursive glob with ** support
│   │   └── GrepTool.hpp      # Regex / fixed-string search
│   ├── tui/
│   │   └── App.hpp           # FTXUI interactive TUI + headless mode
│   └── utils/
│       ├── Config.hpp        # .env + env-var config loader
│       ├── Logger.hpp        # Thread-safe logger
│       └── StringUtils.hpp   # String helpers + ANSI colours
└── src/                      # Implementations mirror include/
```

### Adding a new tool

1. Create `include/tools/MyTool.hpp` extending `agentcpp::tools::Tool`
2. Create `src/tools/MyTool.cpp` implementing `name()`, `description()`, `inputSchema()`, `execute()`
3. Add `src/tools/MyTool.cpp` to `CMakeLists.txt`
4. Call `registry.registerTool(std::make_shared<MyTool>())` in `src/main.cpp`

## Differences from cc-haha (TypeScript)

| Feature | cc-haha (TS) | agentcpp |
|---------|-------------|-------------|
| Runtime | Bun + V8 | Native binary |
| TUI | React + Ink | FTXUI |
| MCP servers | ✓ | ✓ |
| Multi-agent | ✓ | ✓ |
| Memory system | ✓ | ✓ |
| Channel system | ✓ | ✓ |
| Computer Use | ✓ | ✓ |
| Skills system | ✓ | ✓ |

## Skills

Skills are reusable instruction sets (one folder + one `SKILL.md` per skill). Each
`SKILL.md` has YAML frontmatter (`name`, `description`) plus a markdown body. The
agent advertises every loaded skill in its system prompt; the model invokes a
skill by calling the `Skill` tool with the skill name, which returns the full
`SKILL.md` body.

```
./build/agentcpp --skills-dir ~/.agentcpp/skills
./build/agentcpp --list-skills              # show what was loaded
export AGENTCPP_SKILLS_DIR=~/.agentcpp/skills
```

## Persistent memory

A long-term memory store — plain text files under one directory that survive
across sessions. Defaults to `$XDG_DATA_HOME/agentcpp/memory` (Linux/macOS),
`%APPDATA%\agentcpp\memory` (Windows), or `~/.agentcpp/memory`.

Tools exposed: `MemoryList`, `MemoryRead(name)`, `MemoryWrite(name, content)`.
Names are relative paths under the root; path-traversal is rejected.
`--read-only` disables `MemoryWrite`. `--no-memory` (or `AGENTCPP_MEMORY=0`)
disables the subsystem entirely.

## MCP servers

agentcpp can spawn any number of Model Context Protocol (MCP) servers as
subprocesses and expose their tools to the model. Configure them in a JSON
file (Claude Desktop compatible):

```json
{
  "mcpServers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": { "GITHUB_PERSONAL_ACCESS_TOKEN": "..." }
    }
  }
}
```

Default config path: `~/.agentcpp/mcp.json` (override with `--mcp-config`
or `AGENTCPP_MCP_CONFIG`). Each tool is exposed to the model as
`<server-id>__<tool-name>`. `./build/agentcpp --list-mcp` prints what was
discovered. `--read-only` blocks every MCP tool call.

## Multi-agent

The `Task` tool lets the agent spawn a sub-agent for a self-contained sub-task.
The sub-agent runs with its own conversation against the same `ClaudeClient`
and `ToolRegistry`, returns its final text reply to the parent, and is silent
(no events bubble up to the parent's TUI). Recursion is bounded by
`ToolContext::max_subagent_depth` (default 2).

## Channels

A tiny in-process pub/sub bus for coordinating between the main agent and
sub-agents. Tools: `ChannelPublish(channel, text)`, `ChannelRead(channel,
since_id)`, `ChannelList()`. Each channel keeps a ring buffer of 256
recent messages, scoped to the current CLI run. Sender labels are auto-
filled (`main`, `task-d1`, ...).

## Computer Use

The `Computer` tool exposes coarse screen control via shell-out: `screenshot`,
`cursor_position`, `mouse_move`, `left_click`/`right_click`/`middle_click`/
`double_click`, `scroll`, `type`, `key`. Requires `xdotool` + `scrot`/`maim`
on Linux, or `cliclick` + `screencapture` on macOS. Windows is not supported.
`--read-only` blocks all mutating actions.

Screenshots are base64-encoded and sent back to the model as `image` content
blocks inside the `tool_result`, so the model can see them directly. Pass
`--computer-use` on the CLI to enable the `anthropic-beta: computer-use-2024-10-22`
header that the model requires for screen-aware behaviour.

## License

Based on Claude Code source code (c) Anthropic. For educational and research use only.
