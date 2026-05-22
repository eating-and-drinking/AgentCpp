#include <api/Types.hpp>
#include <stdexcept>

namespace agentcpp::api {

// ── StopReason ────────────────────────────────────────────────────────────────
StopReason parseStopReason(const std::string& s) {
    if (s == "end_turn")        return StopReason::EndTurn;
    if (s == "max_tokens")      return StopReason::MaxTokens;
    if (s == "tool_use")        return StopReason::ToolUse;
    if (s == "stop_sequence")   return StopReason::StopSequence;
    return StopReason::Unknown;
}

std::string stopReasonToString(StopReason r) {
    switch (r) {
        case StopReason::EndTurn:       return "end_turn";
        case StopReason::MaxTokens:     return "max_tokens";
        case StopReason::ToolUse:       return "tool_use";
        case StopReason::StopSequence:  return "stop_sequence";
        default:                        return "unknown";
    }
}

// ── Role ──────────────────────────────────────────────────────────────────────
std::string roleToString(Role r) {
    return r == Role::User ? "user" : "assistant";
}

Role parseRole(const std::string& s) {
    if (s == "user")      return Role::User;
    if (s == "assistant") return Role::Assistant;
    throw std::runtime_error("Unknown role: " + s);
}

// ── Message helpers ───────────────────────────────────────────────────────────
Message Message::userText(std::string text) {
    return Message{ Role::User, { TextBlock{ std::move(text) } } };
}

Message Message::assistantText(std::string text) {
    return Message{ Role::Assistant, { TextBlock{ std::move(text) } } };
}

Message Message::toolResult(std::string tool_use_id, std::string content, bool is_error) {
    return Message{
        Role::User,
        { ToolResultBlock{ std::move(tool_use_id), std::move(content), is_error } }
    };
}

Message Message::toolResultWithImage(std::string tool_use_id,
                                     std::string text,
                                     std::string image_b64,
                                     std::string image_media_type,
                                     bool is_error) {
    ToolResultBlock tr;
    tr.tool_use_id     = std::move(tool_use_id);
    tr.content         = std::move(text);
    tr.image_b64       = std::move(image_b64);
    tr.image_media_type = std::move(image_media_type);
    tr.is_error        = is_error;
    return Message{ Role::User, { std::move(tr) } };
}

// Serialise a ContentBlock to JSON
static json blockToJson(const ContentBlock& block) {
    return std::visit([](auto&& b) -> json {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, TextBlock>) {
            return { {"type", "text"}, {"text", b.text} };
        } else if constexpr (std::is_same_v<T, ToolUseBlock>) {
            return { {"type", "tool_use"}, {"id", b.id},
                     {"name", b.name}, {"input", b.input} };
        } else if constexpr (std::is_same_v<T, ToolResultBlock>) {
            json obj = { {"type", "tool_result"},
                         {"tool_use_id", b.tool_use_id},
                         {"is_error", b.is_error} };
            if (b.image_b64.empty()) {
                obj["content"] = b.content;
            } else {
                json arr = json::array();
                if (!b.content.empty()) {
                    arr.push_back({ {"type", "text"}, {"text", b.content} });
                }
                arr.push_back({
                    {"type", "image"},
                    {"source", {
                        {"type", "base64"},
                        {"media_type", b.image_media_type.empty()
                            ? std::string("image/png") : b.image_media_type},
                        {"data", b.image_b64}
                    }}
                });
                obj["content"] = std::move(arr);
            }
            return obj;
        } else if constexpr (std::is_same_v<T, ImageBlock>) {
            return {
                {"type", "image"},
                {"source", {
                    {"type", "base64"},
                    {"media_type", b.media_type},
                    {"data", b.data}
                }}
            };
        }
        return json{};
    }, block);
}

json Message::toJson() const {
    json content_arr = json::array();
    for (const auto& block : content) {
        content_arr.push_back(blockToJson(block));
    }
    return { {"role", roleToString(role)}, {"content", content_arr} };
}

// ── ToolDefinition ────────────────────────────────────────────────────────────
json ToolDefinition::toJson() const {
    return {
        {"name", name},
        {"description", description},
        {"input_schema", input_schema}
    };
}

// ── ApiRequest ────────────────────────────────────────────────────────────────
json ApiRequest::toJson() const {
    json msg_arr = json::array();
    for (const auto& m : messages) msg_arr.push_back(m.toJson());

    json tool_arr = json::array();
    for (const auto& t : tools) tool_arr.push_back(t.toJson());

    json body = {
        {"model",      model},
        {"max_tokens", max_tokens},
        {"messages",   msg_arr},
        {"stream",     stream},
    };
    if (!system.empty())    body["system"] = system;
    if (!tool_arr.empty())  body["tools"]  = tool_arr;
    return body;
}

// ── parseStreamEvent ──────────────────────────────────────────────────────────
std::optional<StreamEvent> parseStreamEvent(const std::string& line) {
    if (line.empty() || line == "data: [DONE]") return std::nullopt;

    std::string data;
    if (line.rfind("data: ", 0) == 0) {
        data = line.substr(6);
    } else {
        return std::nullopt;
    }

    json j;
    try { j = json::parse(data); }
    catch (...) { return std::nullopt; }

    if (!j.contains("type")) return std::nullopt;
    std::string type = j["type"];

    if (type == "ping") return event::Ping{};

    if (type == "error") {
        event::Error ev;
        ev.type    = j.value("error", json{}).value("type", "");
        ev.message = j.value("error", json{}).value("message", "Unknown error");
        return ev;
    }

    if (type == "message_start") {
        event::MessageStart ev;
        auto& msg = j["message"];
        ev.id    = msg.value("id", "");
        ev.model = msg.value("model", "");
        if (msg.contains("usage")) {
            ev.usage.input_tokens  = msg["usage"].value("input_tokens", 0);
            ev.usage.output_tokens = msg["usage"].value("output_tokens", 0);
        }
        return ev;
    }

    if (type == "content_block_start") {
        event::ContentBlockStart ev;
        ev.index = j.value("index", 0);
        auto& cb = j["content_block"];
        std::string cb_type = cb.value("type", "");
        if (cb_type == "text") {
            ev.block = TextBlock{ cb.value("text", "") };
        } else if (cb_type == "tool_use") {
            ev.block = ToolUseBlock{
                cb.value("id", ""),
                cb.value("name", ""),
                json{}
            };
        }
        return ev;
    }

    if (type == "content_block_delta") {
        event::ContentBlockDelta ev;
        ev.index = j.value("index", 0);
        auto& delta = j["delta"];
        ev.delta_type   = delta.value("type", "");
        ev.text         = delta.value("text", "");
        ev.partial_json = delta.value("partial_json", "");
        return ev;
    }

    if (type == "content_block_stop") {
        return event::ContentBlockStop{ j.value("index", 0) };
    }

    if (type == "message_delta") {
        event::MessageDelta ev;
        ev.stop_reason = parseStopReason(j["delta"].value("stop_reason", ""));
        if (j.contains("usage")) {
            ev.usage.output_tokens = j["usage"].value("output_tokens", 0);
        }
        return ev;
    }

    if (type == "message_stop") {
        return event::MessageStop{};
    }

    return std::nullopt;
}

} // namespace agentcpp::api
