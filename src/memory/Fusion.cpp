#include <memory/Fusion.hpp>

#include <algorithm>
#include <unordered_map>

namespace agentcpp::memory {

std::vector<MergedCandidate>
reciprocalRankFusion(const std::vector<std::vector<RetrievalResult>>& result_lists,
                     int k) {
    // hindsight: fusion.reciprocal_rank_fusion
    //   source order is ("semantic", "bm25", "graph", "temporal")
    static const char* source_names[] = {"semantic", "bm25", "graph", "temporal"};

    std::unordered_map<std::string, double>                   rrf_scores;
    std::unordered_map<std::string, std::map<std::string,int>> source_ranks;
    std::unordered_map<std::string, RetrievalResult>          first_retrieval;

    for (std::size_t source_idx = 0; source_idx < result_lists.size(); ++source_idx) {
        const auto& results = result_lists[source_idx];
        std::string src = (source_idx < 4)
            ? source_names[source_idx]
            : ("source_" + std::to_string(source_idx));

        for (std::size_t i = 0; i < results.size(); ++i) {
            int rank = static_cast<int>(i + 1);
            const auto& r = results[i];
            const std::string& doc_id = r.id;
            if (!first_retrieval.count(doc_id)) first_retrieval[doc_id] = r;
            rrf_scores[doc_id] += 1.0 / static_cast<double>(k + rank);
            source_ranks[doc_id][src + "_rank"] = rank;
        }
    }

    // Sort by rrf desc
    std::vector<std::pair<std::string, double>> ranked(rrf_scores.begin(), rrf_scores.end());
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    std::vector<MergedCandidate> out;
    out.reserve(ranked.size());
    int rrf_rank = 1;
    for (auto& [doc_id, score] : ranked) {
        MergedCandidate mc;
        mc.retrieval    = first_retrieval[doc_id];
        mc.rrf_score    = score;
        mc.rrf_rank     = rrf_rank++;
        mc.source_ranks = source_ranks[doc_id];
        out.push_back(std::move(mc));
    }
    return out;
}

void normalizeOnDelta(std::vector<double>& values) {
    if (values.empty()) return;
    double mn = *std::min_element(values.begin(), values.end());
    double mx = *std::max_element(values.begin(), values.end());
    double delta = mx - mn;
    if (delta > 0.0) {
        for (auto& v : values) v = (v - mn) / delta;
    } else {
        for (auto& v : values) v = 0.5;
    }
}

} // namespace agentcpp::memory
