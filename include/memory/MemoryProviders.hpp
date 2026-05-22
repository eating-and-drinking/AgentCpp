#pragma once
//
// MemoryProviders — bundle of the four pluggable provider interfaces that
// MemoryEngine depends on. Callers either build one explicitly or call
// `makeDefaultProviders()` for the all-heuristic configuration.
//
// Runtime selection helper `makeProvidersFromEnv()` reads:
//
//   ANTHROPIC_API_KEY            → enables ClaudeFactExtractor + ClaudeReflectComposer
//   AGENTCPP_MEMORY_LLM          → "1" to opt in even when key was set elsewhere
//   AGENTCPP_MEMORY_EMBED_URL    → base URL for HttpEmbedder (e.g. https://api.openai.com)
//   AGENTCPP_MEMORY_EMBED_KEY    → bearer token
//   AGENTCPP_MEMORY_EMBED_MODEL  → model name (default: text-embedding-3-small)
//   AGENTCPP_MEMORY_EMBED_DIM    → vector dimension (default: 1536)
//
// Any provider that fails its `available()` check is replaced with the
// matching default before MemoryEngine sees it.
//
#include <memory/providers/IEmbedder.hpp>
#include <memory/providers/IFactExtractor.hpp>
#include <memory/providers/IReflectComposer.hpp>
#include <memory/providers/IReranker.hpp>

#include <memory>

namespace agentcpp::api { class ClaudeClient; }

namespace agentcpp::memory {

struct MemoryProviders {
    std::unique_ptr<IFactExtractor>  fact_extractor;
    std::unique_ptr<IEmbedder>       embedder;
    std::unique_ptr<IReranker>       reranker;
    std::unique_ptr<IReflectComposer> reflect_composer;

    // True only if every slot is filled.
    bool complete() const {
        return fact_extractor && embedder && reranker && reflect_composer;
    }
};

// All-heuristic, no-network providers. Always works.
MemoryProviders makeDefaultProviders();

// Resolve providers from environment variables (see header comment) and an
// optional shared ClaudeClient. Any provider whose `available()` returns
// false is replaced with the matching heuristic default.
MemoryProviders makeProvidersFromEnv(
    std::shared_ptr<agentcpp::api::ClaudeClient> claude_client = nullptr);

} // namespace agentcpp::memory
