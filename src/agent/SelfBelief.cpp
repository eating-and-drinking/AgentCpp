#include <agent/SelfBelief.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace agentcpp::agent {

namespace {

// Numerically stable digamma ψ(x) for x > 0.  Uses the recurrence
//   ψ(x) = ψ(x+1) − 1/x
// to push x ≥ 6, then the asymptotic series.
double digamma(double x) {
    double result = 0.0;
    while (x < 6.0) {
        result -= 1.0 / x;
        x      += 1.0;
    }
    const double inv  = 1.0 / x;
    const double inv2 = inv * inv;
    result += std::log(x) - 0.5 * inv;
    result -= inv2 * (1.0 / 12.0
                      - inv2 * (1.0 / 120.0
                                - inv2 / 252.0));
    return result;
}

// log B(α, β) = lnΓ(α) + lnΓ(β) − lnΓ(α+β).
double lbeta(double a, double b) {
    return std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
}

} // anonymous namespace

// ── BetaParam ───────────────────────────────────────────────────────────────

double BetaParam::mean() const {
    const double s = alpha + beta;
    return s > 0.0 ? alpha / s : 0.5;
}

double BetaParam::variance() const {
    const double s = alpha + beta;
    if (s <= 0.0) return 0.0;
    return (alpha * beta) / (s * s * (s + 1.0));
}

double BetaParam::stddev() const {
    return std::sqrt(variance());
}

double BetaParam::entropy() const {
    const double a = alpha;
    const double b = beta;
    return lbeta(a, b)
         - (a - 1.0) * digamma(a)
         - (b - 1.0) * digamma(b)
         + (a + b - 2.0) * digamma(a + b);
}

double BetaParam::expectedInfoGainBernoulli() const {
    const double m       = mean();
    const double H_prior = entropy();
    const BetaParam s_post{alpha + 1.0, beta};
    const BetaParam f_post{alpha,       beta + 1.0};
    const double H_post  = m * s_post.entropy() + (1.0 - m) * f_post.entropy();
    return std::max(0.0, H_prior - H_post);
}

// ── SelfBelief ──────────────────────────────────────────────────────────────

SelfBelief::SelfBelief() {
    // Default competence dimensions.  See SelfBelief.hpp for rationale.
    static const char* kDefaults[] = {
        "tool_use",       // generic: any tool succeeded without error
        "code_edit",      // file-mutating tools (Edit, Write)
        "search",         // information-finding tools (Grep, Glob)
        "reasoning",      // chain-of-thought step quality (from CoTMonitor)
        "task_progress",  // making progress toward the user's stated goal
    };
    for (auto* n : kDefaults) addDimension(n);
}

void SelfBelief::addDimension(const std::string& name, double a, double b) {
    if (params_.count(name)) return;
    params_[name] = BetaParam{a, b};
    order_.push_back(name);
}

bool SelfBelief::hasDimension(const std::string& name) const {
    return params_.count(name) > 0;
}

std::vector<std::string> SelfBelief::dimensions() const {
    return order_;
}

void SelfBelief::observeSuccess(const std::string& dim, double w) {
    auto it = params_.find(dim);
    if (it == params_.end()) return;
    it->second.alpha += w;
}

void SelfBelief::observeFailure(const std::string& dim, double w) {
    auto it = params_.find(dim);
    if (it == params_.end()) return;
    it->second.beta += w;
}

void SelfBelief::observeMixed(const std::string& dim, double p, double w) {
    auto it = params_.find(dim);
    if (it == params_.end()) return;
    p = std::clamp(p, 0.0, 1.0);
    it->second.alpha += w * p;
    it->second.beta  += w * (1.0 - p);
}

BetaParam SelfBelief::get(const std::string& dim) const {
    auto it = params_.find(dim);
    return it == params_.end() ? BetaParam{} : it->second;
}

double SelfBelief::mean(const std::string& dim) const {
    auto it = params_.find(dim);
    return it == params_.end() ? 0.5 : it->second.mean();
}

double SelfBelief::stddev(const std::string& dim) const {
    auto it = params_.find(dim);
    return it == params_.end() ? 0.0 : it->second.stddev();
}

double SelfBelief::overallCompetence() const {
    if (order_.empty()) return 0.5;
    double s = 0.0;
    for (auto& d : order_) s += params_.at(d).mean();
    return s / static_cast<double>(order_.size());
}

double SelfBelief::overallUncertainty() const {
    if (order_.empty()) return 0.0;
    double s = 0.0;
    for (auto& d : order_) s += params_.at(d).stddev();
    return s / static_cast<double>(order_.size());
}

std::string SelfBelief::toString() const {
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(2);
    ss << "SelfBelief{";
    bool first = true;
    for (auto& d : order_) {
        if (!first) ss << ", ";
        first = false;
        auto p = params_.at(d);
        ss << d << "=" << p.mean() << "±" << p.stddev();
    }
    ss << "}";
    return ss.str();
}

} // namespace agentcpp::agent
