#include <memory/providers/ClaudeFactExtractor.hpp>

#include <api/ClaudeClient.hpp>
#include <api/Types.hpp>
#include <utils/Logger.hpp>

#include <sstream>

namespace agentcpp::memory {

namespace {

// Prompt asking Claude to return a JSON array of {text, fact_type, entities[]}.
constexpr const char* kSystemPrompt = R"(You extract atomic facts from text.

Return ONLY a JSON object with a single field "facts" — an array. Each fact:
  {
    "text":      string,   // the atomic fact, self-contained
    "fact_type": "world" | "experience" | "observation",
    "entities":  [string], // people / orgs / places / concepts mentioned
    "where":     string | null
  }

Rules:
- One sentence-sized fact per array element.
- "world" = general knowledge. "experience" = first-person events. "observation" = synthesized notes about an entity.
- Do NOT add explanations. Output must be valid JSON parseable by json.loads.
)";

std::string buildUserMessage(const RetainContent& c) {
    std::ostringstream ss;
    if (!c.context.empty()) ss << "Context: " << c.context << "\n\n";
    ss << "Default fact_type if uncertain: " << factTypeToString(c.fact_type_override) << "\n\n";
    ss << "Content:\n" << c.content;
    return ss.str();
}

// Pull all TextBlocks out of an ApiResponse and concatenate them.
std::string textFrom(const agentcpp::api::ApiResponse& resp) {
    std::string out;
    for (const auto& blk : resp.content) {
        if (auto* t = std::get_if<agentcpp::api::TextBlock>(&blk)) {
            out += t->text;
        }
    }
    return out;
}

// Find the first '{' ... matching '}' block. Tolerates Claude wrapping the
// JSON in ```json fences or prose preamble.
std::string extractJsonObject(const std::string& s) {
    auto start = s.find('{');
    if (start == std::string::npos) return {};
    int depth = 0;
    bool in_str = false, esc = false;
    for (std::size_t i = start; i < s.size(); ++i) {
        char c = s[i];
        if (esc) { esc = false; continue; }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (c == '{') ++depth;
        else if (c == '}') {
            if (--depth == 0) return s.substr(start, i - start + 1);
        }
    }
    return {};
}

} // namespace

ClaudeFactExtractor::ClaudeFactExtractor(std::shared_ptr<agentcpp::api::ClaudeClient> client,
                                         std::string model)
    : client_(std::move(client))
    , model_(std::move(model)) {
    name_ = "claude";
    if (!model_.empty()) {
        name_ += '/';
        name_ += model_;
    }
}

bool ClaudeFactExtractor::available() const {
    return client_ && !client_->config().api_key.empty();
}

std::vector<ExtractedFact>
ClaudeFactExtractor::extract(const RetainContent& content, int content_index) {
    if (!available()) {
        return fallback_.extract(content, content_index);
    }

    agentcpp::api::ApiRequest req;
    req.model      = model_.empty() ? client_->config().default_model : model_;
    req.max_tokens = 2048;
    req.stream     = false;
    req.system     = kSystemPrompt;
    req.messages.push_back(agentcpp::api::Message::userText(buildUserMessage(content)));

    std::string raw_text;
    try {
        auto resp = client_->request(req);
        raw_text = textFrom(resp);
    } catch (const std::exception& e) {
        LOG_WARN(std::string("ClaudeFactExtractor: API call failed: ") + e.what());
        return fallback_.extract(content, content_index);
    }

    auto json_str = extractJsonObject(raw_text);
    if (json_str.empty()) {
        LOG_WARN("ClaudeFactExtractor: no JSON object in response, falling back");
        return fallback_.extract(content, content_index);
    }

    json parsed;
    try {
        parsed = json::parse(json_str);
    } catch (const std::exception& e) {
        LOG_WARN(std::string("ClaudeFactExtractor: JSON parse error: ") + e.what());
        return fallback_.extract(content, content_index);
    }

    if (!parsed.contains("facts") || !parsed["facts"].is_array()) {
        return fallback_.extract(content, content_index);
    }

    std::vector<ExtractedFact> out;
    int chunk_index = 0;
    auto now = nowUtc();
    for (const auto& item : parsed["facts"]) {
        if (!item.is_object() || !item.contains("text")) continue;
        ExtractedFact f;
        f.text = item.value("text", std::string{});
        if (f.text.empty()) continue;
        f.fact_type     = item.contains("fact_type")
                          ? factTypeFromString(item.value("fact_type", std::string("world")))
                          : content.fact_type_override;
        f.context       = content.context;
        f.metadata      = content.metadata;
        f.tags          = content.tags;
        f.content_index = content_index;
        f.chunk_index   = chunk_index++;
        f.occurred_start = content.occurred_start;
        f.occurred_end   = content.occurred_end;
        f.mentioned_at   = content.event_date.value_or(now);
        if (item.contains("entities") && item["entities"].is_array()) {
            for (const auto& e : item["entities"]) {
                if (e.is_string()) f.entity_names.push_back(e.get<std::string>());
            }
        }
        // User-provided hints are merged on top.
        for (const auto& uh : content.entities) {
            if (!uh.text.empty()) f.entity_names.push_back(uh.text);
        }
        out.push_back(std::move(f));
    }
    if (out.empty()) return fallback_.extract(content, content_index);
    return out;
}

} // namespace agentcpp::memory
