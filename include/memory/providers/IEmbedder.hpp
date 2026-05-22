#pragma once
//
// IEmbedder — pluggable text-embedding provider.
//
// hindsight uses pgvector + a configured embedding model. We mirror the same
// abstraction in C++:
//
//   * NullEmbedder  — returns no vectors. available() == false. MemoryEngine
//     skips the embedding-based semantic channel and falls back to a
//     BM25-with-alt-params placeholder.
//   * HttpEmbedder  — POSTs to an OpenAI-compatible /v1/embeddings endpoint
//     (works with OpenAI, OpenRouter, Ollama, LM Studio, etc.). Available
//     when both a base URL and an API key are configured.
//
// Implementations must return one vector of `dimension()` floats per input
// text, in the same order as the input.
//
#include <memory/MemoryTypes.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace agentcpp::memory {

class IEmbedder {
public:
    virtual ~IEmbedder() = default;

    virtual bool available() const = 0;
    virtual std::string name() const = 0;
    virtual std::size_t dimension() const = 0;

    // Embed a batch of texts. Returns one float vector per input text.
    // On any provider error, returns an empty outer vector.
    virtual std::vector<std::vector<float>>
        embed(const std::vector<std::string>& texts) = 0;
};

// Cosine similarity between two same-length vectors. Returns 0.0 if either
// is empty or if magnitudes are zero.
double cosineSimilarity(const std::vector<float>& a,
                        const std::vector<float>& b);

} // namespace agentcpp::memory
