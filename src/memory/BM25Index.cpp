#include <memory/BM25Index.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace agentcpp::memory {

std::vector<std::string> BM25Index::tokenize(const std::string& s) {
    // Lowercase, split on non-alnum. Drops 1-character tokens.
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() >= 2) out.push_back(std::move(cur));
        cur.clear();
    };
    for (char c : s) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            cur += static_cast<char>(std::tolower(uc));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

void BM25Index::build(const std::vector<MemoryUnit>& units) {
    docs_.clear();
    inverted_.clear();
    doc_freq_.clear();
    avg_len_ = 0.0;

    docs_.reserve(units.size());
    double total_len = 0.0;
    for (const auto& u : units) {
        Doc d;
        d.unit = &u;
        // index over text + context — hindsight does the same.
        std::string blob = u.text;
        if (!u.context.empty()) { blob += ' '; blob += u.context; }
        auto tokens = tokenize(blob);
        d.length = static_cast<int>(tokens.size());
        for (auto& t : tokens) d.term_freq[t]++;
        docs_.push_back(std::move(d));
        total_len += docs_.back().length;
    }
    if (!docs_.empty()) avg_len_ = total_len / static_cast<double>(docs_.size());

    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        for (const auto& [term, _tf] : docs_[i].term_freq) {
            inverted_[term].push_back(i);
            doc_freq_[term]++;
        }
    }
}

RetrievalResult BM25Index::rowFor(const Doc& d, double score) const {
    RetrievalResult r;
    const MemoryUnit& u = *d.unit;
    r.id            = u.id;
    r.text          = u.text;
    r.fact_type     = u.fact_type;
    r.context       = u.context.empty() ? std::optional<std::string>{}
                                        : std::optional<std::string>{u.context};
    r.event_date    = u.event_date.time_since_epoch().count() ? u.event_date
                                                              : std::optional<TimePoint>{};
    r.occurred_start = u.occurred_start;
    r.occurred_end   = u.occurred_end;
    r.mentioned_at   = u.mentioned_at;
    r.document_id    = u.document_id.empty() ? std::optional<std::string>{}
                                             : std::optional<std::string>{u.document_id};
    r.chunk_id       = u.chunk_id;
    r.tags           = u.tags;
    r.metadata       = u.metadata;
    r.proof_count    = u.proof_count;
    r.bm25_score     = score;
    return r;
}

std::vector<RetrievalResult> BM25Index::search(const std::string& query,
                                               std::size_t        k) const {
    if (docs_.empty()) return {};
    auto q_tokens = tokenize(query);
    if (q_tokens.empty()) return {};

    const double N = static_cast<double>(docs_.size());
    std::vector<double> scores(docs_.size(), 0.0);
    std::unordered_set<int> touched;

    // Deduplicate identical query tokens but weight by their frequency.
    std::unordered_map<std::string, int> q_tf;
    for (auto& t : q_tokens) q_tf[t]++;

    for (const auto& [term, qtf] : q_tf) {
        auto it = inverted_.find(term);
        if (it == inverted_.end()) continue;
        double df  = static_cast<double>(doc_freq_.at(term));
        double idf = std::log(1.0 + (N - df + 0.5) / (df + 0.5));
        for (int doc_idx : it->second) {
            const Doc& d = docs_[doc_idx];
            auto tf_it = d.term_freq.find(term);
            if (tf_it == d.term_freq.end()) continue;
            double tf = static_cast<double>(tf_it->second);
            double denom = tf + k1 * (1.0 - b + b * (d.length / std::max(avg_len_, 1.0)));
            double term_score = idf * ((tf * (k1 + 1.0)) / denom) * qtf;
            scores[doc_idx] += term_score;
            touched.insert(doc_idx);
        }
    }

    std::vector<std::pair<int, double>> ranked;
    ranked.reserve(touched.size());
    for (int i : touched) {
        if (scores[i] > 0.0) ranked.emplace_back(i, scores[i]);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (k > 0 && ranked.size() > k) ranked.resize(k);

    std::vector<RetrievalResult> out;
    out.reserve(ranked.size());
    for (auto& [idx, sc] : ranked) {
        out.push_back(rowFor(docs_[idx], sc));
    }
    return out;
}

} // namespace agentcpp::memory
