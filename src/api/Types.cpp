#include <api/Types.hpp>
#include <stdexcept>
#include <sstream>

namespace agentcpp::api {

// ── StopReason ───────────────────────────────────────────────────────────────
StopReason parseStopReason(const std::string& s) {
    if (s == "end_turn")        return StopReason::EndTurn;
    if (s == "max_tokens")      return StopReason::MaxTokens;
    if (s == "tool_use")        return StopReason::ToolUse;
    if (s == "stop_sequence")   return StopReason::StopSequence;
    return StopReason::Unknown;
}

std::string stopReasonToString(StopReason r) {
    switch (r) {
        case StopReason::EndTurn:      return "end_turn";
        case StopReason::MaxTokens:    return "max_tokens";
        case StopReason::ToolUse:      return "tool_use";
        case StopReason::StopSequence: return "stop_sequence";
        default:                       return "unknown";
    }
}

// ── Role ─────────────────────────────────────────────────────────────────────
std::string roleToString(Role r) {
    return r == Role::User ? "user" : "assistant";
}

Role parseRole(const std::string& s) {
    if (s == "user")      return Role::User;
    if (s == "assistant") return Role::Assistant;
    throw std::runtime_error("Unknown role: " + s);
}

// ── Message builders ─────────────────────────────────────────────────────────
Message Message::userText(std::string text) {
    return Message{ Role::User, { TextBlock{ std::move(text) } } };
}

Message Message::assistantText(std::string text) {
    return Message{ Role::Assistant, { TextBlock{ std::move(text) } } };
}

Message Message::toolResult(std::string tool_use_id, std::string content, bool is_error) {
    ToolResultBlock tr;
    tr.tool_use_id = std::move(tool_use_id);
    tr.content     = std::move(content);
    tr.is_error    = is_error;
    return Message{ Role::User, { std::move(tr) } };
}

Message Message::toolResultWithImage(std::string tool_use_id,
                                     std::string text,
                                     std::string image_b64,
                                     std::string image_media_type,
                                     bool is_error) {
    ToolResultBlock tr;
    tr.tool_use_id      = std::move(tool_use_id);
    tr.content          = std::move(text);
    tr.image_b64        = std::move(image_b64);
    tr.image_media_type = std::move(image_media_type);
    tr.is_error         = is_error;
    return Message{ Role::User, { std::move(tr) } };
}

Message Message::userWithParts(std::vector<ContentBlock> parts, std::string trailing_text) {
    if (!trailing_text.empty()) {
        parts.push_back(TextBlock{ std::move(trailing_text) });
    }
    return Message{ Role::User, std::move(parts) };
}

Message Message::toolResultMulti(std::string tool_use_id,
                                 std::string primary_text,
                                 std::vector<ImageBlock> images,
                                 std::vector<DataBlock>  datas,
                                 bool is_error) {
    ToolResultBlock tr;
    tr.tool_use_id = std::move(tool_use_id);
    tr.content     = std::move(primary_text);
    tr.image_parts = std::move(images);
    tr.data_parts  = std::move(datas);
    tr.is_error    = is_error;
    return Message{ Role::User, { std::move(tr) } };
}

// ── Helpers ──────────────────────────────────────────────────────────────────
namespace {

// Render a DataBlock to a model-readable text snippet. Used both when the
// model needs to see a DataBlock directly and when downgrading.
std::string dataBlockToText(const DataBlock& d) {
    std::ostringstream ss;
    if (!d.caption.empty()) ss << "**" << d.caption << "**\n\n";

    if (d.schema_id == "table/csv") {
        auto cols = d.content.contains("columns") ? d.content["columns"] : nlohmann::json::array();
        auto rows = d.content.contains("rows")    ? d.content["rows"]    : nlohmann::json::array();
        if (!cols.empty()) {
            ss << "| ";
            for (const auto& c : cols) ss << c.dump() << " | ";
            ss << "\n|";
            for (std::size_t i = 0; i < cols.size(); ++i) ss << "---|";
            ss << "\n";
        }
        for (const auto& row : rows) {
            ss << "| ";
            if (row.is_array()) {
                for (const auto& v : row) {
                    ss << (v.is_string() ? v.get<std::string>() : v.dump()) << " | ";
                }
            }
            ss << "\n";
        }
    } else {
        ss << "```json\n" << d.content.dump(2) << "\n```";
    }
    return ss.str();
}

// Render an Anthropic-shape "image source" object.
nlohmann::json imageSourceJson(const std::string& media_type,
                               const std::string& data_b64,
                               const std::string& url) {
    if (!url.empty()) {
        return { {"type", "url"}, {"url", url} };
    }
    return {
        {"type", "base64"},
        {"media_type", media_type.empty() ? std::string("image/png") : media_type},
        {"data", data_b64}
    };
}

} // anonymous namespace

// ── Per-block serialization ──────────────────────────────────────────────────
static json blockToJson(const ContentBlock& block) {
    return std::visit([](auto&& b) -> json {
        using T = std::decay_t<decltype(b)>;

        if constexpr (std::is_same_v<T, TextBlock>) {
            return { {"type", "text"}, {"text", b.text} };
        }
        else if constexpr (std::is_same_v<T, ToolUseBlock>) {
            return { {"type", "tool_use"}, {"id", b.id},
                     {"name", b.name}, {"input", b.input} };
        }
        else if constexpr (std::is_same_v<T, ToolResultBlock>) {
            json obj = { {"type", "tool_result"},
                         {"tool_use_id", b.tool_use_id},
                         {"is_error", b.is_error} };
            // If the result is plain text (no images, no data parts, no
            // legacy image_b64) we keep the simple string form for the wire.
            bool has_extras = !b.image_b64.empty()
                            || !b.image_parts.empty()
                            || !b.data_parts.empty();
            if (!has_extras) {
                obj["content"] = b.content;
            } else {
                json arr = json::array();
                if (!b.content.empty()) {
                    arr.push_back({ {"type", "text"}, {"text", b.content} });
                }
                // Data parts inline as text (markdown / json fence).
                for (const auto& d : b.data_parts) {
                    arr.push_back({ {"type", "text"},
                                    {"text", dataBlockToText(d)} });
                }
                // Legacy single image (kept for backwards compatibility).
                if (!b.image_b64.empty()) {
                    arr.push_back({
                        {"type", "image"},
                        {"source", imageSourceJson(b.image_media_type, b.image_b64, "")}
                    });
                }
                // Additional images
                for (const auto& img : b.image_parts) {
                    arr.push_back({
                        {"type", "image"},
                        {"source", imageSourceJson(img.media_type, img.data, img.url)}
                    });
                }
                obj["content"] = std::move(arr);
            }
            return obj;
        }
        else if constexpr (std::is_same_v<T, ImageBlock>) {
            return {
                {"type", "image"},
                {"source", imageSourceJson(b.media_type, b.data, b.url)}
            };
        }
        else if constexpr (std::is_same_v<T, AudioBlock>) {
            // Anthropic 2026-style audio (placeholder shape — only providers
            // that support audio_in actually accept this; non-audio models
            // are routed through Capabilities::downgradeRequest first).
            return {
                {"type", "audio"},
                {"source", {
                    {"type", "base64"},
                    {"media_type", b.media_type.empty() ? "audio/wav" : b.media_type},
                    {"data", b.data}
                }}
            };
        }
        else if constexpr (std::is_same_v<T, DocumentBlock>) {
            json src;
            if (!b.url.empty()) {
                src = { {"type", "url"}, {"url", b.url} };
            } else {
                src = { {"type", "base64"},
                        {"media_type", b.media_type.empty() ? "application/pdf" : b.media_type},
                        {"data", b.data} };
            }
            json obj = { {"type", "document"}, {"source", std::move(src)} };
            if (!b.filename.empty()) {
                obj["title"] = b.filename;
            }
            return obj;
        }
        else if constexpr (std::is_same_v<T, DataBlock>) {
            return { {"type", "text"}, {"text", dataBlockToText(b)} };
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

// ── ToolDefinition ───────────────────────────────────────────────────────────
json ToolDefinition::toJson() const {
    return {
        {"name", name},
        {"description", description},
        {"input_schema", input_schema}
    };
}

// ── ApiRequest ───────────────────────────────────────────────────────────────
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

// ── parseStreamEvent ─────────────────────────────────────────────────────────
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
        if (j.contains("delta")) {
            auto& d = j["delta"];
            ev.stop_reason = parseStopReason(d.value("stop_reason", ""));
        }
        if (j.contains("usage")) {
            ev.usage.output_tokens = j["usage"].value("output_tokens", 0);
        }
        return ev;
    }

    if (type == "message_stop") return event::MessageStop{};

    return std::nullopt;
}

} // namespace agentcpp::api
