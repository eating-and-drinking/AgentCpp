#pragma once
//
// IFactExtractor — pluggable fact-extraction provider.
//
// Implementations:
//   * HeuristicFactExtractor — sentence-splitting + capitalized-phrase entity
//     guessing. Always available, no external dependencies.
//   * ClaudeFactExtractor    — calls the Anthropic Messages API via the
//     existing api::ClaudeClient to extract structured facts. Available
//     only when an API key is configured.
//
// Adding a new backend: subclass this interface, populate the
// ExtractedFact list. MemoryEngine treats all impls identically.
//
#include <memory/FactExtractor.hpp>  // ExtractedFact definition
#include <memory/MemoryTypes.hpp>

#include <vector>

namespace agentcpp::memory {

class IFactExtractor {
public:
    virtual ~IFactExtractor() = default;

    // True if this provider can actually run (e.g. API key present, network
    // reachable). Callers may use this to decide whether to fall back to a
    // heuristic provider. The heuristic provider should always return true.
    virtual bool available() const = 0;

    // Human-readable identifier ("heuristic", "claude-opus-4-5", ...).
    virtual std::string name() const = 0;

    // Extract facts from one RetainContent item.
    virtual std::vector<ExtractedFact>
        extract(const RetainContent& content, int content_index) = 0;
};

} // namespace agentcpp::memory
