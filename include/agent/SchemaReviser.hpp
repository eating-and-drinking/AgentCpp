#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SchemaReviser — Layer 4 (Self-Model Revision).
//
//  Accumulates a bounded buffer of recent failure events, periodically
//  clusters them (Jaccard token overlap on the failure descriptions), and
//  proposes either:
//
//    (a) AddDimension — a new θ dimension to extend the SelfBelief vector,
//        when a cluster of failures does not map cleanly to any existing
//        competence dimension; OR
//    (b) AddProposition — a new natural-language SelfProposition to inject
//        into the SelfModelStore, when the cluster does map to an existing
//        dimension but represents a specific recurring pattern worth
//        articulating.
//
//  Proposals are *not* applied automatically.  apply() enforces simple
//  stability checks (evidence count, novelty against existing schema) so the
//  self-model does not balloon uncontrollably.
//
//  This is the only layer that mutates the schema itself — Layers 1-3 only
//  update parameters and rows within a fixed schema.  Layer 4 is what makes
//  the system go from "parameter learning" to "structure learning", and it
//  remains training-free: clustering is heuristic, the LLM is never called.
// ─────────────────────────────────────────────────────────────────────────────
#include <agent/SelfBelief.hpp>
#include <agent/SelfModelStore.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace agentcpp::agent {

struct FailureEvent {
    std::int64_t ts          = 0;     // epoch ms
    std::string  tool;                // tool that failed; empty for reasoning failure
    std::string  description;         // brief: "FileEdit failed: file not found"
    std::string  task_type;            // optional coarse category from caller
};

struct SchemaProposal {
    enum class Kind { AddDimension, AddProposition };
    Kind        kind = Kind::AddProposition;
    std::string name_or_text;         // dimension name OR proposition text
    std::vector<std::string> tags;    // for AddProposition
    std::string rationale;            // human-readable why
    int         evidence_count = 0;   // size of cluster that triggered it
    double      novelty        = 0.0; // 0..1; higher = more distinct from existing
};

class SchemaReviser {
public:
    struct Config {
        std::size_t min_evidence_count       = 3;    // cluster must have ≥N events
        std::size_t review_every_n_episodes  = 5;
        double      cluster_jaccard_thresh   = 0.35;
        double      novelty_threshold        = 0.6;  // proposal must clear this
        std::size_t max_failures_buffer      = 200;
    };

    SchemaReviser();
    explicit SchemaReviser(Config c);

    // Hooks ──────────────────────────────────────────────────────────────────
    void recordFailure(FailureEvent ev);
    void recordSuccess(const std::string& dim_or_tag);  // light positive evidence
    void noteEpisodeComplete();

    // Trigger logic ──────────────────────────────────────────────────────────
    bool shouldReview() const;

    // Generate proposals from buffered failures.  Pure function of state +
    // arguments; does not mutate anything.
    std::vector<SchemaProposal>
    proposeRevisions(const SelfBelief& current_belief,
                     const SelfModelStore& current_store) const;

    // Try to apply a proposal.  Returns true iff stability checks passed
    // (novelty ≥ threshold and evidence ≥ minimum).
    bool apply(const SchemaProposal& prop,
               SelfBelief& belief,
               SelfModelStore& store);

    // Diagnostics
    std::size_t bufferedFailures() const { return failures_.size(); }
    int         episodesSinceReview() const {
        return episode_count_ - last_review_episode_;
    }
    const Config& config() const { return cfg_; }

private:
    Config                   cfg_;
    std::deque<FailureEvent> failures_;
    int                      episode_count_      = 0;
    int                      last_review_episode_ = 0;

    // Greedy cluster: returns vectors of FailureEvent grouped by token overlap.
    std::vector<std::vector<FailureEvent>> clusterFailures() const;

    // Pick representative keywords from a cluster's descriptions.
    static std::vector<std::string> topKeywords(
        const std::vector<FailureEvent>& cluster, std::size_t k);

    // Estimate novelty of a proposed string vs existing belief + store.
    static double estimateNovelty(const std::string& candidate,
                                  const SelfBelief& belief,
                                  const SelfModelStore& store);
};

} // namespace agentcpp::agent
