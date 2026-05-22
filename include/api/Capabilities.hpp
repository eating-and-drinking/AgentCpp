#pragma once
//
// Provider/model capability detection + request downgrade.
//
// Different LLMs accept different content block types. Some examples:
//   - Claude 4.x / 3.5    : text, image, document (PDF) native, no audio_in
//   - GPT-4o              : text, image, audio_in, audio_out, PDF via file API
//   - Gemini 2.x          : text, image, audio_in, audio_out, document native
//   - Ollama / local      : usually text-only; multimodal model-dependent
//
// agentcpp keeps a single internal request shape (api::ApiRequest with the
// full ContentBlock variant). Right before sending we run downgradeRequest()
// to convert blocks the chosen model can't consume into text fallbacks. This
// way the agent loop stays protocol-neutral while real requests remain
// safely bounded to what each model accepts.
//
#include <api/Types.hpp>
#include <string>
#include <string_view>

namespace agentcpp::api::caps {

// Capability feature flags. Names are stable across providers.
enum class Feature {
    Vision,         // can the model read ImageBlock natively?
    AudioIn,        // can it consume AudioBlock?
    AudioOut,       // can it emit audio?
    DocumentNative, // can it consume DocumentBlock (PDF/DOCX) without us extracting text?
    Tools,          // tool_use / function calling support
    ToolStreaming,  // streamed tool_use arguments (incremental input_json_delta)
    ComputerUse,    // Anthropic computer-use beta
};

// Best-effort capability lookup by model id substring matching. Returns
// `true` when we have confident knowledge of support; `false` defaults to
// "be safe, assume not supported".
bool supports(std::string_view model_id, Feature feat);

// Convenience: feature x ContentBlock alternative.
bool blockSupported(std::string_view model_id, const ContentBlock& block);

// Mutate the request in place, replacing unsupported blocks with text
// fallbacks. Returns the number of substitutions made (for logging).
//
// Substitution rules:
//   ImageBlock     unsupported → "[Image: not viewable to this model]"
//   AudioBlock     unsupported → AudioBlock.transcript if set, else placeholder
//   DocumentBlock  unsupported → DocumentBlock.extracted_text if set,
//                                 else "[Document <filename>: native ingestion
//                                       not supported by this model]"
//   DataBlock      always supported (already serializes to text)
//   ToolUseBlock / ToolResultBlock / TextBlock — never substituted
//
int  downgradeRequest(ApiRequest& req);

// Single-block helper exposed for unit testing.
ContentBlock downgradeBlock(std::string_view model_id, const ContentBlock& block);

} // namespace agentcpp::api::caps
