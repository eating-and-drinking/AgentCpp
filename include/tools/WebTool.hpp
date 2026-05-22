#pragma once
//
// Web tool group. Two tools:
//   WebFetch  — GET a URL, return text (HTML or other). Uses libcurl directly.
//   WebSearch — query a search provider (Brave / Serper / Bing) and return
//               ranked results. Provider is selected by env vars; if no
//               key is configured the tool reports its own absence at runtime
//               via execute() returning a clear error.
//
// Both tools are stateless and safe under --read-only. They never write
// files; they only emit text content back to the model.
//
#include "Tool.hpp"

namespace agentcpp::tools {

// ── WebFetch ─────────────────────────────────────────────────────────────────
class WebFetchTool : public Tool {
public:
    std::string name()        const override { return "WebFetch"; }
    std::string category()    const override { return "web"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

private:
    // Caps: do not let an unbounded response flood the context window.
    static constexpr int kMaxBodyBytes        = 512 * 1024;  // 512 KB
    static constexpr int kDefaultTimeoutSec   = 30;
    static constexpr int kMaxRedirects        = 5;

    struct FetchResult {
        long        http_code = 0;
        std::string body;
        std::string content_type;
        std::string final_url;
        bool        truncated = false;
        std::string error;            // non-empty on transport failure
    };

    FetchResult fetch(const std::string& url, int timeout_sec) const;
    static std::string stripHtmlToText(const std::string& html);
};

// ── WebSearch ────────────────────────────────────────────────────────────────
//
// Provider selection (in order):
//   $WEB_SEARCH_PROVIDER  ∈ { "brave", "serper", "bing", "tavily" }
//   else inferred from whichever key env var is set.
//
// Env vars:
//   BRAVE_API_KEY    → brave   (endpoint: api.search.brave.com/res/v1/web/search)
//   SERPER_API_KEY   → serper  (endpoint: google.serper.dev/search)
//   BING_API_KEY     → bing    (endpoint: api.bing.microsoft.com/v7.0/search)
//   TAVILY_API_KEY   → tavily  (endpoint: api.tavily.com/search)
//
class WebSearchTool : public Tool {
public:
    std::string name()        const override { return "WebSearch"; }
    std::string category()    const override { return "web"; }
    std::string description() const override;
    json        inputSchema() const override;

    ToolCallResult execute(const json& input, const ToolContext& ctx) override;

    // Convenience for --list-tools / startup logs.
    static std::string providerInUse();
    static bool        anyProviderConfigured();

private:
    struct Hit { std::string title, url, snippet; };

    std::vector<Hit> searchBrave (const std::string& q, int n, std::string& err) const;
    std::vector<Hit> searchSerper(const std::string& q, int n, std::string& err) const;
    std::vector<Hit> searchBing  (const std::string& q, int n, std::string& err) const;
    std::vector<Hit> searchTavily(const std::string& q, int n, std::string& err) const;

    static std::string urlEncode(const std::string& s);
};

} // namespace agentcpp::tools
