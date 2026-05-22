#pragma once
//
// BM25 lexical retrieval. Substitutes for hindsight's pgvector + tsvector
// hybrid (semantic.search.semantic_retrieval + bm25_retrieval), since the
// AgentCpp build has no embedding model nor PG full-text. The classical
// Okapi BM25 ranking function is enough to drive recall+RRF in a single-
// process file-backed store.
//
// Index is built on demand from a list of MemoryUnits and cached for the
// life of the index instance. Build is O(N * avg_tokens). Lookup is
// O(query_tokens * postings_len).
//
#include <memory/MemoryTypes.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentcpp::memory {

class BM25Index {
public:
    BM25Index() = default;

    // (Re)build the index from a set of memory units.
    void build(const std::vector<MemoryUnit>& units);

    // Rank documents against `query`, return top-k RetrievalResults
    // (bm25_score populated).
    std::vector<RetrievalResult> search(const std::string& query,
                                        std::size_t        k) const;

    bool empty() const { return docs_.empty(); }

    // BM25 hyperparams
    double k1 = 1.5;
    double b  = 0.75;

private:
    struct Doc {
        const MemoryUnit* unit = nullptr;
        std::unordered_map<std::string, int> term_freq;
        int length = 0;
    };

    std::vector<Doc>                                     docs_;
    std::unordered_map<std::string, std::vector<int>>    inverted_; // term -> doc indices
    std::unordered_map<std::string, int>                 doc_freq_; // term -> #docs containing it
    double                                               avg_len_ = 0.0;

    static std::vector<std::string> tokenize(const std::string& s);
    RetrievalResult                  rowFor(const Doc& d, double score) const;
};

} // namespace agentcpp::memory
