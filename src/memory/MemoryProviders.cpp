#include <memory/MemoryProviders.hpp>

#include <memory/providers/ClaudeFactExtractor.hpp>
#include <memory/providers/ClaudeReflectComposer.hpp>
#include <memory/providers/HeuristicFactExtractor.hpp>
#include <memory/providers/HeuristicReranker.hpp>
#include <memory/providers/HttpEmbedder.hpp>
#include <memory/providers/NullEmbedder.hpp>
#include <memory/providers/TemplateReflectComposer.hpp>

#include <utils/Logger.hpp>

#include <cstdlib>
#include <string>

namespace agentcpp::memory {

namespace {

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    if (v && *v) return std::string(v);
    return fallback;
}

bool envTruthy(const char* name) {
    auto v = envOr(name);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

} // namespace

MemoryProviders makeDefaultProviders() {
    MemoryProviders p;
    p.fact_extractor   = std::make_unique<HeuristicFactExtractor>();
    p.embedder         = std::make_unique<NullEmbedder>();
    p.reranker         = std::make_unique<HeuristicReranker>();
    p.reflect_composer = std::make_unique<TemplateReflectComposer>();
    return p;
}

MemoryProviders makeProvidersFromEnv(
    std::shared_ptr<agentcpp::api::ClaudeClient> claude_client)
{
    auto p = makeDefaultProviders();

    // ── LLM: fact extraction + reflect ────────────────────────────────────
    bool llm_opt_in = envTruthy("AGENTCPP_MEMORY_LLM");
    // Auto-enable if a Claude client with a key is supplied.
    if (claude_client && !claude_client->config().api_key.empty()) {
        llm_opt_in = true;
    }
    if (claude_client && llm_opt_in) {
        auto fx = std::make_unique<ClaudeFactExtractor>(claude_client);
        if (fx->available()) {
            LOG_DEBUG("memory providers: ClaudeFactExtractor enabled");
            p.fact_extractor = std::move(fx);
        }
        auto rc = std::make_unique<ClaudeReflectComposer>(claude_client);
        if (rc->available()) {
            LOG_DEBUG("memory providers: ClaudeReflectComposer enabled");
            p.reflect_composer = std::move(rc);
        }
    }

    // ── Embeddings ────────────────────────────────────────────────────────
    HttpEmbedderConfig ec;
    ec.base_url = envOr("AGENTCPP_MEMORY_EMBED_URL");
    ec.api_key  = envOr("AGENTCPP_MEMORY_EMBED_KEY");
    ec.model    = envOr("AGENTCPP_MEMORY_EMBED_MODEL", "text-embedding-3-small");
    auto dim_str = envOr("AGENTCPP_MEMORY_EMBED_DIM");
    if (!dim_str.empty()) {
        try { ec.dimension = static_cast<std::size_t>(std::stoul(dim_str)); }
        catch (...) { /* keep default */ }
    }
    if (!ec.base_url.empty() && !ec.api_key.empty()) {
        auto emb = std::make_unique<HttpEmbedder>(ec);
        if (emb->available()) {
            LOG_DEBUG("memory providers: HttpEmbedder enabled");
            p.embedder = std::move(emb);
        }
    }

    // Reranker: heuristic for now (interface ready for HTTP impl).
    return p;
}

} // namespace agentcpp::memory
