#include <memory/GraphRetrieval.hpp>

#include <algorithm>
#include <queue>
#include <unordered_set>

namespace agentcpp::memory {

GraphRetrieval::Adjacency GraphRetrieval::buildAdjacency() const {
    Adjacency adj;
    for (const auto& l : links_) {
        adj[l.from_unit_id].push_back(&l);
        // undirected expansion: include incoming as well, which matches
        // hindsight's link_expansion_retrieval default.
        adj[l.to_unit_id].push_back(&l);
    }
    return adj;
}

std::vector<RetrievalResult>
GraphRetrieval::expand(const std::vector<std::string>& seed_unit_ids,
                       std::size_t k,
                       int max_hops) const {
    std::vector<RetrievalResult> out;
    if (seed_unit_ids.empty() || links_.empty()) return out;

    auto adj = buildAdjacency();

    // Activation per unit (best score seen so far).
    std::unordered_map<std::string, double> activation;
    // BFS frontier: (unit_id, hop, score)
    struct Node { std::string id; int hop; double score; };
    std::queue<Node> q;
    for (const auto& s : seed_unit_ids) {
        activation[s] = 1.0;
        q.push({s, 0, 1.0});
    }
    while (!q.empty()) {
        auto [id, hop, score] = q.front();
        q.pop();
        if (hop >= max_hops) continue;
        auto it = adj.find(id);
        if (it == adj.end()) continue;
        for (const auto* edge : it->second) {
            const std::string& nxt = (edge->from_unit_id == id) ? edge->to_unit_id
                                                                : edge->from_unit_id;
            double new_score = score * hop_decay * edge->weight;
            auto& cur = activation[nxt];
            if (new_score > cur) {
                cur = new_score;
                q.push({nxt, hop + 1, new_score});
            }
        }
    }

    // Drop seeds themselves — they're already coming in via BM25/semantic.
    std::unordered_set<std::string> seeds(seed_unit_ids.begin(), seed_unit_ids.end());

    std::vector<std::pair<std::string, double>> ranked;
    ranked.reserve(activation.size());
    for (const auto& [id, a] : activation) {
        if (seeds.count(id)) continue;
        if (a <= 0.0) continue;
        ranked.emplace_back(id, a);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (k > 0 && ranked.size() > k) ranked.resize(k);

    out.reserve(ranked.size());
    for (auto& [id, a] : ranked) {
        auto it = unit_index_.find(id);
        if (it == unit_index_.end()) continue;
        const MemoryUnit& u = it->second;
        RetrievalResult r;
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
        r.activation     = a;
        out.push_back(std::move(r));
    }
    return out;
}

} // namespace agentcpp::memory
