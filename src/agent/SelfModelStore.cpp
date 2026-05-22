#include <agent/SelfModelStore.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
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

double jaccard(const std::set<std::string>& a, const std::set<std::string>& b) {
    if (a.empty() && b.empty()) return 0.0;
    std::size_t inter = 0;
    for (auto& x : a) if (b.count(x)) ++inter;
    std::size_t uni = a.size() + b.size() - inter;
    return uni == 0 ? 0.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

} // anonymous

std::string makePropositionId(const std::string& text,
                              const std::vector<std::string>& tags) {
    // Cheap stable hash (FNV-1a) over text + tags.
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    mix(text);
    for (auto& t : tags) { mix(std::string("|")); mix(t); }
    std::ostringstream ss;
    ss << "sp-" << std::hex << h;
    return ss.str();
}

// ── SelfModelStore ──────────────────────────────────────────────────────────

SelfModelStore::SelfModelStore() = default;

void SelfModelStore::loadFromExternal() {
    if (load_) props_ = load_();
}

void SelfModelStore::saveToExternal() const {
    if (persist_) persist_(props_);
}

void SelfModelStore::addProposition(SelfProposition p) {
    if (p.id.empty()) p.id = makePropositionId(p.text, p.tags);
    if (p.created_at == 0) p.created_at = nowMs();
    p.updated_at = nowMs();

    for (auto& existing : props_) {
        if (existing.id == p.id) {
            existing.evidence_count += 1;
            existing.confidence = std::min(1.0, existing.confidence + 0.05);
            existing.updated_at = nowMs();
            // Merge tags (set-like union).
            for (auto& t : p.tags) {
                if (std::find(existing.tags.begin(), existing.tags.end(), t)
                    == existing.tags.end()) {
                    existing.tags.push_back(t);
                }
            }
            return;
        }
    }
    p.evidence_count = std::max(1, p.evidence_count);
    props_.push_back(std::move(p));
}

void SelfModelStore::reinforce(const std::string& id, double delta) {
    for (auto& p : props_) {
        if (p.id == id) {
            p.confidence = std::min(1.0, p.confidence + delta);
            p.evidence_count += 1;
            p.updated_at = nowMs();
            return;
        }
    }
}

void SelfModelStore::weaken(const std::string& id, double delta) {
    for (auto& p : props_) {
        if (p.id == id) {
            p.confidence = std::max(0.0, p.confidence - delta);
            p.updated_at = nowMs();
            return;
        }
    }
}

bool SelfModelStore::remove(const std::string& id) {
    auto it = std::remove_if(props_.begin(), props_.end(),
        [&](const SelfProposition& p) { return p.id == id; });
    if (it == props_.end()) return false;
    props_.erase(it, props_.end());
    return true;
}

double SelfModelStore::scoreRelevance(const SelfProposition& p,
                                      const std::vector<std::string>& query_tokens) {
    std::set<std::string> qset(query_tokens.begin(), query_tokens.end());
    auto ptoks = tokenize(p.text);
    for (auto& t : p.tags) {
        for (auto& tok : tokenize(t)) ptoks.push_back(tok);
    }
    std::set<std::string> pset(ptoks.begin(), ptoks.end());
    double j = jaccard(qset, pset);
    // Confidence-weighted relevance.
    return j * (0.5 + 0.5 * p.confidence);
}

std::vector<SelfProposition>
SelfModelStore::retrieveRelevant(const std::string& task_desc, std::size_t k) const {
    if (props_.empty() || k == 0) return {};
    auto qtoks = tokenize(task_desc);

    std::vector<std::pair<double, std::size_t>> scored;
    scored.reserve(props_.size());
    for (std::size_t i = 0; i < props_.size(); ++i) {
        double s = scoreRelevance(props_[i], qtoks);
        if (s > 0.0) scored.emplace_back(s, i);
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });

    std::vector<SelfProposition> out;
    for (std::size_t i = 0; i < scored.size() && out.size() < k; ++i) {
        out.push_back(props_[scored[i].second]);
    }
    return out;
}

std::string SelfModelStore::renderForPrompt(const std::string& task_desc,
                                            std::size_t k) const {
    auto top = retrieveRelevant(task_desc, k);
    if (top.empty()) return {};

    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << "## Self-knowledge relevant to this task (Layer 3)\n\n"
       << "These are propositions you have accumulated about your own "
          "tendencies from prior episodes. Treat as priors, not facts.\n\n";
    int i = 1;
    for (auto& p : top) {
        ss << i++ << ". " << p.text
           << "  (confidence=" << p.confidence
           << ", seen=" << p.evidence_count << ")\n";
    }
    return ss.str();
}

} // namespace agentcpp::agent
