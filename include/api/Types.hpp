#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <variant>
#include <vector>
#include <optional>
#include <functional>

namespace agentcpp::api {

using json = nlohmann::json;

// ── Stop reasons ─────────────────────────────────────────────────────────────
enum class StopReason {
    EndTurn,
    MaxTokens,
    ToolUse,
    StopSequence,
    Unknown,
};

StopReason parseStopReason(const std::string& s);
std::string stopReasonToString(StopReason r);

// ── Usage stats ───────────────────────────────────────────────────────────────
struct Usage {
    int input_tokens   = 0;
    int output_tokens  = 0;
    int cache_read_input_tokens    = 0;
    int cache_creation_input_tokens = 0;
};

// ── Content blocks ────────────────────────────────────────────────────────────
struct TextBlock {
    std::string text;
};

struct ToolUseBlock {
    std::string id;
    std::string name;
    json        input;  // parsed JSON object
};

struct ToolResultBlock {
    std::string              tool_use_id;
    std::string              content;          // text portion of result
    std::string              image_b64;        // optional: base64 image (PNG/JPEG)
    std::string              image_media_type; // e.g. "image/png", required iff image_b64 set
    bool                     is_error = false;
};

struct ImageBlock {
    std::string media_type;  // e.g. "image/png"
    std::string data;        // base64
};

using ContentBlock = std::variant<TextBlock, ToolUseBlock, ToolResultBlock, ImageBlock>;

// ── Messages ──────────────────────────────────────────────────────────────────
enum class Role { User, Assistant };

std::string roleToString(Role r);
Role parseRole(const std::string& s);

struct Message {
    Role                      role;
    std::vector<ContentBlock> content;

    // Convenience: build a simple user text message
    static Message userText(std::string text);
    // Convenience: build a simple assistant text message
    static Message assistantText(std::string text);
    // Convenience: build a tool result message
    static Message toolResult(std::string tool_use_id, std::string content, bool is_error = false);
    // Tool result with both text and a base64-encoded image attachment.
    static Message toolResultWithImage(std::string tool_use_id,
                                       std::string text,
                                       std::string image_b64,
                                       std::string image_media_type,
                                       bool is_error = false);

    // Serialise to Anthropic API format
    json toJson() const;
};

// ── Tool schema ───────────────────────────────────────────────────────────────
struct ToolDefinition {
    std::string name;
    std::string description;
    json        input_schema;  // JSON Schema object

    json toJson() const;
};

// ── API request ───────────────────────────────────────────────────────────────
struct ApiRequest {
    std::string               model;
    int                       max_tokens = 8096;
    std::string               system;
    std::vector<Message>      messages;
    std::vector<ToolDefinition> tools;
    bool                      stream = true;

    json toJson() const;
};

// ── SSE stream events ─────────────────────────────────────────────────────────
namespace event {

struct MessageStart {
    std::string id;
    std::string model;
    Usage       usage;
};

struct ContentBlockStart {
    int         index;
    ContentBlock block;
};

struct ContentBlockDelta {
    int         index;
    std::string delta_type;   // "text_delta" | "input_json_delta"
    std::string text;         // for text_delta
    std::string partial_json; // for input_json_delta
};

struct ContentBlockStop {
    int index;
};

struct MessageDelta {
    StopReason  stop_reason;
    Usage       usage;
};

struct MessageStop {};
struct Ping {};
struct Error {
    std::string type;
    std::string message;
};

} // namespace event

using StreamEvent = std::variant<
    event::MessageStart,
    event::ContentBlockStart,
    event::ContentBlockDelta,
    event::ContentBlockStop,
    event::MessageDelta,
    event::MessageStop,
    event::Ping,
    event::Error
>;

// Parse an SSE data line into a StreamEvent (returns nullopt on unknown/skip)
std::optional<StreamEvent> parseStreamEvent(const std::string& line);

// ── Completed API response ────────────────────────────────────────────────────
struct ApiResponse {
    std::string               id;
    std::string               model;
    StopReason                stop_reason = StopReason::Unknown;
    std::vector<ContentBlock> content;
    Usage                     usage;
};

// ── Tool call result ──────────────────────────────────────────────────────────
struct ToolResult {
    std::string tool_use_id;
    std::string content;
    bool        is_error = false;
};

// ── Callback types ────────────────────────────────────────────────────────────
using StreamCallback  = std::function<void(const StreamEvent&)>;
using TextCallback    = std::function<void(const std::string&)>;  // text delta

} // namespace agentcpp::api
