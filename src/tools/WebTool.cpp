#include <tools/WebTool.hpp>
#include <utils/Logger.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace agentcpp::tools {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string envOr(const char* k, std::string fallback = "") {
    if (const char* v = std::getenv(k); v && *v) return v;
    return fallback;
}

size_t curlWrite(char* ptr, size_t size, size_t nmemb, void* ud) {
    auto* s = reinterpret_cast<std::string*>(ud);
    s->append(ptr, size * nmemb);
    return size * nmemb;
}

// Single-request curl wrapper. We keep it local to the file so WebFetch /
// WebSearch don't double up on a global helper.
struct CurlReq {
    long                    timeout_sec  = 30;
    long                    max_redirects = 5;
    std::vector<std::string> headers;        // "Key: Value"
    std::string             method = "GET";  // "GET" or "POST"
    std::string             body;            // for POST
};

struct CurlResp {
    long         code = 0;
    std::string  body;
    std::string  content_type;
    std::string  final_url;
    std::string  error;
};

CurlResp doCurl(const std::string& url, const CurlReq& req) {
    CurlResp resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.error = "curl_easy_init failed"; return resp; }

    struct curl_slist* h = nullptr;
    for (const auto& hd : req.headers) h = curl_slist_append(h, hd.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, req.timeout_sec);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, req.max_redirects);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "agentcpp/1.0 (+https://example.invalid)");
    if (h) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, h);

    if (req.method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        resp.error = curl_easy_strerror(rc);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.code);
        char* ct = nullptr; char* fu = nullptr;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &fu);
        if (ct) resp.content_type = ct;
        if (fu) resp.final_url    = fu;
    }
    if (h) curl_slist_free_all(h);
    curl_easy_cleanup(curl);
    return resp;
}

} // anonymous

// ─────────────────────────────────────────────────────────────────────────────
// WebFetch
// ─────────────────────────────────────────────────────────────────────────────
std::string WebFetchTool::description() const {
    return "Fetch a URL and return its body as text. Follows up to 5 "
           "redirects. Truncates large responses to ~512 KB. HTML pages are "
           "lightly stripped to readable text (script/style removed, tags "
           "elided). Use this when the user asks about a specific page.";
}

nlohmann::json WebFetchTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"url",     { {"type","string"}, {"description","Absolute http(s) URL"} }},
            {"timeout", { {"type","integer"}, {"description","Timeout seconds (default 30)"}, {"minimum",1}, {"maximum",120} }},
            {"as_text", { {"type","boolean"}, {"description","Strip HTML to plain text (default true for text/html)"} }}
        }},
        {"required", nlohmann::json::array({"url"})}
    };
}

WebFetchTool::FetchResult WebFetchTool::fetch(const std::string& url, int timeout_sec) const {
    CurlReq req;
    req.timeout_sec   = timeout_sec;
    req.max_redirects = kMaxRedirects;
    req.headers       = { "Accept: text/html,application/xhtml+xml,application/json;q=0.9,*/*;q=0.8" };
    auto r = doCurl(url, req);

    FetchResult out;
    out.http_code    = r.code;
    out.content_type = r.content_type;
    out.final_url    = r.final_url;
    out.error        = r.error;

    if (!r.error.empty()) return out;
    if (static_cast<int>(r.body.size()) > kMaxBodyBytes) {
        out.body      = r.body.substr(0, kMaxBodyBytes);
        out.truncated = true;
    } else {
        out.body      = std::move(r.body);
    }
    return out;
}

// Very lightweight HTML stripper. Not a full parser — just enough to make
// content readable in a tool result. Drops <script>/<style> blocks and tag
// markup; collapses whitespace.
std::string WebFetchTool::stripHtmlToText(const std::string& html) {
    std::string s = html;
    auto eraseBlock = [&](const char* open, const char* close) {
        for (;;) {
            auto a = s.find(open);
            if (a == std::string::npos) break;
            auto b = s.find(close, a);
            if (b == std::string::npos) { s.erase(a); break; }
            s.erase(a, (b - a) + std::string(close).size());
        }
    };
    eraseBlock("<script", "</script>");
    eraseBlock("<style",  "</style>");
    eraseBlock("<!--",    "-->");

    std::string out; out.reserve(s.size());
    bool in_tag = false, prev_ws = false;
    for (char c : s) {
        if (in_tag) { if (c == '>') in_tag = false; continue; }
        if (c == '<') { in_tag = true; if (!prev_ws) { out.push_back(' '); prev_ws = true; } continue; }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prev_ws) { out.push_back(' '); prev_ws = true; }
        } else {
            out.push_back(c);
            prev_ws = false;
        }
    }
    // Trim
    while (!out.empty() && std::isspace((unsigned char)out.front())) out.erase(out.begin());
    while (!out.empty() && std::isspace((unsigned char)out.back()))  out.pop_back();
    return out;
}

