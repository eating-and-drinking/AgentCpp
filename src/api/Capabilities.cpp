#include <api/Capabilities.hpp>
#include <utils/Logger.hpp>
#include <algorithm>
#include <cctype>

namespace agentcpp::api::caps {

namespace {

// Case-insensitive substring check.
bool contains(std::string_view hay, std::string_view needle) {
    if (needle.empty() || hay.size() < needle.size()) return false;
    auto lc = [](char c){ return (char)std::tolower((unsigned char)c); };
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
            if (lc(hay[i+j]) != lc(needle[j])) { ok = false; break; }
        }
        if (ok) return true;
    }
    return false;
}

// Family classification — broad enough to cover most deployed models.
enum class Family { Claude, GPT4o, GPT, GeminiPro, Gemini, Llama, Mistral, Ollama, Unknown };

Family classify(std::string_view m) {
    if (contains(m, "claude"))   return Family::Claude;
    if (contains(m, "gpt-4o"))   return Family::GPT4o;
    if (contains(m, "gpt-"))     return Family::GPT;
    if (contains(m, "gemini-2")) return Family::GeminiPro;
    if (contains(m, "gemini"))   return Family::Gemini;
    if (contains(m, "llama"))    return Family::Llama;
    if (contains(m, "mistral"))  return Family::Mistral;
    if (contains(m, "ollama") || contains(m, "qwen") || contains(m, "phi") ||
        contains(m, "local") || contains(m, ":"))   // many ollama tags are like "llama3:8b"
        return Family::Ollama;
    return Family::Unknown;
}

} // anonymous namespace

bool supports(std::string_view model_id, Feature feat) {
    Family f = classify(model_id);
    switch (feat) {
        case Feature::Tools:
            return f == Family::Claude || f == Family::GPT4o || f == Family::GPT
                || f == Family::GeminiPro || f == Family::Gemini || f == Family::Mistral;
        case Feature::ToolStreaming:
            return f == Family::Claude || f == Family::GPT4o || f == Family::GPT;
        case Feature::Vision:
            // Vision-capable models in each family. We approximate at family
            // level — agents can still be misled by name typos; substring
            // matching keeps us conservative.
            return f == Family::Claude || f == Family::GPT4o
                || f == Family::GeminiPro || f == Family::Gemini;
        case Feature::AudioIn:
            return f == Family::GPT4o
                || f == Family::GeminiPro || f == Family::Gemini;
        case Feature::AudioOut:
            return f == Family::GPT4o
                || f == Family::GeminiPro;
        case Feature::DocumentNative:
            // Claude supports PDF via document blocks; Gemini accepts inline
            // PDF; GPT-4o accepts PDF via file API (different path — we keep
            // it false here so we always pre-extract for GPT in this version).
            return f == Family::Claude
                || f == Family::GeminiPro || f == Family::Gemini;
        case Feature::ComputerUse:
            return f == Family::Claude;
    }
    return false;
}

bool blockSupported(std::string_view model_id, const ContentBlock& block) {
    return std::visit([&](auto&& b) -> bool {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, ImageBlock>)    return supports(model_id, Feature::Vision);
        else if constexpr (std::is_same_v<T, AudioBlock>) return supports(model_id, Feature::AudioIn);
        else if constexpr (std::is_same_v<T, DocumentBlock>) return supports(model_id, Feature::DocumentNative);
        else return true;     // Text, ToolUse, ToolResult, Data — universally OK
    }, block);
}

ContentBlock downgradeBlock(std::string_view model_id, const ContentBlock& block) {
    return std::visit([&](auto&& b) -> ContentBlock {
        using T = std::decay_t<decltype(b)>;

        if constexpr (std::is_same_v<T, ImageBlock>) {
            if (supports(model_id, Feature::Vision)) return b;
            return TextBlock{"[Image: not viewable to this model"
                             + std::string(b.media_type.empty() ? "" : ", type=" + b.media_type)
                             + "]"};
        }
        else if constexpr (std::is_same_v<T, AudioBlock>) {
            if (supports(model_id, Feature::AudioIn)) return b;
            if (!b.transcript.empty()) {
                return TextBlock{"[Audio transcript]\n" + b.transcript};
            }
            return TextBlock{"[Audio: not consumable by this model; no transcript available]"};
        }
        else if constexpr (std::is_same_v<T, DocumentBlock>) {
            if (supports(model_id, Feature::DocumentNative)) return b;
            if (!b.extracted_text.empty()) {
                std::string head = b.filename.empty()
                    ? std::string("[Document]\n")
                    : ("[Document: " + b.filename + "]\n");
                return TextBlock{head + b.extracted_text};
            }
            return TextBlock{
                "[Document " + (b.filename.empty() ? std::string("(unnamed)") : b.filename) +
                ": native ingestion not supported by this model; please re-attach as text]"};
        }
        else {
            return b;
        }
    }, block);
}

int downgradeRequest(ApiRequest& req) {
    int subs = 0;
    for (auto& msg : req.messages) {
        for (auto& blk : msg.content) {
            ContentBlock replaced = downgradeBlock(req.model, blk);
            // std::variant comparison isn't trivial; we use index() shift as a
            // proxy: when the variant index changes we know we swapped types.
            if (replaced.index() != blk.index()) {
                blk = std::move(replaced);
                ++subs;
            }
        }
    }
    if (subs > 0) {
        LOG_INFO("capabilities: downgraded " + std::to_string(subs)
                 + " block(s) for model=" + req.model);
    }
    return subs;
}

} // namespace agentcpp::api::caps
