#include <memory/providers/IEmbedder.hpp>

#include <cmath>

namespace agentcpp::memory {

double cosineSimilarity(const std::vector<float>& a,
                        const std::vector<float>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 0.0;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        double x = a[i], y = b[i];
        dot += x * y;
        na  += x * x;
        nb  += y * y;
    }
    if (na <= 0.0 || nb <= 0.0) return 0.0;
    return dot / (std::sqrt(na) * std::sqrt(nb));
}

} // namespace agentcpp::memory
