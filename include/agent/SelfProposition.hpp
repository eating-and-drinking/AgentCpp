#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SelfProposition — Layer 3 (Structured Self-Model) primitive type.
//
//  A single piece of self-knowledge in natural language, e.g.
//
//      "On C++ code involving move semantics I tend to forget noexcept on
//       the move constructor, which fails the standard library trait checks."
//
//  Together with a small set of tags (domain, tool, failure-mode keywords)
//  and a confidence score updated by Layer 4 / experience.  Stored in
//  SelfModelStore; optionally persisted to MemoryEngine's MentalModel.
// ─────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <string>
#include <vector>

namespace agentcpp::agent {

struct SelfProposition {
    std::string              id;              // unique, e.g. "sp-<hash>"
    std::string              text;            // the natural-language claim
    std::vector<std::string> tags;            // ["c++", "lifetime", "move"]
    double                   confidence    = 0.5;  // 0..1
    int                      evidence_count = 0;   // how many times seen
    std::int64_t             created_at    = 0;    // epoch ms
    std::int64_t             updated_at    = 0;
};

// Stable hash over text+tags so the same proposition gets the same id even
// when re-derived in a future turn.  Implementation in SelfModelStore.cpp.
std::string makePropositionId(const std::string& text,
                              const std::vector<std::string>& tags);

} // namespace agentcpp::agent
