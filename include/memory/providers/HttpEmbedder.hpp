#pragma once
//
// HttpEmbedder — IEmbedder targeting an OpenAI-compatible /v1/embeddings
// endpoint. Works with OpenAI, OpenRouter, Ollama, LM Studio, vLLM, and any
// other server that exposes the same request/response shape.
//
// Request body (POST {base_url}/v1/embeddings):
//   {"model": "<model>", "input": ["text1", "text2", ...]}
// Response body:
//   {"data": [{"embedding": [floats]}, ...], ...}
//
// Constructed with empty api_key or base_url → available() == false.
//
#include <memory/providers/IEmbedder.hpp>

#include <string>

namespace agentcpp::memory {

struct HttpEmbedderConfig {
    std::string base_url;      // e.g. "https://api.openai.com"
    std::string api_key;       // sent as "Authorization: Bearer ..."
    std::string model    = "text-embedding-3-small";
    std::size_t dimension = 1536;  // must match the model
    int         timeout_sec = 60;
};

class HttpEmbedder final : public IEmbedder {
public:
    explicit HttpEmbedder(HttpEmbedderConfig cfg);

    bool        available() const override;
    std::string name()      const override { return name_; }
    std::size_t dimension() const override { return cfg_.dimension; }

    std::vector<std::vector<float>>
    embed(const std::vector<std::string>& texts) override;

private:
    HttpEmbedderConfig cfg_;
    std::string        name_;
};

} // namespace agentcpp::memory