ToolCallResult WebFetchTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    std::string url     = input.value("url", "");
    int         timeout = input.value("timeout", kDefaultTimeoutSec);
    bool        as_text_default = true;
    bool        as_text = input.value("as_text", as_text_default);

    if (url.empty()) return ToolCallResult::error("url is required");
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        return ToolCallResult::error("url must start with http:// or https://");
    }

    auto r = fetch(url, timeout);
    if (!r.error.empty()) {
        return ToolCallResult::error("fetch failed: " + r.error);
    }
    if (r.http_code >= 400) {
        return ToolCallResult::error("HTTP " + std::to_string(r.http_code) +
                                     " from " + (r.final_url.empty() ? url : r.final_url));
    }

    bool looks_html = r.content_type.find("text/html") != std::string::npos
                      || r.content_type.find("application/xhtml") != std::string::npos;
    std::string body = (as_text && looks_html) ? stripHtmlToText(r.body) : r.body;

    std::ostringstream out;
    out << "URL: "          << (r.final_url.empty() ? url : r.final_url) << "\n"
        << "HTTP: "         << r.http_code << "\n"
        << "Content-Type: " << r.content_type << "\n"
        << (r.truncated ? "(body truncated to " + std::to_string(kMaxBodyBytes) + " bytes)\n" : "")
        << "\n"
        << body;
    return ToolCallResult::ok(out.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSearch
// ─────────────────────────────────────────────────────────────────────────────
std::string WebSearchTool::providerInUse() {
    auto pick = envOr("WEB_SEARCH_PROVIDER");
    if (!pick.empty()) return pick;
    if (!envOr("BRAVE_API_KEY").empty())  return "brave";
    if (!envOr("SERPER_API_KEY").empty()) return "serper";
    if (!envOr("BING_API_KEY").empty())   return "bing";
    if (!envOr("TAVILY_API_KEY").empty()) return "tavily";
    return "";
}

bool WebSearchTool::anyProviderConfigured() { return !providerInUse().empty(); }

std::string WebSearchTool::description() const {
    auto p = providerInUse();
    std::string head = "Run a web search and return ranked results "
                       "(title, URL, snippet).";
    if (p.empty()) {
        return head + " NOTE: no search provider is configured; set "
                      "BRAVE_API_KEY or SERPER_API_KEY / BING_API_KEY / "
                      "TAVILY_API_KEY to enable.";
    }
    return head + " Provider: " + p + ".";
}

nlohmann::json WebSearchTool::inputSchema() const {
    return {
        {"type", "object"},
        {"properties", {
            {"query", { {"type","string"}, {"description","Search query"} }},
            {"count", { {"type","integer"}, {"minimum",1}, {"maximum",20},
                        {"description","Number of results to return (default 8)"} }}
        }},
        {"required", nlohmann::json::array({"query"})}
    };
}

std::string WebSearchTool::urlEncode(const std::string& s) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out; out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

std::vector<WebSearchTool::Hit> WebSearchTool::searchBrave(
    const std::string& q, int n, std::string& err) const
{
    auto key = envOr("BRAVE_API_KEY");
    if (key.empty()) { err = "BRAVE_API_KEY not set"; return {}; }

    CurlReq req;
    req.headers = {
        "X-Subscription-Token: " + key,
        "Accept: application/json"
    };
    std::string url = "https://api.search.brave.com/res/v1/web/search?q="
                      + urlEncode(q) + "&count=" + std::to_string(n);
    auto r = doCurl(url, req);
    if (!r.error.empty()) { err = r.error; return {}; }
    if (r.code >= 400)    { err = "HTTP " + std::to_string(r.code); return {}; }

    std::vector<Hit> hits;
    try {
        auto j = nlohmann::json::parse(r.body);
        if (j.contains("web") && j["web"].contains("results")) {
            for (const auto& it : j["web"]["results"]) {
                Hit h;
                h.title   = it.value("title", "");
                h.url     = it.value("url", "");
                h.snippet = it.value("description", "");
                hits.push_back(std::move(h));
            }
        }
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
    }
    return hits;
}

std::vector<WebSearchTool::Hit> WebSearchTool::searchSerper(
    const std::string& q, int n, std::string& err) const
{
    auto key = envOr("SERPER_API_KEY");
    if (key.empty()) { err = "SERPER_API_KEY not set"; return {}; }

    CurlReq req;
    req.method  = "POST";
    req.headers = {
        "X-API-KEY: " + key,
        "Content-Type: application/json"
    };
    nlohmann::json body = { {"q", q}, {"num", n} };
    req.body = body.dump();

    auto r = doCurl("https://google.serper.dev/search", req);
    if (!r.error.empty()) { err = r.error; return {}; }
    if (r.code >= 400)    { err = "HTTP " + std::to_string(r.code); return {}; }

    std::vector<Hit> hits;
    try {
        auto j = nlohmann::json::parse(r.body);
        if (j.contains("organic")) {
            for (const auto& it : j["organic"]) {
                Hit h;
                h.title   = it.value("title", "");
                h.url     = it.value("link", "");
                h.snippet = it.value("snippet", "");
                hits.push_back(std::move(h));
            }
        }
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
    }
    return hits;
}

std::vector<WebSearchTool::Hit> WebSearchTool::searchBing(
    const std::string& q, int n, std::string& err) const
{
    auto key = envOr("BING_API_KEY");
    if (key.empty()) { err = "BING_API_KEY not set"; return {}; }

    CurlReq req;
    req.headers = {
        "Ocp-Apim-Subscription-Key: " + key,
        "Accept: application/json"
    };
    std::string url = "https://api.bing.microsoft.com/v7.0/search?q="
                      + urlEncode(q) + "&count=" + std::to_string(n);
    auto r = doCurl(url, req);
    if (!r.error.empty()) { err = r.error; return {}; }
    if (r.code >= 400)    { err = "HTTP " + std::to_string(r.code); return {}; }

    std::vector<Hit> hits;
    try {
        auto j = nlohmann::json::parse(r.body);
        if (j.contains("webPages") && j["webPages"].contains("value")) {
            for (const auto& it : j["webPages"]["value"]) {
                Hit h;
                h.title   = it.value("name", "");
                h.url     = it.value("url", "");
                h.snippet = it.value("snippet", "");
                hits.push_back(std::move(h));
            }
        }
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
    }
    return hits;
}

std::vector<WebSearchTool::Hit> WebSearchTool::searchTavily(
    const std::string& q, int n, std::string& err) const
{
    auto key = envOr("TAVILY_API_KEY");
    if (key.empty()) { err = "TAVILY_API_KEY not set"; return {}; }

    CurlReq req;
    req.method  = "POST";
    req.headers = { "Content-Type: application/json" };
    nlohmann::json body = {
        {"api_key", key},
        {"query",   q},
        {"max_results", n},
        {"search_depth", "basic"}
    };
    req.body = body.dump();
    auto r = doCurl("https://api.tavily.com/search", req);
    if (!r.error.empty()) { err = r.error; return {}; }
    if (r.code >= 400)    { err = "HTTP " + std::to_string(r.code); return {}; }

    std::vector<Hit> hits;
    try {
        auto j = nlohmann::json::parse(r.body);
        if (j.contains("results")) {
            for (const auto& it : j["results"]) {
                Hit h;
                h.title   = it.value("title", "");
                h.url     = it.value("url", "");
                h.snippet = it.value("content", "");
                hits.push_back(std::move(h));
            }
        }
    } catch (const std::exception& e) {
        err = std::string("parse error: ") + e.what();
    }
    return hits;
}

ToolCallResult WebSearchTool::execute(const nlohmann::json& input, const ToolContext& /*ctx*/) {
    std::string q  = input.value("query", "");
    int         n  = input.value("count", 8);
    if (q.empty()) return ToolCallResult::error("query is required");
    if (n < 1 || n > 20) n = 8;

    auto provider = providerInUse();
    if (provider.empty()) {
        return ToolCallResult::error(
            "No search provider configured. Set one of BRAVE_API_KEY, "
            "SERPER_API_KEY, BING_API_KEY, TAVILY_API_KEY (or "
            "WEB_SEARCH_PROVIDER + the matching key) and restart.");
    }

    std::string err;
    std::vector<Hit> hits;
    if      (provider == "brave")  hits = searchBrave(q, n, err);
    else if (provider == "serper") hits = searchSerper(q, n, err);
    else if (provider == "bing")   hits = searchBing(q, n, err);
    else if (provider == "tavily") hits = searchTavily(q, n, err);
    else return ToolCallResult::error("Unknown WEB_SEARCH_PROVIDER: " + provider);

    if (!err.empty() && hits.empty()) {
        return ToolCallResult::error(provider + ": " + err);
    }

    std::ostringstream out;
    out << "Provider: " << provider << " · " << hits.size() << " results\n\n";
    for (std::size_t i = 0; i < hits.size(); ++i) {
        out << (i + 1) << ". " << hits[i].title << "\n   " << hits[i].url << "\n";
        if (!hits[i].snippet.empty()) out << "   " << hits[i].snippet << "\n";
        out << "\n";
    }
    return ToolCallResult::ok(out.str());
}

} // namespace agentcpp::tools
