#pragma once
// ─────────────────────────────────────────────────────────────────────────────
//  SelfBelief — Layer 1 (Outcome Monitoring) state.
//
//  Maintains a Bayesian belief over a low-dimensional self-competence vector θ.
//  Each dimension is a Beta(α, β) distribution, conjugate to Bernoulli
//  observations of "this kind of action succeeded / failed".  All updates and
//  information-theoretic quantities are closed-form — no training, no fitting.
//
//  This is the MERIT framework's quantitative self-model.  Higher layers
//  (process monitoring, structured self-knowledge, schema revision) read from
//  it; the meta-policy uses its mean/variance to compute Expected Free Energy.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <unordered_map>
#include <vector>

namespace agentcpp::agent {

// A Beta(α, β) distribution over a competence value in [0, 1].
//   mean = α / (α + β)
//   var  = αβ / ((α+β)² (α+β+1))
//   H(p) = ln B(α,β) − (α−1)ψ(α) − (β−1)ψ(β) + (α+β−2)ψ(α+β)
// Default prior is Jeffreys / uniform (α = β = 1) — a deliberately weak prior
// so episode-1 observations dominate quickly.
struct BetaParam {
    double alpha = 1.0;
    double beta  = 1.0;

    double mean()    const;
    double variance() const;
    double stddev()  const;
    double entropy() const;

    // Expected reduction in entropy from a single Bernoulli observation under
    // the current belief:  I = H[prior] − E_v[H[posterior]], v ~ Bern(mean).
    // Always non-negative for Beta.
    double expectedInfoGainBernoulli() const;
};

// A small named set of competence dimensions.  Default dimensions cover the
// minimum that the MERIT Layer 1 policy needs to make non-trivial decisions;
// callers may add task-specific dimensions at any time (Layer 4 work).
class SelfBelief {
public:
    SelfBelief();

    void addDimension(const std::string& name, double alpha = 1.0, double beta = 1.0);
    bool hasDimension(const std::string& name) const;
    std::vector<std::string> dimensions() const;

    // Closed-form Bayesian updates.  No effect if dimension is unknown.
    void observeSuccess(const std::string& dim, double weight = 1.0);
    void observeFailure(const std::string& dim, double weight = 1.0);
    // Soft observation with success-probability p in [0,1] (e.g. CoT quality).
    void observeMixed  (const std::string& dim, double p,  double weight = 1.0);

    BetaParam get   (const std::string& dim) const;
    double    mean  (const std::string& dim) const;  // 0.5 if dim absent
    double    stddev(const std::string& dim) const;  // 0   if dim absent

    // Aggregates over all dimensions (equal-weighted).
    double overallCompetence()   const;
    double overallUncertainty() const;

    // Single-line dump for logs.
    std::string toString() const;

private:
    std::unordered_map<std::string, BetaParam> params_;
    std::vector<std::string>                   order_;  // stable iteration
};

} // namespace agentcpp::agent
