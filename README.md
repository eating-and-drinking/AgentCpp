# agentcpp

A high-performance C++20 rewrite of the [cc-haha](https://github.com/NanmiCoder/cc-haha) AI coding agent (itself based on Claude Code), extended with a hybrid memory engine (BM25 + graph + optional embeddings) and a training-free metacognition layer (MERIT).

## Why C++?

| Metric | TypeScript/Bun | C++ |
|--------|----------------|-----|
| Cold-start time | ~500 ms JIT warmup | <10 ms native |
| Memory baseline | ~150 MB V8 heap | ~8 MB |
| Tool execution overhead | event-loop scheduling | zero-copy POSIX |
| Binary distribution | requires Bun runtime | single static binary |

## Features

- **Streaming API client** — libcurl SSE parser with zero-copy event dispatch
- **Claude Code tool set** — `Bash`, `Read`, `Write`, `Edit`, `Glob`, `Grep`
- **FTXUI terminal UI** — reactive, component-based TUI (same feel as React + Ink)
- **Headless / `--print` mode** — pipe-friendly non-interactive output
- **Skills system** — load `SKILL.md` instruction sets from one or more roots
- **MCP servers** — spawn any Model Context Protocol server and expose its tools
- **Multi-agent** — `Task` tool spawns bounded-depth sub-agents
- **Channels** — in-process pub/sub bus for agent ↔ sub-agent coordination
- **Computer Use** — coarse screen control (screenshot, mouse, keyboard, scroll)
- **Hybrid memory engine** — BM25 lexical + graph activation + optional HTTP embeddings, fused via RRF; pluggable Claude / heuristic providers
- **MERIT metacognition** — Bayesian self-belief, CoT process monitoring, structured self-model, schema revision; training-free
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
3. `.env` file in the current directory (or path given by `--env-file`)
4. `.env` file in `$HOME`

### Core environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ANTHROPIC_API_KEY` | — | **Required.** Your API key |
| `ANTHROPIC_BASE_URL` | `https://api.anthropic.com` | Custom API endpoint |
| `ANTHROPIC_API_VERSION` | `2023-06-01` | API version header |
| `CLAUDE_MODEL` | `claude-opus-4-5` | Model name |
| `CLAUDE_MAX_TOKENS` | `8096` | Max output tokens per turn |
| `CLAUDE_DEBUG` | `0` | Enable debug logging |
| `CLAUDE_HEADLESS` | `0` | Force headless mode |

### Memory environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AGENTCPP_MEMORY_DIR` | `~/.agentcpp/memory` (or `$XDG_DATA_HOME/agentcpp/memory`, `%APPDATA%\agentcpp\memory`) | Memory store root |
| `AGENTCPP_MEMORY` | `1` | Set to `0` to disable the subsystem entirely |
| `AGENTCPP_MEMORY_LLM` | auto if `ANTHROPIC_API_KEY` is set | Enable Claude-backed fact extraction + reflection |
| `AGENTCPP_MEMORY_EMBED_URL` | — | Base URL of an OpenAI-compatible `/v1/embeddings` endpoint |
| `AGENTCPP_MEMORY_EMBED_KEY` | — | Bearer token for the embedder endpoint |
| `AGENTCPP_MEMORY_EMBED_MODEL` | `text-embedding-3-small` | Embedding model name |
| `AGENTCPP_MEMORY_EMBED_DIM` | `1536` | Embedding dimension |

### Skills + MCP environment variables

| Variable | Default | Description |
|----------|---------|-------------|
| `AGENTCPP_SKILLS_DIR` | — | Primary skills root |
| `AGENTCPP_MCP_CONFIG` | `~/.agentcpp/mcp.json` | MCP server config path |

## Usage

```bash
# Interactive TUI
./build/agentcpp

# Headless / print mode (pipe-friendly)
./build/agentcpp -p "Explain the main function in src/main.cpp"

# Custom model / endpoint
./build/agentcpp --model claude-sonnet-4-5 --base-url https://openrouter.ai/api/v1

# Read-only mode (no file writes, no bash, MCP writes blocked, no mutating Computer actions)
./build/agentcpp --read-only

# List available tools / skills / MCP tools
./build/agentcpp --list-tools
./build/agentcpp --list-skills
./build/agentcpp --list-mcp

# Override working directory, skills root, memory root
./build/agentcpp --cwd /path/to/project
./build/agentcpp --skills-dir ~/.agentcpp/skills --add-skills-dir ./skills
./build/agentcpp --memory-dir ./mem

# Enable Computer Use beta (lets the model see screenshots)
./build/agentcpp --computer-use

# Disable persistent memory for this run
./build/agentcpp --no-memory
```

### CLI flags

| Flag | Description |
|------|-------------|
| `-p, --print <prompt>` | Headless one-shot |
| `-m, --model <name>` | Override model |
| `--api-key <key>` | Override `ANTHROPIC_API_KEY` |
| `--base-url <url>` | Override `ANTHROPIC_BASE_URL` |
| `--system-prompt <text>` | Custom system prompt |
| `--cwd <path>` | Working directory |
| `--env-file <path>` | Load specific `.env` |
| `--max-tokens <n>` | Output token cap |
| `--max-turns <n>` | Agent-loop turn cap (default 100) |
| `--read-only` | Disable all writes / shell / mutating Computer actions |
| `--debug` | Verbose debug logging |
| `-y, --auto-approve` | Skip permission prompts |
| `--list-tools` / `--list-skills` / `--list-mcp` | Print and exit |
| `--skills-dir <path>` | Primary skills root |
| `--add-skills-dir <path>` | Additional skills root (repeatable) |
| `--memory-dir <path>` | Persistent memory root |
| `--no-memory` | Disable memory subsystem |
| `--mcp-config <path>` | MCP server config |
| `--computer-use` | Send `anthropic-beta: computer-use-2024-10-22` |
| `-v, --version` | Print version |

## Architecture

```
agentcpp/
├── include/
│   ├── api/                 # libcurl streaming Claude client + message types
│   ├── agent/               # QueryEngine + MERIT metacognition layers
│   ├── tools/               # All tool implementations (Bash, Read, Edit, …)
│   ├── skills/              # SKILL.md loader + registry
│   ├── memory/              # Hybrid memory engine (BM25 + graph + embeddings)
│   │   └── providers/       # Swappable fact / embed / rerank / reflect backends
│   ├── mcp/                 # MCP client + manager (spawns subprocess servers)
│   ├── channels/            # In-process pub/sub bus
│   ├── tui/                 # FTXUI app + headless runner
│   └── utils/               # Config, Logger, StringUtils
└── src/                     # Implementations mirror include/
```

### Adding a new tool

1. Create `include/tools/MyTool.hpp` extending `agentcpp::tools::Tool`.
2. Create `src/tools/MyTool.cpp` implementing `name()`, `description()`, `inputSchema()`, `execute()`.
3. Add `src/tools/MyTool.cpp` to `CMakeLists.txt`.
4. Call `registry.registerTool(std::make_shared<MyTool>())` in `src/main.cpp`.

## Skills

Skills are reusable instruction sets — one folder + one `SKILL.md` per skill. Each `SKILL.md` has YAML frontmatter (`name`, `description`) plus a markdown body. The agent advertises every loaded skill in its system prompt; the model invokes one by calling the `Skill` tool with the skill name, which returns the full `SKILL.md` body.

```bash
./build/agentcpp --skills-dir ~/.agentcpp/skills
./build/agentcpp --list-skills
export AGENTCPP_SKILLS_DIR=~/.agentcpp/skills
```

Pass `--add-skills-dir` (repeatable) to layer additional roots on top of the primary one.

## Persistent memory

A long-term memory store backed by files under one directory that survives across sessions. Defaults to `$XDG_DATA_HOME/agentcpp/memory` (Linux/macOS), `%APPDATA%\agentcpp\memory` (Windows), or `~/.agentcpp/memory`. Disable with `--no-memory` or `AGENTCPP_MEMORY=0`.

### Pipeline

```
                          Retain                           Recall
content ─► FactExtractor ─► EntityResolver ─► MemoryUnits + Links + Documents
                                                       │
                                                       ▼
                                       ┌────────── BM25Index ──────────┐
                                       │  GraphRetrieval (link expand) │
                                       │  (optional) Embedder ANN      │
                                       └───────────────┬───────────────┘
                                                       ▼
                                         Fusion (Reciprocal Rank Fusion)
                                                       ▼
                                          Reranker (recency + temporal)
                                                       ▼
                                          ReflectComposer ─► answer
```

`MemoryUnit`s are atomic facts (`World` / `Experience` / `Observation`) carrying text, context, temporal metadata, and an optional embedding. `MemoryLink`s are typed edges (Temporal, Semantic, Entity, Causes, CausedBy, Enables, Prevents) with weights, used by `GraphRetrieval` for activation spreading. A `Bank` is the top-level namespace and holds a mission, disposition traits (skepticism, literalism, empathy), and stats. `MentalModel`s are user-configured focus areas whose summaries the engine regenerates as the bank grows.

### Pluggable providers

| Slot | Always available | Optional (require config) |
|------|------------------|---------------------------|
| FactExtractor | `HeuristicFactExtractor` | `ClaudeFactExtractor` (when `ANTHROPIC_API_KEY` present) |
| Embedder | `NullEmbedder` (semantic channel disabled) | `HttpEmbedder` (OpenAI-compatible endpoint) |
| Reranker | `HeuristicReranker` (RRF + recency + temporal) | — interface ready for HTTP cross-encoders |
| ReflectComposer | `TemplateReflectComposer` (markdown template) | `ClaudeReflectComposer` (LLM free-form) |

The factory in `memory::makeProvidersFromEnv` reads the `AGENTCPP_MEMORY_*` env vars (see table above) and falls back to heuristics for any slot it can't fill.

### Memory tools exposed to the model

| Tool | What it does |
|------|--------------|
| `MemoryRetain` | Ingests content into a bank: extracts facts, resolves entities, writes units + links |
| `MemoryRecall` | Returns top-k `ScoredResult`s for a query — text + per-channel scores |
| `MemoryReflect` | Synthesises a composed answer over recalled facts, filtered by bank disposition; returns text + `based_on` groups (world / experience / observation / mental_models) |
| `MemoryList` | Lists units, entities, links, and mental models in a bank (debug / overview) |

`--read-only` blocks `MemoryRetain` and `MemoryReflect` (write paths); `MemoryRecall` and `MemoryList` remain available.

## MERIT metacognition

A training-free, four-layer self-monitoring stack wired into the agent loop (`agent/MetacognitionEngine.hpp` is the façade).

1. **`MetaController` — outcome monitoring.** Maintains a `SelfBelief` (Beta(α, β) distributions over competence dimensions such as `planning_quality`, `code_gen`, …) and picks one of `Act` / `Reflect` / `Decompose` / `Escalate` / `Abort` by minimising Expected Free Energy over heuristic cost and value functions.
2. **`CoTMonitor` — process monitoring.** Sliding-window normalisation of the model's reasoning trace; detects step repetition, low-content stalling, and tool-parameter loops, and emits a quality score plus optional intervention prompts. No LLM required.
3. **`SelfModelStore` — structured self-knowledge.** A bag of `SelfProposition`s (`text`, `tags[]`, `confidence`, `evidence_count`, …) like *"I tend to forget noexcept on move constructors."* Token-overlap relevance retrieves the most pertinent ones for the current task and `selfModelPromptSection()` injects them into the system prompt. `SelfModelMemoryAdapter` persists them into the `MentalModel` bank.
4. **`SchemaReviser` — structure learning.** Periodically clusters recent failures (Jaccard token overlap) and proposes new competence dimensions or new propositions; novelty + evidence thresholds gate application.

`QueryEngine` consults `MetacognitionEngine` per iteration and per turn — Bayesian updates after each observation, schema revision every N episodes, prompt-section regeneration when the self-model changes.

## MCP servers

agentcpp can spawn any number of Model Context Protocol (MCP) servers as subprocesses and expose their tools to the model. Configure them in a Claude Desktop-compatible JSON file:

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

Default config path: `~/.agentcpp/mcp.json` (override with `--mcp-config` or `AGENTCPP_MCP_CONFIG`). Each tool is exposed to the model as `<server-id>__<tool-name>`. `./build/agentcpp --list-mcp` prints what was discovered. `--read-only` blocks every MCP tool call.

## Multi-agent

The `Task` tool lets the agent spawn a sub-agent for a self-contained sub-task. The sub-agent runs with its own conversation against the same `ClaudeClient` and `ToolRegistry`, returns its final text reply to the parent, and is silent (no events bubble up to the parent's TUI). Recursion is bounded by `ToolContext::max_subagent_depth` (default 2).

## Channels

A tiny in-process pub/sub bus for coordinating between the main agent and sub-agents. Tools: `ChannelPublish(channel, text)`, `ChannelRead(channel, since_id)`, `ChannelList()`. Each channel keeps a ring buffer of 256 recent messages, scoped to the current CLI run. Sender labels are auto-filled (`main`, `task-d1`, …).

## Computer Use

The `Computer` tool exposes coarse screen control via shell-out: `screenshot`, `cursor_position`, `mouse_move`, `left_click`/`right_click`/`middle_click`/`double_click`, `scroll`, `type`, `key`. Requires `xdotool` + `scrot`/`maim` on Linux, or `cliclick` + `screencapture` on macOS. Windows is not supported. `--read-only` blocks all mutating actions.

Screenshots are base64-encoded and sent back to the model as `image` content blocks inside the `tool_result`, so the model can see them directly. Pass `--computer-use` on the CLI to enable the `anthropic-beta: computer-use-2024-10-22` header that the model requires for screen-aware behaviour.

## Differences from cc-haha (TypeScript)

| Feature | cc-haha (TS) | agentcpp |
|---------|--------------|----------|
| Runtime | Bun + V8 | Native binary |
| TUI | React + Ink | FTXUI |
| MCP servers | ✓ | ✓ |
| Multi-agent | ✓ | ✓ |
| Channels | ✓ | ✓ |
| Computer Use | ✓ | ✓ |
| Skills system | ✓ | ✓ |
| Memory system | text-file store | BM25 + graph + optional embeddings (RRF fusion) |
| Metacognition | — | MERIT (SelfBelief / CoTMonitor / SelfModel / SchemaReviser) |

## License

Based on Claude Code source code (c) Anthropic. For educational and research use only.
