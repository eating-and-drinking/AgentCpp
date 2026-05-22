#pragma once
#include "Tool.hpp"

namespace agentcpp::tools {

// ComputerTool — coarse mouse/keyboard/screenshot control via shell-out to
// platform tools (xdotool/scrot on Linux, osascript/screencapture on macOS).
//
// LIMITATION: screenshots are saved to a file and the path is returned as
// text. The model cannot directly "see" the screenshot in this version
// because ToolResultBlock only carries text content. Full Anthropic
// computer-use beta integration (which round-trips images back to the model)
// requires extending the API types — left as future work.
//
// `--read-only` blocks all mutating actions (everything except `screenshot`
// and `cursor_position`).
class ComputerTool : public Tool {
public:
    std::string name() const override { return "Computer"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;
};

} // namespace agentcpp::tools
