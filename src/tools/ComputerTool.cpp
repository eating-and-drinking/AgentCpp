#include <tools/ComputerTool.hpp>
#include <utils/Logger.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <sys/wait.h>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>

namespace agentcpp::tools {

namespace {

enum class Platform { Linux, MacOS, Other };

Platform detectPlatform() {
#if defined(__linux__)
    return Platform::Linux;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Other;
#endif
}

// Run a command via popen, capture stdout, return {exit_code, output}.
std::pair<int, std::string> runCmd(const std::string& cmd) {
    std::array<char, 4096> buf{};
    std::string out;
    FILE* f = popen((cmd + " 2>&1").c_str(), "r");
    if (!f) return {-1, "popen failed"};
    while (std::size_t n = std::fread(buf.data(), 1, buf.size(), f)) {
        out.append(buf.data(), n);
    }
    int rc = pclose(f);
    if (rc == -1) return {-1, out};
    if (WIFEXITED(rc)) return {WEXITSTATUS(rc), std::move(out)};
    return {-1, std::move(out)};
}

// Shell-quote a string for safe interpolation into bash commands.
std::string shq(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string tempPngPath() {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return "/tmp/agentcpp-screen-" + std::to_string(ms) + ".png";
}

// RFC 4648 base64 encoder (no line breaks).
std::string base64Encode(const std::string& bin) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bin.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= bin.size()) {
        std::uint32_t n = (static_cast<std::uint8_t>(bin[i]) << 16)
                        | (static_cast<std::uint8_t>(bin[i+1]) << 8)
                        |  static_cast<std::uint8_t>(bin[i+2]);
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6)  & 63]);
        out.push_back(T[ n        & 63]);
        i += 3;
    }
    std::size_t rem = bin.size() - i;
    if (rem == 1) {
        std::uint32_t n = static_cast<std::uint8_t>(bin[i]) << 16;
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        std::uint32_t n = (static_cast<std::uint8_t>(bin[i]) << 16)
                        | (static_cast<std::uint8_t>(bin[i+1]) << 8);
        out.push_back(T[(n >> 18) & 63]);
        out.push_back(T[(n >> 12) & 63]);
        out.push_back(T[(n >> 6)  & 63]);
        out.push_back('=');
    }
    return out;
}

// Read a whole file into a string. Returns empty on failure.
std::string readFileBinary(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string out;
    char buf[8192];
    while (auto n = std::fread(buf, 1, sizeof(buf), f)) out.append(buf, n);
    std::fclose(f);
    return out;
}

// Linux dispatch — uses xdotool / scrot.
ToolCallResult execLinux(const std::string& action, const json& input) {
    if (action == "screenshot") {
        std::string path = tempPngPath();
        std::string cmd = "scrot " + shq(path);
        auto [rc, out] = runCmd(cmd);
        if (rc != 0) {
            // Fallback: try maim
            cmd = "maim " + shq(path);
            auto [rc2, out2] = runCmd(cmd);
            if (rc2 != 0) {
                return ToolCallResult::error(
                    "screenshot failed (need `scrot` or `maim` installed). " + out + out2);
            }
        }
        std::error_code ec;
        auto sz = std::filesystem::file_size(path, ec);
        std::string bin = readFileBinary(path);
        if (bin.empty()) {
            return ToolCallResult::error("screenshot file empty/unreadable: " + path);
        }
        std::string b64 = base64Encode(bin);
        std::string note = "screenshot " + path + " (" + std::to_string(sz) + " bytes)";
        return ToolCallResult::okWithImage(std::move(note), std::move(b64), "image/png");
    }
    if (action == "cursor_position") {
        auto [rc, out] = runCmd("xdotool getmouselocation --shell");
        if (rc != 0) return ToolCallResult::error("xdotool failed: " + out);
        return ToolCallResult::ok(out);
    }

    // Mutating actions below — assemble xdotool command
    std::string xcmd = "xdotool ";
    if (action == "mouse_move") {
        xcmd += "mousemove " + std::to_string(input.value("x", 0)) +
                " " + std::to_string(input.value("y", 0));
    } else if (action == "left_click")   xcmd += "click 1";
    else   if (action == "middle_click") xcmd += "click 2";
    else   if (action == "right_click")  xcmd += "click 3";
    else   if (action == "double_click") xcmd += "click --repeat 2 --delay 100 1";
    else   if (action == "scroll") {
        // delta>0 scroll down (button 5), delta<0 scroll up (button 4)
        int delta = input.value("delta", 1);
        int btn   = delta > 0 ? 5 : 4;
        int abs_d = delta > 0 ? delta : -delta;
        xcmd += "click --repeat " + std::to_string(abs_d) + " " + std::to_string(btn);
    } else if (action == "type") {
        std::string text = input.value("text", "");
        xcmd += "type --delay 12 -- " + shq(text);
    } else if (action == "key") {
        std::string keys = input.value("keys", "");
        if (keys.empty()) return ToolCallResult::error("'keys' required for action=key");
        xcmd += "key " + shq(keys);
    } else {
        return ToolCallResult::error("unknown action: " + action);
    }

    auto [rc, out] = runCmd(xcmd);
    if (rc != 0) return ToolCallResult::error("xdotool failed: " + out);
    return ToolCallResult::ok("ok: " + action);
}

