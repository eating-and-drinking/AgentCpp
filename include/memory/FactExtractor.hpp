#pragma once
//
// Heuristic fact extraction. The hindsight pipeline calls an LLM here
// (see hindsight_api/engine/retain/fact_extraction.py) which yields a
// structured list of facts plus per-fact entity mentions. AgentCpp has no
// LLM at this layer, so we approximate:
//
//   * Split the source content into sentence-shaped chunks.
//   * Each chunk becomes one extracted fact.
//   * Capitalized-word runs are picked up as entity name hints.
//
// The caller may also pass user-provided entity hints which are unioned with
// the extracted ones (matching hindsight's RetainContent.entities behaviour).
//
#include <memory/MemoryTypes.hpp>
#include <string>
#include <vector>

namespace agentcpp::memory {

// hindsight: ExtractedFact (retain/types.py)
struct ExtractedFact {
    std::string                          text;
    FactType                             fact_type = FactType::World;
    std::vector<std::string>             entity_names;
    std::optional<TimePoint>             occurred_start;
    std::optional<TimePoint>             occurred_end;
    std::optional<TimePoint>             mentioned_at;
    std::string                          context;
    std::map<std::string, std::string>   metadata;
    std::vector<std::string>             tags;
    int                                  content_index = 0;
    int                                  chunk_index   = 0;
};

class FactExtractor {
public:
    FactExtractor() = default;

    // Extract one or more facts from a single content item.
    // The fact_type is taken from `content.fact_type_override`.
    // User-provided entities in `content.entities` are merged into each fact.
    std::vector<ExtractedFact>
    extract(const RetainContent& content, int content_index) const;

    // Tunables
    std::size_t min_sentence_chars = 4;
    std::size_t max_sentence_chars = 600;
};

// Public helpers (exposed for tests / reuse).
std::vector<std::string> splitSentences(const std::string& text);
std::vector<std::string> extractCapitalizedPhrases(const std::string& text);

} // namespace agentcpp::memory
