#include <memory/FactExtractor.hpp>
#include <utils/StringUtils.hpp>

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace agentcpp::memory {

std::vector<std::string> splitSentences(const std::string& text) {
    // Split on sentence-ending punctuation (. ! ? ; \n) followed by whitespace
    // or end-of-string. Conservative: keeps long sentences whole.
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        auto t = utils::trim(cur);
        if (!t.empty()) out.push_back(std::move(t));
        cur.clear();
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        cur += c;
        bool sentence_end =
            (c == '.' || c == '!' || c == '?' || c == ';' || c == '\n');
        if (sentence_end) {
            // peek next: only split if followed by whitespace, newline, eof
            bool boundary = (i + 1 >= text.size()) ||
                            std::isspace(static_cast<unsigned char>(text[i + 1])) ||
                            text[i + 1] == '\n';
            if (boundary) flush();
        }
    }
    flush();
    return out;
}

std::vector<std::string> extractCapitalizedPhrases(const std::string& text) {
    // Pick up runs of TitleCase / ALLCAPS words, skipping common stop heads
    // like "The", "A", "An".
    static const std::set<std::string> stop_heads = {
        "The", "A", "An", "It", "He", "She", "They", "We", "I", "You",
        "This", "That", "These", "Those", "And", "Or", "But",
    };
    std::vector<std::string> out;
    std::string  cur;
    std::vector<std::string> cur_tokens;
    auto flush = [&] {
        if (cur_tokens.empty()) return;
        if (!(cur_tokens.size() == 1 && stop_heads.count(cur_tokens.front()))) {
            std::string joined;
            for (std::size_t i = 0; i < cur_tokens.size(); ++i) {
                if (i) joined += ' ';
                joined += cur_tokens[i];
            }
            if (joined.size() >= 2) out.push_back(joined);
        }
        cur_tokens.clear();
    };
    std::string tok;
    auto emit_token = [&] {
        if (tok.empty()) return;
        bool starts_upper = std::isupper(static_cast<unsigned char>(tok.front()));
        if (starts_upper) {
            cur_tokens.push_back(tok);
        } else {
            flush();
        }
        tok.clear();
    };
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '\'' || c == '-') {
            tok += c;
        } else {
            emit_token();
            if (c != ' ' && c != '\t') flush();  // any punctuation breaks the phrase
        }
    }
    emit_token();
    flush();
    // dedupe while preserving order
    std::set<std::string> seen;
    std::vector<std::string> uniq;
    for (auto& s : out) {
        if (seen.insert(s).second) uniq.push_back(s);
    }
    return uniq;
}

std::vector<ExtractedFact>
FactExtractor::extract(const RetainContent& c, int content_index) const {
    std::vector<ExtractedFact> out;
    auto sentences = splitSentences(c.content);
    if (sentences.empty()) {
        // Whole-content fallback so empty/single-line text still becomes a fact.
        auto t = utils::trim(c.content);
        if (!t.empty()) sentences.push_back(std::move(t));
    }

    // User-provided entity hints — included on every extracted fact.
    std::vector<std::string> user_entities;
    user_entities.reserve(c.entities.size());
    for (const auto& e : c.entities) {
        if (!e.text.empty()) user_entities.push_back(e.text);
    }

    int chunk_index = 0;
    for (auto& s : sentences) {
        if (s.size() < min_sentence_chars) continue;
        if (s.size() > max_sentence_chars) s.resize(max_sentence_chars);

        ExtractedFact f;
        f.text          = s;
        f.fact_type     = c.fact_type_override;
        f.context       = c.context;
        f.metadata      = c.metadata;
        f.tags          = c.tags;
        f.content_index = content_index;
        f.chunk_index   = chunk_index++;
        f.occurred_start = c.occurred_start;
        f.occurred_end   = c.occurred_end;
        f.mentioned_at   = c.event_date.has_value()
                              ? c.event_date
                              : std::optional<TimePoint>(nowUtc());

        // Merge user-provided entities with heuristic ones.
        auto heuristic = extractCapitalizedPhrases(s);
        std::set<std::string> seen;
        for (auto& name : user_entities) {
            if (seen.insert(name).second) f.entity_names.push_back(name);
        }
        for (auto& name : heuristic) {
            if (seen.insert(name).second) f.entity_names.push_back(name);
        }
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace agentcpp::memory
