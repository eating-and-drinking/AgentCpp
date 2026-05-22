#include <tools/ChannelTool.hpp>
#include <channels/ChannelBus.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace agentcpp::tools {

namespace ch = agentcpp::channels;

namespace {
std::string senderForCtx(const ToolContext& ctx) {
    if (ctx.subagent_depth == 0) return "main";
    return "task-d" + std::to_string(ctx.subagent_depth);
}

std::string fmtTime(std::int64_t epoch_ms) {
    std::time_t t = epoch_ms / 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%H:%M:%S");
    return ss.str();
}
} // namespace

// ──────────────────────────── ChannelPublish ─────────────────────────────────

std::string ChannelPublishTool::description() const {
    return
        "Publish a message to a named in-process channel. Channels are auto-"
        "created on first use and persist for the duration of this CLI run. "
        "Useful for coordinating with sub-agents (Task) or leaving notes for "
        "later turns to read with ChannelRead.";
}

json ChannelPublishTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"channel", {{"type", "string"}, {"description", "Channel name."}}},
            {"text",    {{"type", "string"}, {"description", "Message body."}}}
        }},
        {"required", json::array({"channel", "text"})}
    };
}

ToolCallResult ChannelPublishTool::execute(const json& input, const ToolContext& ctx) {
    std::string channel = input.value("channel", "");
    std::string text    = input.value("text", "");
    if (channel.empty()) return ToolCallResult::error("'channel' is required");

    auto id = ch::ChannelBus::instance().publish(channel, senderForCtx(ctx), text);
    return ToolCallResult::ok(
        "Published message #" + std::to_string(id) + " to '" + channel + "'");
}

// ──────────────────────────── ChannelRead ────────────────────────────────────

std::string ChannelReadTool::description() const {
    return
        "Read messages from a named channel. Optionally pass `since_id` to "
        "return only messages newer than a given id (use the last id you saw).";
}

json ChannelReadTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"channel",  {{"type", "string"},  {"description", "Channel name."}}},
            {"since_id", {{"type", "integer"}, {"description", "Return messages with id > since_id."}, {"default", 0}}}
        }},
        {"required", json::array({"channel"})}
    };
}

ToolCallResult ChannelReadTool::execute(const json& input, const ToolContext& /*ctx*/) {
    std::string channel = input.value("channel", "");
    if (channel.empty()) return ToolCallResult::error("'channel' is required");
    std::uint64_t since = input.value("since_id", 0ULL);

    auto msgs = ch::ChannelBus::instance().read(channel, since);
    std::ostringstream out;
    out << "Channel '" << channel << "' (" << msgs.size() << " msg";
    out << (msgs.size() == 1 ? "" : "s") << " since #" << since << ")\n";
    if (msgs.empty()) {
        out << "(none)";
        return ToolCallResult::ok(out.str());
    }
    for (const auto& m : msgs) {
        out << "\n#" << m.id << " [" << fmtTime(m.epoch_ms) << " " << m.sender << "] " << m.text;
    }
    return ToolCallResult::ok(out.str());
}

// ──────────────────────────── ChannelList ────────────────────────────────────

std::string ChannelListTool::description() const {
    return
        "List every channel that currently has at least one message, with the "
        "message count and the latest message id.";
}

json ChannelListTool::inputSchema() const {
    return {{"type", "object"}, {"properties", json::object()}};
}

ToolCallResult ChannelListTool::execute(const json& /*input*/, const ToolContext& /*ctx*/) {
    auto infos = ch::ChannelBus::instance().list();
    std::ostringstream out;
    out << "Channels: " << infos.size() << "\n";
    if (infos.empty()) { out << "(none)"; return ToolCallResult::ok(out.str()); }
    for (const auto& i : infos) {
        out << "  " << i.name << "  msgs=" << i.message_count
            << "  latest=#" << i.latest_id << "\n";
    }
    return ToolCallResult::ok(out.str());
}

} // namespace agentcpp::tools
