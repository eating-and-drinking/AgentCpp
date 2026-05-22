#include <agent/SchemaReviser.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <set>
#include <sstream>

namespace agentcpp::agent {

namespace {

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur.push_back(static_cast<char>(
                std::tolower(static_cast<unsigned char>(c))));
        } else if (!cur.empty()) {
            if (cur.size() >= 3) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= 3) out.push_back(cur);
    return out;
}

std::set<std::string> tokenSet(const std::string& s) {
    auto v = tokenize(s);
    return std::set<std::string>(v.begin(), v.end());
}

double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0;
    std::size_t inter = 0;
    for (auto& x : a) if (b.count(x)) ++inter;
    std::size_t uni = a.size() + b.size() - inter;
    return uni == 0 ? 0.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

} // anonymous

SchemaReviser::SchemaReviser()           : cfg_(Config{}) {}
SchemaReviser::SchemaReviser(Config c)   : cfg_(c)        {}

void SchemaReviser::recordFailure(FailureEvent ev) {
    if (ev.ts == 0) ev.ts = nowMs();
    failures_.push_back(std::move(ev));
    while (failures_.size() > cfg_.max_failures_buffer) {
        failures_.pop_front();
    }
}

void SchemaReviser::recordSuccess(const std::string& /*dim_or_tag*/) {
    // Positive evidence is currently used only as a passive counter; future
    // work would weaken stale propositions on sustained success.  Kept as a
    // hook so callers don't need to gate the call site.
}

void SchemaReviser::noteEpisodeComplete() {
    ++episode_count_;
}

bool SchemaReviser::shouldReview() const {
    if (failures_.size() < cfg_.min_evidence_count) return false;
    return (episode_count_ - last_review_episode_)
           >= static_cast<int>(cfg_.review_every_n_episodes);
}

std::vector<std::vector<FailureEvent>>
SchemaReviser::clusterFailures() const {
    // Greedy single-pass clustering with Jaccard token-set overlap.
    std::vector<std::vector<FailureEvent>>   clusters;
    std::vector<std::set<std::string>>       centroids;  // union of tokens
    for (const auto& f : failures_) {
        auto toks = tokenSet(f.description + " " + f.tool);
        int best_i = -1;
        double best_j = 0.0;
        for (std::size_t i = 0; i < centroids.size(); ++i) {
            double j = jaccard(toks, centroids[i]);
            if (j > best_j) { best_j = j; best_i = static_cast<int>(i); }
        }
        if (best_i >= 0 && best_j >= cfg_.cluster_jaccard_thresh) {
            clusters[best_i].push_back(f);
            for (auto& t : toks) centroids[best_i].insert(t);
        } else {
            clusters.push_back({f});
            centroids.push_back(toks);
        }
    }
    return clusters;
}

std::vector<std::string>
SchemaReviser::topKeywords(const std::vector<FailureEvent>& cluster,
                           std::size_t k) {
    std::map<std::string, int> freq;
    for (const auto& f : cluster) {
        for (auto& t : tokenize(f.description)) ++freq[t];
        if (!f.tool.empty()) ++freq[f.tool];
    }
    // Strip very common English stopwords-ish; we keep it lean.
    static const std::set<std::string> stop = {
        "the", "and", "for", "with", "that", "this", "not", "but", "from",
        "have", "has", "was", "were", "into", "out", "off", "you", "your",
    };
    std::vector<std::pair<int, std::string>> arr;
    for (auto& kv : freq) {
        if (stop.count(kv.first)) continue;
        arr.emplace_back(kv.second, kv.first);
    }
    std::sort(arr.begin(), arr.end(),
              [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < arr.size() && out.size() < k; ++i) {
        out.push_back(arr[i].second);
    }
    return out;
}

double SchemaReviser::estimateNovelty(const std::string& candidate,
                                      const SelfBelief& belief,
                                      const SelfModelStore& store) {
    auto cand = tokenSet(candidate);

    // Compare against existing dimensions (as token sets of their names).
    double max_overlap = 0.0;
    for (auto& d : belief.dimensions()) {
        max_overlap = std::max(max_overlap, jaccard(cand, tokenSet(d)));
    }
    // Compare against existing propositions.
    for (auto& p : store.all()) {
        auto pset = tokenSet(p.text);
        for (auto& t : p.tags) for (auto& tok : tokenize(t)) pset.insert(tok);
        max_overlap = std::max(max_overlap, jaccard(cand, pset));
    }
    return 1.0 - max_overlap;
}

std::vector<SchemaProposal>
SchemaReviser::proposeRevisions(const SelfBelief& belief,
                                const SelfModelStore& store) const {
    std::vector<SchemaProposal> out;
    auto clusters = clusterFailures();
    for (const auto& c : clusters) {
        if (c.size() < cfg_.min_evidence_count) continue;

        auto kws = topKeywords(c, 4);
        if (kws.empty()) continue;

        // Build a representative description from keywords.
        std::ostringstream tx;
        tx << "I tend to fail when the task involves ";
        for (std::size_t i = 0; i < kws.size(); ++i) {
            if (i) tx << (i + 1 == kws.size() ? " and " : ", ");
            tx << kws[i];
        }
        tx << ".";
        std::string text = tx.str();

        double nov = estimateNovelty(text, belief, store);

        SchemaProposal p;
        p.evidence_count = static_cast<int>(c.size());
        p.novelty        = nov;
        // Decide kind: if the cluster's keywords map to no existing dim,
        // propose a new dim; else propose a new proposition.
        bool maps_to_existing_dim = false;
        for (auto& d : belief.dimensions()) {
            auto dset = tokenSet(d);
            for (auto& kw : kws) if (dset.count(kw)) { maps_to_existing_dim = true; break; }
            if (maps_to_existing_dim) break;
        }
        if (!maps_to_existing_dim) {
            p.kind         = SchemaProposal::Kind::AddDimension;
            // Dim names are lowercase_underscore.
            std::ostringstream nm;
            for (std::size_t i = 0; i < kws.size() && i < 2; ++i) {
                if (i) nm << "_";
                nm << kws[i];
            }
            p.name_or_text = nm.str();
            p.rationale    = "cluster of " + std::to_string(c.size())
                           + " failures with keywords [" + kws.front()
                           + "...] not covered by existing competence dims";
        } else {
            p.kind         = SchemaProposal::Kind::AddProposition;
            p.name_or_text = text;
            p.tags         = kws;
            p.rationale    = "recurring failure pattern (n="
                           + std::to_string(c.size())
                           + ") within an existing competence dim";
        }
        out.push_back(std::move(p));
    }
    return out;
}

bool SchemaReviser::apply(const SchemaProposal& prop,
                          SelfBelief& belief,
                          SelfModelStore& store) {
    if (prop.evidence_count < static_cast<int>(cfg_.min_evidence_count)) return false;
    if (prop.novelty < cfg_.novelty_threshold) return false;

    if (prop.kind == SchemaProposal::Kind::AddDimension) {
        if (prop.name_or_text.empty()) return false;
        if (belief.hasDimension(prop.name_or_text)) return false;
        // Weak prior on the new dim: slight pessimism since it was triggered
        // by recurring failure.
        belief.addDimension(prop.name_or_text, /*alpha=*/1.0, /*beta=*/2.0);
    } else {
        SelfProposition sp;
        sp.text       = prop.name_or_text;
        sp.tags       = prop.tags;
        sp.confidence = std::min(0.8, 0.4 + 0.1 * prop.evidence_count);
        sp.evidence_count = prop.evidence_count;
        store.addProposition(std::move(sp));
    }
    return true;
}

} // namespace agentcpp::agent
