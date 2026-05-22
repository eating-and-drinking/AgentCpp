#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SelfModelStore — Layer 3 (Structured Self-Model).
//
//  A small in-memory bag of SelfPropositions plus:
//    - confidence updates (reinforce / weaken on subsequent evidence)
//    - retrieval by token-overlap relevance to the current task description
//    - rendering of the top-k relevant propositions as a system-prompt section
//    - optional external persistence (intended target: MemoryEngine's
//      MentalModel table) via load/persist callbacks — keeps this header
//      free of any MemoryEngine dependency so callers can wire it up
//      without forcing a cyclic include.
//
//  No LLM is needed to operate this layer.  Propositions can be authored by
//  hand at first, then accumulated by Layer 4's schema reviser as failure
//  modes are detected.  This is the place "I am the kind of agent who…"
//  facts live in the MERIT architecture.
// ─────────────────────────────────────────────────────────────────────────────
#include <agent/SelfProposition.hpp>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace agentcpp::agent {

class SelfModelStore {
public:
    SelfModelStore();

    // External persistence hooks (optional).
    //   persist_fn is called with the current proposition list on saveToExternal().
    //   load_fn returns a fresh proposition list (e.g. from MemoryEngine).
    using PersistFn = std::function<void(const std::vector<SelfProposition>&)>;
    using LoadFn    = std::function<std::vector<SelfProposition>()>;
    void setPersistFn(PersistFn fn) { persist_ = std::move(fn); }
    void setLoadFn   (LoadFn    fn) { load_    = std::move(fn); }

    void loadFromExternal();   // overwrites in-memory list
    void saveToExternal() const;

    // Mutation ───────────────────────────────────────────────────────────────
    // If a proposition with the same id already exists, evidence_count is
    // incremented and confidence is moved toward 1 with a small step.
    void addProposition(SelfProposition p);
    void reinforce(const std::string& id, double delta = 0.05);
    void weaken   (const std::string& id, double delta = 0.05);
    bool remove   (const std::string& id);

    // Retrieval ──────────────────────────────────────────────────────────────
    // Jaccard token-overlap of task_desc against (text + tags), weighted by
    // confidence.  Cheap, no embeddings.  When MemoryEngine is wired in,
    // callers can swap this for an embedding-based retrieval upstream.
    std::vector<SelfProposition> retrieveRelevant(const std::string& task_desc,
                                                  std::size_t k = 3) const;

    // Format top-k relevant propositions as a system-prompt section.  Empty
    // string when store is empty or nothing scored above the floor.
    std::string renderForPrompt(const std::string& task_desc,
                                std::size_t k = 3) const;

    // Read-only views
    std::size_t                       size() const { return props_.size(); }
    const std::vector<SelfProposition>& all() const { return props_; }

private:
    std::vector<SelfProposition> props_;
    PersistFn                    persist_;
    LoadFn                       load_;

    static double scoreRelevance(const SelfProposition& p,
                                 const std::vector<std::string>& query_tokens);
};

} // namespace agentcpp::agent