// macOS dispatch — uses screencapture + cliclick (or osascript).
ToolCallResult execMacOS(const std::string& action, const json& input) {
    if (action == "screenshot") {
        std::string path = tempPngPath();
        auto [rc, out] = runCmd("screencapture -x " + shq(path));
        if (rc != 0) return ToolCallResult::error("screencapture failed: " + out);
        std::error_code ec;
        auto sz = std::filesystem::file_size(path, ec);
        std::string bin = readFileBinary(path);
        if (bin.empty()) {
            return ToolCallResult::error("screenshot file empty/unreadable: " + path);
        }
        std::string b64 = base64Encode(bin);
        std::string note = "screenshot " + path + " (" + std::to_string(sz) + " bytes)";
        return ToolCallResult::okWithImage(std::move(note), std::move(b64), "image/png");
    }
    if (action == "cursor_position") {
        // Requires `cliclick` (brew install cliclick) for cursor pos
        auto [rc, out] = runCmd("cliclick p");
        if (rc != 0) return ToolCallResult::error(
            "cliclick failed (install via `brew install cliclick`): " + out);
        return ToolCallResult::ok(out);
    }

    // For mouse/keyboard, prefer cliclick.
    std::string ccmd = "cliclick ";
    if (action == "mouse_move") {
        ccmd += "m:" + std::to_string(input.value("x", 0)) +
                "," + std::to_string(input.value("y", 0));
    } else if (action == "left_click") {
        if (input.contains("x") && input.contains("y")) {
            ccmd += "c:" + std::to_string(input["x"].get<int>()) +
                    "," + std::to_string(input["y"].get<int>());
        } else {
            ccmd += "c:.";
        }
    } else if (action == "right_click") {
        ccmd += "rc:.";
    } else if (action == "double_click") {
        ccmd += "dc:.";
    } else if (action == "type") {
        ccmd += "t:" + shq(input.value("text", ""));
    } else if (action == "key") {
        ccmd += "kp:" + shq(input.value("keys", ""));
    } else if (action == "scroll") {
        // cliclick lacks native scroll; punt
        return ToolCallResult::error("scroll not supported on macOS in this version");
    } else {
        return ToolCallResult::error("unknown action: " + action);
    }
    auto [rc, out] = runCmd(ccmd);
    if (rc != 0) return ToolCallResult::error(
        "cliclick failed (install via `brew install cliclick`): " + out);
    return ToolCallResult::ok("ok: " + action);
}

bool isMutating(const std::string& action) {
    return action != "screenshot" && action != "cursor_position";
}

} // namespace

std::string ComputerTool::description() const {
    return
        "Control the screen and input devices. Actions: screenshot, "
        "cursor_position, mouse_move, left_click, right_click, middle_click, "
        "double_click, scroll, type, key. Requires `xdotool`+`scrot`/`maim` "
        "on Linux, `cliclick`+`screencapture` on macOS. Windows is not "
        "supported. Note: screenshots are saved to /tmp and the path is "
        "returned as text — the model does not directly see the image.";
}

json ComputerTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"action", {{"type", "string"},
                {"description", "screenshot | cursor_position | mouse_move | "
                                "left_click | right_click | middle_click | "
                                "double_click | scroll | type | key"}}},
            {"x",      {{"type", "integer"}, {"description", "x coord (for mouse_move/click)"}}},
            {"y",      {{"type", "integer"}, {"description", "y coord"}}},
            {"text",   {{"type", "string"},  {"description", "text to type"}}},
            {"keys",   {{"type", "string"},  {"description", "key combo, e.g. \"ctrl+c\", \"Return\""}}},
            {"delta",  {{"type", "integer"}, {"description", "scroll amount (negative=up)"}}}
        }},
        {"required", json::array({"action"})}
    };
}

ToolCallResult ComputerTool::execute(const json& input, const ToolContext& ctx) {
    std::string action = input.value("action", "");
    if (action.empty()) return ToolCallResult::error("'action' is required");

    if (ctx.read_only && isMutating(action)) {
        return ToolCallResult::error(
            "read-only mode: Computer action '" + action + "' is disabled");
    }

    switch (detectPlatform()) {
        case Platform::Linux: return execLinux(action, input);
        case Platform::MacOS: return execMacOS(action, input);
        default:
            return ToolCallResult::error(
                "Computer tool not supported on this platform (Linux/macOS only)");
    }
}

} // namespace agentcpp::tools
