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

StopReason  parseStopReason(const std::string& s);
std::string stopReasonToString(StopReason r);

// ── Usage stats ──────────────────────────────────────────────────────────────
struct Usage {
    int input_tokens                = 0;
    int output_tokens               = 0;
    int cache_read_input_tokens     = 0;
    int cache_creation_input_tokens = 0;
};

// ── Content blocks ───────────────────────────────────────────────────────────
//
// PR3 expands the block set with three new multimodal types: AudioBlock,
// DocumentBlock, and DataBlock. They sit alongside the original Text /
// ToolUse / ToolResult / Image blocks. Provider serialization happens in
// Types.cpp::blockToJson(); blocks the active model can't consume are
// downgraded by api::Capabilities before request emission (see Capabilities.hpp).
//
struct TextBlock {
    std::string text;
};

struct ImageBlock {
    std::string media_type;          // "image/png", "image/jpeg", "image/webp", "image/gif"
    std::string data;                // base64
    std::string url;                 // alternative to data — provider-dependent
};

struct AudioBlock {
    std::string media_type;          // "audio/mp3", "audio/wav", "audio/ogg"
    std::string data;                // base64
    int         sample_rate_hz = 0;  // optional hint, 0 = unknown
    // Pre-transcribed text (fallback when the model can't ingest audio
    // natively). Capabilities::downgradeRequest swaps the AudioBlock for a
    // TextBlock carrying this transcript on non-audio models.
    std::string transcript;
};

struct DocumentBlock {
    std::string media_type;          // "application/pdf", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
    std::string filename;            // user-visible name; sent as metadata where supported
    std::string data;                // base64 (preferred for Anthropic doc API)
    std::string url;                 // alternative; some providers prefer URL
    // Client-side pre-extracted text. Capabilities downgrade falls back to
    // this when the provider can't ingest native PDFs/DOCX.
    std::string extracted_text;
};

struct DataBlock {
    // schema_id picks the serialization shape:
    //   "table/csv"        — content is { columns: [...], rows: [[...], ...] }
    //   "json"             — content is arbitrary JSON
    //   "kv"               — content is a flat key/value map
    // Always rendered as a text part in the API request — the model sees a
    // markdown table for "table/csv" or a JSON code block for "json"/"kv".
    std::string    schema_id;
    nlohmann::json content;
    std::string    caption;
};

struct ToolUseBlock {
    std::string id;
    std::string name;
    json        input;               // parsed JSON object
};

struct ToolResultBlock {
    std::string              tool_use_id;
    std::string              content;            // primary text portion of result
    std::string              image_b64;          // optional inline image
    std::string              image_media_type;   // required iff image_b64 set
    // PR3: extra parts the tool may have emitted (data tables, additional
    // images, document outputs). Serialized into the Anthropic "content" array.
    std::vector<DataBlock>   data_parts;
    std::vector<ImageBlock>  image_parts;
    bool                     is_error = false;
};

using ContentBlock = std::variant<
    TextBlock,
    ToolUseBlock,
    ToolResultBlock,
    ImageBlock,
    AudioBlock,
    DocumentBlock,
    DataBlock
>;

// ── Messages ─────────────────────────────────────────────────────────────────
enum class Role { User, Assistant };

std::string roleToString(Role r);
Role        parseRole(const std::string& s);

struct Message {
    Role                      role;
    std::vector<ContentBlock> content;

    // Convenience builders
    static Message userText(std::string text);
    static Message assistantText(std::string text);
    static Message toolResult(std::string tool_use_id, std::string content, bool is_error = false);
    static Message toolResultWithImage(std::string tool_use_id,
                                       std::string text,
                                       std::string image_b64,
                                       std::string image_media_type,
                                       bool is_error = false);

    // PR3: user message carrying an arbitrary mix of multimodal blocks plus
    // a trailing text prompt. The order in `parts` is preserved.
    static Message userWithParts(std::vector<ContentBlock> parts, std::string trailing_text = "");

    // PR3: tool result with arbitrary content parts (text + images + data).
    static Message toolResultMulti(std::string tool_use_id,
                                   std::string primary_text,
                                   std::vector<ImageBlock> images,
                                   std::vector<DataBlock>  datas,
                                   bool is_error = false);

    // Serialize to Anthropic API format
    json toJson() const;
};

// ── Tool schema ──────────────────────────────────────────────────────────────
struct ToolDefinition {
    std::string name;
    std::string description;
    json        input_schema;
    json        toJson() const;
};

// ── API request ──────────────────────────────────────────────────────────────
struct ApiRequest {
    std::string                 model;
    int                         max_tokens = 8096;
    std::string                 system;
    std::vector<Message>        messages;
    std::vector<ToolDefinition> tools;
    bool                        stream = true;
    json                        toJson() const;
};

// ── SSE stream events ────────────────────────────────────────────────────────
namespace event {

struct MessageStart {
    std::string id;
    std::string model;
    Usage       usage;
};

struct ContentBlockStart {
    int          index;
    ContentBlock block;
};

struct ContentBlockDelta {
    int         index;
    std::string delta_type;          // "text_delta" | "input_json_delta"
    std::string text;
    std::string partial_json;
};

struct ContentBlockStop { int index; };

struct MessageDelta {
    StopReason stop_reason;
    Usage      usage;
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

std::optional<StreamEvent> parseStreamEvent(const std::string& line);

// ── Completed API response ───────────────────────────────────────────────────
struct ApiResponse {
    std::string               id;
    std::string               model;
    StopReason                stop_reason = StopReason::Unknown;
    std::vector<ContentBlock> content;
    Usage                     usage;
};

// ── Tool call result (legacy, used by ToolCallResult -> Message bridging) ───
struct ToolResult {
    std::string tool_use_id;
    std::string content;
    bool        is_error = false;
};

// ── Callback types ───────────────────────────────────────────────────────────
using StreamCallback = std::function<void(const StreamEvent&)>;
using TextCallback   = std::function<void(const std::string&)>;

} // namespace agentcpp::api
