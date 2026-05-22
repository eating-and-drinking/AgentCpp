#include <memory/Reranker.hpp>
#include <memory/Fusion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace agentcpp::memory {

namespace {

double recencyScore(const RetrievalResult& r, TimePoint now, double halflife_sec) {
    auto pick = [&]() -> TimePoint {
        if (r.mentioned_at)   return *r.mentioned_at;
        if (r.occurred_start) return *r.occurred_start;
        if (r.event_date)     return *r.event_date;
        return TimePoint{};
    };
    auto t = pick();
    if (t.time_since_epoch().count() == 0) return 0.5;
    auto delta = std::chrono::duration_cast<std::chrono::seconds>(now - t).count();
    if (delta < 0) delta = 0;
    return std::exp(-static_cast<double>(delta) /
                    std::max(halflife_sec, 1.0)); // ∈ (0, 1]
}

double temporalProximity(const RetrievalResult& r,
                         const std::optional<TimePoint>& question_date) {
    if (!question_date) return 0.5;
    TimePoint t{};
    if (r.occurred_start)      t = *r.occurred_start;
    else if (r.mentioned_at)   t = *r.mentioned_at;
    else if (r.event_date)     t = *r.event_date;
    else                       return 0.5;
    if (t.time_since_epoch().count() == 0) return 0.5;
    auto delta = std::abs(std::chrono::duration_cast<std::chrono::seconds>(
                              *question_date - t).count());
    // half-life of 7 days for temporal proximity
    constexpr double kHalfWeek = 7.0 * 24.0 * 3600.0;
    return std::exp(-static_cast<double>(delta) / kHalfWeek);
}

} // namespace

void Reranker::rerank(std::vector<ScoredResult>& results,
                      const RecallQuery&         query,
                      TimePoint                  now) const {
    if (results.empty()) return;

    // 1) collect raw scores
    std::vector<double> rrf;
    rrf.reserve(results.size());
    for (auto& r : results) rrf.push_back(r.candidate.rrf_score);

    auto rrf_norm = rrf;
    normalizeOnDelta(rrf_norm);

    for (std::size_t i = 0; i < results.size(); ++i) {
        results[i].rrf_normalized = rrf_norm[i];
        results[i].recency        = recencyScore(results[i].retrieval(),
                                                 now,
                                                 recency_halflife_sec);
        results[i].temporal       = temporalProximity(results[i].retrieval(),
                                                      query.question_date);
        results[i].combined_score = w_rrf * results[i].rrf_normalized
                                  + w_rec * results[i].recency
                                  + w_tmp * results[i].temporal;
        results[i].weight         = results[i].combined_score;
    }

    std::sort(results.begin(), results.end(),
              [](const ScoredResult& a, const ScoredResult& b) {
                  return a.combined_score > b.combined_score;
              });
}

} // namespace agentcpp::memory
